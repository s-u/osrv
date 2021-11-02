/* Interface from R to the object store */

#include "obj.h"
#include "sfs.h"

SEXP C_put(SEXP sKey, SEXP sWhat, SEXP sSFS) {
    int use_sfs = asInteger(sSFS);
    if (TYPEOF(sKey) != STRSXP || LENGTH(sKey) != 1)
	Rf_error("Invalid key, must be a string");
    if (!use_sfs && TYPEOF(sWhat) != RAWSXP)
	Rf_error("Value must be a raw vector unless SFS is used");
    obj_init();
    obj_add(CHAR(STRING_ELT(sKey, 0)), sWhat, use_sfs ? 0 : RAW(sWhat), use_sfs ? 0 : XLENGTH(sWhat));
    return ScalarLogical(1);
}

struct fetch_api {
    fetch_fn_t  fetch;
    const char *fbuf;
    sfs_len_t   flen;
};

static void fetch_buf(fetch_api_t *api, void *buf, sfs_len_t len) {
    if (api->flen < len)
	Rf_error("Read error: need %lu, got %lu\n", len, api->flen);
    memcpy(buf, api->fbuf, len);
    api->fbuf += len;
    api->flen -= len;
}

SEXP C_get(SEXP sKey, SEXP sSFS, SEXP sRM) {
    SEXP res = R_NilValue;
    int use_sfs = asInteger(sSFS);
    int rm = asInteger(sRM);
    if (TYPEOF(sKey) != STRSXP || LENGTH(sKey) != 1)
	Rf_error("Invalid key, must be a string");
    obj_init();
    obj_entry_t *o = obj_get(CHAR(STRING_ELT(sKey, 0)), rm);
    if (o) {
	if (o->sWhat)
	    return o->sWhat;
	if (use_sfs) {
	    fetch_api_t api;
	    api.fbuf  = (const char*) o->obj;
	    api.flen  = o->len;
	    api.fetch = fetch_buf;
	    res = sfs_load(&api);
	} else {
	    res = Rf_allocVector(RAWSXP, o->len);
	    if (o->obj && o->len)
		memcpy(RAW(res), o->obj, o->len);
	}
	/* don't bother with updating sWhat if rm is set */
	if (rm)
	    return res ? res : R_NilValue;
	if (res) {
	    o->sWhat = res;
	    if (res != R_NilValue)
		R_PreserveObject(res);
	}
    }
    return res;
}   

SEXP C_clean() {
    obj_init();
    obj_gc();
    return ScalarLogical(1);
}
