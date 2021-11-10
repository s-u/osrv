/* object server, TCP server

   Author and (c) Simon Urbanek <urbanek@R-project.org>
   License: MIT


=== protocol:

GET /data/<key>
PUT /data/<key>
HEAD /data/<key>
DELETE /data/<key>

=== R API:

SEXP C_start_http(SEXP sHost, SEXP sPort, SEXP sThreads);

*/

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
/* for TCP_NODELAY */
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

#include "therver.h"
#include "obj.h"
#include "http.h"

#include <Rinternals.h>

#define FETCH_SIZE (512*1024)

#define SOCKET int
#define closesocket(X) close(X)

#define MAX_BUF  65536
#define MAX_OBUF 2048
#define MAX_SEND (1024*1024) /* 1Mb */

typedef struct {
    int  bol, n;
    char buf[MAX_BUF];
    char obuf[MAX_OBUF];
} work_t;


static void http_process(http_request_t *req, http_connection_t *conn) {
    if (!strncmp("/data/", req->path, 6)) {
	/* FIXME: should we put some limits on the keys? */
	char *c = req->path + 6;
	const char *key = req->path + 6;
	/* end the key if we see / or ? */
	while (*c && *c != '/' && *c != '?') c++;
	*c = 0;
	if (req->method == METHOD_HEAD || req->method == METHOD_GET) {
	    obj_entry_t *o = obj_get(key, 0);
	    if (req->method == METHOD_GET && o && !o->obj) {
		/* FIXME: this would require chunked transfer, or
		   pre-allocating, so more work ...*/
		http_response(conn, 501, "Object requires streaming serialization which is not implemented",
			      0, 0, 0);
		return;
	    }
	    if (!o) {
		http_response(conn, 404, "Object Not Found", 0, 0, 0);
		return;
	    }
	    http_response(conn, 200, "OK", "application/octet-stream",
			  o->len, 0);
	    if (req->method == METHOD_GET)
		http_send(conn, o->obj, o->len);
	    return;
	}
	if (req->method == METHOD_DELETE) {
	    obj_entry_t *o = obj_get(key, 1);
	    if (o) {
		http_response(conn, 200, "OK", 0, 0, 0);
	    } else {
		http_response(conn, 404, "Object Not Found", 0, 0, 0);
	    }
	    return;
	}
	if (req->method == METHOD_PUT) {
	    obj_add(key, 0, req->body, req->content_length);
	    /* obj store takes ownership, so reset the request
	       body pointer so it doesn't get freed */
	    req->body = 0;
	    http_response(conn, 200, "OK", 0, 0, 0);
	    return;
	}
    }
    http_response(conn, 404, "Invalid API Path", 0, 0, 0);
}

/* therver's callback - we just pretty much pass it to http */
static void do_process(conn_t *c) {
    int s = c->s;
    int opt = 1;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const void*) &opt, sizeof(opt));

    http_connected(s, 0, http_process);
    /* it is guaranteed to shut down the socket */
    c->s = -1;
}

#include <Rinternals.h>

/* start object server */
SEXP C_start_http(SEXP sHost, SEXP sPort, SEXP sThreads) {
    const char *host = (TYPEOF(sHost) == STRSXP && LENGTH(sHost) > 0) ?
	CHAR(STRING_ELT(sHost, 0)) : 0;
    int port = Rf_asInteger(sPort);
    int threads = Rf_asInteger(sThreads);

    if (therver_id)
	Rf_error("Server is already running, can only start one");
    therver_id++;	    

    if (port < 1 || port > 65535)
	Rf_error("Invalid port %d", port);
    if (threads < 1 || threads > 1000)
	Rf_error("Invalid number of threads %d", threads);

    obj_init();
    if (therver(host, port, threads, do_process))
	return ScalarLogical(0);

    Rprintf("HTTP: started on %s:%d, try me.\n", host ? host : "*", port);

    return ScalarLogical(1);
}
