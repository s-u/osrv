/* track dependencies

   Author and (c) Simon Urbanek <urbanek@R-project.org>
   License: MIT

*/

#include "obj.h"
#include "evqueue.h"

/* message posted on the queue */
typedef struct ev_resolved_s {
    int msg;
    char name[1];
} ev_resolved_t;

void dep_init();

/* return the event queue */
ev_queue_t *deps_queue();

/* add requirement to track;
   name: the key to use in the notification on completion
   keys: list of keys to track
   n: number of keys to track
   msg: message to use in the notification to allow multiplexing
   returns 0 on success; failure is mostly due to memory allocation issues */
int deps_add(const char *name, const char **keys, int n, int msg);

void deps_complete(const char *key);

