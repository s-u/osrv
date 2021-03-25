/* Simple Fast Serialisation
   experimental serialiser focused on speed

   (C)2021 Simon Urbanek <simon.urbanek@R-project.org>

   License: MIT

   R-API: SEXP C_stat_store(SEXP sWhat);
   
   Doesn't actually store anything, counts all object
   types and their size (in bytes).
*/

#include "sfs.h"

struct store_api {
    store_fn_t store;
    sfs_len_t cs[256];
    sfs_len_t ls[256];
};

static void add(store_api_t *api, sfs_ts ts, sfs_len_t el, sfs_len_t len, const void *buf) {
    if (el > 0)
	len *= el;
    api->cs[ts]++;
    api->ls[ts] += len;
}

SEXP C_stat_store(SEXP sWhat, SEXP sVerb) {
    SEXP res, sDim;
    store_api_t api;
    double *cs, *ls;
    int i = 0, *dim;
    memset(&api, 0, sizeof(api));
    api.store = add;
    sfs_store(&api, sWhat);
    res = PROTECT(allocVector(REALSXP, 512));
    cs = REAL(res);
    ls = cs + 256;
    while (i < 256) {
	cs[i] = (double) api.cs[i];
	ls[i] = (double) api.ls[i];
	i++;
    }
    sDim = allocVector(INTSXP, 2);
    dim = INTEGER(sDim);
    dim[0] = 256;
    dim[1] = 2;
    setAttrib(res, R_DimSymbol, sDim);
    UNPROTECT(1);
    return res;
}
