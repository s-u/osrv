/* Simple Fast Serialisation
   experimental serialiser focused on speed

   (C)2021 Simon Urbanek <simon.urbanek@R-project.org>

   License: MIT

   R-API: SEXP C_mem_restore(SEXP sWhat);
   
   Simple de-serialisation from a raw vector.
*/

#include "sfs.h"

struct fetch_api {
    fetch_fn_t fetch;
    char      *fbuf;
    sfs_len_t  flen;
};

static void fetch_buf(fetch_api_t *api, void *buf, sfs_len_t len) {
    if (api->flen < len)
	Rf_error("Read error: need %lu, got %lu\n", len, api->flen);
    memcpy(buf, api->fbuf, len);
    api->fbuf += len;
    api->flen -= len;
}

SEXP C_mem_restore(SEXP sWhat) {
    fetch_api_t api;
    api.fbuf = (char*) RAW(sWhat);
    api.flen = XLENGTH(sWhat);
    api.fetch= fetch_buf;
    return sfs_load(&api);
}

