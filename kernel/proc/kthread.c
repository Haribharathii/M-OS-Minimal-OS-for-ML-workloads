

#include "config.h"
#include "globals.h"

#include "errno.h"

#include "util/init.h"
#include "util/debug.h"
#include "util/list.h"
#include "util/string.h"

#include "proc/kthread.h"
#include "proc/proc.h"
#include "proc/sched.h"

#include "mm/slab.h"
#include "mm/page.h"

kthread_t *curthr; /* global */
static slab_allocator_t *kthread_allocator = NULL;

#ifdef __MTP__
/* Stuff for the reaper daemon, which cleans up dead detached threads */
static proc_t *reapd = NULL;
static kthread_t *reapd_thr = NULL;
static ktqueue_t reapd_waitq;
static list_t kthread_reapd_deadlist; /* Threads to be cleaned */

static void *kthread_reapd_run(int arg1, void *arg2);
#endif

void
kthread_init()
{
        kthread_allocator = slab_allocator_create("kthread", sizeof(kthread_t));
        KASSERT(NULL != kthread_allocator);
}

/**
 * Allocates a new kernel stack.
 *
 * @return a newly allocated stack, or NULL if there is not enough
 * memory available
 */
static char *
alloc_stack(void)
{
        /* extra page for "magic" data */
        char *kstack;
        int npages = 1 + (DEFAULT_STACK_SIZE >> PAGE_SHIFT);
        kstack = (char *)page_alloc_n(npages);

        return kstack;
}

/**
 * Frees a stack allocated with alloc_stack.
 *
 * @param stack the stack to free
 */
static void
free_stack(char *stack)
{
        page_free_n(stack, 1 + (DEFAULT_STACK_SIZE >> PAGE_SHIFT));
}

void
kthread_destroy(kthread_t *t)
{
        KASSERT(t && t->kt_kstack);
        free_stack(t->kt_kstack);
        if (list_link_is_linked(&t->kt_plink))
                list_remove(&t->kt_plink);

        slab_obj_free(kthread_allocator, t);
}

/*
 * Allocate a new stack with the alloc_stack function. The size of the
 * stack is DEFAULT_STACK_SIZE.
 *
 * Don't forget to initialize the thread context with the
 * context_setup function. The context should have the same pagetable
 * pointer as the process.
 */
kthread_t *
kthread_create(struct proc *p, kthread_func_t func, long arg1, void *arg2)
{
	KASSERT(NULL != p);
	//dbg(DBG_PRINT, "(GRADING1A 3.a)\n");
	kthread_t *t = slab_obj_alloc(kthread_allocator);
	if(!t) return NULL;

	memset(t, 0, sizeof(*t)); //set memory to 0

	t->kt_kstack = alloc_stack();
	if(!t->kt_kstack) return NULL;

	t->kt_retval = 0;
	t->kt_errno = 0;
	t->kt_proc = p;
	t->kt_cancelled = 0;
	t->kt_state = KT_RUN;
	t->kt_wchan = NULL;

	list_link_init(&t->kt_qlink);   // set all queues to empty
	list_link_init(&t->kt_plink);

	list_insert_tail(&p->p_threads, &t->kt_plink);
	//setting up context for the thread
	context_setup(&t->kt_ctx, func, (int)arg1, arg2,
                  t->kt_kstack, DEFAULT_STACK_SIZE, p->p_pagedir); 

        //dbg(DBG_PRINT, "(GRADING1E)\n");
          return t;
}

/*
 * If the thread to be cancelled is the current thread, this is
 * equivalent to calling kthread_exit. Otherwise, the thread is
 * sleeping (either on a waitqueue or a runqueue) 
 * and we need to set the cancelled and retval fields of the
 * thread. On wakeup, threads should check their cancelled fields and
 * act accordingly. 
 *
 * If the thread's sleep is cancellable, cancelling the thread should
 * wake it up from sleep.
 *
 * If the thread's sleep is not cancellable, we do nothing else here.
 *
 */
void
kthread_cancel(kthread_t *kthr, void *retval)
{
	KASSERT(kthr != NULL);
	//dbg(DBG_PRINT, "(GRADING1A 3.b)\n");
        //dbg(DBG_PRINT, "(GRADING1C)\n");
	if (kthr == curthr) {
        kthread_exit(retval);    
		return;
    }

	kthr->kt_retval = retval;

	//sets the kt_cancelled flag and handles q as per thread state
	sched_cancel(kthr); 
	//dbg(DBG_PRINT, "(GRADING1E)\n");
        
}

/*
 * You need to set the thread's retval field and alert the current
 * process that a thread is exiting via proc_thread_exited. You should
 * refrain from setting the thread's state to KT_EXITED until you are
 * sure you won't make any more blocking calls before you invoke the
 * scheduler again.
 *
 * It may seem unneccessary to push the work of cleaning up the thread
 * over to the process. However, if you implement MTP, a thread
 * exiting does not necessarily mean that the process needs to be
 * cleaned up.
 *
 * The void * type of retval is simply convention and does not necessarily
 * indicate that retval is a pointer
 */
void
kthread_exit(void *retval)
{
	curthr->kt_retval = retval;
	curthr->kt_state = KT_EXITED;  
	proc_thread_exited(retval); 

	KASSERT(!curthr->kt_wchan);
    //dbg(DBG_PRINT, "(GRADING1A 3.c)\n");

	KASSERT(!curthr->kt_qlink.l_next && !curthr->kt_qlink.l_prev);
    //dbg(DBG_PRINT, "(GRADING1A 3.c)\n");

    KASSERT(curthr->kt_proc == curproc);
    //dbg(DBG_PRINT, "(GRADING1A 3.c)\n");
	
	//no more blocking calls  
 	sched_switch(); 
        
        //dbg(DBG_PRINT, "(GRADING1E)\n");
    return;
}

/*
 * The new thread will need its own context and stack. Think carefully
 * about which fields should be copied and which fields should be
 * freshly initialized.
 *
 * You do not need to worry about this until VM.
 */
kthread_t *
kthread_clone(kthread_t *thr)
{
	KASSERT(thr != NULL);

	kthread_t *newthr = slab_obj_alloc(kthread_allocator);
	if (newthr == NULL) return NULL;

	void *kstack = alloc_stack();
	if (kstack == NULL) {
		slab_obj_free(kthread_allocator, newthr);
		return NULL;
	}

	*newthr = *thr;                 

	newthr->kt_kstack   = kstack;
	newthr->kt_retval   = thr->kt_retval;
	newthr->kt_errno    = thr->kt_errno;
	newthr->kt_cancelled = thr->kt_cancelled;
    KASSERT(thr->kt_wchan == NULL);
    newthr->kt_wchan = thr->kt_wchan;

    KASSERT(thr->kt_state == KT_RUN);
    newthr->kt_state = thr->kt_state;

	list_link_init(&newthr->kt_plink);
	list_link_init(&newthr->kt_qlink);

	newthr->kt_ctx = thr->kt_ctx;

	uintptr_t old_base = (uintptr_t)thr->kt_kstack;
	uintptr_t new_base = (uintptr_t)newthr->kt_kstack;
	uintptr_t old_sp   = (uintptr_t)newthr->kt_ctx.c_esp;
	uintptr_t offset   = old_sp - old_base;

	newthr->kt_ctx.c_esp = (uintptr_t)(new_base + offset);


	newthr->kt_proc = NULL;  

	return newthr;
}

/*
 * The following functions will be useful if you choose to implement
 * multiple kernel threads per process. This is strongly discouraged
 * unless your weenix is perfect.
 */
#ifdef __MTP__
int
kthread_detach(kthread_t *kthr)
{
        NOT_YET_IMPLEMENTED("MTP: kthread_detach");
        return 0;
}

int
kthread_join(kthread_t *kthr, void **retval)
{
        NOT_YET_IMPLEMENTED("MTP: kthread_join");
        return 0;
}

/* ------------------------------------------------------------------ */
/* -------------------------- REAPER DAEMON ------------------------- */
/* ------------------------------------------------------------------ */
static __attribute__((unused)) void
kthread_reapd_init()
{
        NOT_YET_IMPLEMENTED("MTP: kthread_reapd_init");
}
init_func(kthread_reapd_init);
init_depends(sched_init);

void
kthread_reapd_shutdown()
{
        NOT_YET_IMPLEMENTED("MTP: kthread_reapd_shutdown");
}

static void *
kthread_reapd_run(int arg1, void *arg2)
{
        NOT_YET_IMPLEMENTED("MTP: kthread_reapd_run");
        return (void *) 0;
}
#endif
