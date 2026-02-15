

#include "kernel.h"
#include "config.h"
#include "globals.h"
#include "errno.h"

#include "util/debug.h"
#include "util/list.h"
#include "util/string.h"
#include "util/printf.h"

#include "proc/kthread.h"
#include "proc/proc.h"
#include "proc/sched.h"

#include "mm/slab.h"
#include "mm/page.h"
#include "mm/mmobj.h"
#include "mm/mm.h"
#include "mm/mman.h"

#include "vm/vmmap.h"

#include "fs/vfs.h"
#include "fs/vfs_syscall.h"
#include "fs/vnode.h"
#include "fs/file.h"

proc_t *curproc = NULL; /* global */
static slab_allocator_t *proc_allocator = NULL;

static list_t _proc_list;
static proc_t *proc_initproc = NULL; /* Pointer to the init process (PID 1) */

void
proc_init()
{
        list_init(&_proc_list);
        proc_allocator = slab_allocator_create("proc", sizeof(proc_t));
        KASSERT(proc_allocator != NULL);
}

proc_t *
proc_lookup(int pid)
{
        proc_t *p;
        list_iterate_begin(&_proc_list, p, proc_t, p_list_link) {
                if (p->p_pid == pid) {
                        return p;
                }
        } list_iterate_end();
        return NULL;
}

list_t *
proc_list()
{
        return &_proc_list;
}

size_t
proc_info(const void *arg, char *buf, size_t osize)
{
        const proc_t *p = (proc_t *) arg;
        size_t size = osize;
        proc_t *child;

        KASSERT(NULL != p);
        KASSERT(NULL != buf);

        iprintf(&buf, &size, "pid:          %i\n", p->p_pid);
        iprintf(&buf, &size, "name:         %s\n", p->p_comm);
        if (NULL != p->p_pproc) {
                iprintf(&buf, &size, "parent:       %i (%s)\n",
                        p->p_pproc->p_pid, p->p_pproc->p_comm);
        } else {
                iprintf(&buf, &size, "parent:       -\n");
        }

#ifdef __MTP__
        int count = 0;
        kthread_t *kthr;
        list_iterate_begin(&p->p_threads, kthr, kthread_t, kt_plink) {
                ++count;
        } list_iterate_end();
        iprintf(&buf, &size, "thread count: %i\n", count);
#endif

        if (list_empty(&p->p_children)) {
                iprintf(&buf, &size, "children:     -\n");
        } else {
                iprintf(&buf, &size, "children:\n");
        }
        list_iterate_begin(&p->p_children, child, proc_t, p_child_link) {
                iprintf(&buf, &size, "     %i (%s)\n", child->p_pid, child->p_comm);
        } list_iterate_end();

        iprintf(&buf, &size, "status:       %i\n", p->p_status);
        iprintf(&buf, &size, "state:        %i\n", p->p_state);

#ifdef __VFS__
#ifdef __GETCWD__
        if (NULL != p->p_cwd) {
                char cwd[256];
                lookup_dirpath(p->p_cwd, cwd, sizeof(cwd));
                iprintf(&buf, &size, "cwd:          %-s\n", cwd);
        } else {
                iprintf(&buf, &size, "cwd:          -\n");
        }
#endif /* __GETCWD__ */
#endif

#ifdef __VM__
        iprintf(&buf, &size, "start brk:    0x%p\n", p->p_start_brk);
        iprintf(&buf, &size, "brk:          0x%p\n", p->p_brk);
#endif

        return size;
}

size_t
proc_list_info(const void *arg, char *buf, size_t osize)
{
        size_t size = osize;
        proc_t *p;

        KASSERT(NULL == arg);
        KASSERT(NULL != buf);

#if defined(__VFS__) && defined(__GETCWD__)
        iprintf(&buf, &size, "%5s %-13s %-18s %-s\n", "PID", "NAME", "PARENT", "CWD");
#else
        iprintf(&buf, &size, "%5s %-13s %-s\n", "PID", "NAME", "PARENT");
#endif

        list_iterate_begin(&_proc_list, p, proc_t, p_list_link) {
                char parent[64];
                if (NULL != p->p_pproc) {
                        snprintf(parent, sizeof(parent),
                                 "%3i (%s)", p->p_pproc->p_pid, p->p_pproc->p_comm);
                } else {
                        snprintf(parent, sizeof(parent), "  -");
                }

#if defined(__VFS__) && defined(__GETCWD__)
                if (NULL != p->p_cwd) {
                        char cwd[256];
                        lookup_dirpath(p->p_cwd, cwd, sizeof(cwd));
                        iprintf(&buf, &size, " %3i  %-13s %-18s %-s\n",
                                p->p_pid, p->p_comm, parent, cwd);
                } else {
                        iprintf(&buf, &size, " %3i  %-13s %-18s -\n",
                                p->p_pid, p->p_comm, parent);
                }
#else
                iprintf(&buf, &size, " %3i  %-13s %-s\n",
                        p->p_pid, p->p_comm, parent);
#endif
        } list_iterate_end();
        return size;
}

static pid_t next_pid = 0;

/**
 * Returns the next available PID.
 *
 * Note: Where n is the number of running processes, this algorithm is
 * worst case O(n^2). As long as PIDs never wrap around it is O(n).
 *
 * @return the next available PID
 */
static int
_proc_getid()
{
        proc_t *p;
        pid_t pid = next_pid;
        while (1) {
failed:
                list_iterate_begin(&_proc_list, p, proc_t, p_list_link) {
                        if (p->p_pid == pid) {
                                if ((pid = (pid + 1) % PROC_MAX_COUNT) == next_pid) {
                                        return -1;
                                } else {
                                        goto failed;
                                }
                        }
                } list_iterate_end();
                next_pid = (pid + 1) % PROC_MAX_COUNT;
                return pid;
        }
}

/*
 * The new process, although it isn't really running since it has no
 * threads, should be in the PROC_RUNNING state.
 *
 * Don't forget to set proc_initproc when you create the init
 * process. You will need to be able to reference the init process
 * when reparenting processes to the init process.
 */
proc_t *
proc_create(char *name)
{
	proc_t *p = slab_obj_alloc(proc_allocator);
	if(!p) return NULL;

	memset(p, 0, sizeof(*p)); //set memory to 0

	list_init(&p->p_threads); // set all queues to empty
	list_init(&p->p_children);
	list_link_init(&p->p_list_link);
	list_link_init(&p->p_child_link);
	sched_queue_init(&p->p_wait);

	if (!name) name = "None"; // set name
	strncpy(p->p_comm, name, PROC_NAME_LEN);
	p->p_comm[PROC_NAME_LEN - 1] = '\0'; 
 
	p->p_pid = _proc_getid(); //set process fields
	if (p->p_pid < 0) { 
		slab_obj_free(proc_allocator, p); 
		return NULL; 
	}

	KASSERT(PID_IDLE != p->p_pid || list_empty(&_proc_list));


	KASSERT(PID_INIT != p->p_pid || PID_IDLE == curproc->p_pid);

	
	p->p_pproc = curproc;

	p->p_state = PROC_RUNNING;
	p->p_status = 0;
	p->p_pagedir = pt_create_pagedir();   

	p->p_vmmap     = vmmap_create();
	if (p->p_vmmap == NULL){

		if (list_link_is_linked(&p->p_child_link)){
		    list_remove(&p->p_child_link);
		}

		pt_destroy_pagedir(p->p_pagedir);
		list_remove(&p->p_list_link);
		slab_obj_free(proc_allocator, p);
		return NULL;
    }
	p->p_vmmap->vmm_proc = p;
	p->p_brk       = NULL;
	p->p_start_brk = NULL;

	int i = 0;
	while(i < NFILES){
		p->p_files[i++] = NULL;
	}

	if( p->p_pid != 0)
	{
		list_insert_head(&(curproc->p_children), &(p->p_child_link));

	}
	if(p->p_pid == 1){

		proc_initproc = p;
	} 
	list_insert_head(&_proc_list, &p->p_list_link); 
	
	 if(p->p_pid > 2)
	    {
	    	//dbg(DBG_PRINT, "proc create 1\n");
		p->p_cwd = p->p_pproc->p_cwd;
		//dbg(DBG_PRINT, "proc create 2\n");
		vref(p->p_cwd);
		//dbg(DBG_PRINT, "proc create 3\n");
	    }
	    else
	    {
	    	//dbg(DBG_PRINT,"\nQQ proc_creat else");
		p->p_cwd = NULL;
	    }
        return p;
}

/**
 * Cleans up as much as the process as can be done from within the
 * process. This involves:
 *    - Closing all open files (VFS)
 *    - Cleaning up VM mappings (VM)
 *    - Waking up its parent if it is waiting
 *    - Reparenting any children to the init process
 *    - Setting its status and state appropriately
 *
 * The parent will finish destroying the process within do_waitpid (make
 * sure you understand why it cannot be done here). Until the parent
 * finishes destroying it, the process is informally called a 'zombie'
 * process.
 *
 * This is also where any children of the current process should be
 * reparented to the init process (unless, of course, the current
 * process is the init process. However, the init process should not
 * have any children at the time it exits).
 *
 * Note: You do _NOT_ have to special case the idle process. It should
 * never exit this way.
 *
 * @param status the status to exit the process with
 */
void
proc_cleanup(int status)
{
	
	KASSERT(NULL != proc_initproc);
    //dbg(DBG_PRINT, "(GRADING1A 2.b)\n");

	KASSERT(1 <= curproc->p_pid);
	//dbg(DBG_PRINT, "(GRADING1A 2.b)\n");

	KASSERT(NULL != curproc->p_pproc);
	//dbg(DBG_PRINT, "(GRADING1A 2.b)\n");

	int i = 0;
	proc_t *p = curproc;

	for(i = 0; i < NFILES; i++)
	{
		if(curproc->p_files[i]!=NULL)
		{
			
			do_close(i);
		}

	}

	if (proc_initproc && p != proc_initproc) {
		proc_t *ch;
		list_iterate_begin(&p->p_children, ch, proc_t, p_child_link) {
		    list_remove(&ch->p_child_link); 
		    ch->p_pproc = proc_initproc;
		    
		    list_insert_tail(&proc_initproc->p_children, 
					&ch->p_child_link);
		} list_iterate_end();
	    }
	
	p->p_state = PROC_DEAD;
	p->p_status = status;
	if (p->p_cwd != NULL) {
		vput(p->p_cwd);
		p->p_cwd = NULL;
	}
	if (p->p_vmmap) { 
		vmmap_destroy(p->p_vmmap);
		p->p_vmmap = NULL;
	} 
	// wake the parent
	if(p->p_pproc) {
		//dbg(DBG_PRINT, "(GRADING1C 1)\n");
		sched_broadcast_on(&p->p_pproc->p_wait);
	} 

	KASSERT(NULL != curproc->p_pproc);
	//dbg(DBG_PRINT, "(GRADING1A 2.b)\n");

	KASSERT(KT_EXITED == curthr->kt_state);
	//dbg(DBG_PRINT, "(GRADING1A 2.b)\n");

	//dbg(DBG_PRINT, "(GRADING1E)\n");
	return;
}

/*
 * This has nothing to do with signals and kill(1).
 *
 * Calling this on the current process is equivalent to calling
 * do_exit().
 *
 * In Weenix, this is only called from proc_kill_all.
 */
void
proc_kill(proc_t *p, int status)
{
	
	if(p == curproc){
		//dbg(DBG_PRINT, "(GRADING1C 9)\n");
		do_exit(status);
	}else{
		kthread_t *t;
		//dbg(DBG_PRINT, "(GRADING1C 8)\n");
		list_iterate_begin(&p->p_threads, t, kthread_t, kt_plink) {
        			kthread_cancel(t, (void*)(intptr_t)status);
    		} list_iterate_end();
	}
	//dbg(DBG_PRINT, "(GRADING1E)\n");
	return;
}

/*
 * Remember, proc_kill on the current process will _NOT_ return.
 * Don't kill direct children of the idle process.
 *
 * In Weenix, this is only called by sys_halt.
 */
void
proc_kill_all()
{
	proc_t *p;
	list_iterate_begin(&_proc_list, p, proc_t, p_list_link) {
                        if (p->p_pid > 2 && p != curproc){
							//dbg(DBG_PRINT, "(GRADING1C 9)\n");
                        	proc_kill(p, 0);
                        }
						//dbg(DBG_PRINT, "(GRADING1C 9)\n");
					} list_iterate_end();
	if (curproc->p_pid != 0  && curproc->p_pid != 1) {
		proc_kill(curproc, 0);
	}
	
	return;
}

/*
 * This function is only called from kthread_exit.
 *
 * Unless you are implementing MTP, this just means that the process
 * needs to be cleaned up and a new thread needs to be scheduled to
 * run. If you are implementing MTP, a single thread exiting does not
 * necessarily mean that the process should be exited.
 */
void
proc_thread_exited(void *retval)
{
	//dbg(DBG_PRINT, "(GRADING1C 1)\n");
	proc_cleanup((int)(intptr_t)retval);
	//dbg(DBG_PRINT, "(GRADING1E)\n");
	//gdb
}

/* If pid is -1 dispose of one of the exited children of the current
 * process and return its exit status in the status argument, or if
 * all children of this process are still running, then this function
 * blocks on its own p_wait queue until one exits.
 *
 * If pid is greater than 0 and the given pid is a child of the
 * current process then wait for the given pid to exit and dispose
 * of it.
 *
 * If the current process has no children, or the given pid is not
 * a child of the current process return -ECHILD.
 *
 * Pids other than -1 and positive numbers are not supported.
 * Options other than 0 are not supported.
 */

static void cleanup_child_proc(proc_t *p){
    KASSERT(p->p_state == PROC_DEAD && "attempting to clean up a running process\n");

   list_remove(&p->p_child_link); 
   list_remove(&p->p_list_link);
   pt_destroy_pagedir(p->p_pagedir);
   p->p_pagedir = NULL;
   slab_obj_free(proc_allocator, p);
}
pid_t
do_waitpid(pid_t pid, int options, int *status)
{
	//dbg(DBG_TEST, "(GRADING1C 1)\n");
	if (options != 0)            return -EINVAL;
    	if (pid == 0 || pid < -1)    return -EINVAL; //removed pid == 0 || 
	
	proc_t *p = curproc;
	dbg(DBG_TEST, "Proc %d came to reap child : %d\n", curproc->p_pid, pid);
	int size =0;
	proc_t *ch = NULL;
	list_iterate_begin(&p->p_children, ch, proc_t, p_child_link){
	size++;
	}list_iterate_end();
	if (list_empty(&p->p_children)){ 
		//dbg(DBG_TEST, "EXIT\n");
		return -ECHILD; }            
	
	if(pid == (pid_t)-1){
		
		while(1){
			 //proc_t *ch;
			ch = NULL;
			list_iterate_begin(&p->p_children, ch, proc_t, 							p_child_link) {
				if (ch->p_state == PROC_DEAD) {

					pid_t cpid = ch->p_pid;
					if (status) *status = ch->p_status;

					//list_remove(&ch->p_child_link);
					//list_remove(&ch->p_list_link);
					//pt_destroy_pagedir(ch->p_pagedir);
					
					//dbg(DBG_TEST, "PPPPPPPPPP->CH = %d\n", sizeof(p->p_children));
					//if(ch->p_pagedir) pt_destroy_pagedir(ch->p_pagedir);
					cleanup_child_proc(ch);
					//slab_obj_free(proc_allocator, (void *) ch);
					//dbg(DBG_TEST, "IIIIIIIIIIIIIIIIIIIIIIIII\n");
					return cpid;              
				}else{
					//dbg(DBG_TEST, "\nChild (make dir) %d:", ch->p_pid);
				}
			} list_iterate_end();
			sched_sleep_on(&p->p_wait);
		}	
	}
	if(pid > 0){
		//dbg(DBG_TEST, "PID > 0 \n");
		proc_t *it = NULL, *ch = NULL;
		list_iterate_begin(&p->p_children, it, proc_t, 							p_child_link) {
			if (it->p_pid == pid) {
				ch = it;        
			}
		} list_iterate_end();

		if(!ch) return -ECHILD; //no child found

		while(1){
			if(ch->p_state == PROC_DEAD){
				pid_t cpid = ch->p_pid;
				if (status) *status = ch->p_status;

				//dbg(DBG_TEST, "Calling List remove\n");
				//list_remove(&ch->p_child_link);
				//list_remove(&ch->p_list_link);
				//if(ch->p_pagedir) pt_destroy_pagedir(ch->p_pagedir);
				
				//pt_destroy_pagedir(ch->p_pagedir);
				cleanup_child_proc(ch);
				//slab_obj_free(proc_allocator, (void *) ch);
				return cpid;  
			}
			sched_sleep_on(&p->p_wait);
		}
	}
	//dbg(DBG_PRINT, "(GRADING1E)\n");
	return -EINVAL;
}

/*
 * Cancel all threads and join with them (if supporting MTP), and exit from the current
 * thread.
 *
 * @param status the exit status of the process
 */
void
do_exit(int status)
{
	//dbg(DBG_PRINT, "(GRADING1C 1)\n");
dbg(DBG_TEST, "do_exit: pid=%d exiting with status=%d\n", curproc->p_pid, status);
	kthread_cancel(curthr, (void*)(intptr_t)status);
	//dbg(DBG_PRINT, "(GRADING1E)\n");
	return;
}



