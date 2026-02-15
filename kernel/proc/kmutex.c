

#include "globals.h"
#include "errno.h"

#include "util/debug.h"

#include "proc/kthread.h"
#include "proc/kmutex.h"

/*
 * IMPORTANT: Mutexes can _NEVER_ be locked or unlocked from an
 * interrupt context. Mutexes are _ONLY_ lock or unlocked from a
 * thread context.
 */

void
kmutex_init(kmutex_t *mtx)
{
    //dbg(DBG_PRINT,"(GRADING1C 7)\n");   
	KASSERT(mtx != NULL);
	sched_queue_init(&mtx->km_waitq);
	mtx->km_holder = NULL;
	//dbg(DBG_PRINT, "(GRADING1E)\n");
}

/*
 * This should block the current thread (by sleeping on the mutex's
 * wait queue) if the mutex is already taken.
 *
 * No thread should ever try to lock a mutex it already has locked.
 */
void
kmutex_lock(kmutex_t *mtx)
{
       
	KASSERT(mtx != NULL);
	KASSERT(curthr != NULL);
	KASSERT(mtx->km_holder != curthr);
	//dbg(DBG_PRINT, "(GRADING1A 6.a)\n");

	while(mtx->km_holder != NULL){
		//dbg(DBG_PRINT, "(GRADING1C 7)\n");
		sched_sleep_on(&mtx->km_waitq);
	}
	mtx->km_holder = curthr;
	//dbg(DBG_PRINT, "(GRADING1E)\n");
}

/*
 * This should do the same as kmutex_lock, but use a cancellable sleep
 * instead. Also, if you are cancelled while holding mtx, you should unlock mtx.
 */
int
kmutex_lock_cancellable(kmutex_t *mtx)
{
    KASSERT(mtx != NULL);
    KASSERT(curthr != NULL);
    KASSERT(mtx->km_holder != curthr);
	//dbg(DBG_PRINT, "(GRADING1A 6.b)\n");
	//dbg(DBG_PRINT, "(GRADING1C)\n");

    while (mtx->km_holder != NULL) {
		//dbg(DBG_PRINT,"(GRADING1C 7)\n");
        int cancelled = sched_cancellable_sleep_on(&mtx->km_waitq);
        if (cancelled) {
            /* cancelled while waiting; did not take the mutex */
            return -EINTR;
        }
    }

    mtx->km_holder = curthr;
	
    if (curthr->kt_cancelled) {
        curthr->kt_cancelled = 0; 
		//dbg(DBG_PRINT,"(GRADING1C 7)\n"); 
        kmutex_unlock(mtx);
        return -EINTR;
    }
	//dbg(DBG_PRINT, "(GRADING1E)\n");
	
    return 0;
}


/*
 * If there are any threads waiting to take a lock on the mutex, one
 * should be woken up and given the lock.
 *
 * Note: This should _NOT_ be a blocking operation!
 *
 * Note: Ensure the new owner of the mutex enters the run queue.
 *
 * Note: Make sure that the thread on the head of the mutex's wait
 * queue becomes the new owner of the mutex.
 *
 * @param mtx the mutex to unlock
 */
void
kmutex_unlock(kmutex_t *mtx)
{

	kthread_t *new_owner;
	KASSERT(mtx != NULL);
	KASSERT(curthr != NULL);
	KASSERT(mtx->km_holder == curthr);
	//dbg(DBG_PRINT, "(GRADING1A 6.c)\n");

	if(mtx->km_waitq.tq_size == 0){
		mtx->km_holder = NULL;	
	}
	else{
		new_owner = sched_wakeup_on(&mtx->km_waitq);
		KASSERT(new_owner != NULL);
		mtx->km_holder = NULL;	
	}

	KASSERT(curthr != mtx->km_holder);
    //dbg(DBG_PRINT, "(GRADING1A 6.c)\n");
	//dbg(DBG_PRINT, "(GRADING1E)\n");
}
