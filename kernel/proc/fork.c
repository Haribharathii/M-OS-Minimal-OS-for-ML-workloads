
#include "types.h"
#include "globals.h"
#include "errno.h"

#include "util/debug.h"
#include "util/string.h"

#include "proc/proc.h"
#include "proc/kthread.h"

#include "mm/mm.h"
#include "mm/mman.h"
#include "mm/page.h"
#include "mm/pframe.h"
#include "mm/mmobj.h"
#include "mm/pagetable.h"
#include "mm/tlb.h"

#include "fs/file.h"
#include "fs/vnode.h"

#include "vm/shadow.h"
#include "vm/vmmap.h"

#include "api/exec.h"

#include "main/interrupt.h"

/* Pushes the appropriate things onto the kernel stack of a newly forked thread
 * so that it can begin execution in userland_entry.
 * regs: registers the new thread should have on execution
 * kstack: location of the new thread's kernel stack
 * Returns the new stack pointer on success. */
static uint32_t
fork_setup_stack(const regs_t *regs, void *kstack)
{
        /* Pointer argument and dummy return address, and userland dummy return
         * address */
        uint32_t esp = ((uint32_t) kstack) + DEFAULT_STACK_SIZE - (sizeof(regs_t) + 12);
        *(void **)(esp + 4) = (void *)(esp + 8); /* Set the argument to point to location of struct on stack */
        memcpy((void *)(esp + 8), regs, sizeof(regs_t)); /* Copy over struct */
        return esp;
}


/*
 * The implementation of fork(2). Once this works,
 * you're practically home free. This is what the
 * entirety of Weenix has been leading up to.
 * Go forth and conquer.
 */
/* used to verify the state of two vmas after a call to vmmap_clone() */
static void assert_vma_state(vmarea_t *oldvma, vmarea_t *newvma, vmmap_t *newvmm){
    KASSERT(oldvma->vma_start == newvma->vma_start);
    KASSERT(oldvma->vma_end == newvma->vma_end);
    KASSERT(oldvma->vma_off == newvma->vma_off);
    KASSERT(oldvma->vma_prot == newvma->vma_prot);
    KASSERT(oldvma->vma_flags == newvma->vma_flags);
    KASSERT(oldvma->vma_vmmap == curproc->p_vmmap && newvma->vma_vmmap == newvmm);
    KASSERT(oldvma->vma_obj != NULL && newvma->vma_obj == NULL);
    KASSERT(list_link_is_linked(&oldvma->vma_plink));
    KASSERT(list_link_is_linked(&newvma->vma_plink));
    /*KASSERT(list_link_is_linked(&oldvma->vma_olink));*/
    KASSERT(!list_link_is_linked(&newvma->vma_olink));
}

/* used to verify the state of two vma's which have private mappings on top
 * of the same memory object*/
static void assert_vmas_equivalent(vmarea_t *oldvma, vmarea_t *newvma){
    KASSERT(oldvma->vma_start == newvma->vma_start);
    KASSERT(oldvma->vma_end == newvma->vma_end);
    KASSERT(oldvma->vma_off == newvma->vma_off);
    KASSERT(oldvma->vma_prot == newvma->vma_prot);
    KASSERT(oldvma->vma_flags == newvma->vma_flags);
    KASSERT(oldvma->vma_vmmap == curproc->p_vmmap && newvma->vma_vmmap != NULL &&
            newvma->vma_vmmap != curproc->p_vmmap);

    int map_type = oldvma->vma_flags & MAP_TYPE;
    if (map_type == MAP_PRIVATE){
        KASSERT(oldvma->vma_obj->mmo_shadowed != NULL
                && oldvma->vma_obj->mmo_shadowed == newvma->vma_obj->mmo_shadowed);
        KASSERT(oldvma->vma_obj->mmo_nrespages == 0
                && newvma->vma_obj->mmo_nrespages == 0);
        KASSERT(oldvma->vma_obj->mmo_un.mmo_bottom_obj
                == newvma->vma_obj->mmo_un.mmo_bottom_obj);
        KASSERT(list_link_is_linked(&oldvma->vma_olink));
        KASSERT(list_link_is_linked(&newvma->vma_olink));
    }

    KASSERT(list_link_is_linked(&oldvma->vma_plink));
    KASSERT(list_link_is_linked(&newvma->vma_plink));
}

static void setup_shadow_obj(vmarea_t *vma, mmobj_t *shadow_obj){
    mmobj_t *bottom_obj = mmobj_bottom_obj(vma->vma_obj);

    /* bottom object cannot be a shadow object */
    KASSERT(bottom_obj->mmo_shadowed == NULL);

    shadow_obj->mmo_un.mmo_bottom_obj = bottom_obj;
    bottom_obj->mmo_ops->ref(bottom_obj);

    /* Shadow steals the reference that vma already holds */
    shadow_obj->mmo_shadowed = vma->vma_obj;
    // NO ref() here - we're stealing vma's existing reference

    if (list_link_is_linked(&vma->vma_olink)){
        list_remove(&vma->vma_olink);
    }

    list_insert_tail(&bottom_obj->mmo_un.mmo_vmas, &vma->vma_olink);

    /* shadow_obj already has a reference from before */
    vma->vma_obj = shadow_obj;
}

static int setup_shadow_objects(vmarea_t *oldvma, vmarea_t *newvma){
    mmobj_t *shadow_obj_1 = shadow_create();
    if (shadow_obj_1 == NULL){
        return -ENOSPC;
    }
    /* DON'T call ref() - shadow_create() already returns refcount=1 */
    KASSERT(shadow_obj_1->mmo_refcount == 1);
    
    mmobj_t *shadow_obj_2 = shadow_create();
    if (shadow_obj_2 == NULL){
        shadow_obj_1->mmo_ops->put(shadow_obj_1);
        return -ENOSPC;
    }
    /* DON'T call ref() - shadow_create() already returns refcount=1 */
    KASSERT(shadow_obj_2->mmo_refcount == 1);
    
    /* oldvma already owns a ref to vma_obj, shadow1 will steal it */
    dbg(DBG_TEST, "setup_shadow_objects: oldvma=%p, oldvma->obj=%p\n", oldvma, oldvma->vma_obj);
    setup_shadow_obj(oldvma, shadow_obj_1);
    dbg(DBG_TEST, "setup_shadow_objects: oldvma->obj updated to %p (S2)\n", oldvma->vma_obj);
    
    /* CRITICAL: newvma doesn't own a ref yet (just a pointer copy), 
     * so take one for shadow2 to steal */
   newvma->vma_obj->mmo_ops->ref(newvma->vma_obj);
    setup_shadow_obj(newvma, shadow_obj_2);
    dbg(DBG_TEST, "setup_shadow_objects: newvma->obj updated to %p (S3)\n", newvma->vma_obj);
    
    return 0;
}

/* undo the creation of shadow objects in the old vmmap_t */
static void vmmap_revert(list_t *old_vma_list, list_t *new_vma_list){
    list_link_t *oldcurr = old_vma_list->l_next;
    list_link_t *newcurr = new_vma_list->l_next;
    
    while (oldcurr != old_vma_list){
        KASSERT(newcurr != new_vma_list && "lists are of different lengths");
        
        vmarea_t *oldvma = list_item(oldcurr, vmarea_t, vma_plink);
        vmarea_t *newvma = list_item(newcurr, vmarea_t, vma_plink);
        
        /* if we found a vma w/o an mmobj, then we never got this far */
        if (newvma->vma_obj == NULL){
            return;
        }
        
        assert_vmas_equivalent(oldvma, newvma);
        
        if ((oldvma->vma_flags & MAP_TYPE) == MAP_PRIVATE){
            KASSERT((newvma->vma_flags & MAP_TYPE) == MAP_PRIVATE);
            KASSERT(newvma->vma_obj->mmo_shadowed != NULL);
            KASSERT(oldvma->vma_obj->mmo_shadowed != NULL);
            
            mmobj_t *oldmmo = oldvma->vma_obj->mmo_shadowed;
            oldmmo->mmo_ops->ref(oldmmo);
            
            /* make sure that putting it will take care of it */
            KASSERT(oldvma->vma_obj->mmo_refcount == 1);
            oldvma->vma_obj->mmo_ops->put(oldvma->vma_obj);
            oldvma->vma_obj = oldmmo;
        }
        
        oldcurr = oldcurr->l_next;
        newcurr = newcurr->l_next;
    }
    
    KASSERT(newcurr == new_vma_list && "lists are of different lengths");
}

/* returns 0 on success, and -errno on error */
int copy_vmmap(proc_t *p) {
    vmmap_t *newvmm = vmmap_clone(curproc->p_vmmap);
    if (!newvmm) return -ENOMEM;
    newvmm->vmm_proc = p;

    list_t *old_list = &curproc->p_vmmap->vmm_list;
    list_t *new_list = &newvmm->vmm_list;
    list_link_t *oldcur = old_list->l_next;
    list_link_t *newcur = new_list->l_next;

    int err = 0;

    while (oldcur != old_list && !err) {
        vmarea_t *oldvma = list_item(oldcur, vmarea_t, vma_plink);
        vmarea_t *newvma = list_item(newcur, vmarea_t, vma_plink);

        assert_vma_state(oldvma, newvma, newvmm);

        /* First: logically share the backing object */
        newvma->vma_obj = oldvma->vma_obj;
      //  newvma->vma_obj->mmo_ops->ref(newvma->vma_obj);

        int map_type = oldvma->vma_flags & MAP_TYPE;
        KASSERT(map_type == MAP_PRIVATE || map_type == MAP_SHARED);

        if (map_type == MAP_PRIVATE) {
	    err = setup_shadow_objects(oldvma, newvma);
		 dbg(DBG_TEST, "copy_vmmap: oldvma=%p obj=%p, newvma=%p obj=%p\n", oldvma, oldvma->vma_obj, newvma, newvma->vma_obj);
	    if (err < 0) {
		newvma->vma_obj = NULL;  // cleanup
		break;
	    }
	}else {
        /* MAP_SHARED: ref the object and link newvma */
		newvma->vma_obj->mmo_ops->ref(newvma->vma_obj);
		list_insert_tail(mmobj_bottom_vmas(newvma->vma_obj), &newvma->vma_olink);
	    }

        oldcur = oldcur->l_next;
        newcur = newcur->l_next;
    }

    if (err) {
        vmmap_revert(old_list, new_list);
        vmmap_destroy(newvmm);
        return err;
    }

    vmmap_destroy(p->p_vmmap);
    p->p_vmmap = newvmm;
    return 0;
}
/* used to assert the state of a newly cloned thread */
static void assert_new_thread_state(kthread_t *k){
    KASSERT(&k->kt_ctx != &curthr->kt_ctx);
    KASSERT(k->kt_kstack != curthr->kt_kstack);
    KASSERT(k->kt_retval == curthr->kt_retval);
    KASSERT(k->kt_errno == curthr->kt_errno);
    KASSERT(k->kt_proc == NULL);
    KASSERT(k->kt_cancelled == curthr->kt_cancelled);
    KASSERT(k->kt_wchan == curthr->kt_wchan);
    KASSERT(k->kt_state == curthr->kt_state);
    KASSERT(list_link_is_linked(&k->kt_qlink)
            == list_link_is_linked(&curthr->kt_qlink));
    KASSERT(!list_link_is_linked(&k->kt_plink));
}

static kthread_t *setup_thread(proc_t *p, struct regs *regs){
    kthread_t *newthr = kthread_clone(curthr);

    if (newthr == NULL){
        return NULL;
    }

    assert_new_thread_state(newthr);

    KASSERT(newthr->kt_proc == NULL && "new thread already has a process");
    KASSERT(!list_link_is_linked(&newthr->kt_plink));
    newthr->kt_proc = p;
    list_insert_tail(&p->p_threads, &newthr->kt_plink);

    /* set the return value in the regs struct to 0 
     * before we copy the regs struct into the new
     * thread's stack*/
    regs->r_eax = 0;
    dbg(DBG_TEST, "setup_thread: Child's trap frame: r_eip = 0x%08x, r_eax = %d, r_ebp = 0x%08x, r_esp = 0x%08x\n",
        regs->r_eip, regs->r_eax, regs->r_ebp, regs->r_useresp);

    uint32_t stack_setup_res = fork_setup_stack(regs, newthr->kt_kstack);

    /* Read back the child's trap frame to verify */
    regs_t *child_regs = (regs_t *)(stack_setup_res + 8);
    dbg(DBG_TEST, "setup_thread: Child's trap frame: r_eip = 0x%08x, r_eax = %d\n",
        child_regs->r_eip, child_regs->r_eax);

    newthr->kt_ctx.c_pdptr = p->p_pagedir;
    newthr->kt_ctx.c_eip = (uint32_t) userland_entry;
    newthr->kt_ctx.c_esp = stack_setup_res;
    newthr->kt_ctx.c_kstack = (uintptr_t) newthr->kt_kstack;
    newthr->kt_ctx.c_kstacksz = DEFAULT_STACK_SIZE;

    return newthr;
}

/* copy filetable of curproc into p */
static void copy_filetable(proc_t *p){
    int i;
    for (i = 0; i < NFILES; i++){
        KASSERT(p->p_files[i] == NULL);

        p->p_files[i] = curproc->p_files[i];
        if (p->p_files[i] != NULL){
            fref(p->p_files[i]);
        }
    }
}

static void unmap_pagetable(){
    tlb_flush_all();
    pt_unmap_range(curproc->p_pagedir, USER_MEM_LOW, USER_MEM_HIGH);
}

static void set_brk_vals(proc_t *p){
    p->p_brk = curproc->p_brk;
    p->p_start_brk = curproc->p_start_brk;
}

static void cleanup_proc(proc_t *p){
    if (list_link_is_linked(&p->p_list_link))
        list_remove(&p->p_list_link);
    if (p->p_pagedir)
        pt_destroy_pagedir(p->p_pagedir);
    if (list_link_is_linked(&p->p_child_link))
        list_remove(&p->p_child_link);
    if (p->p_cwd){
        vput(p->p_cwd);
	p->p_cwd = NULL;
    }
    if (p->p_vmmap){
        vmmap_destroy(p->p_vmmap);
	p->p_vmmap = NULL;
    }
}
/*
 * The implementation of fork(2). Once this works,
 * you're practically home free. This is what the
 * entirety of Weenix has been leading up to.
 * Go forth and conquer.
 */
int
do_fork(struct regs *regs)
{
	KASSERT(regs != NULL);
	KASSERT(curproc != NULL);
	KASSERT(curproc->p_state == PROC_RUNNING);
    proc_t *childproc = proc_create("clonedproc");
dbg(DBG_TEST, "NOOOOOOOOOOOOO\n");
    if (childproc == NULL){
        curthr->kt_errno = ENOMEM;
        return -ENOMEM;
    }

    int err = copy_vmmap(childproc);

    if (err){
        KASSERT(err == -ENOMEM || err == -ENOSPC);
        cleanup_proc(childproc);
        curthr->kt_errno = ENOMEM;
        return -ENOMEM;
    }
    /* CRITICAL: Invalidate all user PTEs to force COW on first write.
     * Without this, parent writes to shared physical pages before child runs,
     * causing child to see parent's modified stack (e.g., waitpid return addr
     * instead of fork return addr). */
   // pt_unmap_range(curproc->p_pagedir, USER_MEM_LOW, USER_MEM_HIGH);
   // pt_unmap_range(childproc->p_pagedir, USER_MEM_LOW, USER_MEM_HIGH);
   // tlb_flush_all();

    kthread_t *newthr = setup_thread(childproc, regs);

    if (newthr == NULL){
        vmmap_revert(&curproc->p_vmmap->vmm_list, &childproc->p_vmmap->vmm_list);
        cleanup_proc(childproc);
        curthr->kt_errno = ENOMEM;
        return -ENOMEM;
    }

    copy_filetable(childproc);
    set_brk_vals(childproc);
  //  pt_unmap_range(curproc->p_pagedir,  USER_MEM_LOW, USER_MEM_HIGH);
  //  pt_unmap_range(childproc->p_pagedir, USER_MEM_LOW, USER_MEM_HIGH);
  //  tlb_flush_all();     /* flush current CPU TLB entries for parent */
	unmap_pagetable();
    sched_make_runnable(newthr);

    /* set eax to the child's pid, now that we've copied it over into the
     * new thread with a value of 0 */
    regs->r_eax = childproc->p_pid;
	dbg(DBG_TEST, "do_fork: regs->r_eip = 0x%08x\n", regs->r_eip);
    dbg(DBG_TEST, "Proc %d forked child process with pid : %d\n", (int)curproc->p_pid, (int)childproc->p_pid);
    return (int)childproc->p_pid;
}