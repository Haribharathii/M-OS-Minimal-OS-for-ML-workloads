
#include "test/kshell/kshell.h"
#include "types.h"
#include "globals.h"
#include "kernel.h"
#include "errno.h"
#include "drivers/tty/tty.h"
#include "util/gdb.h"
#include "util/init.h"
#include "util/debug.h"
#include "util/string.h"
#include "util/printf.h"

#include "mm/mm.h"
#include "mm/page.h"
#include "mm/pagetable.h"
#include "mm/pframe.h"

#include "vm/vmmap.h"
#include "vm/shadowd.h"
#include "vm/shadow.h"
#include "vm/anon.h"

#include "main/acpi.h"
#include "main/apic.h"
#include "main/interrupt.h"
#include "main/gdt.h"

#include "proc/sched.h"
#include "proc/proc.h"
#include "proc/kthread.h"

#include "drivers/dev.h"
#include "drivers/blockdev.h"
#include "drivers/disk/ata.h"
#include "drivers/tty/virtterm.h"
#include "drivers/pci.h"

#include "api/exec.h"
#include "api/syscall.h"

#include "fs/vfs.h"
#include "fs/vnode.h"
#include "fs/vfs_syscall.h"
#include "fs/fcntl.h"
#include "fs/stat.h"
#include "fs/s5fs/s5fs.h"
#include "fs/ramfs/ramfs.h"
#include "test/kshell/kshell.h"
#include "test/s5fs_test.h"

#include "drivers/dev.h"
#include "fs/stat.h"

GDB_DEFINE_HOOK(initialized)

void      *bootstrap(int arg1, void *arg2);
void      *idleproc_run(int arg1, void *arg2);
kthread_t *initproc_create(void);
void      *initproc_run(int arg1, void *arg2);
void      *final_shutdown(void);
extern void *faber_thread_test(int, void *);       // from kernel/proc/faber_test.c
extern void *sunghan_test(int, void *);            // from kernel/proc/sunghan_test.c
extern void *sunghan_deadlock_test(int, void *);   // from kernel/proc/sunghan_test.c
extern int vfstest_main(int , char **);

extern int faber_fs_thread_test(kshell_t *ksh, int argc, char **argv);
extern int faber_directory_test(kshell_t *ksh, int argc, char **argv);
/**
 * This function is called from kmain, however it is not running in a
 * thread context yet. It should create the idle process which will
 * start executing idleproc_run() in a real thread context.  To start
 * executing in the new process's context call context_make_active(),
 * passing in the appropriate context. This function should _NOT_
 * return.
 *
 * Note: Don't forget to set curproc and curthr appropriately.
 *
 * @param arg1 the first argument (unused)
 * @param arg2 the second argument (unused)
 */
void *
bootstrap(int arg1, void *arg2)
{
        //dbg(DBG_PRINT, "(GRADING1A)\n");
        /* If the next line is removed/altered in your submission, 20 points will be deducted. */
        dbgq(DBG_TEST, "SIGNATURE: 53616c7465645f5fd3ed17de9ecb2e52f8b4ef972c11d2e5d26d5a9770c49dce535860ccf09730378ef0057a8842f41e\n");
        /* necessary to finalize page table information */
        pt_template_init();

	proc_t *idle = proc_create("idle");
    	KASSERT(idle != NULL);
	kthread_t *idle_thr = kthread_create(idle, idleproc_run, 0, NULL);
    	KASSERT(idle_thr != NULL);
  	
    	curproc = idle;
    	curthr  = idle_thr;

        KASSERT(NULL != curproc);
        //dbg(DBG_PRINT, "(GRADING1A 1.a)\n");

        KASSERT(PID_IDLE == curproc->p_pid);
        //dbg(DBG_PRINT, "(GRADING1A 1.a)\n");

        KASSERT(NULL != curthr);
        //dbg(DBG_PRINT, "(GRADING1A 1.a)\n");

	context_make_active(&idle_thr->kt_ctx);

        panic("weenix returned to bootstrap()!!! BAD!!!\n");
        return NULL;
}

/**
 * Once we're inside of idleproc_run(), we are executing in the context of the
 * first process-- a real context, so we can finally begin running
 * meaningful code.
 *
 * This is the body of process 0. It should initialize all that we didn't
 * already initialize in kmain(), launch the init process (initproc_run),
 * wait for the init process to exit, then halt the machine.
 *
 * @param arg1 the first argument (unused)
 * @param arg2 the second argument (unused)
 */
void *
idleproc_run(int arg1, void *arg2)
{
        int status;
        pid_t child;

//dbg(DBG_PRINT, "do_mknod /dev/null 0->");
        /* create init proc */
        kthread_t *initthr = initproc_create();
//dbg(DBG_PRINT, "do_mknod /dev/null 1->");
dbg(DBG_TEST, "!!!!!!!!!!!!!!!!!!!!!!");
        init_call_all();
//dbg(DBG_PRINT, "do_mknod /dev/null 2->");
        GDB_CALL_HOOK(initialized);
	
        /* Create other kernel threads (in order) */


#ifdef __VFS__
        /* Once you have VFS remember to set the current working directory
         * of the idle and init processes */
        
        // Set CWD for the idle process (PID 0)
        curproc->p_cwd = vfs_root_vn;
        vref(vfs_root_vn);
        
        // Set CWD for the init process (PID 1)
        initthr->kt_proc->p_cwd = vfs_root_vn;
        vref(vfs_root_vn);
	//dbg(DBG_PRINT, "do_mknod /dev/null hari->");
        

#ifdef __DRIVERS__
        /*
         * Initialize device subsystems BEFORE creating device nodes.
         * There's usually a dev_init() (or separate memdev_init()/tty_init()).
         */
	


        /* Ensure /dev exists (ignore if already exists) */
        int ret = do_mkdir("/dev");
        KASSERT(ret == 0 || ret == -EEXIST);

        /* Create device nodes. Use correct devid macros from your headers. */
dbg(DBG_TEST, "@@@@@@@@@@@@@@@@@@@@@@");
	do_mknod("/dev/null", S_IFCHR, MEM_NULL_DEVID);
dbg(DBG_TEST, "@@@@@@@@@@@@@@@@@@@@@@");
	do_mknod("/dev/zero", S_IFCHR, MEM_ZERO_DEVID);

	//do_mknod("/dev/tty0", S_IFCHR, MKDEVID(TTY_MAJOR, 0));
	do_mknod("/dev/tty0", S_IFCHR, MKDEVID(TTY_MAJOR, 0));
do_mknod("/dev/tty1", S_IFCHR, MKDEVID(TTY_MAJOR, 1));
do_mknod("/dev/tty2", S_IFCHR, MKDEVID(TTY_MAJOR, 2));
            

#endif /* _DRIVERS_ */

#endif /* _VFS_ */

        /* Finally, enable interrupts (we want to make sure interrupts
         * are enabled AFTER all drivers are initialized) */
        intr_enable();

        /* Run initproc */
        sched_make_runnable(initthr);
        /* Now wait for it */
	
        child = do_waitpid(-1, 0, &status);
	//dbg(DBG_TEST, "@@@@@@@@@@@@@@@@@@@@@@############$$$$$$$$$$$$$$$$$$pid INIT: %d child=%d\n", PID_INIT, child);
        KASSERT(PID_INIT == child);
dbg(DBG_TEST, "%d ------------------- %d\n", vfs_root_vn->vn_refcount, vfs_root_vn->vn_nrespages);
	//while (vfs_root_vn->vn_refcount > vfs_root_vn->vn_nrespages + 1) {
	//    vput(vfs_root_vn);
	//}
	//vfs_root_vn->vn_fs->fs_op->umount(vfs_root_vn->vn_fs);
	//vput(vfs_root_vn);
	    /* Shutdown the pframe system */
	dbg(DBG_TEST, "@@@@@@@@@@@@@@@@@@@@@@############$$$$$$$$$$$$$$$$$$pid ");
	
	
        return final_shutdown();
}

/**
 * This function, called by the idle process (within 'idleproc_run'), creates the
 * process commonly refered to as the "init" process, which should have PID 1.
 *
 * The init process should contain a thread which begins execution in
 * initproc_run().
 *
 * @return a pointer to a newly created thread which will execute
 * initproc_run when it begins executing
 */
kthread_t *
initproc_create(void)
{
	proc_t *init = proc_create("init");
	KASSERT(init != NULL);
	kthread_t *thr = kthread_create(init, initproc_run, 0, NULL);
	KASSERT(thr != NULL);

        KASSERT(NULL != init);
        //dbg(DBG_PRINT, "(GRADING1A 1.b)\n");

        KASSERT(PID_INIT == init->p_pid);
        //dbg(DBG_PRINT, "(GRADING1A 1.b)\n");

        KASSERT(NULL != thr);
        //dbg(DBG_PRINT, "(GRADING1A 1.b)\n");

	KASSERT(curproc);                  // should be idle
	KASSERT(curproc->p_pproc == NULL); // idle has no parent
	// after proc_create("init"):
	KASSERT(init->p_pproc == curproc);
	KASSERT(!list_empty(&curproc->p_children));
        return thr;
}


static int cmd_faber(kshell_t *ksh, int argc, char **argv) {
    proc_t *p = proc_create("faber_proc");
    if (!p) return -ENOMEM;
    kthread_t *t = kthread_create(p, faber_thread_test, 0, NULL);
    if (!t) return -ENOMEM;
    sched_make_runnable(t);
    int status = 0;
    do_waitpid(p->p_pid, 0, &status);     /* reap when done */
    return 0;
}

static int cmd_sunghan(kshell_t *ksh, int argc, char **argv) {
    proc_t *p = proc_create("sunghan_proc");
    if (!p) return -ENOMEM;
    kthread_t *t = kthread_create(p, sunghan_test, 0, NULL);
    if (!t) return -ENOMEM;
    sched_make_runnable(t);
    int status = 0;
    do_waitpid(p->p_pid, 0, &status);
    return 0;
}

static int cmd_sunghan_deadlock(kshell_t *ksh, int argc, char **argv) {
    proc_t *p = proc_create("sunghan_deadlock_proc");
    if (!p) return -ENOMEM;
    kthread_t *t = kthread_create(p, sunghan_deadlock_test, 0, NULL);
    if (!t) return -ENOMEM;
    sched_make_runnable(t);
    int status = 0;
    do_waitpid(p->p_pid, 0, &status);
    return 0;
}


#ifdef __DRIVERS__
static void *
vfstest_thread_wrapper(int arg1, void *arg2)
{
    
    char *argv[] = { NULL };
    vfstest_main(1, argv);
    return NULL;
}

static int
kshell_vfstest_wrapper(kshell_t *ksh, int argc, char **argv)
{
    //kprintf(ksh, "Starting vfstest_main thread...\n");
    proc_t *p = proc_create("vfstest");
    if (!p) return -ENOMEM;
    kthread_t *t = kthread_create(p, vfstest_thread_wrapper, 1, NULL);
    if (!t) {
        //kprintf(ksh, "Failed to create vfstest thread\n");
	dbg(DBG_PRINT, "(GRADING2B)\n");
        return -ENOMEM;
    }

    sched_make_runnable(t);   
    int status = 0;
    do_waitpid(p->p_pid, 0, &status);
    return 0;
}
#endif



/**
 * The init thread's function changes depending on how far along your Weenix is
 * developed. Before VM/FI, you'll probably just want to have this run whatever
 * tests you've written (possibly in a new process). After VM/FI, you'll just
 * exec "/sbin/init".
 *
 * Both arguments are unused.
 *
 * @param arg1 the first argument (unused)
 * @param arg2 the second argument (unused)
 */
static int test1(kshell_t *ksh, int argc, char **argv) {
    dbg(DBG_TEST, "test1: hello from kernel!\n");
    return 0;
}

static int test2(kshell_t *ksh, int argc, char **argv) {
    dbg(DBG_TEST, "test2: argc=%d\n", argc);
    for (int i = 0; i < argc; i++) {
        dbg(DBG_TEST, "  argv[%d]=%s\n", i, argv[i] ? argv[i] : "(null)");
    }
    return 0;
}
void *
initproc_run(int arg1, void *arg2)
{
#if 0
	#ifdef __DRIVERS__
	kshell_add_command("test1", test1, "tests something...");
	kshell_add_command("test2", test2, "tests something else...");
	kshell_add_command("faber", cmd_faber, "run faber_thread_test");
	kshell_add_command("sunghan", cmd_sunghan, "run sunghan_test");
	kshell_add_command("sunghan_deadlock", cmd_sunghan_deadlock, "run sunghan_deadlock_test");
	kshell_add_command("vfstest", kshell_vfstest_wrapper, "Run vfstest_main().");	
	kshell_add_command("thrtest", faber_fs_thread_test, "Run faber_fs_thread_test().");
	kshell_add_command("dirtest", faber_directory_test, "Run faber_directory_test().");
	/* One kernel shell on TTY 0 */
	
	kshell_t *ksh = kshell_create(0);
	if (ksh == NULL) panic("init: Couldn't create kernel shell\n");

	int err;
	while ((err = kshell_execute_next(ksh)) > 0) { }

	KASSERT(err == 0 && "kernel shell exited with an error");
	kshell_destroy(ksh);
	#endif /* __DRIVERS__ */
#endif
#if 0
	int fd;
	
	dbg(DBG_TEST, "initproc: opening /dev/tty0 for stdin\n");
	fd = do_open("/dev/tty0", O_RDONLY);
	if (fd < 0) {
		panic("initproc: failed to open /dev/tty0 for stdin: %d\n", fd);
	}
	KASSERT(fd == 0 && "stdin should be fd 0");
	
	dbg(DBG_TEST, "initproc: opening /dev/tty0 for stdout\n");
	fd = do_open("/dev/tty0", O_WRONLY);
	if (fd < 0) {
		panic("initproc: failed to open /dev/tty0 for stdout: %d\n", fd);
	}
	KASSERT(fd == 1 && "stdout should be fd 1");
	
	dbg(DBG_TEST, "initproc: opening /dev/tty0 for stderr\n");
	fd = do_open("/dev/tty0", O_WRONLY);
	if (fd < 0) {
		panic("initproc: failed to open /dev/tty0 for stderr: %d\n", fd);
	}
	KASSERT(fd == 2 && "stderr should be fd 2");



	fd = do_open("/dev/tty1", O_RDONLY);
	if (fd < 0) {
		panic("initproc: failed to open /dev/tty1 for stdin: %d\n", fd);
	}
	KASSERT(fd == 0 && "stdin should be fd 0");
	
	dbg(DBG_TEST, "initproc: opening /dev/tty1 for stdout\n");
	fd = do_open("/dev/tty1", O_WRONLY);
	if (fd < 0) {
		panic("initproc: failed to open /dev/tty1 for stdout: %d\n", fd);
	}
	KASSERT(fd == 1 && "stdout should be fd 1");
	
	dbg(DBG_TEST, "initproc: opening /dev/tty1 for stderr\n");
	fd = do_open("/dev/tty1", O_WRONLY);
	if (fd < 0) {
		panic("initproc: failed to open /dev/tty1 for stderr: %d\n", fd);
	}
	KASSERT(fd == 2 && "stderr should be fd 2");



	fd = do_open("/dev/tty2", O_RDONLY);
	if (fd < 0) {
		panic("initproc: failed to open /dev/tty2 for stdin: %d\n", fd);
	}
	KASSERT(fd == 0 && "stdin should be fd 0");
	
	dbg(DBG_TEST, "initproc: opening /dev/tty2 for stdout\n");
	fd = do_open("/dev/tty2", O_WRONLY);
	if (fd < 0) {
		panic("initproc: failed to open /dev/tty2 for stdout: %d\n", fd);
	}
	KASSERT(fd == 1 && "stdout should be fd 1");
	
	dbg(DBG_TEST, "initproc: opening /dev/tty2 for stderr\n");
	fd = do_open("/dev/tty2", O_WRONLY);
	if (fd < 0) {
		panic("initproc: failed to open /dev/tty2 for stderr: %d\n", fd);
	}
	KASSERT(fd == 2 && "stderr should be fd 2");
	dbg(DBG_TEST, "initproc: stdin/stdout/stderr set up successfully\n");
	    char *const argvec[] = { "/usr/bin/args", "ab", "cde", "fghi", "j", NULL };
    
#endif
char *const envvec[] = { NULL };
 //   kernel_execve("/usr/bin/args", argvec, envvec);
	char *argv[] = { "/bin/ls.c", NULL };
	char *envp[] = { NULL };
	dbg(DBG_TEST, "YESSSSSSS\n");
	//kernel_execve("/usr/bin/fork-and-wait", argv, envp);
	//kernel_execve("/usr/bin/hello", argv, envp); 
	//kernel_execve("/usr/bin/forkbomb", argv, envp);
	kernel_execve("sbin/init", argv, envp); 
	//kernel_execve("/usr/bin/vfstest", argv, envp);
	//kernel_execve("/usr/bin/forktest", argv, envp);
	//kernel_execve("/usr/bin/memtest", argv, envp); 
	//kernel_execve("/bin/sh", argv, envp);
	//kernel_execve("/usr/bin/tests/malloctest", argv, envp);
	// Should never return
	panic("initproc returned from kernel_execve!\n");

	do_exit(0);

        // dbg(DBG_PRINT, "(GRADING1A)\n");             
	return NULL;            
}
