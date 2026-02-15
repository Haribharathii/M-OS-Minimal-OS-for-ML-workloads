

#include "types.h"
#include "globals.h"
#include "kernel.h"
#include "errno.h"

#include "util/debug.h"

#include "proc/proc.h"

#include "mm/mm.h"
#include "mm/mman.h"
#include "mm/page.h"
#include "mm/mmobj.h"
#include "mm/pframe.h"
#include "mm/pagetable.h"

#include "vm/pagefault.h"
#include "vm/vmmap.h"
#include "mm/tlb.h"
/*
 * This gets called by _pt_fault_handler in mm/pagetable.c The
 * calling function has already done a lot of error checking for
 * us. In particular it has checked that we are not page faulting
 * while in kernel mode. Make sure you understand why an
 * unexpected page fault in kernel mode is bad in Weenix. You
 * should probably read the _pt_fault_handler function to get a
 * sense of what it is doing.
 *
 * Before you can do anything you need to find the vmarea that
 * contains the address that was faulted on. Make sure to check
 * the permissions on the area to see if the process has
 * permission to do [cause]. If either of these checks does not
 * pass kill the offending process, setting its exit status to
 * EFAULT (normally we would send the SIGSEGV signal, however
 * Weenix does not support signals).
 *
 * Now it is time to find the correct page. Make sure that if the
 * user writes to the page it will be handled correctly. This
 * includes your shadow objects' copy-on-write magic working
 * correctly.
 *
 * Finally call pt_map to have the new mapping placed into the
 * appropriate page table.
 *
 * @param vaddr the address that was accessed to cause the fault
 *
 * @param cause this is the type of operation on the memory
 *              address which caused the fault, possible values
 *              can be found in pagefault.h
 */
#if 0
void
handle_pagefault(uintptr_t vaddr, uint32_t cause)
{
    uint32_t pagenum = ADDR_TO_PN(vaddr);
    
    // 2. Find the vmarea that contains this address
    vmarea_t *vma = vmmap_lookup(curproc->p_vmmap, pagenum);
    if (!vma) {
        // Segmentation fault - no mapping exists
        dbg(DBG_PRINT, "Page fault at 0x%08x - no mapping\n", vaddr);
        do_exit(EFAULT);
        return;
    }
    
    // 3. Calculate offset within the memory object
    uint32_t pageoff = pagenum - vma->vma_start;
    uint32_t objpage = vma->vma_off + pageoff;
    dbg(DBG_TEST,
        "PF DEBUG: pid=%d vaddr=0x%08x pagenum=%u objpage=%u "
        "vma=[0x%08x-0x%08x) off=%u flags=0x%x obj=%p shadowed=%p bottom=%p\n",
        curproc->p_pid,
        vaddr,
        pagenum,
        objpage,
        vma->vma_start, vma->vma_end,
        vma->vma_off,
        vma->vma_flags,
        vma->vma_obj,
        vma->vma_obj ? vma->vma_obj->mmo_shadowed : NULL,
        vma->vma_obj ? mmobj_bottom_obj(vma->vma_obj) : NULL);
	dbg(DBG_TEST, "PF DEBUG: vma_ptr=%p prot=0x%x\n", vma, vma->vma_prot);
    // 4. Get the page from the memory object (this loads it)
    pframe_t *pf;
    int write_fault = (cause & FAULT_WRITE) != 0;
	/* Only private mappings should trigger COW on write faults */
	int forwrite = write_fault && (vma->vma_flags & MAP_PRIVATE);
    int ret = pframe_lookup(vma->vma_obj, objpage, forwrite, &pf);
    if (ret < 0) {
        dbg(DBG_PRINT, "Page fault at 0x%08x - pframe_lookup failed: %d\n", vaddr, ret);
        do_exit(EFAULT);
        return;
    }
    if (cause & FAULT_WRITE){
        pframe_pin(pf);
        int dirty_res = pframe_dirty(pf);
        pframe_unpin(pf);

        if (dirty_res < 0){
            do_exit(EFAULT);
            panic("returned from do_exit");
        }
    }
    // 5. Map the page into the page table
    uintptr_t paddr = pt_virt_to_phys((uintptr_t)pf->pf_addr);
    uint32_t pdflags = PD_PRESENT | PD_USER;

if (vma->vma_prot & PROT_WRITE) {
    if (vma->vma_flags & MAP_SHARED) {
        /* shared writable mapping: writes go straight to the object */
        pdflags |= PD_WRITE;
    } else if ((vma->vma_flags & MAP_PRIVATE) &&
               pf->pf_obj == vma->vma_obj) {
        /* private mapping, and we've already done COW into top shadow */
        pdflags |= PD_WRITE;
    }
    /* else: private mapping still using bottom page -> keep read-only
       so the first write will fault and we can COW */
}
    
    pt_map(curproc->p_pagedir, 
           (uintptr_t)PAGE_ALIGN_DOWN(vaddr),
           paddr,
           pdflags,
           pdflags);
    if(pf->pf_pincount > 0) pframe_unpin(pf); 
    tlb_flush_all();
}
#endif
void
handle_pagefault(uintptr_t vaddr, uint32_t cause)
{
    uint32_t pagenum = ADDR_TO_PN(vaddr);
    
    // 2. Find the vmarea that contains this address
    vmarea_t *vma = vmmap_lookup(curproc->p_vmmap, pagenum);
    if (!vma) {
        // Segmentation fault - no mapping exists
        dbg(DBG_TEST, "Page fault at 0x%08x - no mapping\n", vaddr);
        do_exit(EFAULT);
        return;
    }
    
    // 3. Check permissions before handling the fault
    int write_fault = (cause & FAULT_WRITE) != 0;
    if (write_fault && !(vma->vma_prot & PROT_WRITE)) {
        // Write fault to a read-only VMA - this is a permission violation
        dbg(DBG_TEST, "Permission violation: write fault at 0x%08x to read-only VMA (prot=0x%x)\n", 
            vaddr, vma->vma_prot);
        do_exit(EFAULT);
        return;
    }
	
 if (!write_fault && !(vma->vma_prot & PROT_READ)) {
        /* Read/exec fault on non-readable VMA */
        dbg(DBG_PRINT, "Read fault at 0x%08x to non-readable VMA (prot=0x%x)\n",
            vaddr, vma->vma_prot);
        do_exit(EFAULT);
        return;
    }
    
    // 4. Calculate offset within the memory object
    uint32_t pageoff = pagenum - vma->vma_start;
    uint32_t objpage = vma->vma_off + pageoff;
    dbg(DBG_TEST,
        "PF DEBUG: pid=%d vaddr=0x%08x pagenum=%u objpage=%u "
        "vma=[0x%08x-0x%08x) off=%u flags=0x%x obj=%p shadowed=%p bottom=%p\n",
        curproc->p_pid,
        vaddr,
        pagenum,
        objpage,
        vma->vma_start, vma->vma_end,
        vma->vma_off,
        vma->vma_flags,
        vma->vma_obj,
        vma->vma_obj ? vma->vma_obj->mmo_shadowed : NULL,
        vma->vma_obj ? mmobj_bottom_obj(vma->vma_obj) : NULL);
	dbg(DBG_TEST, "PF DEBUG: vma_ptr=%p prot=0x%x\n", vma, vma->vma_prot);
    // 5. Get the page from the memory object (this loads it)
    pframe_t *pf;
	/* Only private mappings should trigger COW on write faults */
	int forwrite = write_fault && (vma->vma_flags & MAP_PRIVATE);
    int ret = pframe_lookup(vma->vma_obj, objpage, forwrite, &pf);
    if (ret < 0) {
        dbg(DBG_TEST, "Page fault at 0x%08x - pframe_lookup failed: %d\n", vaddr, ret);
        do_exit(EFAULT);
        return;
    }
    if (cause & FAULT_WRITE){
        pframe_pin(pf);
        int dirty_res = pframe_dirty(pf);
        pframe_unpin(pf);

        if (dirty_res < 0){
            do_exit(EFAULT);
            panic("returned from do_exit");
        }
    }
    // 5. Map the page into the page table
    uintptr_t paddr = pt_virt_to_phys((uintptr_t)pf->pf_addr);
    uint32_t pdflags = PD_PRESENT | PD_USER;

if (vma->vma_prot & PROT_WRITE) {
    if (vma->vma_flags & MAP_SHARED) {
        /* shared writable mapping: writes go straight to the object */
        pdflags |= PD_WRITE;
    } else if ((vma->vma_flags & MAP_PRIVATE) &&
               pf->pf_obj == vma->vma_obj) {
        /* private mapping, and we've already done COW into top shadow */
        pdflags |= PD_WRITE;
    }
    /* else: private mapping still using bottom page -> keep read-only
       so the first write will fault and we can COW */
}
    
    pt_map(curproc->p_pagedir, 
           (uintptr_t)PAGE_ALIGN_DOWN(vaddr),
           paddr,
           pdflags,
           pdflags);
    if(pf->pf_pincount > 0) pframe_unpin(pf); 
    tlb_flush_all();
}
