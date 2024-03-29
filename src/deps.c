/* track dependencies
   used by object store to notify dep_queue on completion of requirements

   Author and (c) Simon Urbanek <urbanek@R-project.org>
   License: MIT

*/

#include "deps.h"
#include "evqueue.h"

#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#include <Rinternals.h>

static pthread_mutex_t dep_mutex;
static int dep_init_ = 0;

typedef struct depent_s {
    struct depent_s *next;
    int nreq, msg;
    char **keys;
    char *status;
    char name[1];
} depent_t; /* DEPpendency ENTtry */

/* queue to post resolved dependencies to */
static ev_queue_t *dep_queue;

static depent_t *head, *tail;

void dep_init() {
    if (!dep_init_) {
        pthread_mutex_init(&dep_mutex, 0);
	if (!dep_queue)
	    dep_queue = ev_queue_create();
        dep_init_ = 1;
    }
}

ev_queue_t *deps_queue() {
    dep_init();
    return dep_queue;
}

/* is NOT allowed to call any object API
   since it is called from critical region in obj.
   NOTE: currently, we allow key to be NULL which essentially
   casues all depednencies to be checked for completion.
*/
void deps_complete(const char *key) {
    depent_t *e, *prev = 0;
    pthread_mutex_lock(&dep_mutex);
    e = head;
    while (e) {
	int i = 0, n = e->nreq, sat = 0;
	while (i < n) {
	    if (key && !strcmp(key, e->keys[i]))
		e->status[i] = 1;
	    sat += e->status[i];
	    i++;
	}
	if (sat == n) { /* all satisfied */
	    depent_t *cur = e;
	    ev_entry_t *ev;
	    ev_resolved_t *msg;
	    /* remove this entry */
	    if (head == e)
		head = e->next;
	    if (prev)
		prev->next = e->next;
	    /* add completion to queue */
	    ev = ev_create(NULL, strlen(e->name) + sizeof(int) + 4, NULL);
	    if (ev) {
		msg = (ev_resolved_t*) ev->data;
		msg->msg = e->msg;
		strcpy(msg->name, e->name);
		ev_push(dep_queue, ev, 0);
	    }
	    /* remove entry */
	    e = e->next;
	    free(cur);
	} else {
	    prev = e;
	    e = e->next;
	}
    }
    pthread_mutex_unlock(&dep_mutex);    
}

/* FIXME: nothing is efficient here - we could sort the deps,
   or hash them or do many other things to make it faster ... */
int deps_add(const char *name, const char **keys, int n, int msg) {
    depent_t *e;
    int slen = 0, nlen = strlen(name), i = 0;
    char *d;

    if (n < 0)
	n = 0;
    while (i < n)
	slen += strlen(keys[i++]) + 1;

    e = calloc(1, sizeof(depent_t) + nlen + ((sizeof(char*)) * (n + 1)) + (n + 1) + slen + 1);
    if (!e)
	return -1;
    e->nreq = n;
    e->msg = msg;
    strcpy(e->name, name);
    e->keys = (char**)  (e->name + nlen + 1);
    e->status = (char*) (e->name + nlen + 1 + ((sizeof(char*)) * (n + 1)));

    d = (char*) (e->name + nlen + 1 + ((sizeof(char*)) * (n + 1)) + (n + 1));

    /* FIXME! We have a problem: we need to check
       the initial existence of objects, but we can't do it under dep_mutex
       lock, because obj will try to lock it in its critical region, so
       if it did while we check, it would cause a deadlock.
       We are between a rock and a hard place: if we do this check *before*
       placing the dep entry then we are safe from memory management point
       of view, because we own the structure. But if an object will get added
       while we check, it won't signal and the dependency can get stuck.
       On the other hand if we try to do this check *after* adding the entry,
       then we guarantee that nothing will get lost, but if the dependency
       got statisfied in the meantime the structure will be already released.
       For now we go with Plan A, because Plan B could result in a segfault.
    */
    i = 0;
    while (i < n) {
	int l = strlen(keys[i]);
	memcpy(d, keys[i], l);
	e->keys[i] = d;
	e->status[i] = obj_get(d, 0) ? 1 : 0;
	d += l + 1;
	i++;
    }

    pthread_mutex_lock(&dep_mutex);
    if (!head)
	head = tail = e;
    else {
	tail->next = e;
	tail = e;
    }
    pthread_mutex_unlock(&dep_mutex);

    /* safety: verify all completions */
    deps_complete(0);

    return 0;
}
