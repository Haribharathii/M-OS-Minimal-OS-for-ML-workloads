
/*
 *  FILE: vfs_syscall.c
 *  AUTH: mcc | jal
 *  DESC:
 *  DATE: Wed Apr  8 02:46:19 1998
 *  $Id: vfs_syscall.c,v 1.2 2018/05/27 03:57:26 cvsps Exp $
 */

#include "kernel.h"
#include "errno.h"
#include "globals.h"
#include "fs/vfs.h"
#include "fs/file.h"
#include "fs/vnode.h"
#include "fs/vfs_syscall.h"
#include "fs/open.h"
#include "fs/fcntl.h"
#include "fs/lseek.h"
#include "mm/kmalloc.h"
#include "util/string.h"
#include "util/printf.h"
#include "fs/stat.h"
#include "util/debug.h"

/*
 * Syscalls for vfs. Refer to comments or man pages for implementation.
 * Do note that you don't need to set errno, you should just return the
 * negative error code.
 */

/* To read a file:
 *      o fget(fd)
 *      o call its virtual read vn_op
 *      o update f_pos
 *      o fput() it
 *      o return the number of bytes read, or an error
 *
 * Error cases you must handle for this function at the VFS level:
 *      o EBADF
 *        fd is not a valid file descriptor or is not open for reading.
 *      o EISDIR
 *        fd refers to a directory.
 *
 * In all cases, be sure you do not leak file refcounts by returning before
 * you fput() a file that you fget()'ed.
 */
int
do_read(int fd, void *buf, size_t nbytes)
{
        // NOT_YET_IMPLEMENTED("VFS: do_read");
        if(fd < 0 || fd >= NFILES){
             dbg(DBG_PRINT, "(GRADING2B)\n");
             return -EBADF;   
        }

        file_t *ft = fget(fd);
        
        if(ft == NULL) return -EBADF;   
        
        if(S_ISDIR(ft->f_vnode->vn_mode)){
             fput(ft);
             dbg(DBG_PRINT, "(GRADING2B)\n");
             return -EISDIR;   
        }
        
        if(!(ft->f_mode & FMODE_READ)){
             fput(ft);
             dbg(DBG_PRINT, "(GRADING2B)\n");
             return -EBADF;   
        }
    if (!ft->f_vnode->vn_ops || !ft->f_vnode->vn_ops->read) {
        fput(ft);
        dbg(DBG_PRINT, "(GRADING2B)\n");
        return -EBADF;   // or -EBADF per your handout; vfstest usually doesn't hit this
    }
        int count = ft->f_vnode->vn_ops->read(ft->f_vnode, ft->f_pos, buf, nbytes);
        if (count >= 0){
                ft->f_pos += count;
		dbg(DBG_PRINT, "(GRADING2B)\n"); 
        }
        fput(ft);
        dbg(DBG_PRINT, "(GRADING2B)\n");
        return count;
}

/* Very similar to do_read.  Check f_mode to be sure the file is writable.  If
 * f_mode & FMODE_APPEND, do_lseek() to the end of the file, call the write
 * vn_op, and fput the file.  As always, be mindful of refcount leaks.
 *
 * Error cases you must handle for this function at the VFS level:
 *      o EBADF
 *        fd is not a valid file descriptor or is not open for writing.
 */
int
do_write(int fd, const void *buf, size_t nbytes)
{
    if (fd < 0 || fd >= NFILES)
        return -EBADF;

    file_t *ft = fget(fd);
    if (!ft){
        dbg(DBG_PRINT, "(GRADING2B)\n");
        return -EBADF;
}

    vnode_t *vn = ft->f_vnode;
    if (!vn) {
        fput(ft);
        dbg(DBG_PRINT, "(GRADING2B)\n");
        return -EBADF;
    }

    if (!(ft->f_mode & FMODE_WRITE)) {
        fput(ft);
        dbg(DBG_PRINT, "(GRADING2B)\n");
        return -EBADF;
    }

    if (!vn->vn_ops || !vn->vn_ops->write) {
        fput(ft);
        dbg(DBG_PRINT, "(GRADING2B)\n");
        return -EBADF;
    }

    if (ft->f_mode & FMODE_APPEND) {
        /* Seek to end (for regular files) */
        ft->f_pos = vn->vn_len;
    }

    dbg(DBG_PRINT, "(GRADING2B)\n");
    int count = vn->vn_ops->write(vn, ft->f_pos, buf, nbytes);
    if (count >= 0) {
        ft->f_pos += count;
        KASSERT(S_ISCHR(vn->vn_mode) ||
                S_ISBLK(vn->vn_mode) ||
                (S_ISREG(vn->vn_mode) && ft->f_pos <= vn->vn_len));
        dbg(DBG_PRINT, "(GRADING2A 3.a)\n");
	
    }

    fput(ft);
    dbg(DBG_PRINT, "(GRADING2B)\n");
    return count;
}

/*
 * Zero curproc->p_files[fd], and fput() the file. Return 0 on success
 *
 * Error cases you must handle for this function at the VFS level:
 *      o EBADF
 *        fd isn't a valid open file descriptor.
 */
int
do_close(int fd)
{
        // NOT_YET_IMPLEMENTED("VFS: do_close");
        if(fd < 0 || fd >= NFILES || curproc->p_files[fd] == NULL){
	     dbg(DBG_PRINT, "(GRADING2B)\n");
             return -EBADF;   
        }

        fput(curproc->p_files[fd]);
        curproc->p_files[fd] = NULL;

        dbg(DBG_PRINT, "(GRADING2B)\n");
        return 0;
}


/* To dup a file:
 *      o fget(fd) to up fd's refcount
 *      o get_empty_fd()c
 *      o point the new fd to the same file_t* as the given fd
 *      o return the new file descriptor
 *
 * Don't fput() the fd unless something goes wrong.  Since we are creating
 * another reference to the file_t*, we want to up the refcount.
 *
 * Error cases you must handle for this function at the VFS level:
 *      o EBADF
 *        fd isn't an open file descriptor.
 *      o EMFILE
 *        The process already has the maximum number of file descriptors open
 *        and tried to open a new one.
 */
int
do_dup(int fd)
{
        // NOT_YET_IMPLEMENTED("VFS: do_dup");
        if(fd < 0 || fd >= NFILES){
             dbg(DBG_PRINT, "(GRADING2B)\n");
             return -EBADF;   
        }

        file_t *ft = fget(fd);

        if(ft == NULL){ 
	        dbg(DBG_PRINT, "(GRADING2B)\n");
		return -EBADF;
	}
        
        int newfd = get_empty_fd(curproc);
        
        if(newfd == -EMFILE){
                fput(ft);
                dbg(DBG_PRINT, "(GRADING2B)\n");
                return -EMFILE;
        }
        
        curproc->p_files[newfd] = ft;
        dbg(DBG_PRINT, "(GRADING2B)\n");
        return newfd;
}

/* Same as do_dup, but insted of using get_empty_fd() to get the new fd,
 * they give it to us in 'nfd'.  If nfd is in use (and not the same as ofd)
 * do_close() it first.  Then return the new file descriptor.
 *
 * Error cases you must handle for this function at the VFS level:
 *      o EBADF
 *        ofd isn't an open file descriptor, or nfd is out of the allowed
 *        range for file descriptors.
 */
int
do_dup2(int ofd, int nfd)
{
        // NOT_YET_IMPLEMENTED("VFS: do_dup2");
        if(ofd < 0 || ofd >= NFILES){
             dbg(DBG_PRINT, "(GRADING2B)\n");
             return -EBADF;   
        }
        file_t *ft = fget(ofd);
        if(ft == NULL) return -EBADF;
        if(nfd < 0 || nfd >= NFILES){
             fput(ft);
             dbg(DBG_PRINT, "(GRADING2B)\n");
             return -EBADF;   
        }
	if (ofd == nfd) {
	     fput(ft);         
	     return nfd;        
    	}
        if(curproc->p_files[nfd] != NULL && nfd != ofd){
                do_close(nfd);
        }
        curproc->p_files[nfd] = ft;
    	dbg(DBG_PRINT, "(GRADING2B)\n");
        return nfd;
}

/*
 * This routine creates a special file of the type specified by 'mode' at
 * the location specified by 'path'. 'mode' should be one of S_IFCHR or
 * S_IFBLK (you might note that mknod(2) normally allows one to create
 * regular files as well-- for simplicity this is not the case in Weenix).
 * 'devid', as you might expect, is the device identifier of the device
 * that the new special file should represent.
 *
 * You might use a combination of dir_namev, lookup, and the fs-specific
 * mknod (that is, the containing directory's 'mknod' vnode operation).
 * Return the result of the fs-specific mknod, or an error.
 *
 * Error cases you must handle for this function at the VFS level:
 *      o EINVAL
 *        mode requested creation of something other than a device special
 *        file.
 *      o EEXIST
 *        path already exists.
 *      o ENOENT
 *        A directory component in path does not exist.
 *      o ENOTDIR
 *        A component used as a directory in path is not, in fact, a directory.
 *      o ENAMETOOLONG
 *        A component of path was too long.
 */
int
do_mknod(const char *path, int mode, unsigned devid)
{
    const char *name;
    size_t namelen;
    vnode_t *parent = NULL;
    vnode_t *exists = NULL;
    int ret;

    /* Only character or block special files are supported. */
    if (!S_ISCHR(mode) && !S_ISBLK(mode)) {
        dbg(DBG_PRINT, "(GRADING2B)\n");
        return -EINVAL;
    }

    /* Resolve the parent directory and final component name. */
    ret = dir_namev(path, &namelen, &name, NULL, &parent);
    if (ret < 0) {
        return ret; /* ENOENT, ENOTDIR, ENAMETOOLONG propagated */
    }

    /* Sanity check on name length (dir_namev normally enforces this) */
    if (namelen == 0 || namelen >= NAME_LEN) {
        vput(parent);
        dbg(DBG_PRINT, "(GRADING2B)\n");
        return -ENAMETOOLONG;
    }

    /* If the target already exists, fail with EEXIST. */
    ret = lookup(parent, name, namelen, &exists);
    if (ret == 0) {
        /* file already exists */
        vput(exists);
        vput(parent);
        dbg(DBG_PRINT, "(GRADING2B)\n");
        return -EEXIST;
    } 
	if (ret != -ENOENT) {
        /* some other lookup error */
        vput(parent);
        return ret;
    }

    /* Ensure the parent supports mknod (filesystem-specific implementation). */
    if (!parent->vn_ops || !parent->vn_ops->mknod) {
        vput(parent);
        dbg(DBG_PRINT, "(GRADING2B)\n");
        return -ENOTDIR;
    }

    /* Delegate to filesystem-specific mknod.
     * Note: vnode-level mknod signature is:
     *   int (*mknod)(struct vnode *dir, const char *name, size_t name_len,
     *                int mode, devid_t devid);
     */
    KASSERT(NULL != parent->vn_ops->mknod);
    dbg(DBG_PRINT, "(GRADING2A 3.b)\n");
   
    ret = parent->vn_ops->mknod(parent, name, namelen, mode, (devid_t)devid);
    

    /* Release parent vnode reference and return filesystem result. */
    vput(parent);
    dbg(DBG_PRINT, "(GRADING2A)\n");
    return ret;
}

/* Use dir_namev() to find the vnode of the dir we want to make the new
 * directory in.  Then use lookup() to make sure it doesn't already exist.
 * Finally call the dir's mkdir vn_ops. Return what it returns.
 *
 * Error cases you must handle for this function at the VFS level:
 *      o EEXIST
 *        path already exists.
 *      o ENOENT
 *        A directory component in path does not exist.
 *      o ENOTDIR
 *        A component used as a directory in path is not, in fact, a directory.
 *      o ENAMETOOLONG
 *        A component of path was too long.
 */
int
do_mkdir(const char *path)
{
    vnode_t *dir = NULL;
    vnode_t *res = NULL;
    // vnode_t *newdir = NULL; // This variable is not used
    const char *name = NULL;
    size_t namelen = 0;
    int ret;

    dbg(DBG_PRINT, "(GRADING2B)\n");
    if ((ret = dir_namev(path, &namelen, &name, NULL, &dir)) < 0)
        return ret;

    if (namelen == 0) {
        vput(dir);
        dbg(DBG_PRINT, "(GRADING2B)\n");
        return -EEXIST;
    }


 
    if (namelen >= NAME_LEN) {
        vput(dir);
        dbg(DBG_PRINT, "(GRADING2B)\n");
        return -ENAMETOOLONG;
    }

    ret = lookup(dir, name, namelen, &res);
    if (ret == 0) {
        
        vput(res); 
        vput(dir); 
        dbg(DBG_PRINT, "(GRADING2B)\n");
        return -EEXIST;
    } else if (ret != -ENOENT) {
      
        vput(dir);
        return ret;
    }

    
    if (!dir->vn_ops || !dir->vn_ops->mkdir) {
        vput(dir);
        dbg(DBG_PRINT, "(GRADING2B)\n");
        return -ENOTDIR;
    }

   
    KASSERT(NULL != dir->vn_ops->mkdir);
    
    dbg(DBG_PRINT, "(GRADING2A 3.c)\n");
    
    ret = dir->vn_ops->mkdir(dir, name, namelen);

 
    vput(dir); // <-- THIS IS THE FIX
    dbg(DBG_PRINT, "(GRADING2B)\n");
    return ret;
}

/* Use dir_namev() to find the vnode of the directory containing the dir to be
 * removed. Then call the containing dir's rmdir v_op.  The rmdir v_op will
 * return an error if the dir to be removed does not exist or is not empty, so
 * you don't need to worry about that here. Return the value of the v_op,
 * or an error.
 *
 * Error cases you must handle for this function at the VFS level:
 *      o EINVAL
 *        path has "." as its final component.
 *      o ENOTEMPTY
 *        path has ".." as its final component.
 *      o ENOENT
 *        A directory component in path does not exist.
 *      o ENOTDIR
 *        A component used as a directory in path is not, in fact, a directory.
 *      o ENAMETOOLONG
 *        A component of path was too long.
 */
int
do_rmdir(const char *path)
{
        vnode_t *dir = NULL;
    vnode_t *target = NULL;
    const char *name = NULL;
    size_t namelen = 0;
    int ret;

    /* 1. Use dir_namev() to find the parent directory vnode */
    ret = dir_namev(path, &namelen, &name, NULL, &dir);
    if (ret < 0){
        return ret; /* ENOENT, ENOTDIR, etc. already handled inside dir_namev() */
}
    /* 2. Validate name length */

    if (namelen == 0) {
        vput(dir);
        dbg(DBG_PRINT, "(GRADING2B)\n");
        return -EINVAL;
    }

    if (namelen >= NAME_LEN) {
        vput(dir);
        dbg(DBG_PRINT, "(GRADING2B)\n");
        return -ENAMETOOLONG;
    }

    /* 3. Handle special cases "." and ".." */
    if (namelen == 1 && name[0] == '.') {
        vput(dir);
        dbg(DBG_PRINT, "(GRADING2B)\n");
        return -EINVAL;
    }
    if (namelen == 2 && name[0] == '.' && name[1] == '.') {
        vput(dir);
        dbg(DBG_PRINT, "(GRADING2B)\n");
        return -ENOTEMPTY;
    }

    /* 4. Lookup the directory entry we want to remove */
    ret = lookup(dir, name, namelen, &target);
    if (ret < 0) {
        vput(dir);
	dbg(DBG_PRINT, "(GRADING2B)\n");
        return ret; /* ENOENT, ENOTDIR, etc. */
    }

    /* 5. Ensure the target is a directory */
    if (!S_ISDIR(target->vn_mode)) {
        vput(target);
        vput(dir);
        dbg(DBG_PRINT, "(GRADING2B)\n");
        return -ENOTDIR;
    }

    /* 6. Check if filesystem supports rmdir */
    if (!dir->vn_ops || !dir->vn_ops->rmdir) {
        vput(target);
        vput(dir);
        dbg(DBG_PRINT, "(GRADING2B)\n");
        return -ENOTDIR;
    }

    /* 7. Call the filesystem-specific rmdir implementation */
    KASSERT(NULL != dir->vn_ops->rmdir);
    dbg(DBG_PRINT, "(GRADING2A 3.d)\n");
    dbg(DBG_PRINT, "(GRADING2C 1)\n");
    ret = dir->vn_ops->rmdir(dir, name, namelen);

    /* 8. Release references */
    vput(target);
    vput(dir);
    dbg(DBG_PRINT, "(GRADING2B)\n");
    return ret;
}

/*
 * Similar to do_rmdir, but for files.
 *
 * Error cases you must handle for this function at the VFS level:
 *      o EPERM
 *        path refers to a directory.
 *      o ENOENT
 *        Any component in path does not exist, including the element at the
 *        very end.
 *      o ENOTDIR
 *        A component used as a directory in path is not, in fact, a directory.
 *      o ENAMETOOLONG
 *        A component of path was too long.
 */
int
do_unlink(const char *path)
{
        vnode_t *dir_vn = NULL;
    vnode_t *target_vn = NULL;
    const char *name = NULL;
    size_t namelen = 0;
    int ret;

    /* 1) Resolve parent directory and basename */
    ret = dir_namev(path, &namelen, &name, NULL, &dir_vn);
    if (ret < 0)
        return ret;

    /* 2) Validate name length */

    if (namelen == 0) {
        vput(dir_vn);
        dbg(DBG_PRINT, "(GRADING2B)\n");
        return -EINVAL;
    }

    if (namelen >= NAME_LEN) {
        vput(dir_vn);
        dbg(DBG_PRINT, "(GRADING2B)\n");
        return -ENAMETOOLONG;
    }

    /* 3) Lookup target file in parent directory */
    ret = lookup(dir_vn, name, namelen, &target_vn);
    if (ret < 0) {
        vput(dir_vn);
	dbg(DBG_PRINT, "(GRADING2B)\n");
        return ret;  /* e.g., -ENOENT */
    }

    /* 4) Ensure target is not a directory */
    if (S_ISDIR(target_vn->vn_mode)) {
        vput(target_vn);
        vput(dir_vn);
        dbg(DBG_TEST, "ERROR HERE do_unlink\n");
        return -EPERM;
    }

    /* 5) Ensure the parent directory supports unlink */
    if (!dir_vn->vn_ops || !dir_vn->vn_ops->unlink) {
        vput(target_vn);
        vput(dir_vn);
        dbg(DBG_PRINT, "(GRADING2B)\n");
        return -ENOTDIR;
    }

    /* 6) Call filesystem-specific unlink operation */
    KASSERT(NULL != dir_vn->vn_ops->unlink);
    dbg(DBG_PRINT, "(GRADING2A 3.e)\n");
    dbg(DBG_PRINT, "(GRADING2C 1)\n");
    ret = dir_vn->vn_ops->unlink(dir_vn, name, namelen);

    /* 7) Clean up vnodes */
    vput(target_vn);
    vput(dir_vn);
    dbg(DBG_PRINT, "(GRADING2B)\n");
    return ret;
}

/* To link:
 *      o open_namev(from)
 *      o dir_namev(to)
 *      o call the destination dir's (to) link vn_ops.
 *      o return the result of link, or an error
 *
 * Remember to vput the vnodes returned from open_namev and dir_namev.
 *
 * Error cases you must handle for this function at the VFS level:
 *      o EEXIST
 *        to already exists.
 *      o ENOENT
 *        A directory component in from or to does not exist.
 *      o ENOTDIR
 *        A component used as a directory in from or to is not, in fact, a
 *        directory.
 *      o ENAMETOOLONG
 *        A component of from or to was too long.
 *      o EPERM
 *        from is a directory.
 */
int
do_link(const char *from, const char *to)
{
        vnode_t *from_vn = NULL;
    vnode_t *to_dir = NULL;
    vnode_t *exist = NULL;
    const char *name = NULL;
    size_t namelen = 0;
    int ret;

    /* 1) Get vnode for 'from' (open_namev will increment refcount) */
    ret = open_namev(from, 0, &from_vn, NULL);
    if (ret < 0){
 	dbg(DBG_PRINT, "(GRADING2B)\n");
        return ret;
    }

    /* 2) 'from' must not be a directory */
    if (S_ISDIR(from_vn->vn_mode)) {
        vput(from_vn);
        dbg(DBG_TEST, "ERROR HERE do_link\n");

        return -EPERM;
    }

    /* 3) Resolve parent directory of 'to' */
    ret = dir_namev(to, &namelen, &name, NULL, &to_dir);
    if (ret < 0) {
        vput(from_vn);
        return ret;
    }

    /* 4) Validate name length */
    if (namelen == 0 || namelen >= NAME_LEN) {
        vput(from_vn);
        vput(to_dir);
        dbg(DBG_PRINT, "(GRADING2B)\n");
        return -ENAMETOOLONG;
    }

    /* 5) Check destination doesn't already exist */
    ret = lookup(to_dir, name, namelen, &exist);
    if (ret == 0) {
        /* already exists */
        vput(exist);
        vput(from_vn);
        vput(to_dir);
        dbg(DBG_PRINT, "(GRADING2B)\n");
        return -EEXIST;
    } else if (ret != -ENOENT) {
        /* some other error */
        vput(from_vn);
        vput(to_dir);
	dbg(DBG_PRINT, "(GRADING2B)\n");
        return ret;
    }

    /* 6) Prevent cross-filesystem links (common behavior) */
    if (from_vn->vn_fs != to_dir->vn_fs) {
        vput(from_vn);
        vput(to_dir);
        dbg(DBG_PRINT, "(GRADING2B)\n");
        return -EXDEV;
    }

    /* 7) Ensure the target directory supports link */
    if (!to_dir->vn_ops || !to_dir->vn_ops->link) {
        vput(from_vn);
        vput(to_dir);
        return -ENOTDIR;
    }

    /* 8) Call filesystem-specific link operation */
    ret = to_dir->vn_ops->link(from_vn, to_dir, name, namelen);

    /* 9) Clean up vnodes we held */
    vput(from_vn);
    vput(to_dir);
    dbg(DBG_PRINT, "(GRADING2B)\n");
    return ret;
}

/*      o link newname to oldname
 *      o unlink oldname
 *      o return the value of unlink, or an error
 *
 * Note that this does not provide the same behavior as the
 * Linux system call (if unlink fails then two links to the
 * file could exist).
 */
int
do_rename(const char *oldname, const char *newname)
{
        vnode_t *old_vn = NULL;
    int ret;

    /* 1) Open vnode for the old name */
    ret = open_namev(oldname, 0, &old_vn, NULL);
    if (ret < 0)
        return ret;

    /* 2) Link old vnode to the new name */
    ret = do_link(oldname, newname);
    if (ret < 0) {
        vput(old_vn);
        return ret;
    }

    /* 3) Unlink the old name */
    ret = do_unlink(oldname);

    /* 4) Clean up old vnode reference */
    vput(old_vn);
    dbg(DBG_PRINT, "(GRADING2B)\n");
    return ret;
}

/* Make the named directory the current process's cwd (current working
 * directory).  Don't forget to down the refcount to the old cwd (vput()) and
 * up the refcount to the new cwd (open_namev() or vget()). Return 0 on
 * success.
 *
 * Error cases you must handle for this function at the VFS level:
 *      o ENOENT
 *        path does not exist.
 *      o ENAMETOOLONG
 *        A component of path was too long.
 *      o ENOTDIR
 *        A component of path is not a directory.
 */
int
do_chdir(const char *path)
{
        vnode_t *new_vn = NULL;
    int ret;

    /* 1) Get vnode for the target directory */
    ret = open_namev(path, 0, &new_vn, NULL);
    if (ret < 0) {
        /* Path invalid or doesn't exist */
        return ret;
    }
 dbg(DBG_TEST,
            "do_chdir: pid=%d path=\"%s\" old_cwd=%p new_vn=%p\n",
            curproc->p_pid, path, curproc->p_cwd, new_vn);
    /* 2) Ensure it is a directory */
    if (!S_ISDIR(new_vn->vn_mode)) {
        vput(new_vn);  // decrement refcount since we won’t use it
        dbg(DBG_PRINT, "(GRADING2B)\n");
        return -ENOTDIR;
    }

    /* 3) Decrement refcount of current cwd */
    if (curproc->p_cwd) {
	dbg(DBG_PRINT, "(GRADING2B)\n");
        vput(curproc->p_cwd);
    }

    /* 4) Set new cwd (refcount already incremented by open_namev) */
    curproc->p_cwd = new_vn;
    dbg(DBG_PRINT, "(GRADING2B)\n");
    return 0;
}

/* Call the readdir vn_op on the given fd, filling in the given dirent_t*.
 * If the readdir vn_op is successful, it will return a positive value which
 * is the number of bytes copied to the dirent_t.  You need to increment the
 * file_t's f_pos by this amount.  As always, be aware of refcounts, check
 * the return value of the fget and the virtual function, and be sure the
 * virtual function exists (is not null) before calling it.
 *
 * Return either 0 or sizeof(dirent_t), or -errno.
 *
 * Error cases you must handle for this function at the VFS level:
 *      o EBADF
 *        Invalid file descriptor fd.
 *      o ENOTDIR
 *        File descriptor does not refer to a directory.
 */
/*
int
do_getdent(int fd, struct dirent *dirp)
{
    file_t *f = fget(fd);
    if (!f)
        return -EBADF;

    if (fd >= NFILES) return -EBADF;

    vnode_t *vn = f->f_vnode;

    if (!S_ISDIR(vn->vn_mode)) {
        fput(f);
        return -ENOTDIR;
    }
    
    if (!vn->vn_ops || !vn->vn_ops->readdir) {
        fput(f);
        return -ENOTDIR;
    }
    
    int ret = vn->vn_ops->readdir(vn, f->f_pos, dirp);

    if (ret < 0) {
        fput(f);
        return ret;
    }

    if (ret == 0) {    
        fput(f);
        return 0;
    }

    f->f_pos += ret;
    fput(f);

    return sizeof(struct dirent);
}*/
#if 0
int
do_getdent(int fd, struct dirent *dirp)
{
    /* 1. Validate fd range */
    if (fd < 0 || fd >= NFILES)
        return -EBADF;

    /* 2. Validate that fd is open */
    file_t *f = curproc->p_files[fd];
    if (f == NULL)
        return -EBADF;

    vnode_t *vn = f->f_vnode;
    if (vn == NULL)
        return -EBADF;

    /* 3. Must be a directory: require a valid readdir op */
    if (vn->vn_ops == NULL || vn->vn_ops->readdir == NULL)
        return -ENOTDIR;

    /* 4. Ask filesystem for the next dirent */
    int ret = vn->vn_ops->readdir(vn, f->f_pos, dirp);

    if (ret < 0) {
        /* Propagate underlying error (-errno) */
        return ret;
    }

    if(ret == 0){
	return ret;	
}

    

    f->f_pos += ret;
    return sizeof(dirent_t);
    //if((ret % sizeof(dirent_t)) == 0) return sizeof(dirent_t) ;
    //dbg()


    /* ret is now:
       - 0: EOF
       - sizeof(dirent_t): one entry
       both valid for ksys_getdents.
     */
    //return  ret;
}
#endif

int
do_getdent(int fd, struct dirent *dirp)
{
    /* 1. Validate fd range */
    if (fd < 0 || fd >= NFILES){
        dbg(DBG_PRINT, "(GRADING2B)\n");
        return -EBADF;}

    /* 2. Get file object (this handles refcount and open check) */
    file_t *f = fget(fd);
    if (f == NULL){
        dbg(DBG_PRINT, "(GRADING2B)\n");
        return -EBADF;}

    /* 3. Must be a directory (ENOTDIR check) */
    if (!S_ISDIR(f->f_vnode->vn_mode)) {
        fput(f); // Release file reference
        dbg(DBG_PRINT, "(GRADING2B)\n");
        return -ENOTDIR;
    }

    /* 4. Check that the vnode operation exists */
    if (!f->f_vnode->vn_ops || !f->f_vnode->vn_ops->readdir) {
        fput(f); // Release file reference
        dbg(DBG_PRINT, "(GRADING2B)\n");
        return -ENOTDIR;
    }

    /* 5. Ask filesystem for the next dirent */
    int ret = f->f_vnode->vn_ops->readdir(f->f_vnode, f->f_pos, dirp);

    if (ret < 0) {
        /* Propagate underlying error (-errno) */
        fput(f); // Release file reference
        return ret;
    }

    if (ret == 0) {
        /* End of directory */
        fput(f); // Release file reference
        return 0;
    }
    dbg(DBG_TEST, "do_getdent: pid=%d fd=%d ret=%d d_name='%s' d_ino=%d\n",
        curproc->p_pid, fd, ret, dirp->d_name, dirp->d_ino);
    /* 6. Success: update position, release, and return */
    f->f_pos += ret;
    fput(f); // Release file reference
            dbg(DBG_PRINT, "(GRADING2B)\n");
    return sizeof(dirent_t);
}
/*
 * Modify f_pos according to offset and whence.
 *
 * Error cases you must handle for this function at the VFS level:
 *      o EBADF
 *        fd is not an open file descriptor.
 *      o EINVAL
 *        whence is not one of SEEK_SET, SEEK_CUR, SEEK_END; or the resulting
 *        file offset would be negative.
 */
int
do_lseek(int fd, int offset, int whence)
{
        // NOT_YET_IMPLEMENTED("VFS: do_lseek");
        if(fd < 0 || fd >= NFILES){
             return -EBADF;   
        }
        file_t *ft = fget(fd);
        if(ft == NULL) {        dbg(DBG_PRINT, "(GRADING2B)\n"); return -EBADF;}
        off_t new_pos = 0;
        switch(whence){
                case SEEK_SET:
                        new_pos = offset;
                        break;
                case SEEK_CUR:
                        new_pos = ft->f_pos + offset;
                        break;
                case SEEK_END:
                        new_pos = ft->f_vnode->vn_len + offset;
                        break;
                default:
                        fput(ft);
		        dbg(DBG_PRINT, "(GRADING2B)\n");
                        return -EINVAL;
        }
        if(new_pos < 0){
                fput(ft);
         	dbg(DBG_PRINT, "(GRADING2B)\n");
                return -EINVAL;
        }
        ft->f_pos = new_pos;
        off_t ret_pos = ft->f_pos;
        fput(ft);
        dbg(DBG_PRINT, "(GRADING2B)\n");
        return ret_pos;
}

/*
 * Find the vnode associated with the path, and call the stat() vnode operation.
 *
 * Error cases you must handle for this function at the VFS level:
 *      o ENOENT
 *        A component of path does not exist.
 *      o ENOTDIR
 *        A component of the path prefix of path is not a directory.
 *      o ENAMETOOLONG
 *        A component of path was too long.
 *      o EINVAL
 *        path is an empty string.
 */
int
do_stat(const char *path, struct stat *buf)
{
        // NOT_YET_IMPLEMENTED("VFS: do_stat");
        if (path == NULL || strlen(path) == 0) {
        	dbg(DBG_PRINT, "(GRADING2B)\n");
                return -EINVAL;
        }
        if(buf == NULL) {
		dbg(DBG_PRINT, "(GRADING2B)\n");
                return -EINVAL;
        }
        vnode_t *node = NULL;
        int ret = open_namev(path, 0, &node, NULL);
        if (ret != 0) {
		dbg(DBG_PRINT, "(GRADING2B)\n");
                return ret;
        }
	KASSERT(NULL != node->vn_ops->stat);
	dbg(DBG_PRINT, "(GRADING2A 3.f)\n");
        dbg(DBG_PRINT, "(GRADING2B)\n");
        ret = node->vn_ops->stat(node, buf);
        vput(node);
        if (ret != 0) {
                return ret;
        }
        dbg(DBG_PRINT, "(GRADING2B)\n");
        return 0;
}

#ifdef __MOUNTING__
/*
 * Implementing this function is not required and strongly discouraged unless
 * you are absolutely sure your Weenix is perfect.
 *
 * This is the syscall entry point into vfs for mounting. You will need to
 * create the fs_t struct and populate its fs_dev and fs_type fields before
 * calling vfs's mountfunc(). mountfunc() will use the fields you populated
 * in order to determine which underlying filesystem's mount function should
 * be run, then it will finish setting up the fs_t struct. At this point you
 * have a fully functioning file system, however it is not mounted on the
 * virtual file system, you will need to call vfs_mount to do this.
 *
 * There are lots of things which can go wrong here. Make sure you have good
 * error handling. Remember the fs_dev and fs_type buffers have limited size
 * so you should not write arbitrary length strings to them.
 */
int
do_mount(const char *source, const char *target, const char *type)
{
        NOT_YET_IMPLEMENTED("MOUNTING: do_mount");
        return -EINVAL;
}

/*
 * Implementing this function is not required and strongly discouraged unless
 * you are absolutley sure your Weenix is perfect.
 *
 * This function delegates all of the real work to vfs_umount. You should not worry
 * about freeing the fs_t struct here, that is done in vfs_umount. All this function
 * does is figure out which file system to pass to vfs_umount and do good error
 * checking.
 */
int
do_umount(const char *target)
{
        NOT_YET_IMPLEMENTED("MOUNTING: do_umount");
        return -EINVAL;
}
#endif
