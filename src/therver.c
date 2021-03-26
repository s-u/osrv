/* Simple threaded TCP server (therver), based on a
   FIFO queue and worker threads. It is intended to
   run entirely in a parallel to the main thread.
   Also intentionally it uses static variables,
   the only dynamically allocated pieces are
   a) the thread pool and b) the queue entries

   Author and (c) Simon Urbanek <urbanek@R-project.org>
   License: MIT

   (work in progress, ports expected eventually)
*/

/* --- interface --- */

typedef struct conn_s {
    int s; /* socket to the client */
    void *data; /* opaque per-thread pointer */
} conn_t;

/* The process(conn_t*) API:
   You don't own the parameter, but it is guaranteed
   to live until you return. If you close the socket
   you must also set s = -1 to indicate you did so,
   otherwise the socket is automatically closed. */
typedef void (*process_fn_t)(conn_t*);

/* Binds host/port, then starts threads, host can be NULL for ANY.
   Returns non-zero for errors. */
int therver(const char *host, int port, int max_threads, process_fn_t process_fn);


/* ------ cut here ------ */

/* --- implementation --- */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <errno.h>
#include <unistd.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <signal.h>
#include <pthread.h>

#define FETCH_SIZE (512*1024)

#define SOCKET int
#define closesocket(X) close(X)

volatile int active = 1;

static int ss;

static process_fn_t process;

static pthread_t *worker_threads;
static pthread_t accept_thread;

typedef struct qentry_s {
    /* used for queuing */
    struct qentry_s *prev, *next;
    /* connection info */
    conn_t c;
} qentry_t;

static qentry_t root;
static pthread_mutex_t pool_mutex;
static pthread_cond_t pool_work_cond;

static void *worker_thread(void *pool_arg) {
    qentry_t *me;
    void *data = 0;
    /* printf("worker_thread %p is a go\n", (void*)&me); */
    while (active) {
	/* lock queue mutex */
	pthread_mutex_lock(&pool_mutex);
	/* printf("worker %p waiting\n", (void*)&me); */
	
	/* wait on condition until we get work */
	while (!(me = root.next) || me == &root) 
	    pthread_cond_wait(&pool_work_cond, &pool_mutex);

	/* remove us from the queue */
	root.next = me->next;
	if (me->next) me->next->prev = &root;
	/* we don't care to update our prev/next since we never use it */

	/* release queue lock */
	pthread_mutex_unlock(&pool_mutex);

	/* printf("worker %p calling process() with s=%d\n", (void*)&me, me->c.s); */
	me->c.data = data;
	/* serve the connection */
	process(&me->c);
	data = me->c.data;

	/* clean up */
	if (me->c.s != -1)
	    close(me->c.s);
	/* printf("worker %p is done\n", (void*)&me); */
	free(me);
    }
    return 0;
}

/* me must be free()-able and we take ownership */
static int add_task(qentry_t *me) {
    /* printf("add_task(%d) about to lock\n", me->c.s); */
    pthread_mutex_lock(&pool_mutex);
    /* printf(" add_task() locked, adding\n"); */
    me->next = &root;
    me->prev = root.prev;
    if (me->prev) me->prev->next = me;
    root.prev = me;
    /* printf(" add_task() broadcasting\n"); */
    pthread_cond_broadcast(&pool_work_cond);
    pthread_mutex_unlock(&pool_mutex);
    /* printf(" add_task() unlocked\n"); */
    return 0;
}

/* this is not absolutely safe, but fork() is asking for trouble anyway,
   so we only make sure that the child does not process anything and
   closes all its sockets. If any communications are in-flight,
   it's anyone's guess what will hapen since we don't try to join the threads
   before forking (which would be the only way to do this safely).
*/
static void forked_child() {
    qentry_t *me;
    /* make accept thread quit */
    active = 0;
    /* close server socket */
    closesocket(ss);
    ss = -1;
    /* close and reset all sockets in the queue */
    me = root.next;
    while (me && me != &root) {
	if (me->c.s != -1)
	    closesocket(me->c.s);
	me->c.s = -1;
	me = me->next;
    }
}

/* thread for the incoming connections */
static void *accept_thread_run(void *nothing) {
    int s;
    socklen_t cli_al;
    struct sockaddr_in sin_cli;
    /* printf("accept_thread %p is a go\n", (void*)&s); */
    while (active) {
	cli_al = sizeof(sin_cli);
	s = accept(ss, (struct sockaddr*) &sin_cli, &cli_al);
	/* printf("accept_thread: accept=%d\n", s); */
	if (s != -1) {
	    qentry_t *me = (qentry_t*) calloc(1, sizeof(qentry_t));
	    if (me) {
		/* once enqueued the task takes ownership of me.
		   On any kind of error we have to free it. */
		/* printf(" - accept_thread got me, enqueuing\n"); */
		me->c.s = s;
		if (add_task(me)) {
		    /* printf(" - add_task() failed, oops\n"); */
		    free(me);
		    me = 0;
		    close(s);
		}
	    } else /* sorry, out of memory, over and out */
		close(s);
	}
    }
    close(ss);
    ss = -1;
    return 0;
}

static int start_threads(int max_threads) {
    sigset_t mask, omask;
    pthread_attr_t t_attr;
    pthread_attr_init(&t_attr); /* all out threads are detached since we don't care */
    pthread_attr_setdetachstate(&t_attr, PTHREAD_CREATE_DETACHED);

    root.next = root.prev = &root;
    root.c.s = -1;

    if (!(worker_threads = malloc(sizeof(pthread_t) * max_threads)))
	return -1;

    /* init cond/mutex */
    pthread_mutex_init(&pool_mutex, 0);
    pthread_cond_init(&pool_work_cond, 0);

    /* mask all signals - the threads will inherit the mask
       and thus not fire and leave it to R */
    sigfillset(&mask);
    sigprocmask(SIG_SETMASK, &mask, &omask);

    /* start worker threads */
    for (int i = 0; i < max_threads; i++)
	pthread_create(&worker_threads[i], &t_attr, worker_thread, 0);

    /* start accept thread */
    pthread_create(&accept_thread, &t_attr, accept_thread_run, 0);

    /* re-set the mask back for the main thread */
    sigprocmask(SIG_SETMASK, &omask, 0);

    /* in case the user uses multicore or something else, we want to shut down
       all proessing in the children (not perfect, see comments above) */
    pthread_atfork(0, 0, forked_child);

    return 0;
}

int therver(const char *host, int port, int max_threads, process_fn_t process_fn) {
    int i;
    struct sockaddr_in sin;
    struct hostent *haddr;

    ss = socket(AF_INET, SOCK_STREAM, 0);

    i = 1;
    setsockopt(ss, SOL_SOCKET, SO_REUSEADDR, (const char*)&i, sizeof(i));

    sin.sin_family = AF_INET;
    sin.sin_port = htons(port);
    if (host) {
        if (inet_pton(sin.sin_family, host, &sin.sin_addr) != 1) { /* invalid, try DNS */
            if (!(haddr = gethostbyname(host))) { /* DNS failed, */
                closesocket(ss);
                ss = -1;
            }
            sin.sin_addr.s_addr = *((uint32_t*) haddr->h_addr); /* pick first address */
        }
    } else
        sin.sin_addr.s_addr = htonl(INADDR_ANY);

    if (ss != -1 && (bind(ss, (struct sockaddr*)&sin, sizeof(sin)) || listen(ss, 8))) {
        closesocket(ss);
	ss = -1;
    }
    if (ss == -1) {
        perror("ERROR: failed to bind or listen");
        return 1;
    }

    process = process_fn;
    
    return start_threads(max_threads);
}

/* client */

#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

#include <Rinternals.h>

#define MAX_BUF  65536
#define MAX_OBUF 2048
#define MAX_SEND (1024*1024) /* 1Mb */

typedef struct {
    int  bol, n;
    char buf[MAX_BUF];
    char obuf[MAX_OBUF];
} work_t;

typedef unsigned long int obj_len_t;

typedef struct entry_s {
    struct entry_s *next;
    obj_len_t len;
    void *obj;
    SEXP sWhat;
    char key[1];
} entry_t;

static pthread_mutex_t obj_mutex;

/* FIXME: use hash */
static entry_t *obj_root;
static entry_t *obj_gc_pool;

static void obj_add(const char *key, SEXP sWhat, void *data, obj_len_t len) {
    entry_t *e = (entry_t*) calloc(1, sizeof(entry_t) + strlen(key));
    strcpy(e->key, key);
    e->len = len;
    e->obj = data;
    e->sWhat = sWhat;
    R_PreserveObject(sWhat);
    pthread_mutex_lock(&obj_mutex);
    e->next = obj_root;
    obj_root = e;
    pthread_mutex_unlock(&obj_mutex);
}

static void obj_gc() {
    pthread_mutex_lock(&obj_mutex);
    /* FIXME: is this safe? We are hoping that
       R_ReleaseObject() cannot longjmp... */
    while (obj_gc_pool) {
	entry_t *c = obj_gc_pool; 
	R_ReleaseObject(obj_gc_pool->sWhat);
	obj_gc_pool = obj_gc_pool->next;
	free(c);
    }
    pthread_mutex_unlock(&obj_mutex);
}

static entry_t *obj_get(const char *key, int rm) {
    entry_t *e = obj_root, *prev = 0;
    /* FIXME: this is conservative, can we reduce the region? */
    pthread_mutex_lock(&obj_mutex);
    while (e) {
	if (!strcmp(key, e->key)) {
	    if (rm) {
		if (prev)
		    prev->next = e->next;
		else
		    obj_root = e->next;
		e->next = obj_gc_pool;
		obj_gc_pool = e;
	    }
	    pthread_mutex_unlock(&obj_mutex);
	    return e;
	}
	e = e->next;
    }
    pthread_mutex_unlock(&obj_mutex);
    return 0;
}

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
	    entry_t *o = obj_get(a, 0);
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
	    entry_t *o = obj_get(a, 1);
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

static int init_pt;

static void do_init() {
    if (!init_pt) {
	pthread_mutex_init(&obj_mutex, 0);
	init_pt = 1;
    }
}

SEXP C_start(SEXP sHost, SEXP sPort, SEXP sThreads) {
    const char *host = (TYPEOF(sHost) == STRSXP && LENGTH(sHost) > 0) ?
	CHAR(STRING_ELT(sHost, 0)) : 0;
    int port = Rf_asInteger(sPort);
    int threads = Rf_asInteger(sThreads);

    if (port < 1 || port > 65535)
	Rf_error("Invalid port %d", port);
    if (threads < 1 || threads > 1000)
	Rf_error("Invalid number of threads %d", threads);

    do_init();
    if (therver(host, port, threads, do_process))
	return ScalarLogical(0);

    /* printf("Started on %s:%d, try me.\n", host ? host : "*", port); */

    return ScalarLogical(1);
}

SEXP C_put(SEXP sKey, SEXP sWhat, SEXP sSFS) {
    int use_sfs = asInteger(sSFS);
    if (TYPEOF(sKey) != STRSXP || LENGTH(sKey) != 1)
	Rf_error("Invalid key, must be a string");
    if (!use_sfs && TYPEOF(sWhat) != RAWSXP)
	Rf_error("Value must be a raw vector unless SFS is used");
    do_init();
    obj_add(CHAR(STRING_ELT(sKey, 0)), sWhat, use_sfs ? 0 : RAW(sWhat), use_sfs ? 0 : XLENGTH(sWhat));
    return ScalarLogical(1);
}

SEXP C_clean() {
    do_init();
    obj_gc();
    return ScalarLogical(1);
}

/* from sock_restore.c */
SEXP sock_restore(int s, int need_opts);

SEXP C_ask(SEXP sHost, SEXP sPort, SEXP sCmd, SEXP sSFS) {
    SOCKET ss;
    int n, l, i = 1, port = asInteger(sPort), use_sfs = asInteger(sSFS);
    const char *host = 0;
    struct sockaddr_in sin;
    struct hostent *haddr;
    struct timeval tv;

    if (TYPEOF(sHost) != STRSXP || LENGTH(sHost) != 1)
	Rf_error("host must be a string");
    if (TYPEOF(sCmd) != RAWSXP &&
	(TYPEOF(sCmd) != STRSXP || LENGTH(sCmd) != 1))
	Rf_error("command must be string or a raw vector");
    host = CHAR(STRING_ELT(sHost, 0));
    if (port < 0 || port > 65535)
	Rf_error("invalid port");

    ss = socket(AF_INET, SOCK_STREAM, 0);
    if (ss == -1)
	Rf_error("Cannot obtain a socket %s", errno ? strerror(errno) : "");

    sin.sin_family = AF_INET;
    sin.sin_port = htons(port);
    if (host) {
        if (inet_pton(sin.sin_family, host, &sin.sin_addr) != 1) { /* invalid, try DNS */
            if (!(haddr = gethostbyname(host))) { /* DNS failed, */
                closesocket(ss);
                ss = -1;
		Rf_error("Cannot resolve host '%s'", host);
            }
            sin.sin_addr.s_addr = *((uint32_t*) haddr->h_addr); /* pick first address */
        }
    } else
        sin.sin_addr.s_addr = htonl(INADDR_ANY);

    if (connect(ss, (struct sockaddr*)&sin, sizeof(sin)))
	Rf_error("Unable to connect to %s:%d %s", host, port, errno ? strerror(errno) : "");

    /* enable TCP_NODELAY */
    setsockopt(ss, IPPROTO_TCP, TCP_NODELAY, (const void*) &i, sizeof(i));

    /* enable timeout so we can support R-level interrupts */
    tv.tv_sec = 1;
    tv.tv_usec = 0;
    setsockopt(ss, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);

    if (TYPEOF(sCmd) == RAWSXP)
	n = send(ss, RAW(sCmd), l = LENGTH(sCmd), 0);
    else {
	const char *cmd = CHAR(STRING_ELT(sCmd, 0));
	l = strlen(cmd);
	n = send(ss, cmd, l, 0);
    }

    if (n < l) {
	closesocket(ss);
	if (n < 0)
	    Rf_error("Error sending command %s", errno ? strerror(errno) : "");
	Rf_error("Error sending command, could only send %d of %d bytes", n, l);
    }

    {
	char buf[64], *c = 0;
	int p = 0;
	while (1) {
	    int need;
	    buf[p] = 0;
	    if ((c = strchr(buf, '\n'))) {
		*c = 0;
		break;
	    }
	    if (p > sizeof(buf) - 8) {
		closesocket(ss);
		Rf_error("Invalid response: %s", buf);
	    }
	    need = sizeof(buf) - 1 - p;
	    if (use_sfs) { /* we can't allow overflow, so fetch length by one */
		/* valid response must have at least 5 bytes: 'O' 'K' ' ' [0-9] '\n' */
		need = (p < 5) ? (5  - p) : 1;
	    }
	    n = recv(ss, buf + p, need, 0);
	    /* Rprintf("recv(@%d,%d)=%d\n", p, sizeof(buf) - 1 - p, n); */
	    if (n <= 0) {
		/* we have set timeout as to allow interrupts, HOWEVER,
		   the socket will NOT be closed after an interrupt! */
		if (errno == EAGAIN || errno == EWOULDBLOCK) {
		    R_CheckUserInterrupt();
		    continue;
		}
		closesocket(ss);
		if (n == 0) {
		    Rf_error("Connection closed unexpectedly");
		}
		Rf_error("Error while receiving response %s", errno ? strerror(errno) : "");
	    }
	    p += n;
	}
	if (strlen(buf) == 2 || !strcmp(buf, "UNSUPP")) { /* OK/NF alone */
	    closesocket(ss);
	    return mkString(buf);
	}
	if (!strncmp(buf, "OK ", 3)) { /* OK + length */
	    SEXP res;
	    char *dst;
	    unsigned long ii = 0, pl;
	    if (use_sfs) {
		res = sock_restore(ss, 0);
		closesocket(ss);
		return res;
	    }
	    pl = (unsigned long) atol(buf + 3);
	    res = allocVector(RAWSXP, pl);
	    dst = (char*) RAW(res);
	    p -= (c - buf) + 1; /* eat the header */
	    if (p > 0) {
		if (p > pl) /* can't exceed payload */
		    p = pl;
		memcpy(dst, c + 1, p);
		ii += p;
	    }
	    while (ii < pl) {
		unsigned long rq = (pl - ii < FETCH_SIZE) ? pl - ii : FETCH_SIZE;
		n = recv(ss, dst + ii, rq, 0);
		if (n < 0) {
		    if ((errno == EAGAIN || errno == EWOULDBLOCK)) {
			R_CheckUserInterrupt();
			continue;
		    }
		    closesocket(ss);
		    Rf_error("Error while reading, got %lu of %lu bytes %s", ii, pl, errno ? strerror(errno) : "");
		}
		if (n == 0) {
		    closesocket(ss);
		    Rf_error("Connections closed unexpectedly, got %lu of %lu bytes", ii, pl);
		}
		ii += n;
	    }
	    closesocket(ss);
	    return res;
	} else {
	    closesocket(ss);
	    Rf_error("Invalid response: %s", buf);
	}
    }
    /* never reached, but compiler may not know */
    return R_NilValue;
}
