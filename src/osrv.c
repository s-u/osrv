/* object server, TCP server

   Author and (c) Simon Urbanek <urbanek@R-project.org>
   License: MIT


=== protocol:

request: "GET "<key>\n
responses:
  "OK "<length>"\n" - object found
    followed by <length> bytes of payload
  "OK ?\n" - SFS object found
    the payload is an STF stream
  "NF\n"   - object not found

request: "DEL "<key>\n
reponses:
  "OK\n" - found and removed
  "NF\n" - not found

all other requests:
response:
  "UNSUPP\n" - unsupported

=== R API:

SEXP C_start(SEXP sHost, SEXP sPort, SEXP sThreads);

*/

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>

#include "therver.h"
#include "obj.h"

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

static int send_buf(int s, const char* buf, obj_len_t len) {
    while (len) {
	int ts = (len > MAX_SEND) ? MAX_SEND : ((int) len);
	int n = send(s, buf, ts, 0);
	if (n < 1)
	    return (n < 0) ? -1 : 1;
	len -= n;
	buf += n;
    }
    return 0;
}

/* from fd_store.c */
void fd_store(int s, SEXP sWhat);

static void do_process(conn_t *c) {
    int s = c->s, n;
    work_t *w;
    char *d, *e, *a;
    
    /* make sure c is valid, allocate work_t if needed */
    if (s < 0 || (!c->data && !(c->data = calloc(1, sizeof(work_t)))))
	return;

    {
        int opt = 1;
        setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const void*) &opt, sizeof(opt));
    }

    w = (work_t*) c->data;

    while (1) {
	n = recv(s, w->buf, sizeof(w->buf) - 1, 0);
	if (n < 1)
	    break;

	d = strchr(w->buf, '\n');
	if (d) {
	    while (d >= w->buf && (*d == '\r' || *d == '\n'))
		d--;
	    d++;
	    *d = 0;
	}
	e = w->buf;
	while (*e >= 'A' && *e <= 'Z')
	    e++;
	if (!*e)
	    break;
	a = e;
	while (*a == ' ' || *a == '\t')
	    a++;
	*e = 0;
	/* w->buf is cmd, a = arg */
	if (!strcmp("GET", w->buf)) {
	    obj_entry_t *o = obj_get(a, 0);
	    /* printf("finding '%s' (%s)\n", a, o ? "OK" : "NF"); */
	    if (o) {
		if (!o->obj) { /* if obj is NULL if we have to serialise */
		    static const char *ok_ser = "OK ?\n";
		    if (send_buf(s, ok_ser, 5))
			break;
		    fd_store(s, o->sWhat);
		    break;
		} else {
		    snprintf(w->obuf, sizeof(w->obuf), "OK %lu\n",
			     (unsigned long) o->len);
		    if (send_buf(s, w->obuf, strlen(w->obuf)) ||
			send_buf(s, o->obj, o->len))
			break;
		}
	    } else if (send_buf(s, "NF\n", 3))
		break;
	} else if (!strcmp("DEL", w->buf)) {
	    obj_entry_t *o = obj_get(a, 1);
	    int res = o ? send_buf(s, "OK\n", 3) : send_buf(s, "NF\n", 3);
	    if (res)
		break;
	} else {
	    if (send_buf(s, "UNSUPP\n", 7))
		break;
	}
	/* we end here after successful completion
	   FIXME: we do assume that the client
	   was waiting since we will not
	   keep previous buffer around */
    }
    closesocket(s);
    c->s = -1;
}

#include <Rinternals.h>

/* start object server */
SEXP C_start(SEXP sHost, SEXP sPort, SEXP sThreads) {
    const char *host = (TYPEOF(sHost) == STRSXP && LENGTH(sHost) > 0) ?
	CHAR(STRING_ELT(sHost, 0)) : 0;
    int port = Rf_asInteger(sPort);
    int threads = Rf_asInteger(sThreads);

    if (port < 1 || port > 65535)
	Rf_error("Invalid port %d", port);
    if (threads < 1 || threads > 1000)
	Rf_error("Invalid number of threads %d", threads);

    obj_init();
    if (therver(host, port, threads, do_process))
	return ScalarLogical(0);

    /* printf("Started on %s:%d, try me.\n", host ? host : "*", port); */

    return ScalarLogical(1);
}
