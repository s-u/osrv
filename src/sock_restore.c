/* Simple Fast Serialisation
   experimental serialiser focused on speed

   (C)2021 Simon Urbanek <simon.urbanek@R-project.org>

   License: MIT

   R-API: SEXP C_sock_restore(SEXP sSock);

   Simple de-serialisation from a socket.
   It sets a RCVTIMEO and allows user-interupt.
*/

#include "sfs.h"

#include <sys/socket.h>
#include <sys/time.h>
#include <errno.h>
#include <unistd.h>

struct fetch_api {
    fetch_fn_t fetch;
    int s;
};

#define MAX_RECV_SIZE (1024*1024)

static void fetch(fetch_api_t *api, void *buf, sfs_len_t len) {
    sfs_len_t i = 0;
    while (i < len) {
	int need = (len - i > MAX_RECV_SIZE) ? MAX_RECV_SIZE : ((int) (len - i));
	int n = recv(api->s, buf + i, need, 0);
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

SEXP sock_restore(int s, int use_opts) {
    fetch_api_t api;

    if (use_opts) {
	struct timeval tv;
    
	/* enable timeout so we can support R-level interrupts */
	tv.tv_sec = 0;
	tv.tv_usec = 200000;
	setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);
    }

    api.s = s;
    api.fetch = fetch;
    return sfs_load(&api);
}

SEXP C_sock_restore(SEXP sSock) {
    return sock_restore(asInteger(sSock), 1);
}
