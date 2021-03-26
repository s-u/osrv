/* Simple Fast Serialisation
   experimental serialiser focused on speed

   (C)2021 Simon Urbanek <simon.urbanek@R-project.org>

   License: MIT

   R-API: SEXP C_file_store(SEXP sWhat, SEXP sFilename);

*/

#include "sfs.h"

#include <fcntl.h>
#include <errno.h>
#include <unistd.h>

#define MAX_SEND_SIZE (1024*1024*128)

struct store_api {
    store_fn_t store;
    int s;
};

static void add(store_api_t *api, sfs_ts ts, sfs_len_t el, sfs_len_t len, const void *buf) {
    int n;
    sfs_len_t hdr = len, i = 0;
    hdr <<= 8;
    hdr |= ts;
    n = (int) write(api->s, &hdr, sizeof(hdr));
    if (n < sizeof(hdr)) {
	close(api->s);
	api->s = -1;
	Rf_error("Failed to write header (n=%d) %s", n, (n == -1 && errno) ? strerror(errno) : "");
    }
    if (el > 1)
	len *= el;
    if (buf)
	while (i < len) {
	    int need = (len - i > MAX_SEND_SIZE) ? MAX_SEND_SIZE : ((int) (len - i));
	    n = (int) write(api->s, buf + i, need);
	    if (n < 1) {
		close(api->s);
		api->s = -1;
		Rf_error("Failed to write (n=%d of %d) %s", n, need, (n == -1 && errno) ? strerror(errno) : "");
	    }
	    i += n;
	}
}

void fd_store(int s, SEXP sWhat) {
    store_api_t api;
    api.store = add;
    api.s = s;
    sfs_store(&api, sWhat);    
}

SEXP C_file_store(SEXP sWhat, SEXP sFilename) {
    int s;
    if (TYPEOF(sFilename) != STRSXP || LENGTH(sFilename) != 1)
	Rf_error("filename must be a string");
    s = open(CHAR(STRING_ELT(sFilename, 0)), O_WRONLY | O_CREAT | O_APPEND | O_TRUNC, 0000666);
    if (s == -1)
	Rf_error("Unable to create '%s': %s", CHAR(STRING_ELT(sFilename, 0)), strerror(errno));
    fd_store(s, sWhat);
    close(s);
    return R_NilValue;
}
