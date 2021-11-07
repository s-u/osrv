#define EVQUEUE_INTERNAL__ 1

#include "evqueue.h"

struct ev_entry_s {
    void *data;
    size_t len;
    struct ev_entry_s *prev, *next;
    ev_entry_free_fn *free_fn;
    void *res;
};

#include <time.h>
#include <pthread.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>

struct ev_queue_s {
    ev_entry_t *head, *tail;
    int fd;
    pthread_mutex_t mutex;
    pthread_cond_t  cond;
};

/* free_fn can be NULL if no additional deallocation is
   needed for event objects */
ev_queue_t *ev_queue_create() {
    ev_queue_t *q = (ev_queue_t*) calloc(1, sizeof(ev_queue_t));
    if (!q) return q;
    q->tail = q->head = 0;
    q->fd = -1;
    if (pthread_mutex_init(&(q->mutex), NULL) ||
	pthread_cond_init(&(q->cond), NULL)) {
	free(q);
	return NULL;
    }
    return q;
}

/* there can be only one fd, so possible return values:
    0 = ok
    1 = different fd already set
   -1 = error (queue invalid) */
int ev_queue_notify_fd(ev_queue_t *queue, int fd) {
    if (!queue) return -1;
    if (queue->fd == -1 || queue->fd == fd) {
	queue->fd = fd;
	return 0;
    }
    return 1;
}

/* NOTE: this requires that there are no threads using this
   queue anymore. It does not synchronize threads using the
   queue, tha tit the responsibility of the caller */   
void ev_queue_destroy(ev_queue_t *queue) {
    pthread_mutex_destroy(&(queue->mutex));
    pthread_cond_destroy(&(queue->cond));
    free(queue);
}

/* put an event on the queue - either at the end (front=0)
   or at the head *front=1). Both queue and e must not be NULL.
   Also e must be a valid entry so the only way it can be
   obtained is from previous ev_pop(). The queue owns the
   event from this point on so the caller MUST NOT use the
   event anymore. Events are allowed to be passed between
   queues, i.e. you can push an even obtained by ev_pop()
   from a different queue.
*/
ev_entry_t *ev_push(ev_queue_t *queue, ev_entry_t *e, int front) {
    int fd;
    if (!e) return NULL;
    /* now lock and add */
    if (pthread_mutex_lock(&(queue->mutex)))
	return NULL;

    if (!(queue->head)) { /* first and only */
	queue->head = queue->tail = e;
    } else {
	if (front) {
	    ev_entry_t *head = queue->head;
	    e->next = head;
	    queue->head = head->prev = e;
	} else {
	    ev_entry_t *tail = queue->tail;
	    e->prev = tail;
	    queue->tail = tail->next = e;
	}
    }
    fd = queue->fd;
    /* modified, can signal */
    pthread_cond_signal(&(queue->cond));
    pthread_mutex_unlock(&(queue->mutex));

    /* if notify is wanted, can do now */
    /* we do this outside of the mutex so we don't block */
    if (fd != -1) {
	char one = 1;
	/* it would not be safe to meddle with fd, so we
	   don't do anything on error */
	if (write(fd, &one, 1) == -1) {}
    }
    return e;
}

/* If data is NULL then ev_create allocates len bytes
   inside the queue entry structure and points data
   into that space. Otherwise data and len values are simply
   stored in the structure - in that case free_fn
   as set in ev_queue_create will be called (if set)
   to released the contents. So there are following options:

   1) data = NULL, ev_free() works automatically
   2) data != NULL, free_fn != NULL: free_fn is called on release
   3) data != NULL, free_fn == NULL: data is never relased

   len is only used if data = NULL, otherwise the application
   is free to use it for storing any size it wishes.

   May return NULL if allocation fails
*/
ev_entry_t *ev_create(void *data, size_t len, ev_entry_free_fn *free_fn) {
    ev_entry_t *e;
    /* we gaurantee 2-4 guard bytes for safety just in case */
    e = calloc(1, sizeof(ev_entry_t) + ((data || len < 3) ? 0 : len ));
    if (!e) return e;
    if (!data && len > 0) /* automatic in-strucutre storage */
	e->data = (void*) &(e->res);
    else if (data)
	e->data = (void*) data;
    e->len = len;
    e->free_fn = free_fn;
    return e;
}

/* remove an entry - this one is internal, NOT using locks,
   and NO error checking on inputs, so it is assumed that the caller
   already has the queue lock and has verified inputs.
   Note that we use doubly-linked lists to allow removal
   of any entry in the queue, but the user API does not expose it,
   because the user has no way of guaranteeing the presence of
   an entry on the queue which is needed here.
*/
static ev_entry_t *ev_rm_(ev_queue_t *queue, ev_entry_t *e) {
    if (e == queue->head)
	queue->head = e->next;
    if (e == queue->tail)
	queue->tail = e->prev;
    if (e->next)
	e->next->prev = e->prev;
    if (e->prev)
	e->prev->next = e->next;
    /* must zero both to allow re-insertion */
    e->next = e->prev = NULL;
    return e;
}

/* pop event from the front of the queue
   NOTE: the caller takes ownership so must call
   ev_free() to release the event
 */
ev_entry_t *ev_pop(ev_queue_t *queue) {
    ev_entry_t *e;
    if (!queue) return NULL;
    if (pthread_mutex_lock(&(queue->mutex)))
	return NULL;
    e = (queue->head) ? ev_rm_(queue, queue->head) : NULL;
    pthread_mutex_unlock(&(queue->mutex));
    return e;
}

ev_entry_t *ev_pop_wait(ev_queue_t *queue, double timeout) {
    struct timespec tm;
    ev_entry_t *e;
    double at;
    if (!queue) return NULL;
    if (pthread_mutex_lock(&(queue->mutex)))
	return NULL;
    clock_gettime(CLOCK_REALTIME , &tm);
    at = ((double) tm.tv_sec) + (((double) tm.tv_nsec) / 1e9);
    at += timeout;
    tm.tv_sec = (time_t) at;
    at -= (double) ((time_t) at);
    at *= 1000000000.0;
    tm.tv_nsec = (long) at;
    while (!(queue->head)) {
	int res = pthread_cond_timedwait(&(queue->cond), &(queue->mutex), &tm);
	if (res == ETIMEDOUT)
	    break;
    }
    e = (queue->head) ? ev_rm_(queue, queue->head) : NULL;
    pthread_mutex_unlock(&(queue->mutex));
    return e;
}

/* IMPORTANT: the entry must NOT be on any queue */   
void ev_free(ev_entry_t *e) {
    if (e->data && e->data != &(e->res) && e->free_fn)
	e->free_fn(e);
    free(e);
}

ev_entry_t *ev_free_data(ev_entry_t *e) {
    if (e && e->data)
	free(e->data);
    return e;
}
