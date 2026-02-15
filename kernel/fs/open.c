
/*
 *  FILE: open.c
 *  AUTH: mcc | jal
 *  DESC:
 *  DATE: Mon Apr  6 19:27:49 1998
 */

#include "globals.h"
#include "errno.h"
#include "fs/fcntl.h"
#include "util/string.h"
#include "util/printf.h"
#include "fs/vfs.h"
#include "fs/vnode.h"
#include "fs/file.h"
#include "fs/vfs_syscall.h"
#include "fs/open.h"
#include "fs/stat.h"
#include "util/debug.h"
#define O_ACCMODE 0x3

/* find empty index in p->p_files[] */
int
get_empty_fd(proc_t *p)
{
        int fd;

        for (fd = 0; fd < NFILES; fd++) {
                if (!p->p_files[fd])
                        return fd;
        }

        dbg(DBG_ERROR | DBG_VFS, "ERROR: get_empty_fd: out of file descriptors "
            "for pid %d\n", curproc->p_pid);
        return -EMFILE;
}

/*
 * There a number of steps to opening a file:
 *      1. Get the next empty file descriptor.
 *      2. Call fget to get a fresh file_t.
 *      3. Save the file_t in curproc's file descriptor table.
 *      4. Set file_t->f_mode to OR of FMODE_(READ|WRITE|APPEND) based on
 *         oflags, which can be O_RDONLY, O_WRONLY or O_RDWR, possibly OR'd with
 *         O_APPEND.
 *      5. Use open_namev() to get the vnode for the file_t.
 *      6. Fill in the fields of the file_t.
 *      7. Return new fd.
 *
 * If anything goes wrong at any point (specifically if the call to open_namev
 * fails), be sure to remove the fd from curproc, fput the file_t and return an
 * error.
 *
 * Error cases you must handle for this function at the VFS level:
 *      o EINVAL
 *        oflags is not valid.
 *      o EMFILE
 *        The process already has the maximum number of files open.
 *      o ENOMEM
 *        Insufficient kernel memory was available.
 *      o ENAMETOOLONG
 *        A component of filename was too long.
 *      o ENOENT
 *        O_CREAT is not set and the named file does not exist.  Or, a
 *        directory component in pathname does not exist.
 *      o EISDIR
 *        pathname refers to a directory and the access requested involved
 *        writing (that is, O_WRONLY or O_RDWR is set).
 *      o ENXIO
 *        pathname refers to a device special file and no corresponding device
 *        exists.
 */

int
do_open(const char *filename, int oflags)
{
        vnode_t *vn = NULL;
    file_t *file = NULL;
    int fd, ret;
dbg(DBG_TEST, "%d processss open\n", curproc->p_pid);
    /* Step 1: Validate flags */
    int access_mode = oflags & O_ACCMODE;
    if (access_mode != O_RDONLY && access_mode != O_WRONLY && access_mode != O_RDWR){
        dbg(DBG_PRINT, "(GRADING2B)\n");
	return -EINVAL;
}

    /* Step 2: Get empty fd slot */
    fd = get_empty_fd(curproc);
    if (fd < 0)
        return fd;  // already -EMFILE

    /* Step 3: Allocate file structure */
    file = fget(-1);   // creates a new file_t with refcount 1
    if (!file){
        dbg(DBG_PRINT, "(GRADING2B)\n");
        return -ENOMEM;
}

    /* Step 4: Get vnode for path */
    ret = open_namev(filename, oflags, &vn, NULL);
    if (ret < 0) {
        fput(file);
	dbg(DBG_PRINT, "(GRADING2B)\n");
        return ret;
    }

    /* Step 5: If directory and writing requested -> EISDIR */
    if (S_ISDIR(vn->vn_mode) && (access_mode == O_WRONLY || access_mode == O_RDWR)) {
        vput(vn);
        fput(file);
        dbg(DBG_PRINT, "(GRADING2B)\n");
        return -EISDIR;
    }

    /* Step 6: Fill file structure */
    file->f_pos = 0;
    file->f_vnode = vn;
    file->f_mode = 0;

    if (access_mode == O_RDONLY)
        file->f_mode |= FMODE_READ;
    else if (access_mode == O_WRONLY){
	dbg(DBG_PRINT, "(GRADING2C)\n");
        file->f_mode |= FMODE_WRITE;
    }
    else if (access_mode == O_RDWR){
	dbg(DBG_PRINT, "(GRADING2B)\n");
        file->f_mode |= (FMODE_READ | FMODE_WRITE);
    }

    if (oflags & O_APPEND)
        file->f_mode |= FMODE_APPEND;

    /* Step 7: Add to process table */
    curproc->p_files[fd] = file;
    dbg(DBG_PRINT, "(GRADING2B)\n");
    /* Step 8: Success */
    return fd;
}
