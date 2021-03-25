/* Simple Fast Serialisation
   experimental serialiser focused on speed

   (C)2021 Simon Urbanek <simon.urbanek@R-project.org>

   License: MIT
 */

#include <string.h>
#include <stdlib.h>

#include <Rinternals.h>

typedef unsigned long sfs_len_t;
typedef unsigned char sfs_ts;

typedef struct store_api store_api_t;
typedef struct fetch_api fetch_api_t;

typedef void(*store_fn_t)(store_api_t *api, sfs_ts ts, sfs_len_t el, sfs_len_t len, const void *buf);
typedef void(*fetch_fn_t)(fetch_api_t *api, void *buf, sfs_len_t len);

/* implementations need to define store_api and fetch_api
   structs with at least the following:

struct store_api {
    store_fn_t store;
    // implementations can add anything here...
};

struct fetch_api {
    fetch_fn_t fetch;
    // implementations can add anything here...
};

*/

void sfs_store(store_api_t *api, SEXP sWhat);

/* FIXME: sfs_load() currently uses Rf_error() and
   Rf_warning() - we should let the API decide what to do */
SEXP sfs_load(fetch_api_t *api);
