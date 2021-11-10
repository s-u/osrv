/* Simple threaded TCP server (therver), based on a
   FIFO queue and worker threads. It is intended to
   run entirely in a parallel to the main thread.

   Author and (c) Simon Urbanek <urbanek@R-project.org>
   License: MIT

*/

typedef struct conn_s {
    int s;      /* socket to the client */
    void *data; /* opaque per-thread pointer */
} conn_t;

/* The process(conn_t*) API:
   You don't own the parameter, but it is guaranteed
   to live until you return. If you close the socket
   you must also set s = -1 to indicate you did so,
   otherwise the socket is automatically closed. */
typedef void (*process_fn_t)(conn_t*);

/* opaque therver structure */
typedef struct therver_s therver_t;

/* Binds host/port, then starts threads, host can be NULL for ANY.
   Returns non-zero for errors. */
therver_t *therver(const char *host, int port, int max_threads, process_fn_t process_fn);

/* shuts down the therver, the handle may no longer be used. */
int therver_shutdown(therver_t *th);

/* NOTE: To avoid threading issues, thervers are never actually
   released. They can be asked to shut down, but the resources
   associated with a therver are possibly never released in case
   children threads have outstanding work. At this point you cannot
   assume that you may re-start a therver on the same port as
   a previously started therver. We may change this in the
   future. */
