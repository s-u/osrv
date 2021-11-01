/* Simple threaded TCP server (therver), based on a
   FIFO queue and worker threads. It is intended to
   run entirely in a parallel to the main thread.
   Also intentionally it uses static variables,
   the only dynamically allocated pieces are
   a) the thread pool and b) the queue entries

   Author and (c) Simon Urbanek <urbanek@R-project.org>
   License: MIT

   --- interface --- */

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

/* Binds host/port, then starts threads, host can be NULL for ANY.
   Returns non-zero for errors. */
int therver(const char *host, int port, int max_threads, process_fn_t process_fn);

