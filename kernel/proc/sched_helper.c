

#include "globals.h"
#include "errno.h"

#include "main/interrupt.h"

#include "proc/sched.h"
#include "proc/kthread.h"

#include "util/init.h"
#include "util/debug.h"

void ktqueue_enqueue(ktqueue_t *q, kthread_t *thr);
kthread_t * ktqueue_dequeue(ktqueue_t *q);

/*
 * Updates the thread's state and enqueues it on the given
 * queue. Returns when the thread has been woken up with wakeup_on or
 * broadcast_on.
 *
 * Use the private queue manipulation functions above.
 */
void
sched_sleep_on(ktqueue_t *q)
{
	uint8_t oIPL = intr_getipl();
	intr_setipl(IPL_HIGH);               

	curthr->kt_state = KT_SLEEP;
	//dbg(DBG_PRINT, "(GRADING1C 4)\n");
	ktqueue_enqueue(q, curthr);           

	intr_setipl(oIPL);                
	sched_switch();  
}

kthread_t *
sched_wakeup_on(ktqueue_t *q)
{
    uint8_t oIPL = intr_getipl();
	kthread_t *thr = NULL;

	intr_setipl(IPL_HIGH);  

	//dbg(DBG_PRINT,"(GRADING1C 4)\n");
	thr = ktqueue_dequeue(q); 
	
	if(thr!= NULL){
		KASSERT((thr->kt_state == KT_SLEEP) || (thr->kt_state == KT_SLEEP_CANCELLABLE));
		//dbg(DBG_PRINT, "(GRADING1A 4.a)\n");
	}
	
	intr_setipl(oIPL);
	//dbg(DBG_PRINT, "(GRADING1A 4.a)\n");
	if(thr) sched_make_runnable(thr);
        return thr;
}

void
sched_broadcast_on(ktqueue_t *q)
{
        uint8_t oIPL = intr_getipl();
	kthread_t *thr = NULL;

	intr_setipl(IPL_HIGH); 
	//dbg(DBG_PRINT,"(GRADING1C 3)\n");              
	while((thr = ktqueue_dequeue(q))){      
		intr_setipl(oIPL);   
		//dbg(DBG_PRINT, "(GRADING1A 4.a)\n"); 
		sched_make_runnable(thr);
		intr_setipl(IPL_HIGH); 
	}
	intr_setipl(oIPL);  
        return;
}

