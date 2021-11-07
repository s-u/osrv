/* Event Queue

   Thread-safe queue. Uses mutex to guard queue modifications
   and signals condition on push (but not pop). Events can
   be pushed from either end, but pop works always from the
   front. It allows re-insertion of events.

   Events must be created using ev_add() and are placed on the
   queue. They can be retrieved using ev_pop() [returns
   immediately] or pop_wait() [waits until sucess or timeout].
   Once retrieved, they can be either placed back on the
   queue using ev_push() or released using ev_free();

   The payload of the event can be one of:
   1) fixed-length, automatically released by ev_free()
      [recommended]
      ev_create(NULL, length, NULL);
   2) any pointer, never released (constant)
      ev_create(ptr, ?, NULL);
   3) any pointer with custom function to free the event
      ev_create(ptr, ?, custom_free)
      [for holding nested objects]
   In both latter cases the length is stored but not used by
   the system so can be anything.

   The events can be passed between queues (i.e., it is legal
   to pop from one queue and push into another).

   Author and (c)2021 Simon Urbanek <urbanek@R-project.org>
   License: MIT

*/

#ifndef EVQUEUE_H__
#define EVQUEUE_H__

#include <stddef.h>

typedef struct ev_entry_s ev_entry_t;
typedef struct ev_queue_s ev_queue_t;

/* type for custom free function */
typedef ev_entry_t*(ev_entry_free_fn)(ev_entry_t*);

#ifndef EVQUEUE_INTERNAL__
struct ev_entry_s {
    void *data;
    size_t len;
};
#endif

/* queue API */
ev_queue_t *ev_queue_create();
void ev_queue_destroy(ev_queue_t *queue);

/* queue entry API */
ev_entry_t *ev_create(void *data, size_t len, ev_entry_free_fn *free_fn);
void ev_free(ev_entry_t *e);

ev_entry_t *ev_push(ev_queue_t *queue, ev_entry_t *e, int front);
ev_entry_t *ev_pop(ev_queue_t *queue);
ev_entry_t *ev_pop_wait(ev_queue_t *queue, double timeout);

/* custom free function that simply calls free() on the data pointer */
ev_entry_t *ev_free_data(ev_entry_t *e);

/* experimental */
/* observer FD can be registered (only one per queue)
   which will be sent exactly one byte after each
   push *after* the mutex is released. Note that this
   does not guarantee the availability of the event
   in case there are threads consuming the events,
   it is merely a convenience for simple situations 
   where meshing with fd-driven loops is required.
   Also note that setting the fd may cause ev_push()
   to block in case the fd blocking. */
int ev_queue_notify_fd(ev_queue_t *queue, int fd);

#endif
