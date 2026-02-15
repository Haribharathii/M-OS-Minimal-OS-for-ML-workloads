
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

int anon_count = 0; /* for debugging/verification purposes */

static slab_allocator_t *anon_allocator;

static void anon_ref(mmobj_t *o);
static void anon_put(mmobj_t *o);
static int  anon_lookuppage(mmobj_t *o, uint32_t pagenum, int forwrite, pframe_t **pf);
static int  anon_fillpage(mmobj_t *o, pframe_t *pf);
static int  anon_dirtypage(mmobj_t *o, pframe_t *pf);
static int  anon_cleanpage(mmobj_t *o, pframe_t *pf);

static mmobj_ops_t anon_mmobj_ops = {
        .ref = anon_ref,
        .put = anon_put,
        .lookuppage = anon_lookuppage,
        .fillpage  = anon_fillpage,
        .dirtypage = anon_dirtypage,
        .cleanpage = anon_cleanpage
};

/*
 * This function is called at boot time to initialize the
 * anonymous page sub system. Currently it only initializes the
 * anon_allocator object.
 */
void
anon_init()
{
        anon_allocator = slab_allocator_create("anon", sizeof(mmobj_t));
	dbg(DBG_TEST, "anon_mmobj_ops at %p\n", &anon_mmobj_ops);
        KASSERT(NULL != anon_allocator);
}

/*
 * You'll want to use the anon_allocator to allocate the mmobj to
 * return, then initialize it. Take a look in mm/mmobj.h for
 * definitions which can be of use here. Make sure your initial
 * reference count is correct.
 */
mmobj_t *
anon_create()
{
        mmobj_t *o = (mmobj_t *) slab_obj_alloc(anon_allocator);
        if (o == NULL) {
                return NULL;
        }
	mmobj_init(o, &anon_mmobj_ops);
	o->mmo_refcount = 1;
        return o;
}

/* Implementation of mmobj entry points: */

/*
 * Increment the reference count on the object.
 */
static void
anon_ref(mmobj_t *o)
{
	KASSERT(NULL != o);
	o->mmo_refcount++;
	
}

/*
 * Decrement the reference count on the object. If, however, the
 * reference count on the object reaches the number of resident
 * pages of the object, we can conclude that the object is no
 * longer in use and, since it is an anonymous object, it will
 * never be used again. You should unpin and uncache all of the
 * object's pages and then free the object itself.
 */
static void
anon_put(mmobj_t *o)
{
#if 0
	KASSERT(NULL != o);
dbg(DBG_TEST, "anon_put: 0x%p, ref=%d, nres=%d\n", o, o->mmo_refcount, o->mmo_nrespages);
	
	KASSERT(o->mmo_refcount >= o->mmo_nrespages);
	if(o->mmo_nrespages == 0 || o->mmo_refcount == 0){
		slab_obj_free(anon_allocator, o);
		return;
	}
	if (o->mmo_nrespages == (o->mmo_refcount - 1)) {
                pframe_t *pf;
                list_iterate_begin(&o->mmo_respages, pf, pframe_t, pf_olink) {
                        if (pframe_is_pinned(pf))
                                pframe_unpin(pf);
                        KASSERT(!pframe_is_busy(pf));
                        pframe_free(pf);
                } list_iterate_end();

                KASSERT(o->mmo_nrespages == 0);
                KASSERT(o->mmo_refcount == 1);
        }

        if (--o->mmo_refcount > 0)
                return;

        KASSERT(o->mmo_refcount == 0);
        KASSERT(o->mmo_nrespages == 0);

        slab_obj_free(anon_allocator, o);
#endif
    KASSERT(o != NULL);
    KASSERT(o->mmo_refcount > 0);

    o->mmo_refcount--;

    /* If only resident pages are holding refs, start freeing them */
    if (o->mmo_refcount == o->mmo_nrespages &&
        !list_empty(&o->mmo_respages)) {

        pframe_t *pf = list_item(o->mmo_respages.l_next,
                                 pframe_t, pf_olink);
        if (pframe_is_pinned(pf))
            pframe_unpin(pf);
        KASSERT(!pframe_is_busy(pf));

        pframe_free(pf);    // this will drop one ref and one nrespage
        return;
    }

    /* Still has external refs or resident pages: don't free mmobj yet */
    if (o->mmo_refcount > 0 || o->mmo_nrespages > 0)
        return;

    /* At this point, no refs and no resident pages */
    KASSERT(list_empty(&o->mmo_respages));
    slab_obj_free(anon_allocator, o);	
}

/* Get the corresponding page from the mmobj. No special handling is
 * required. */
static int
anon_lookuppage(mmobj_t *o, uint32_t pagenum, int forwrite, pframe_t **pf)
{
	KASSERT(o != NULL);

        return pframe_get(o, pagenum, pf);;
}

/* The following three functions should not be difficult. */

static int
anon_fillpage(mmobj_t *o, pframe_t *pf)
{
	KASSERT(o != NULL);
	KASSERT(pf != NULL);
	KASSERT(pf->pf_obj == o);
  //  pframe_pin(pf);
	memset(pf->pf_addr, 0, PAGE_SIZE);
	
        return 0;
}

static int
anon_dirtypage(mmobj_t *o, pframe_t *pf)
{
        KASSERT(o != NULL);
	KASSERT(pf != NULL);
	KASSERT(pf->pf_obj == o);

        return 0;
}

static int
anon_cleanpage(mmobj_t *o, pframe_t *pf)
{
        KASSERT(o != NULL);
	KASSERT(pf != NULL);
	KASSERT(pf->pf_obj == o);

        return 0;
}
