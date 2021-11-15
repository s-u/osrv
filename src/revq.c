/* R API for event queues
   Gives access to evqueue.h from R

   SEXP C_evq_new()
   SEXP C_evq_pop(SEXP sQ, SEXP sTimeout, SEXP sRef)
   SEXP C_evq_push(SEXP sQ, SEXP sWhat, SEXP sFront)

   additional exported C API:
   SEXP evq2R(ev_queue_t *q)

 */

#include "evqueue.h"

#include <Rinternals.h>
#include <string.h>

SEXP evq2R(ev_queue_t *q) {
    SEXP res = PROTECT(R_MakeExternalPtr(q, R_NilValue, R_NilValue));
    /* FIXME: we don't register a finalizer, because we never destroy queues */
    setAttrib(res, R_ClassSymbol, PROTECT(mkString("evqueue")));
    UNPROTECT(2);
    return res;
}

SEXP C_evq_new() {
    ev_queue_t *q = ev_queue_create();
    if (!q)
	Rf_error("Cannot start queue");
    return evq2R(q);
}

static ev_queue_t *R2evq(SEXP sQ) {
    ev_queue_t *q;

    if (!Rf_inherits(sQ, "evqueue") || TYPEOF(sQ) != EXTPTRSXP)
	Rf_error("Invalid queue parameter");

    q = (ev_queue_t*) R_ExternalPtrAddr(sQ);
    if (!q)
	Rf_error("Invalid NULL queue (possibly from another workspace?)");

    return q;
}


static void eventry_R_free(SEXP ref) {
    ev_entry_t *e = (ev_entry_t*) R_ExternalPtrAddr(ref);
    if (e)
	ev_free(e);
    /* not really needed, but just in case ... */
    R_SetExternalPtrAddr(ref, 0);
}

static SEXP entry2R(ev_entry_t *e) {
    SEXP res = PROTECT(R_MakeExternalPtr(e, R_NilValue, R_NilValue));
    R_RegisterCFinalizerEx(res, eventry_R_free, 1);
    setAttrib(res, R_ClassSymbol, PROTECT(mkString("eventry")));
    UNPROTECT(2);
    return res;
}

SEXP C_evq_pop(SEXP sQ, SEXP sTimeout, SEXP sRef) {
    int ref = Rf_asInteger(sRef);
    double tout = Rf_asReal(sTimeout);
    ev_queue_t *q = R2evq(sQ);
    SEXP res, eve;

    ev_entry_t *e = (tout == 0.0) ? ev_pop(q) : ev_pop_wait(q, tout);
    if (!e)
	return R_NilValue;

    /* This is a leak defense: we register the entry even if reference
       was not requested just in case the allocation fails so it
       will be finalized */
    eve = entry2R(e);
    if (ref)
	return eve;

    PROTECT(eve);
    res = Rf_allocVector(RAWSXP, e->len);
    if (e->data && e->len)
	memcpy(RAW(res), e->data, e->len);
    ev_free(e);
    /* zero the pointer, the object should be unreachable now */
    R_SetExternalPtrAddr(eve, 0);
    UNPROTECT(1);
    return res;
}

SEXP C_evq_push(SEXP sQ, SEXP sWhat, SEXP sFront) {
    int front = Rf_asInteger(sFront);
    ev_queue_t *q = R2evq(sQ);
    ev_entry_t *e, *r;

    if (inherits(sWhat, "eventry")) {
	e = (ev_entry_t*) R_ExternalPtrAddr(sWhat);
	if (!e)
	    Rf_error("Invalid (NULL) event entry");
	r = ev_push(q, e, front);
	if (!r)
	    Rf_error("Push failed");
	/* The queue now owns the event, so zero it */
	R_SetExternalPtrAddr(sWhat, 0);
	return Rf_ScalarLogical(1);
    }

    if (TYPEOF(sWhat) != RAWSXP)
	Rf_error("Only events and raw vectors can be pushed on the queue");

    /* FIXME: we always duplicate the content to avoid memory
       management issues since free is not guaranteed to happen
       on the main thread so we can't use R API. We could use collection
       pool like obj.c does, but queue events are less likely to be big. */
    e = ev_create(0, XLENGTH(sWhat), 0);
    if (!e)
	Rf_error("Cannot allocate event object");
    memcpy(e->data, RAW(sWhat), XLENGTH(sWhat));
    r = ev_push(q, e, front);
    if (!r) {
	ev_free(e);
	Rf_error("Push failed");
    }
    return Rf_ScalarLogical(1);
}
