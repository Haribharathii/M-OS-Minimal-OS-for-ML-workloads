

#include "globals.h"
#include "errno.h"
#include "types.h"

#include "mm/mm.h"
#include "mm/tlb.h"
#include "mm/mman.h"
#include "mm/page.h"

#include "proc/proc.h"

#include "util/string.h"
#include "util/debug.h"

#include "fs/vnode.h"
#include "fs/vfs.h"
#include "fs/file.h"

#include "vm/vmmap.h"
#include "vm/mmap.h"
#include "mm/mmobj.h" 
static int
valid_map_type(int flags)
{
    int type = flags & (MAP_SHARED | MAP_PRIVATE);
    if (type != MAP_SHARED && type != MAP_PRIVATE) {
        return 0;
    }

    if (flags & ~(MAP_SHARED | MAP_PRIVATE | MAP_FIXED | MAP_ANON)) {
        return 0;
    }

    return 1;
}

static int
valid_fd(int fd)
{
    return (fd >= 0 && fd < NFILES);
}

/*
 * This function implements the mmap(2) syscall, but only
 * supports the MAP_SHARED, MAP_PRIVATE, MAP_FIXED, and
 * MAP_ANON flags.
 *
 * Add a mapping to the current process's address space.
 * You need to do some error checking; see the ERRORS section
 * of the manpage for the problems you should anticipate.
 * After error checking most of the work of this function is
 * done by vmmap_map(), but remember to clear the TLB.
 */
int
do_mmap(void *addr, size_t len, int prot, int flags,
        int fd, off_t off, void **ret)
{
    if (len == 0) {
        return -EINVAL;
    }

    if (!valid_map_type(flags)) {
        return -EINVAL;
    }

    if (!PAGE_ALIGNED(off)) {
        return -EINVAL;
    }

    if ((flags & MAP_FIXED) && !PAGE_ALIGNED(addr)) {
        return -EINVAL;
    }

    if (flags & MAP_ANON) {
        if (fd != -1 || off != 0) {
            return -EINVAL;
        }
    }

    if ((flags & MAP_FIXED) && addr != NULL) {
        uintptr_t a = (uintptr_t)addr;
        if (a < USER_MEM_LOW || a >= USER_MEM_HIGH) {
            return -EINVAL;
        }
        if (len > USER_MEM_HIGH - a) {
            return -EINVAL;
        }
    }

    if (len > USER_MEM_HIGH) {
        return -EINVAL;
    }

    if ((flags & MAP_FIXED) && addr == NULL) {
        return -EINVAL;
    }

    vnode_t *vnode = NULL;

    if (!(flags & MAP_ANON)) {

        if (!valid_fd(fd) || curproc->p_files[fd] == NULL) {
            return -EBADF;
        }

        file_t *f = curproc->p_files[fd];
        vnode = f->f_vnode;
if (f != NULL) {
   dbg(DBG_TEST, "MMAP: vnode=%d len=%d\n", f->f_vnode->vn_vno, f->f_vnode->vn_len);
}
        if ((flags & MAP_PRIVATE) && !(f->f_mode & FMODE_READ)) {
            return -EACCES;
        }

        if ((flags & MAP_SHARED) && (prot & PROT_WRITE) &&
            !((f->f_mode & FMODE_READ) && (f->f_mode & FMODE_WRITE))) {
            return -EACCES;
        }
    }

    vmarea_t *vma = NULL;

    uint32_t npages = (uint32_t)PAGE_ALIGN_UP(len) / PAGE_SIZE;
    uint32_t lopage = (addr != NULL) ? ADDR_TO_PN(addr) : 0;

    int retval = vmmap_map(curproc->p_vmmap, vnode, lopage, npages,
                           prot, flags, off, VMMAP_DIR_HILO, &vma);

    KASSERT(retval == 0 || retval == -ENOMEM);

    if (retval < 0) {
        return retval;
    }
    if (vma != NULL) {
        dbg(DBG_TEST,
            "MMAP DEBUG: pid=%d vnode=%p vma=[0x%08x-0x%08x) off=%u flags=0x%x "
            "obj=%p shadowed=%p bottom=%p\n",
            curproc->p_pid,
            vnode,
            vma->vma_start, vma->vma_end,
            vma->vma_off,
            vma->vma_flags,
            vma->vma_obj,
            vma->vma_obj ? vma->vma_obj->mmo_shadowed : NULL,
            vma->vma_obj ? mmobj_bottom_obj(vma->vma_obj) : NULL);
    }
    if (ret != NULL) {
        *ret = PN_TO_ADDR(vma->vma_start);
    }

    uintptr_t start = (uintptr_t)PN_TO_ADDR(vma->vma_start);
    uintptr_t end   = start + npages * PAGE_SIZE;

    pt_unmap_range(curproc->p_pagedir, start, end);
    tlb_flush_range(start, npages);

    return 0;
}



/*
 * This function implements the munmap(2) syscall.
 *
 * As with do_mmap() it should perform the required error checking,
 * before calling upon vmmap_remove() to do most of the work.
 * Remember to clear the TLB.
 */
int
do_munmap(void *addr, size_t len)
{
    if (len == 0) {
        return -EINVAL;
    }

    if (!PAGE_ALIGNED(addr)) {
        return -EINVAL;
    }

    uintptr_t a = (uintptr_t)addr;

    if (a < USER_MEM_LOW || a >= USER_MEM_HIGH) {
        return -EINVAL;
    }

    if (len > USER_MEM_HIGH - a) {
        return -EINVAL;
    }

    uint32_t lopage = ADDR_TO_PN(addr);
    uint32_t npages = (uint32_t)PAGE_ALIGN_UP(len) / PAGE_SIZE;

    int ret = vmmap_remove(curproc->p_vmmap, lopage, npages);
    if (ret < 0) {
        return ret;
    }

    uintptr_t start = (uintptr_t)PN_TO_ADDR(lopage);
    uintptr_t end   = start + npages * PAGE_SIZE;

    pt_unmap_range(curproc->p_pagedir, start, end);
    tlb_flush_range(start, npages);

    return 0;
}

