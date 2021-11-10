/* Simple implementation of the HTTP(s) 1.x server protocol

   (c)2008-2021 Simon Urbanek

   License: MIT

*/

#ifndef HTTP_H__
#define HTTP_H__

#include "sconn.h"

#define HTTP_WS_UPGRADE 0x10
#define HTTP_RAW_BODY   0x20 /* if set, no attempts are made to decode the request body of known types */

/* http_request->method */
#define METHOD_POST   1
#define METHOD_GET    2
#define METHOD_HEAD   3
#define METHOD_PUT    4
#define METHOD_DELETE 5
#define METHOD_OTHER  8 /* for custom requests only */

/* http_request->attr */
#define CONNECTION_CLOSE  0x0001 /* Connection: close response behavior is requested */
#define HOST_HEADER       0x0002 /* headers contained Host: header (required for HTTP/1.1) */
#define HTTP_1_0          0x0004 /* the client requested HTTP/1.0 */
#define CONTENT_LENGTH    0x0008 /* Content-length: was specified in the headers */
#define THREAD_OWNED      0x0010 /* the worker is owned by a thread and cannot removed */
#define THREAD_DISPOSE    0x0020 /* the thread should dispose of the worker */
#define CONTENT_TYPE      0x0040 /* message has a specific content type set */
#define CONTENT_FORM_UENC 0x0080 /* message content type is application/x-www-form-urlencoded */
#define WS_UPGRADE        0x0100 /* upgrade to WebSockets protocol */

/* simple structure holding both the length and payload
   so it can be free()d in one shot */
typedef struct raw_s {
	size_t length;
	char data[1];
} raw_t;

/* structure represeting the HTTP requast */
typedef struct http_request {
	char *path, *body;             /* URL and request body */
	char *content_type;            /* content type (if set) */
	long content_length;           /* desired content length */
	char method;                   /* request part, method */
	int  attr;                     /* connection attributes */
	char *ws_protocol, *ws_version, *ws_key;
	raw_t *headers;
} http_request_t;

/* NOTE: all pointers in the structure are allocated
   using malloc/strdup. The process function can modify
   the contents. If it wishes to take ownership, it may do
   so, but then must replace the value with NULL. */

typedef struct http_connection http_connection_t;

typedef void(*http_process_callback)(http_request_t *req, http_connection_t *conn);

/* Runs HTTP server loop until the connection closes.
   For each request calls the process() callback. */
int http_connected(SOCKET s, int flags, http_process_callback process);

/* the following API can be used inside the process callback */

/* Send HTTP response. If any body payload is required, is must be sent
   using http_send() after this. Headers (if not NULL) must be complete
   lines terminated with exactly one \r\n each. If content_type is not
   set, it defaults to "text/plain". content_length can be -1 if no
   Content-Length header is desired.
*/
void http_response(http_connection_t *conn, int code, const char *txt,
                   const char *content_type, int content_length, const char *headers);

/* The process callback must either leave the connection in fulfilled
   state so next requests are allowed, or it must call http_abort
   to close the connection */

/* 0 = ok, 1 = connection closed, -1 = error */
int http_send(http_connection_t *c, const void *buf, size_t len);
void http_abort(http_connection_t *c);

#endif

/*--- The following makes the indenting behavior of emacs compatible
      with Xcode's 4/4 setting ---*/
/* Local Variables: */
/* indent-tabs-mode: t */
/* tab-width: 4 */
/* c-basic-offset: 4 */
/* End: */
