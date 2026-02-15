

#include "globals.h"
#include "errno.h"
#include "util/debug.h"

#include "mm/mm.h"
#include "mm/page.h"
#include "mm/mman.h"

#include "vm/mmap.h"
#include "vm/vmmap.h"
#include "mm/tlb.h"
#include "proc/proc.h"

/*
 * This function implements the brk(2) system call.
 *
 * This routine manages the calling process's "break" -- the ending address
 * of the process's "dynamic" region (often also referred to as the "heap").
 * The current value of a process's break is maintained in the 'p_brk' member
 * of the proc_t structure that represents the process in question.
 *
 * The 'p_brk' and 'p_start_brk' members of a proc_t struct are initialized
 * by the loader. 'p_start_brk' is subsequently never modified; it always
 * holds the initial value of the break. Note that the starting break is
 * not necessarily page aligned!
 *
 * 'p_start_brk' is the lower limit of 'p_brk' (that is, setting the break
 * to any value less than 'p_start_brk' should be disallowed).
 *
 * The upper limit of 'p_brk' is defined by the minimum of (1) the
 * starting address of the next occuring mapping or (2) USER_MEM_HIGH.
 * That is, growth of the process break is limited only in that it cannot
 * overlap with/expand into an existing mapping or beyond the region of
 * the address space allocated for use by userland. (note the presence of
 * the 'vmmap_is_range_empty' function).
 *
 * The dynamic region should always be represented by at most ONE vmarea.
 * Note that vmareas only have page granularity, you will need to take this
 * into account when deciding how to set the mappings if p_brk or p_start_brk
 * is not page aligned.
 *
 * You are guaranteed that the process data/bss region is non-empty.
 * That is, if the starting brk is not page-aligned, its page has
 * read/write permissions.
 *
 * If addr is NULL, you should "return" the current break. We use this to
 * implement sbrk(0) without writing a separate syscall. Look in
 * user/libc/syscall.c if you're curious.
 *
 * You should support combined use of brk and mmap in the same process.
 *
 * Note that this function "returns" the new break through the "ret" argument.
 * Return 0 on success, -errno on failure.
 */
int
do_brk(void *addr, void **ret)
{
                KASSERT(curproc != NULL);
        KASSERT(curproc->p_vmmap != NULL);
	dbg(DBG_TEST, "do_brk: pid=%d addr=%p oldbrk=%p start_brk=%p\n", 
            curproc->p_pid, addr, curproc->p_brk, curproc->p_start_brk);
        /* sbrk(0) style: just return current break */
        if (addr == NULL) {
                if (ret != NULL) {
                        *ret = curproc->p_brk;
                }
		dbg(DBG_TEST, "do_brk: sbrk(0) returning p_brk=%p\n", curproc->p_brk);
                return 0;
        }

        uintptr_t newbrk   = (uintptr_t)addr;
        uintptr_t oldbrk   = (uintptr_t)curproc->p_brk;
        uintptr_t startbrk = (uintptr_t)curproc->p_start_brk;

        /* lower bound: can't go below start_brk */
        if (newbrk < startbrk) {
                return -ENOMEM;
        }

        /* upper bound: can't go past user space */
        if (newbrk >= USER_MEM_HIGH) {
                return -ENOMEM;
        }

        /* no change */
        if (newbrk == oldbrk) {
                if (ret != NULL) {
                        *ret = curproc->p_brk;
                }
                return 0;
        }

        /* Align to page boundaries for vmarea operations */
        uint32_t old_end_vfn = ADDR_TO_PN(PAGE_ALIGN_UP(oldbrk));
        uint32_t new_end_vfn = ADDR_TO_PN(PAGE_ALIGN_UP(newbrk));

        vmmap_t *map = curproc->p_vmmap;

         if (newbrk > oldbrk) {
                /* -------- grow heap -------- */
                
                /* If new break is within the same page, just update p_brk */
                if (new_end_vfn == old_end_vfn) {
                        curproc->p_brk = (void *)newbrk;
                        if (ret != NULL) *ret = curproc->p_brk;
                        return 0;
                }

                /* Check for collision with existing mappings */
                if (!vmmap_is_range_empty(map, old_end_vfn, new_end_vfn - old_end_vfn)) {
                        return -ENOMEM;
                }

                /* Find or create the heap vmarea */
                vmarea_t *vma = vmmap_lookup(map, ADDR_TO_PN(startbrk));
                if (vma == NULL) {
                        /* First time growing heap - create the vmarea */
                        uint32_t start_vfn = ADDR_TO_PN(startbrk);
                        int ret = vmmap_map(map, NULL, start_vfn, new_end_vfn - start_vfn,
                                          PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_FIXED, 0, 0, NULL);
                        if (ret < 0) {
                                return ret;
                        }
                        dbg(DBG_TEST, "do_brk: created heap vmarea [%#x-%#x)\n", start_vfn, new_end_vfn);
                } else {
                        /* Extend the existing vmarea */
                        vma->vma_end = new_end_vfn;
                        dbg(DBG_TEST, "do_brk: extended heap vmarea to %#x\n", new_end_vfn);
                }
                
        } else {
                /* -------- shrink heap -------- */
                
                /* If new break is within the same page, just update p_brk */
                if (new_end_vfn == old_end_vfn) {
                        curproc->p_brk = (void *)newbrk;
                        if (ret != NULL) *ret = curproc->p_brk;
                        return 0;
                }

                /* Find the heap vmarea */
                vmarea_t *vma = vmmap_lookup(map, ADDR_TO_PN(startbrk));
                if (vma == NULL) {
                        return -EFAULT;
                }

                /* Shrink the vmarea */
                vma->vma_end = new_end_vfn;

                /* Unmap pages in the shrunk range */
                /* Note: vmmap_remove is not needed because we just shrunk the vma. 
                 * But we MUST unmap the pages from the page table and flush TLB. 
                 * Actually, vmmap_remove would destroy the vma if we passed the range, 
                 * but here we just want to shrink it. 
                 * The correct way is to use pt_unmap_range on the removed range.
                 */
                 
                 /* However, since we modified vma->vma_end, we effectively removed the range 
                  * from the VMA. Now we need to clean up the physical memory/mappings.
                  */
                 
                 /* Wait, vmmap_remove removes vmareas in the range. 
                  * We don't want to remove the heap vma, just shrink it.
                  * So modifying vma->vma_end is correct.
                  * Now we need to handle the pages.
                  */
                 
                 uintptr_t start = (uintptr_t)PN_TO_ADDR(new_end_vfn);
                 uintptr_t end   = (uintptr_t)PN_TO_ADDR(old_end_vfn);
                 pt_unmap_range(curproc->p_pagedir, start, end);
                 tlb_flush_range(start, (end - start) / PAGE_SIZE);
        }

        curproc->p_brk = (void *)newbrk;
        if (ret != NULL) {
                *ret = curproc->p_brk;
        }

        return 0;
}

