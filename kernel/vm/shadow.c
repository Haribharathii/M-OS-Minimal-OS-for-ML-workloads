

#include "globals.h"
#include "errno.h"

#include "util/string.h"
#include "util/debug.h"

#include "mm/mmobj.h"
#include "mm/pframe.h"
#include "mm/mm.h"
#include "mm/page.h"
#include "mm/slab.h"
#include "mm/tlb.h"

#include "vm/vmmap.h"
#include "vm/shadow.h"
#include "vm/shadowd.h"

#define SHADOW_SINGLETON_THRESHOLD 5

int shadow_count = 0; /* for debugging/verification purposes */
#ifdef __SHADOWD__
/*
 * number of shadow objects with a single parent, that is another shadow
 * object in the shadow objects tree(singletons)
 */
static int shadow_singleton_count = 0;
#endif

static slab_allocator_t *shadow_allocator;

static void shadow_ref(mmobj_t *o);
static void shadow_put(mmobj_t *o);
static int  shadow_lookuppage(mmobj_t *o, uint32_t pagenum, int forwrite, pframe_t **pf);
static int  shadow_fillpage(mmobj_t *o, pframe_t *pf);
static int  shadow_dirtypage(mmobj_t *o, pframe_t *pf);
static int  shadow_cleanpage(mmobj_t *o, pframe_t *pf);

static mmobj_ops_t shadow_mmobj_ops = {
        .ref = shadow_ref,
        .put = shadow_put,
        .lookuppage = shadow_lookuppage,
        .fillpage  = shadow_fillpage,
        .dirtypage = shadow_dirtypage,
        .cleanpage = shadow_cleanpage
};

/*
 * This function is called at boot time to initialize the
 * shadow page sub system. Currently it only initializes the
 * shadow_allocator object.
 */
void
shadow_init()
{
	shadow_allocator = slab_allocator_create("shadow", sizeof(mmobj_t));
	dbg(DBG_TEST, "shadow_mmobj_ops at %p\n", &shadow_mmobj_ops);
        KASSERT(NULL != shadow_allocator);
}
/*
 * You'll want to use the shadow_allocator to allocate the mmobj to
 * return, then then initialize it. Take a look in mm/mmobj.h for
 * macros or functions which can be of use here. Make sure your initial
 * reference count is correct.
 */
mmobj_t *
shadow_create()
{
	mmobj_t *mmo = (mmobj_t *) slab_obj_alloc(shadow_allocator);
	
	if (mmo == NULL) {
		return NULL;
	}
	mmobj_init(mmo, &shadow_mmobj_ops);
	mmo->mmo_refcount = 1;
	mmo->mmo_shadowed = NULL;  
	mmo->mmo_un.mmo_bottom_obj = NULL; //loook clearly after - HIGH ERROR POINT
	shadow_count++; 
        return mmo;
}

/* Implementation of mmobj entry points: */

/*
 * Increment the reference count on the object.
 */
static void
shadow_ref(mmobj_t *o)
{
		KASSERT(o != NULL);
	KASSERT(o->mmo_refcount >= o->mmo_nrespages);
	o->mmo_refcount++;
dbg(DBG_TEST, "Proc : %d Shadow %p ref upto : %d\n", curproc->p_pid, o, o->mmo_refcount);
	return;
}

/*
 * Decrement the reference count on the object. If, however, the
 * reference count on the object reaches the number of resident
 * pages of the object, we can conclude that the object is no
 * longer in use and, since it is a shadow object, it will never
 * be used again. You should unpin and uncache all of the object's
 * pages and then free the object itself.
 */
static void
shadow_put(mmobj_t *o)
{
   KASSERT(NULL != o);
	KASSERT(o->mmo_refcount >= 0);
//	
	if (o->mmo_refcount > o->mmo_nrespages + 1) {
		o->mmo_refcount--;
		return;
	}
	dbg(DBG_TEST, "Proc : %d Shadow ref down to : %d", curproc->p_pid, o->mmo_refcount);
	pframe_t *pf = NULL;
	list_iterate_begin(&o->mmo_respages, pf, pframe_t, pf_olink) {
		while (pframe_is_pinned(pf)) {
		    pframe_unpin(pf);
		}
		pframe_free(pf);
        } list_iterate_end();
	if (o->mmo_shadowed) {
	    o->mmo_shadowed->mmo_ops->put(o->mmo_shadowed);
	}
	mmobj_t *bottom = o->mmo_un.mmo_bottom_obj;
	if (bottom && bottom != o->mmo_shadowed) {
	    bottom->mmo_ops->put(bottom);
	}
	slab_obj_free(shadow_allocator, o);

	return;

}

/* This function looks up the given page in this shadow object. The
 * forwrite argument is true if the page is being looked up for
 * writing, false if it is being looked up for reading. This function
 * must handle all do-not-copy-on-not-write magic (i.e. when forwrite
 * is false find the first shadow object in the chain which has the
 * given page resident). copy-on-write magic (necessary when forwrite
 * is true) is handled in shadow_fillpage, not here. It is important to
 * use iteration rather than recursion here as a recursive implementation
 * can overflow the kernel stack when looking down a long shadow chain */
static int
shadow_lookuppage(mmobj_t *o, uint32_t pagenum, int forwrite, pframe_t **pf)
{
#if 0
	KASSERT(o != NULL);
	KASSERT(pf != NULL);

	if (forwrite != 0) {
        	return pframe_get(o, pagenum, pf);
    	}
	mmobj_t  *cur   = o;
	pframe_t *result = NULL;

        while(cur != NULL) {
		while (1) {
		    result = pframe_get_resident(cur, pagenum);
		    if (result == NULL) {
			break;
		    }

		    if (pframe_is_busy(result)) {
			sched_sleep_on(&result->pf_waitq);
			continue;
		    }

		    *pf = result;
		    return 0;
		}
		cur = cur->mmo_shadowed;
	}

	mmobj_t *bottom = mmobj_bottom_obj(o);
	return pframe_get(bottom, pagenum, pf);
#endif
    if (forwrite){
        return pframe_get(o, pagenum, pf);
    }

    pframe_t *p = NULL;
    mmobj_t *curr = o;

    while (p == NULL && curr->mmo_shadowed != NULL){
        p = pframe_get_resident(curr, pagenum);
        curr = curr->mmo_shadowed;
    }

    if (p == NULL){
        return pframe_lookup(curr, pagenum, 0, pf);
    }
   
    *pf = p;
    return 0;
}

/* As per the specification in mmobj.h, fill the page frame starting
 * at address pf->pf_addr with the contents of the page identified by
 * pf->pf_obj and pf->pf_pagenum. This function handles all
 * copy-on-write magic (i.e. if there is a shadow object which has
 * data for the pf->pf_pagenum-th page then we should take that data,
 * if no such shadow object exists we need to follow the chain of
 * shadow objects all the way to the bottom object and take the data
 * for the pf->pf_pagenum-th page from the last object in the chain).
 * It is important to use iteration rather than recursion here as a
 * recursive implementation can overflow the kernel stack when
 * looking down a long shadow chain */
static int
shadow_fillpage(mmobj_t *o, pframe_t *pf)
{
#if 0
    KASSERT(o != NULL);
    KASSERT(pf != NULL);
    KASSERT(pf->pf_obj == o);

    uint32_t pagenum = pf->pf_pagenum;
    mmobj_t *cur = o->mmo_shadowed;
    pframe_t *srcpf = NULL;
    int ret;

    while(cur != NULL && srcpf == NULL) {
        while (1) {
            srcpf = pframe_get_resident(cur, pagenum);
            if (srcpf == NULL) {
                break;
            }

            if (pframe_is_busy(srcpf)) {
                sched_sleep_on(&srcpf->pf_waitq);
                continue;
            }

            break;
        }

        if (srcpf != NULL && !pframe_is_busy(srcpf)) {
            break;
        } else {
            srcpf = NULL; 
        }
	cur = cur->mmo_shadowed;
    }

	if (srcpf == NULL) {
		cur = mmobj_bottom_obj(o);
		dbg(DBG_TEST, "shadow_fillpage: calling pframe_lookup bottom=%p pagenum=%d\n", cur, pagenum);
		ret = pframe_lookup(cur, pagenum, 0, &srcpf);
		if (ret < 0) {
		dbg(DBG_TEST, "shadow_fillpage: pframe_lookup FAILED ret=%d\n", ret);
		return ret;
		}
		dbg(DBG_TEST, "shadow_fillpage: pframe_lookup returned srcpf=%p\n", srcpf);
	
    }

    KASSERT(srcpf != NULL);
    KASSERT(!pframe_is_busy(srcpf));
	char *c = (char *)srcpf->pf_addr;
        dbg(DBG_TEST,
            "SHADOW DEBUG: bottom page first 16 bytes: "
            "%02x %02x %02x %02x  %02x %02x %02x %02x  "
            "%02x %02x %02x %02x  %02x %02x %02x %02x\n",
            (unsigned char)c[0],  (unsigned char)c[1],
            (unsigned char)c[2],  (unsigned char)c[3],
            (unsigned char)c[4],  (unsigned char)c[5],
            (unsigned char)c[6],  (unsigned char)c[7],
            (unsigned char)c[8],  (unsigned char)c[9],
            (unsigned char)c[10], (unsigned char)c[11],
            (unsigned char)c[12], (unsigned char)c[13],
            (unsigned char)c[14], (unsigned char)c[15]);
    

    memcpy(pf->pf_addr, srcpf->pf_addr, PAGE_SIZE);
	// if (srcpf->pf_obj != o->mmo_shadowed) {
	//	pframe_unpin(srcpf);
	//}
    return 0;

#endif
pframe_t *p = NULL;
    mmobj_t *curr = o->mmo_shadowed;

    while (p == NULL && curr != o->mmo_un.mmo_bottom_obj){
        p = pframe_get_resident(curr, pf->pf_pagenum);
        curr = curr->mmo_shadowed;
    }
    
    if (p == NULL){
        KASSERT(curr == o->mmo_un.mmo_bottom_obj);
        int lookup_res = pframe_lookup(curr, pf->pf_pagenum, 1, &p);

        if (lookup_res < 0){
            return lookup_res;
        }
    }

    KASSERT(p != NULL);

    pframe_pin(pf);
    memcpy(pf->pf_addr, p->pf_addr, PAGE_SIZE);
    return 0;
}

/* These next two functions are not difficult. */

static int
shadow_dirtypage(mmobj_t *o, pframe_t *pf)
{

	return 0;
}

static int
shadow_cleanpage(mmobj_t *o, pframe_t *pf)
{

	return 0;
}
