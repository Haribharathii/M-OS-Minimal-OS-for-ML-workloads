
#include "kernel.h"
#include "globals.h"
#include "types.h"
#include "errno.h"

#include "util/string.h"
#include "util/printf.h"
#include "util/debug.h"

#include "fs/dirent.h"
#include "fs/fcntl.h"
#include "fs/stat.h"
#include "fs/vfs.h"
#include "fs/vnode.h"

/* This takes a base 'dir', a 'name', its 'len', and a result vnode.
 * Most of the work should be done by the vnode's implementation
 * specific lookup() function.
 *
 * If dir has no lookup(), return -ENOTDIR.
 *
 * Note: returns with the vnode refcount on *result incremented.
 */
int
lookup(vnode_t *dir, const char *name, size_t len, vnode_t **result)
{
    //NOT_YET_IMPLEMENTED("VFS: lookup");*/
        /* Check if to incremnet reference count or no*/
        
        KASSERT(NULL != dir);
	dbg(DBG_PRINT, "(GRADING2A 2.a)\n");
	
        
        KASSERT(NULL != result);
	dbg(DBG_PRINT, "(GRADING2A 2.a)\n");
       

	KASSERT(NULL != name);
	dbg(DBG_PRINT, "(GRADING2A 2.a)\n");
	
        
	if(len > NAME_LEN)
	{
                dbg(DBG_PRINT, "(GRADING2B)\n");
		return -ENAMETOOLONG;
	}
	
        if(dir->vn_ops->lookup == NULL)
	{
	        
	        if(dir->vn_mode&S_IFDIR)
	        {
        		dbg(DBG_PRINT, "(GRADING2B)\n");
	        	return	-ENOENT;
	        }
	    	dbg(DBG_PRINT, "(GRADING2B)\n");
		return	-ENOTDIR;
	}
        
	
     

	

	int res = dir->vn_ops->lookup(dir, name, len, result);
	dbg(DBG_PRINT, "(GRADING2B)\n");
		return	res;
	
}



/* When successful this function returns data in the following "out"-arguments:
 *  o res_vnode: the vnode of the parent directory of "name"
 *  o name: the `basename' (the element of the pathname)
 *  o namelen: the length of the basename
 *
 * For example: dir_namev("/s5fs/bin/ls", &namelen, &name, NULL,
 * &res_vnode) would put 2 in namelen, "ls" in name, and a pointer to the
 * vnode corresponding to "/s5fs/bin" in res_vnode.
 *
 * The "base" argument defines where we start resolving the path from:
 * A base value of NULL means to use the process's current working directory,
 * curproc->p_cwd.  If pathname[0] == '/', ignore base and start with
 * vfs_root_vn.  dir_namev() should call lookup() to take care of resolving each
 * piece of the pathname.
 *
 * Note: A successful call to this causes vnode refcount on *res_vnode to
 * be incremented.
 */
int
dir_namev(const char *pathname, size_t *namelen, const char **name,
          vnode_t *base, vnode_t **res_vnode)
{
 	KASSERT(NULL != pathname);
	dbg(DBG_PRINT, "(GRADING2A 2.b)\n");
        KASSERT(NULL != namelen);
	dbg(DBG_PRINT, "(GRADING2A 2.b)\n");
        KASSERT(NULL != name);
	dbg(DBG_PRINT, "(GRADING2A 2.b)\n"); 
        KASSERT(NULL != res_vnode); 
	dbg(DBG_PRINT, "(GRADING2A 2.b)\n"); 
	
 	if(pathname[0]=='\0')
		{
		        dbg(DBG_PRINT, "(GRADING2B)\n");
			return -EINVAL;
		}       
	vnode_t *currBase = NULL;
        if(pathname[0] == '/'){
                currBase = vfs_root_vn;
        } else {
                if(base == NULL){
			dbg(DBG_PRINT, "(GRADING2B)\n");
                        currBase = curproc->p_cwd;
                } else {
                        currBase = base;
                }
        }
        vref(currBase); //fishy line***********fishy line
	KASSERT(NULL != currBase);
	dbg(DBG_PRINT, "(GRADING2A 2.b)\n");
        vnode_t *next_Dir = NULL; //***not vputting it******
        size_t s = 0;           
        size_t i = 0;           
        size_t file_len = 0;    
        size_t anchor = 0;      

        while(1) {
                if (pathname[i] == '\0') {
			dbg(DBG_PRINT, "(GRADING2B)\n");
                        file_len = i - s;
                        anchor = s;
                        break;
                }
		
                if (pathname[i] == '/') {
	
                        size_t dir_len = i - s;

                        if (dir_len > 0) {
                                
                                if (dir_len > NAME_LEN) {
                                        vput(currBase);
				        dbg(DBG_PRINT, "(GRADING2B)\n");
                                        return -ENAMETOOLONG;
                                }
				dbg(DBG_PRINT, "(GRADING2B)\n");
                                int get = lookup(currBase, pathname + s, dir_len, &next_Dir);
                                if(get != 0){
                                        vput(currBase);
                                        return get;
                                }
                                
                                vput(currBase);
                                currBase = next_Dir;
                        }
                        
                        while (pathname[i+1] == '/') {
			        dbg(DBG_PRINT, "(GRADING2B)\n");
                                i++;
                        }
			dbg(DBG_PRINT, "(GRADING2B)\n");
                        s = i + 1;
                }
		dbg(DBG_PRINT, "(GRADING2B)\n");
                i++;
        }

        if(file_len >= NAME_LEN){
                vput(currBase);
        dbg(DBG_PRINT, "(GRADING2B)\n");
                return -ENAMETOOLONG;
        }

        /*
	 * Only the completely empty string ("") is invalid.
	 * For "/", "foo/", "a/b/", we return namelen == 0 and the
	 * directory vnode in *res_vnode; callers handle it.
	 */
	if (file_len == 0 && pathname[0] == '\0') {
		vput(currBase);
        	dbg(DBG_PRINT, "(GRADING2D 5)\n");
		return -EINVAL;
	}

        *namelen = file_len;
        *name = pathname + anchor;//check something feels off
        *res_vnode = currBase;
	dbg(DBG_PRINT, "(GRADING2B)\n");
        return 0;
}

/* This returns in res_vnode the vnode requested by the other parameters.
 * It makes use of dir_namev and lookup to find the specified vnode (if it
 * exists).  flag is right out of the parameters to open(2); see
 * <weenix/fcntl.h>.  If the O_CREAT flag is specified and the file does
 * not exist, call create() in the parent directory vnode. However, if the
 * parent directory itself does not exist, this function should fail - in all
 * cases, no files or directories other than the one at the very end of the path
 * should be created.
 *
 * Note: Increments vnode refcount on *res_vnode.
 */

int open_namev(const char *pathname, int flag, vnode_t **res_vnode, vnode_t *base)
{
    size_t namelen;
    const char *name;
    vnode_t *dir;
    int ret;

    ret = dir_namev(pathname, &namelen, &name, base, &dir);
    if (ret < 0)
        return ret;


    /* 2. Too long */
    if (namelen > NAME_LEN) {
        vput(dir);
        dbg(DBG_PRINT, "(GRADING2B)\n");
        return -ENAMETOOLONG;
    }

    /* Case 1: path like "/" (dir_namev returns namelen == 0, dir = that vnode).
       Just open that directory; do NOT call lookup with len 0. */
    if (namelen == 0) {
    /* Path ended with '/', so it must refer to a directory. */
    if (!S_ISDIR(dir->vn_mode)) {
        vput(dir);
        dbg(DBG_PRINT, "(GRADING2B)\n");
        return -ENOTDIR;
    }
    *res_vnode = dir;   /* transfer ref from dir_namev */
    return 0;
}


    vnode_t *vn = NULL;
    ret = lookup(dir, name, namelen, &vn);

    if (ret == 0) {
	
        vput(dir);
        *res_vnode = vn;
	dbg(DBG_PRINT, "(GRADING2B)\n");
        return 0;
    }

    if (ret == -ENOENT && (flag & O_CREAT)) {
        KASSERT(dir->vn_ops && dir->vn_ops->create);
        dbg(DBG_PRINT, "(GRADING2A 2.c)\n");
	dbg(DBG_PRINT, "(GRADING2B)\n");
        ret = dir->vn_ops->create(dir, name, namelen, &vn);
        vput(dir);
        if (ret == 0){
	    dbg(DBG_PRINT, "(GRADING2B)\n");
            *res_vnode = vn;
	}
	dbg(DBG_PRINT, "(GRADING2B)\n");
        return ret;
    }

    vput(dir);
    dbg(DBG_PRINT, "(GRADING2B)\n");
    return ret;   /* includes -ENOENT when !O_CREAT */
}



#ifdef __GETCWD__
/* Finds the name of 'entry' in the directory 'dir'. The name is writen
 * to the given buffer. On success 0 is returned. If 'dir' does not
 * contain 'entry' then -ENOENT is returned. If the given buffer cannot
 * hold the result then it is filled with as many characters as possible
 * and a null terminator, -ERANGE is returned.
 *
 * Files can be uniquely identified within a file system by their
 * inode numbers. */
int
lookup_name(vnode_t *dir, vnode_t *entry, char *buf, size_t size)
{
        NOT_YET_IMPLEMENTED("GETCWD: lookup_name");
        return -ENOENT;
}


/* Used to find the absolute path of the directory 'dir'. Since
 * directories cannot have more than one link there is always
 * a unique solution. The path is writen to the given buffer.
 * On success 0 is returned. On error this function returns a
 * negative error code. See the man page for getcwd(3) for
 * possible errors. Even if an error code is returned the buffer
 * will be filled with a valid string which has some partial
 * information about the wanted path. */
ssize_t
lookup_dirpath(vnode_t *dir, char *buf, size_t osize)
{
        NOT_YET_IMPLEMENTED("GETCWD: lookup_dirpath");

        return -ENOENT;
}
#endif /* __GETCWD__ */
