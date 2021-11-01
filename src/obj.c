/* simple thread-safe key/value object store

   Author and (c) Simon Urbanek <urbanek@R-project.org>
   License: MIT

*/

#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#include <Rinternals.h>

static pthread_mutex_t obj_mutex;
static int obj_init_ = 0;

#define OSRV_OBJ_STRUCT_ 1
#include "obj.h"

/* structure of the object entry */
struct obj_entry_s {
    obj_len_t len;
    void *obj;
    SEXP sWhat;
    /* private, may not be touched by client code */
    struct obj_entry_s *next;
    char key[1];
};

/* FIXME: use hash */
static obj_entry_t *obj_root;
static obj_entry_t *obj_gc_pool;

/* FIXME: the lifetime of data is not defined, need destructor? */
void obj_add(const char *key, SEXP sWhat, void *data, obj_len_t len) {
    obj_entry_t *e = (obj_entry_t*) calloc(1, sizeof(obj_entry_t) + strlen(key));
    strcpy(e->key, key);
    e->len = len;
    e->obj = data;
    e->sWhat = sWhat;
    if (sWhat) R_PreserveObject(sWhat);
    pthread_mutex_lock(&obj_mutex);
    e->next = obj_root;
    obj_root = e;
    pthread_mutex_unlock(&obj_mutex);
}

void obj_gc() {
    pthread_mutex_lock(&obj_mutex);
    /* FIXME: is this safe? We are hoping that
       R_ReleaseObject() cannot longjmp... */
    while (obj_gc_pool) {
	obj_entry_t *c = obj_gc_pool; 
	if (c->sWhat)
	    R_ReleaseObject(c->sWhat);
	obj_gc_pool = c->next;
	free(c);
    }
    pthread_mutex_unlock(&obj_mutex);
}

obj_entry_t *obj_get(const char *key, int rm) {
    obj_entry_t *e = obj_root, *prev = 0;
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
	prev = e;
	e = e->next;
    }
    pthread_mutex_unlock(&obj_mutex);
    return 0;
}

void obj_init() {
    if (!obj_init_) {
	pthread_mutex_init(&obj_mutex, 0);
	obj_init_ = 1;
    }
}
