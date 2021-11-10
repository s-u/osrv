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

#include "therver.h"

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

int therver_id = 0;

typedef struct qentry_s {
    /* used for queuing */
    struct qentry_s *prev, *next;
    /* connection info */
    conn_t c;
} qentry_t;

struct therver_s {
    volatile int active;
    int ss;

    process_fn_t process;

    pthread_t *worker_threads;
    pthread_t accept_thread;

    qentry_t root;
    pthread_mutex_t pool_mutex;
    pthread_cond_t pool_work_cond;

    /* we keep all thervers recorded to support fork() handling */
    struct therver_s *next;
};

static therver_t *first_therver;

static void *worker_thread(void *arg) {
    therver_t *t = (therver_t*) arg;
    qentry_t *me;
    void *data = 0;
    /* printf("worker_thread %p is a go\n", (void*)&me); */
    while (t->active) {
	/* lock queue mutex */
	pthread_mutex_lock(&(t->pool_mutex));
	/* printf("worker %p waiting\n", (void*)&me); */
	
	/* wait on condition until we get work */
	/* FIXME: we should use timed wait in case something gets
	   stuck and we get a shutdown */
	while (t->active && (!(me = t->root.next) || me == &t->root))
	    pthread_cond_wait(&t->pool_work_cond, &t->pool_mutex);

	/* if the server was shut down, don't process anything in the queue */
	if (!t->active) {
	    pthread_mutex_unlock(&t->pool_mutex);
	    break;
	}

	/* remove us from the queue */
	t->root.next = me->next;
	if (me->next) me->next->prev = &t->root;
	/* we don't care to update our prev/next since we never use it */

	/* release queue lock */
	pthread_mutex_unlock(&t->pool_mutex);

	/* printf("worker %p calling process() with s=%d\n", (void*)&me, me->c.s); */
	me->c.data = data;
	/* serve the connection */
	t->process(&me->c);
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
static int add_task(therver_t *t, qentry_t *me) {
    /* printf("add_task(%d) about to lock\n", me->c.s); */
    pthread_mutex_lock(&t->pool_mutex);
    /* printf(" add_task() locked, adding\n"); */
    me->next = &t->root;
    me->prev = t->root.prev;
    if (me->prev) me->prev->next = me;
    t->root.prev = me;
    /* printf(" add_task() broadcasting\n"); */
    pthread_cond_broadcast(&t->pool_work_cond);
    pthread_mutex_unlock(&t->pool_mutex);
    /* printf(" add_task() unlocked\n"); */
    return 0;
}

/* we have to grab the mutex - you should NOT
   fork in the critical region (we don't ..)
   The good news is that fork() *only* copies
   the thread that called the fork() and we
   know that none of ours did, so none of
   the threads should be impacted.
 */
static void prefork() {
    therver_t *t = first_therver;
    while (t) {
	pthread_mutex_lock(&t->pool_mutex);
	t = t->next;
    }
}

/* nothing to do, just release the mutex
   and we can go about our business */
static void forked_parent() {
    therver_t *t = first_therver;
    while (t) {
	pthread_mutex_unlock(&t->pool_mutex);
	t = t->next;
    }
}

/* we want to close all sockets in the child
   to make sure it won't interfere with the
   parent's communication. Note that although
   we signal the server thread to quit, it
   certainly didn't call fork() so it's just
   a theoretical case since we pretty much know
   it wasn't copied into the child */
static void forked_child() {
    therver_t *t = first_therver;
    while (t) {
	qentry_t *me;
	/* make accept thread quit */
	t->active = 0;
	/* close server socket */
	if (t->ss != -1) {
	    closesocket(t->ss);
	    t->ss = -1;
	}
	/* close and reset all sockets in the queue */
	me = t->root.next;
	while (me && me != &t->root) {
	    if (me->c.s != -1)
		closesocket(me->c.s);
	    me->c.s = -1;
	    me = me->next;
	}
	pthread_mutex_unlock(&t->pool_mutex);
	t = t->next;
    }
}

/* thread for the incoming connections */
static void *accept_thread_run(void *th) {
    therver_t *t = (therver_t*) th;
    int s;
    socklen_t cli_al;
    struct sockaddr_in sin_cli;
    /* printf("accept_thread %p is a go\n", (void*)&s); */
    while (t->active) {
	cli_al = sizeof(sin_cli);
	s = accept(t->ss, (struct sockaddr*) &sin_cli, &cli_al);
	/* printf("accept_thread: accept=%d\n", s); */
	if (s != -1) {
	    qentry_t *me = (qentry_t*) calloc(1, sizeof(qentry_t));
	    if (me) {
		/* once enqueued the task takes ownership of me.
		   On any kind of error we have to free it. */
		/* printf(" - accept_thread got me, enqueuing\n"); */
		me->c.s = s;
		if (add_task(t, me)) {
		    /* printf(" - add_task() failed, oops\n"); */
		    free(me);
		    me = 0;
		    close(s);
		}
	    } else /* sorry, out of memory, over and out */
		close(s);
	}
    }
    close(t->ss);
    t->ss = -1;
    return 0;
}

static int atfork_set = 0;

static int start_threads(therver_t *t, int max_threads) {
    sigset_t mask, omask;
    pthread_attr_t t_attr;
    pthread_attr_init(&t_attr); /* all out threads are detached since we don't care */
    pthread_attr_setdetachstate(&t_attr, PTHREAD_CREATE_DETACHED);

    t->root.next = t->root.prev = &t->root;
    t->root.c.s = -1;

    if (!(t->worker_threads = malloc(sizeof(pthread_t) * max_threads)))
	return -1;

    /* init cond/mutex */
    pthread_mutex_init(&t->pool_mutex, 0);
    pthread_cond_init(&t->pool_work_cond, 0);

    if (!atfork_set) {
	/* in case the user uses multicore or something else, we want to shut down
	   all proessing in the children */
	pthread_atfork(prefork, forked_parent, forked_child);
	atfork_set = 1;
    }

    /* mask all signals - the threads will inherit the mask
       and thus not fire and leave it to R */
    sigfillset(&mask);
    sigprocmask(SIG_SETMASK, &mask, &omask);

    /* start worker threads */
    for (int i = 0; i < max_threads; i++)
	pthread_create(&t->worker_threads[i], &t_attr, worker_thread, t);

    /* start accept thread */
    pthread_create(&t->accept_thread, &t_attr, accept_thread_run, t);

    /* re-set the mask back for the main thread */
    sigprocmask(SIG_SETMASK, &omask, 0);

    return 0;
}

therver_t *therver(const char *host, int port, int max_threads, process_fn_t process_fn) {
    therver_t *t;
    int i, ss;
    struct sockaddr_in sin;
    struct hostent *haddr;

    if (!(t = (therver_t*) calloc(1, sizeof(therver_t))))
	return 0;

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
	free(t);
        return 0;
    }

    t->ss = ss;
    t->active = 1;
    t->process = process_fn;

    /* record this one in the list of thervers for fork() handling */
    if (!first_therver)
	first_therver = t;
    else {
	therver_t *x = first_therver;
	while (x->next)
	    x = x->next;
	x->next = t;
    }

    if (start_threads(t, max_threads)) {
	close(ss);
	t->active = 0;
	t->ss = -1;
	/* we cannot safely release any therver that has been registered
	   so it will stay there */
	return 0;
    }

    return t;
}

int therver_shutdown(therver_t *th) {
    /* we don't actually do anything other than signalling
       the therver to shut down */
    if (th) {
	th->active = 0;
	/* FIXME: we should signal the workers ... */
	return 0;
    }
    return -1; /* invalid th */
}
