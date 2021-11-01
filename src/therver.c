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

