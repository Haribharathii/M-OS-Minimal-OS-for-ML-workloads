

#include "kernel.h"
#include "errno.h"
#include "globals.h"

#include "vm/vmmap.h"
#include "vm/shadow.h"
#include "vm/anon.h"

#include "proc/proc.h"

#include "util/debug.h"
#include "util/list.h"
#include "util/string.h"
#include "util/printf.h"

#include "fs/vnode.h"
#include "fs/file.h"
#include "fs/fcntl.h"
#include "fs/vfs_syscall.h"

#include "limits.h"

#include "mm/slab.h"
#include "mm/page.h"
#include "mm/mm.h"
#include "mm/mman.h"
#include "mm/mmobj.h"

static slab_allocator_t *vmmap_allocator;
static slab_allocator_t *vmarea_allocator;

void
vmmap_init(void)
{
        vmmap_allocator = slab_allocator_create("vmmap", sizeof(vmmap_t));
        KASSERT(NULL != vmmap_allocator && "failed to create vmmap allocator!");
        vmarea_allocator = slab_allocator_create("vmarea", sizeof(vmarea_t));
        KASSERT(NULL != vmarea_allocator && "failed to create vmarea allocator!");
}

vmarea_t *
vmarea_alloc(void)
{
        vmarea_t *newvma = (vmarea_t *) slab_obj_alloc(vmarea_allocator);
        if (newvma) {
                newvma->vma_vmmap = NULL;
        }
        return newvma;
}

void
vmarea_free(vmarea_t *vma)
{
        KASSERT(NULL != vma);
        slab_obj_free(vmarea_allocator, vma);
}

/* a debugging routine: dumps the mappings of the given address space. */
size_t
vmmap_mapping_info(const void *vmmap, char *buf, size_t osize)
{
        KASSERT(0 < osize);
        KASSERT(NULL != buf);
        KASSERT(NULL != vmmap);

        vmmap_t *map = (vmmap_t *)vmmap;
        vmarea_t *vma;
        ssize_t size = (ssize_t)osize;

        int len = snprintf(buf, size, "%21s %5s %7s %8s %10s %12s\n",
                           "VADDR RANGE", "PROT", "FLAGS", "MMOBJ", "OFFSET",
                           "VFN RANGE");

        list_iterate_begin(&map->vmm_list, vma, vmarea_t, vma_plink) {
                size -= len;
                buf += len;
                if (0 >= size) {
                        goto end;
                }

                len = snprintf(buf, size,
                               "%#.8x-%#.8x  %c%c%c  %7s 0x%p %#.5x %#.5x-%#.5x\n",
                               vma->vma_start << PAGE_SHIFT,
                               vma->vma_end << PAGE_SHIFT,
                               (vma->vma_prot & PROT_READ ? 'r' : '-'),
                               (vma->vma_prot & PROT_WRITE ? 'w' : '-'),
                               (vma->vma_prot & PROT_EXEC ? 'x' : '-'),
                               (vma->vma_flags & MAP_SHARED ? " SHARED" : "PRIVATE"),
                               vma->vma_obj, vma->vma_off, vma->vma_start, vma->vma_end);
        } list_iterate_end();

end:
        if (size <= 0) {
                size = osize;
                buf[osize - 1] = '\0';
        }
        /*
        KASSERT(0 <= size);
        if (0 == size) {
                size++;
                buf--;
                buf[0] = '\0';
        }
        */
        return osize - size;
}

/* Create a new vmmap, which has no vmareas and does
 * not refer to a process. */
vmmap_t *
vmmap_create(void)
{
	vmmap_t *vm_t = slab_obj_alloc(vmmap_allocator);
	if(!vm_t) return NULL;

	list_init(&vm_t->vmm_list); //empty vmarea list

   	vm_t->vmm_proc = NULL; // no process yet
        return vm_t;
}

/* Removes all vmareas from the address space and frees the
 * vmmap struct. */
void
vmmap_destroy(vmmap_t *map)
{
	KASSERT(map != NULL);
	vmarea_t *vma;
	list_iterate_begin(&map->vmm_list, vma, vmarea_t, vma_plink) {

		list_remove(&vma->vma_plink);

		if (vma->vma_obj) {
  dbg(DBG_TEST, "vmmap_destroy: vma 0x%p, obj 0x%p\n", vma, vma->vma_obj);
		    vma->vma_obj->mmo_ops->put(vma->vma_obj);
		}

		/* Finally free the vmarea itself */
		slab_obj_free(vmarea_allocator, vma);

	} list_iterate_end();

	/* Now free the map struct */
	slab_obj_free(vmmap_allocator, map);
	return;
}

/* Add a vmarea to an address space. Assumes (i.e. asserts to some extent)
 * the vmarea is valid.  This involves finding where to put it in the list
 * of VM areas, and adding it. Don't forget to set the vma_vmmap for the
 * area. */
void
vmmap_insert(vmmap_t *map, vmarea_t *newvma)
{
	KASSERT(map != NULL);
	KASSERT(newvma != NULL);

	vmarea_t *vma;
	newvma->vma_vmmap = map;
	uint32_t start = newvma->vma_start;
	uint32_t end = newvma->vma_end;
	
	list_iterate_begin(&map->vmm_list, vma, vmarea_t, vma_plink) {
                if(vma->vma_start > start){
			list_insert_before(&vma->vma_plink, &newvma->vma_plink);
			return;
		}
			
        } list_iterate_end();

	list_insert_tail(&map->vmm_list, &newvma->vma_plink);
	
	return;
}

/* Find a contiguous range of free virtual pages of length npages in
 * the given address space. Returns starting vfn for the range,
 * without altering the map. Returns -1 if no such range exists.
 *
 * Your algorithm should be first fit. If dir is VMMAP_DIR_HILO, you
 * should find a gap as high in the address space as possible; if dir
 * is VMMAP_DIR_LOHI, the gap should be as low as possible. */
int
vmmap_find_range(vmmap_t *map, uint32_t npages, int dir)
{
	KASSERT(map != NULL);
	KASSERT(dir == VMMAP_DIR_HILO || dir == VMMAP_DIR_LOHI);
	if((int)npages <= 0){
		return -1;
	}
	uint32_t lo = USER_MEM_LOW >> PAGE_SHIFT;;
        uint32_t hi = USER_MEM_HIGH >> PAGE_SHIFT;;    
	vmarea_t *vma;
	uint32_t start, end;
	start = end = (uint32_t)-1;
	if(dir == VMMAP_DIR_LOHI){
		list_iterate_begin(&map->vmm_list, vma, vmarea_t, vma_plink) {
		        if(start != (uint32_t)-1){
				if(vma->vma_start - end >= npages){
					return end;
				}
			}else{
				if (vma->vma_start - lo >= npages) {
                                	return lo;
                        	}
			}
			start = vma->vma_start;
			end = vma->vma_end;
		} list_iterate_end();
		if (start == (uint32_t)-1) {
		        if (hi - lo >= npages) {
		                return lo;
		        }
		} else {
		        if (hi > end && hi - end >= npages) {
		                return end;
		        }
		}
	}else{
		list_iterate_reverse(&map->vmm_list, vma, vmarea_t, vma_plink) {
		        if(start != (uint32_t)-1){
				if(start - vma->vma_end >= npages){
					return start - npages;
				}
			}else{
				if (hi - vma->vma_end >= npages) {
                                	return hi - npages;
                        	}
			}
			start = vma->vma_start;
			end = vma->vma_end;
		} list_iterate_end();
		if (start == (uint32_t)-1) {
		        if (hi - lo >= npages) {
		                return hi - npages;
		        }
		} else {
		        if (lo < start && start - lo >= npages) {
		                return start - npages;
		        }
		}
	}
	
        return -1;
}

/* Find the vm_area that vfn lies in. Simply scan the address space
 * looking for a vma whose range covers vfn. If the page is unmapped,
 * return NULL. */
vmarea_t *
vmmap_lookup(vmmap_t *map, uint32_t vfn)
{
	KASSERT(map != NULL);
	if((int)vfn < 0) return NULL;

	vmarea_t *vma;
	
	list_iterate_begin(&map->vmm_list, vma, vmarea_t, vma_plink) {
                if(vma->vma_start <= vfn && vfn < vma->vma_end){
			return vma;
		}
			
        } list_iterate_end();

        return NULL;
}

/* Allocates a new vmmap containing a new vmarea for each area in the
 * given map. The areas should have no mmobjs set yet. Returns pointer
 * to the new vmmap on success, NULL on failure. This function is
 * called when implementing fork(2). */
vmmap_t *
vmmap_clone(vmmap_t *map)
{
	KASSERT(map != NULL);
	
	vmmap_t *newMap = vmmap_create();
	if (newMap == NULL) {
		return NULL;
	}
	vmarea_t *vma;
	
	list_iterate_begin(&map->vmm_list, vma, vmarea_t, vma_plink) {
                vmarea_t * newvma = (vmarea_t *) slab_obj_alloc(vmarea_allocator);
		if (!newvma) {
		    vmmap_destroy(newMap);
		    return NULL;
		}
		newvma->vma_start = vma->vma_start;
		newvma->vma_end = vma->vma_end;
		newvma->vma_off = vma->vma_off;
		newvma->vma_prot = vma->vma_prot;
		newvma->vma_flags = vma->vma_flags;
		newvma->vma_obj = NULL;
		list_link_init(&newvma->vma_olink);
		vmmap_insert(newMap, newvma);
			
        } list_iterate_end();

        return newMap;
}

/* Insert a mapping into the map starting at lopage for npages pages.
 * If lopage is zero, we will find a range of virtual addresses in the
 * process that is big enough, by using vmmap_find_range with the same
 * dir argument.  If lopage is non-zero and the specified region
 * contains another mapping that mapping should be unmapped.
 *
 * If file is NULL an anon mmobj will be used to create a mapping
 * of 0's.  If file is non-null that vnode's file will be mapped in
 * for the given range.  Use the vnode's mmap operation to get the
 * mmobj for the file; do not assume it is file->vn_obj. Make sure all
 * of the area's fields except for vma_obj have been set before
 * calling mmap.
 *
 * If MAP_PRIVATE is specified set up a shadow object for the mmobj.
 *
 * All of the input to this function should be valid (KASSERT!).
 * See mmap(2) for for description of legal input.
 * Note that off should be page aligned.
 *
 * Be very careful about the order operations are performed in here. Some
 * operation are impossible to undo and should be saved until there
 * is no chance of failure.
 *
 * If 'new' is non-NULL a pointer to the new vmarea_t should be stored in it.
 */
int
vmmap_map(vmmap_t *map, vnode_t *file, uint32_t lopage, uint32_t npages,
          int prot, int flags, off_t off, int dir, vmarea_t **new)
{
        KASSERT(map != NULL);
	KASSERT(npages > 0);
	KASSERT((off & (PAGE_SIZE - 1)) == 0);  
	KASSERT((flags & (MAP_SHARED | MAP_PRIVATE)) != 0);
	KASSERT(!((flags & MAP_SHARED) && (flags & MAP_PRIVATE)));

	if (dir == 0) { // possible problem
        	dir = VMMAP_DIR_LOHI;
    	}
	KASSERT(dir == VMMAP_DIR_LOHI || dir == VMMAP_DIR_HILO);

	int       ret;
	uint32_t  startvfn;
	vmarea_t *vma = NULL;
	mmobj_t  *obj = NULL;
	mmobj_t  *shadow = NULL;


	if (lopage == 0) {
		startvfn = vmmap_find_range(map, npages, dir);
		if (startvfn == (uint32_t)-1 || (int)startvfn == -1) {
		    return -ENOMEM;
		}
		} else {
			startvfn = lopage;

			ret = vmmap_remove(map, lopage, npages);
			if (ret < 0) {
			    return ret;
		}
	}


	vma = vmarea_alloc();
	if (vma == NULL) {
		return -ENOMEM;
	}

	vma->vma_start = startvfn;
	vma->vma_end   = startvfn + npages;
	vma->vma_off   = (off >> PAGE_SHIFT);
	vma->vma_prot  = prot;
	vma->vma_flags = flags;
	vma->vma_obj   = NULL;        


	if (file == NULL) {
		obj = anon_create();
		if (obj == NULL) {
		    vmarea_free(vma);
		    return -ENOMEM;
		}
	} else {
		KASSERT(file->vn_ops != NULL);
		KASSERT(file->vn_ops->mmap != NULL);

		ret = file->vn_ops->mmap(file, vma, &obj);
		if (ret < 0) {
		    vmarea_free(vma);
		    return ret;
		}
		KASSERT(obj != NULL);
		        dbg(DBG_TEST, "vmmap_map: mmap returned obj=%p refcount=%d\n", obj, obj->mmo_refcount);
        dbg(DBG_TEST, "vmmap_map: file vnode=%p refcount=%d\n", file, file->vn_refcount);
	}


	if (flags & MAP_PRIVATE) {
		if (file != NULL) {
		    shadow = shadow_create();
		    if (shadow == NULL) {
			obj->mmo_ops->put(obj);
			vmarea_free(vma);
			return -ENOMEM;
		    }

		    shadow->mmo_shadowed          = obj;
		    shadow->mmo_un.mmo_bottom_obj = mmobj_bottom_obj(obj);
	          //  shadow->mmo_un.mmo_bottom_obj->mmo_ops->ref(shadow->mmo_un.mmo_bottom_obj);
		    vma->vma_obj = shadow;

		    list_insert_tail(mmobj_bottom_vmas(shadow), &vma->vma_olink);
		} else {
		    vma->vma_obj = obj;
		    list_insert_tail(mmobj_bottom_vmas(obj), &vma->vma_olink);
		}
	} else {
		vma->vma_obj = obj;
		list_insert_tail(mmobj_bottom_vmas(obj), &vma->vma_olink);
	}


	vmmap_insert(map, vma);

	if (new != NULL) {
		*new = vma;
	}

	return 0;
}

/*
 * We have no guarantee that the region of the address space being
 * unmapped will play nicely with our list of vmareas.
 *
 * You must iterate over each vmarea that is partially or wholly covered
 * by the address range [addr ... addr+len). The vm-area will fall into one
 * of four cases, as illustrated below:
 *
 * key:
 *          [             ]   Existing VM Area
 *        *******             Region to be unmapped
 *
 * Case 1:  [   ******    ]
 * The region to be unmapped lies completely inside the vmarea. We need to
 * split the old vmarea into two vmareas. be sure to increment the
 * reference count to the file associated with the vmarea.
 *
 * Case 2:  [      *******]**
 * The region overlaps the end of the vmarea. Just shorten the length of
 * the mapping.
 *
 * Case 3: *[*****        ]
 * The region overlaps the beginning of the vmarea. Move the beginning of
 * the mapping (remember to update vma_off), and shorten its length.
 *
 * Case 4: *[*************]**
 * The region completely contains the vmarea. Remove the vmarea from the
 * list.
 */
int
vmmap_remove(vmmap_t *map, uint32_t lopage, uint32_t npages)
{
    KASSERT(map != NULL);

    if (npages == 0) {
        return 0;
    }

    uint32_t u_start = lopage;
    uint32_t u_end   = lopage + npages;

    vmarea_t *vma;

    list_iterate_begin(&map->vmm_list, vma, vmarea_t, vma_plink) {
        uint32_t s = vma->vma_start;
        uint32_t e = vma->vma_end;

        if (e <= u_start || s >= u_end) {
            continue;
        }

        if (u_start <= s && u_end >= e) {
            list_remove(&vma->vma_plink);

            if (vma->vma_obj != NULL) {
                list_remove(&vma->vma_olink);
                vma->vma_obj->mmo_ops->put(vma->vma_obj);
            }

            slab_obj_free(vmarea_allocator, vma);
            continue;
        }

        if (u_start <= s && u_end > s && u_end < e) {
            uint32_t old_start = vma->vma_start;

            vma->vma_start = u_end;
            vma->vma_off  += (u_end - old_start);

            continue;
        }

        if (s < u_start && u_start < e && u_end >= e) {
            vma->vma_end = u_start;
            continue;
        }

        if (s < u_start && u_end < e) {
            vmarea_t *right = vmarea_alloc();
            if (right == NULL) {
                return -ENOMEM;
            }

            right->vma_start = u_end;
            right->vma_end   = e;
            right->vma_off   = vma->vma_off + (u_end - s);
            right->vma_prot  = vma->vma_prot;
            right->vma_flags = vma->vma_flags;
            right->vma_vmmap = NULL;
            right->vma_obj   = vma->vma_obj;

            if (right->vma_obj != NULL) {
                right->vma_obj->mmo_ops->ref(right->vma_obj);
                list_insert_tail(mmobj_bottom_vmas(right->vma_obj),
                                 &right->vma_olink);
            }

            vma->vma_end = u_start;

            vmmap_insert(map, right);

            break;
        }


    } list_iterate_end();

    return 0;
}


/*
 * Returns 1 if the given address space has no mappings for the
 * given range, 0 otherwise.
 */
int
vmmap_is_range_empty(vmmap_t *map, uint32_t startvfn, uint32_t npages)
{
	KASSERT(map != NULL);
	if(npages == 0) return 1;
	
	uint32_t endvfn = startvfn + npages;
	vmarea_t *vma;
	
	list_iterate_begin(&map->vmm_list, vma, vmarea_t, vma_plink) {
	        if (!(vma->vma_end <= startvfn || vma->vma_start >= endvfn)) {
		    return 0; 
		}
	} list_iterate_end();
        return 1;
}

/* Read into 'buf' from the virtual address space of 'map' starting at
 * 'vaddr' for size 'count'. To do so, you will want to find the vmareas
 * to read from, then find the pframes within those vmareas corresponding
 * to the virtual addresses you want to read, and then read from the
 * physical memory that pframe points to. You should not check permissions
 * of the areas. Assume (KASSERT) that all the areas you are accessing exist.
 * Returns 0 on success, -errno on error.
 */
int
vmmap_read(vmmap_t *map, const void *vaddr, void *buf, size_t count)
{
    KASSERT(map != NULL);

    if(!buf || !vaddr){
	return 0;
    }
    if (count == 0) {
        return 0;
    }

    uintptr_t cur_va = (uintptr_t)vaddr;
    char     *dst    = (char *)buf;
    size_t    left   = count;

    while (left > 0) {
        uint32_t  vfn      = ADDR_TO_PN(cur_va);
        uint32_t  page_off = cur_va & (PAGE_SIZE - 1);

        vmarea_t *vma = vmmap_lookup(map, vfn);
        KASSERT(vma != NULL);

        mmobj_t  *obj       = vma->vma_obj;
        uint32_t  obj_pagen = vma->vma_off + (vfn - vma->vma_start);

        pframe_t *pf  = NULL;
        int       ret = pframe_lookup(obj, obj_pagen, 0, &pf);
        if (ret < 0) {
            return ret;   
        }

        size_t bytes_in_page = PAGE_SIZE - page_off;
        size_t nread         = (left < bytes_in_page) ? left : bytes_in_page;

        memcpy(dst, (char *)pf->pf_addr + page_off, nread);

        cur_va += nread;
        dst    += nread;
        left   -= nread;
        if(pf->pf_pincount > 0) pframe_unpin(pf); 
    }
   /* Debug: show first few bytes of what we read if it's small */
    if (count <= 256) {
        char *cbuf = (char *)buf;
        dbg(DBG_TEST, "vmmap_read: pid=%d vaddr=%p count=%d first_bytes='%.*s' (hex: %02x %02x %02x %02x)\n",
            curproc->p_pid, vaddr, count, 
            (count < 32 ? count : 32), cbuf,
            (count > 0 ? (unsigned char)cbuf[0] : 0),
            (count > 1 ? (unsigned char)cbuf[1] : 0),
            (count > 2 ? (unsigned char)cbuf[2] : 0),
            (count > 3 ? (unsigned char)cbuf[3] : 0));
    }
    return 0;
}


/* Write from 'buf' into the virtual address space of 'map' starting at
 * 'vaddr' for size 'count'. To do this, you will need to find the correct
 * vmareas to write into, then find the correct pframes within those vmareas,
 * and finally write into the physical addresses that those pframes correspond
 * to. You should not check permissions of the areas you use. Assume (KASSERT)
 * that all the areas you are accessing exist. Remember to dirty pages!
 * Returns 0 on success, -errno on error.
 */
int
vmmap_write(vmmap_t *map, void *vaddr, const void *buf, size_t count)
{
    KASSERT(map != NULL);
    KASSERT(vaddr != NULL);
    KASSERT(buf != NULL);

    if (count == 0) {
        return 0;
    }
 /* Debug: show what we are writing */
    if (count <= 256) {
        const char *cbuf = (const char *)buf;
        dbg(DBG_TEST, "vmmap_write: pid=%d vaddr=%p count=%d first_bytes='%.*s' (hex: %02x %02x %02x %02x)\n",
            curproc->p_pid, vaddr, count, 
            (count < 32 ? count : 32), cbuf,
            (count > 0 ? (unsigned char)cbuf[0] : 0),
            (count > 1 ? (unsigned char)cbuf[1] : 0),
            (count > 2 ? (unsigned char)cbuf[2] : 0),
            (count > 3 ? (unsigned char)cbuf[3] : 0));
    }
    uintptr_t cur_va = (uintptr_t)vaddr;
    const char *src  = (const char *)buf;
    size_t left      = count;

    while (left > 0) {
        uint32_t vfn      = ADDR_TO_PN(cur_va);
        uint32_t page_off = cur_va & (PAGE_SIZE - 1);

        vmarea_t *vma = vmmap_lookup(map, vfn);
        KASSERT(vma != NULL);

        mmobj_t  *obj       = vma->vma_obj;
        uint32_t  obj_pagen = vma->vma_off + (vfn - vma->vma_start);

        pframe_t *pf  = NULL;
        int       ret = pframe_lookup(obj, obj_pagen, 1, &pf);
        if (ret < 0) {
            return ret;   
        }

        size_t bytes_in_page = PAGE_SIZE - page_off;
        size_t nwrite        = (left < bytes_in_page) ? left : bytes_in_page;

        memcpy((char *)pf->pf_addr + page_off, src, nwrite);

        ret = pframe_dirty(pf);
        if (ret < 0) {
            return ret;
        }

        cur_va += nwrite;
        src    += nwrite;
        left   -= nwrite;
	if(pf->pf_pincount > 0) pframe_unpin(pf); 
    }

    return 0;
}

