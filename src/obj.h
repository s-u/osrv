/* simple thread-safe key/value object store

   Author and (c) Simon Urbanek <urbanek@R-project.org>
   License: MIT

   NOTES:
   - obj_init() MUST be called before any of the functions are used

   - obj_get() can be called from any thread, but the result is onyl
     valid until obj_gc() call

   - obj_add() 
*/

#ifndef OSRV_OBJ_H_
#define OSRV_OBJ_H_

#include <Rinternals.h>

typedef unsigned long int obj_len_t;

#ifndef OSRV_OBJ_STRUCT_

/* public part of the object entry structure */
struct obj_entry_s {
    obj_len_t len;
    void *obj;
    SEXP sWhat;
};

#endif

typedef struct obj_entry_s obj_entry_t;

void obj_init();

/* add object to the object store
   Uses R_PreserveObject on sWhat so must be called from a place
   where R API calls are safe (unless sWhat is NULL).
   key is copied, sWhat/data is stored as-is
*/
void obj_add(const char *key, SEXP sWhat, void *data, obj_len_t len);

/* release all objects that were deleted
   Must be called from a place where R API is safe. */
void obj_gc();

/* retrieves object for a key
   if rm != 0 then the object is also removed from the store
   This function is thread-safe, but the result is only valid until
   the next call to obj_gc() */
obj_entry_t *obj_get(const char *key, int rm);

#endif
