/* Simple Fast Serialisation
   experimental serialiser focused on speed

   (C)2021 Simon Urbanek <simon.urbanek@R-project.org>

   License: MIT

   R-API: SEXP C_file_restore(SEXP sFilename);

*/

#include "sfs.h"

#include <fcntl.h>
#include <errno.h>
#include <unistd.h>

struct fetch_api {
    fetch_fn_t fetch;
    int s;
};

#define MAX_RECV_SIZE (1024*1024*128)

static void fetch(fetch_api_t *api, void *buf, sfs_len_t len) {
    sfs_len_t i = 0;
    while (i < len) {
	int need = (len - i > MAX_RECV_SIZE) ? MAX_RECV_SIZE : ((int) (len - i));
	int n = (int)read(api->s, buf + i, need);
	if (n < 0) {
	    if (errno == EAGAIN || errno == EWOULDBLOCK) {
		R_CheckUserInterrupt();
		continue;
	    }
	    close(api->s);
	    Rf_error("read error: %s", strerror(errno));
	}
	if (n == 0) {
	    close(api->s);
	    Rf_error("connection closed before all data was received");
	}
	i += n;
    }
}

SEXP fd_restore(int s) {
    fetch_api_t api;

    api.s = s;
    api.fetch = fetch;
    return sfs_load(&api);
}

SEXP C_file_restore(SEXP sFilename) {
    int s;
    SEXP res;
    if (TYPEOF(sFilename) != STRSXP || LENGTH(sFilename) != 1)
	Rf_error("filename must be a string");
    s = open(CHAR(STRING_ELT(sFilename, 0)), O_RDONLY);
    if (s == -1)
	Rf_error("Unable to open '%s': %s", CHAR(STRING_ELT(sFilename, 0)), strerror(errno));
    res = fd_restore(s);
    close(s);
    return res;
}
