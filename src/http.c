/* Simple implementation of the HTTP(s) 1.x server protocol

   (c)2008-2021 Simon Urbanek

   License: MIT

*/

#include "tls.h"
#include "http.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

/* size of the line buffer for each worker (request and header only)
 * requests that have longer headers will be rejected with 413
 * Note that cookies can be quite big and some browsers send them
 * in one line, so this should not be too small */
#define LINE_BUF_SIZE 32768

/* debug output - change the DBG(X) X to enable debugging output */
#ifdef RSERV_DEBUG
#define DBG(X) X
#else
#define DBG(X)
#endif

#define MAX_SEND (1024*1024) /* 1Mb */

/* --- httpd --- */

#define PART_REQUEST 0
#define PART_HEADER  1
#define PART_BODY    2

struct buffer {
	struct buffer *next, *prev;
	int size, length;
	char data[1];
};

struct http_connection {
	SOCKET s;
	recv_fn_t recv;
	send_fn_t send;
	void *res;

	http_request_t *request;

	http_process_callback process;

	int flags;
	int part;

	char *line_buf;                  /* line buffer (used for request and headers) */
	unsigned int line_pos, body_pos; /* positions in the buffers */
	struct buffer *headers;
};

#ifdef unix
#include <sys/un.h> /* needed for unix sockets */
#endif

#define IS_HTTP_1_1(C) (((C)->attr & HTTP_1_0) == 0)

/* returns the HTTP/x.x string for a given connection - we support 1.0 and 1.1 only */
#define HTTP_SIG(C) (IS_HTTP_1_1(C) ? "HTTP/1.1" : "HTTP/1.0")

/* free buffers starting from the tail(!!) */
static void free_buffer(struct buffer *buf) {
	if (!buf) return;
	if (buf->prev)
		free_buffer(buf->prev);
	free(buf);
}

/* allocate a new buffer */
static struct buffer *alloc_buffer(int size, struct buffer *parent) {
	struct buffer *buf = (struct buffer*) malloc(sizeof(struct buffer) + size);
	if (!buf) return buf;
	buf->next = 0;
	buf->prev = parent;
	if (parent)
		parent->next = buf;
	buf->size = size;
	buf->length = 0;
	return buf;
}

/* convert doubly-linked buffers into one big raw vector */
static raw_t *collect_buffers(struct buffer *buf) {
	raw_t *res;
	char  *dst;
	size_t len = 0;
	if (!buf) return 0;
	while (buf->prev) { /* count the total length and find the root */
		len += buf->length;
		buf = buf->prev;
	}
	res = (raw_t*) malloc(sizeof(raw_t) + len + buf->length);
	if (res) {
		res->length = len + buf->length;
		dst = (char*) (buf->data);
		while (buf) {
			memcpy(dst, buf->data, buf->length);
			dst += buf->length;
			buf = buf->next;
		}
	}
	return res;
}

static void clear_http_request(http_request_t *c)
{
	if (c->path) {
		free(c->path);
		c->path = NULL;
	}
	if (c->body) {
		free(c->body);
		c->body = NULL;
	}
	if (c->content_type) {
		free(c->content_type);
		c->content_type = NULL;
	}
	if (c->ws_key) {
		free(c->ws_key);
		c->ws_key = NULL;
	}
	if (c->ws_protocol) {
		free(c->ws_protocol);
		c->ws_protocol = NULL;
	}
	if (c->ws_version) {
		free(c->ws_version);
		c->ws_version = NULL;
	}
	if (c->headers) {
		free(c->headers);
		c->headers = NULL;
	}
	c->content_length = 0;
	c->attr = 0;
	c->method = 0;
}

static void free_http_connection(http_connection_t *c)
{
	if (c->line_buf) {
		free(c->line_buf);
		c->line_buf = NULL;
	}
	if (c->request) {
		clear_http_request(c->request);
		free(c->request);
		c->request = NULL;
	}
	if (c->headers) {
		free_buffer(c->headers);
		c->headers = NULL;
	}
	if (c->s != INVALID_SOCKET) {
		closesocket(c->s);
		c->s = INVALID_SOCKET;
	}
	free(c);
}

static int send_response(http_connection_t *c, const char *buf, unsigned int len)
{
	unsigned int i = 0;
	/* we have to tell R to ignore SIGPIPE otherwise it can raise an error
	   and get us into deep trouble */
	while (i < len) {
		int n = c->send((socket_connection_t*) c, buf + i, len - i);
		if (n < 1)
			return -1;
		i += n;
	}
	return 0;
}

/* sends HTTP/x.x plus the text (which should be of the form " XXX ...") */
static int send_http_response(http_connection_t *c, const char *text) {
	char buf[96];
	const char *s = HTTP_SIG(c->request);
	int l = strlen(text), res;
	/* reduce the number of packets by sending the payload en-block from buf */
	if (l < sizeof(buf) - 10) {
		strcpy(buf, s);
		strcpy(buf + 8, text);
		return send_response(c, buf, l + 8);
	}
	res = c->send((socket_connection_t*) c, s, 8);
	if (res < 8) return -1;
	return send_response(c, text, strlen(text));
}

/* decode URI in place (decoding never expands) */
void uri_decode(char *s) {
	char *t = s;
	while (*s) {
		if (*s == '+') { /* + -> SPC */
			*(t++) = ' '; s++;
		} else if (*s == '%') {
			unsigned char ec = 0;
			s++;
			if (*s >= '0' && *s <= '9') ec |= ((unsigned char)(*s - '0')) << 4;
			else if (*s >= 'a' && *s <= 'f') ec |= ((unsigned char)(*s - 'a' + 10)) << 4;
			else if (*s >= 'A' && *s <= 'F') ec |= ((unsigned char)(*s - 'A' + 10)) << 4;
			if (*s) s++;
			if (*s >= '0' && *s <= '9') ec |= (unsigned char)(*s - '0');
			else if (*s >= 'a' && *s <= 'f') ec |= (unsigned char)(*s - 'a' + 10);
			else if (*s >= 'A' && *s <= 'F') ec |= (unsigned char)(*s - 'A' + 10);
			if (*s) s++;
			*(t++) = (char) ec;
		} else *(t++) = *(s++);
	}
	*t = 0;
}

/* finalize a request - essentially for HTTP/1.0 it means that
 * we have to close the connection */
static void fin_request(http_request_t *c) {
	if (!IS_HTTP_1_1(c))
		c->attr |= CONNECTION_CLOSE;
}

static void http_close(http_connection_t *arg) {
	closesocket(arg->s);
	arg->s = -1;
}

static void process_request(http_connection_t *c) {
	if (c->process) {
		if (c->headers)
			c->request->headers = collect_buffers(c->headers);
		c->process(c->request, c);
	}
	fin_request(c->request);
}

/* this function is called to fetch new data from the client
 * connection socket and process it */
static void http_input_iteration(http_connection_t *c) {
	int n;
	http_request_t *req;

	DBG(printf("worker_input_handler, data=%p\n", (void*) c));
	if (!c || !c->request) return;
	req = c->request;

	DBG(printf("input handler for worker %p (sock=%d, part=%d, method=%d, line_pos=%d)\n", (void*) c, (int)c->s, (int)c->part, (int)req->method, (int)c->line_pos));

    /* FIXME: there is one edge case that is not caught on unix: if
     * recv reads two or more full requests into the line buffer then
     * this function exits after the first one, but input handlers may
     * not trigger, because there may be no further data. It is not
     * trivial to fix, because just checking for a full line at the
     * beginning and not calling recv won't trigger a new input
     * handler. However, under normal circumstance this should not
     * happen, because clients should wait for the response and even
     * if they don't it's unlikely that both requests get combined
     * into one packet. */
	if (c->part < PART_BODY) {
		char *s = c->line_buf;
		n = c->recv((socket_connection_t*) c, c->line_buf + c->line_pos, LINE_BUF_SIZE - c->line_pos - 1);
		DBG(printf("[recv n=%d, line_pos=%d, part=%d]\n", n, c->line_pos, (int)c->part));
		if (n < 0) { /* error, scrape this worker */
			http_close(c);
			return;
		}
		if (n == 0) { /* connection closed -> try to process and then remove */
			/* process makes only sense if we at least have the request line */
			if (c->request->path)
				process_request(c);
			http_close(c);
			return;
		}
		c->line_pos += n;
		c->line_buf[c->line_pos] = 0;
		DBG(printf("in buffer: {%s}\n", c->line_buf));
		while (*s) {
			/* ok, we have genuine data in the line buffer */
			if (s[0] == '\n' || (s[0] == '\r' && s[1] == '\n')) { /* single, empty line - end of headers */
				/* --- check request validity --- */
				DBG(printf(" end of request, moving to body\n"));
				if (!(req->attr & HTTP_1_0) && !(req->attr & HOST_HEADER)) { /* HTTP/1.1 mandates Host: header */
					send_http_response(c, " 400 Bad Request (Host: missing)\r\nConnection: close\r\n\r\n");
					http_close(c);
					return;
				}
				if ((req->attr & CONTENT_LENGTH) && req->content_length) {
					DBG(printf(" allocating buffer for body %ld bytes\n", (long) req->content_length));
					if (req->content_length < 0 ||  /* we are parsing signed so negative numbers are bad */
						req->content_length > 2147483640 || /* R will currently have issues with body around 2Gb or more, so better to not go there */
						!(req->body = (char*) malloc(req->content_length + 1 /* allocate an extra termination byte */ ))) {
						send_http_response(c, " 413 Request Entity Too Large (request body too big)\r\nConnection: close\r\n\r\n");
						http_close(c);
						return;
					}
				}
				c->body_pos = 0;
				c->part = PART_BODY;
				if (s[0] == '\r') s++;
				s++;
				/* move the body part to the beginning of the buffer */
				c->line_pos -= s - c->line_buf;
				memmove(c->line_buf, s, c->line_pos);
				/* GET/HEAD or no content length mean no body */
				if (req->method == METHOD_GET || req->method == METHOD_HEAD ||
					!(req->attr & CONTENT_LENGTH) || req->content_length == 0) {
					if ((req->attr & CONTENT_LENGTH) && req->content_length > 0) {
						send_http_response(c, " 400 Bad Request (GET/HEAD with body)\r\n\r\n");
						http_close(c);
						return;
					}
					process_request(c);
					if (req->attr & CONNECTION_CLOSE) {
						http_close(c);
						return;
					}
					/* keep-alive - reset the worker so it can process a new request */
					clear_http_request(c->request);
					if (c->headers) { free_buffer(c->headers); c->headers = NULL; }
					c->body_pos = 0;
					c->part = PART_REQUEST;
					return;
				}
				/* copy body content (as far as available) */
				c->body_pos = (req->content_length < c->line_pos) ? req->content_length : c->line_pos;
				DBG(printf(" body_pos=%d, content-length=%ld\n", c->body_pos, (long) req->content_length));
				if (c->body_pos) {
					memcpy(req->body, c->line_buf, c->body_pos);
					c->line_pos -= c->body_pos; /* NOTE: we are NOT moving the buffer since non-zero left-over causes connection close */
				}
				/* POST/PUT will continue into the BODY part */
				break;
			}
			{
				char *bol = s;
				while (*s && *s != '\r' && *s != '\n') s++;
				if (!*s) { /* incomplete line */
					if (bol == c->line_buf) {
						if (c->line_pos < LINE_BUF_SIZE) /* one, incomplete line, but the buffer is not full yet, just return */
							return;
						/* the buffer is full yet the line is incomplete - we're in trouble */
						send_http_response(c, " 413 Request entity too large\r\nConnection: close\r\n\r\n");
						http_close(c);
						return;
					}
					/* move the line to the begining of the buffer for later requests */
					c->line_pos -= bol - c->line_buf;
					memmove(c->line_buf, bol, c->line_pos);
					return;
				} else { /* complete line, great! */
					if (*s == '\r') *(s++) = 0;
					if (*s == '\n') *(s++) = 0;
					DBG(printf("complete line: {%s}\n", bol));
					if (c->part == PART_REQUEST) {
						/* --- process request line --- */
						unsigned int rll = strlen(bol); /* request line length */
						char *url = strchr(bol, ' ');
						if (!url || rll < 14 || strncmp(bol + rll - 9, " HTTP/1.", 8)) { /* each request must have at least 14 characters [GET / HTTP/1.0] and have HTTP/1.x */
							send_response(c, "HTTP/1.0 400 Bad Request\r\n\r\n", 28);
							http_close(c);
							return;
						}
						url++;
						if (!strncmp(bol + rll - 3, "1.0", 3)) req->attr |= HTTP_1_0;
						if (!strncmp(bol, "GET ", 4))  req->method = METHOD_GET;
						if (!strncmp(bol, "PUT ", 4))  req->method = METHOD_PUT;
						if (!strncmp(bol, "POST ", 5)) req->method = METHOD_POST;
						if (!strncmp(bol, "HEAD ", 5)) req->method = METHOD_HEAD;
						if (!strncmp(bol, "DELETE ", 7))  req->method = METHOD_DELETE;
						{
							char *mend = url - 1;
							/* we generate a header with the method so it can be passed to the handler */
							if (!c->headers)
								c->headers = alloc_buffer(1024, NULL);
							/* make sure it fits */
							if (c->headers->size - c->headers->length >= 18 + (mend - bol)) {
								if (!req->method) req->method = METHOD_OTHER;
								/* add "Request-Method: xxx" */
								memcpy(c->headers->data + c->headers->length, "Request-Method: ", 16);
								c->headers->length += 16;
								memcpy(c->headers->data + c->headers->length, bol, mend - bol);
								c->headers->length += mend - bol;	
								c->headers->data[c->headers->length++] = '\n';
							}
						}
						if (!req->method) {
							send_http_response(c, " 501 Invalid or unimplemented method\r\n\r\n");
							http_close(c);
							return;
						}
						bol[strlen(bol) - 9] = 0;
						req->path = strdup(url);
						c->part = PART_HEADER;
						DBG(printf("parsed request, method=%d, URL='%s'\n", (int)req->method, req->path));
					} else if (c->part == PART_HEADER) {
						/* --- process headers --- */
						char *k = bol;
						if (!c->headers)
							c->headers = alloc_buffer(1024, NULL);
						if (c->headers) { /* record the header line in the buffer */
							int l = strlen(bol);
							if (l) { /* this should be really always true */
								if (c->headers->length + l + 1 > c->headers->size) { /* not enough space? */
									int fits = c->headers->size - c->headers->length;
									int needs = 2048;
									if (fits) {
										memcpy(c->headers->data + c->headers->length, bol, fits);
										c->headers->length += fits;
									}
									while (l + 1 - fits >= needs) needs <<= 1;
									if (alloc_buffer(needs, c->headers)) {
										c->headers = c->headers->next;
										memcpy(c->headers->data, bol + fits, l - fits);
										c->headers->length = l - fits;
										c->headers->data[c->headers->length++] = '\n';
									}
								} else {
									memcpy(c->headers->data + c->headers->length, bol, l);
									c->headers->length += l;	
									c->headers->data[c->headers->length++] = '\n';
								}
							}
						}
						/* lower-case all header names */
						while (*k && *k != ':') {
							if (*k >= 'A' && *k <= 'Z')
								*k |= 0x20;
							k++;
						}
						if (*k == ':') {
							*(k++) = 0;
							while (*k == ' ' || *k == '\t') k++;
							DBG(printf("header '%s' => '%s'\n", bol, k));
							if (!strcmp(bol, "upgrade") && !strcmp(k, "websocket"))
								req->attr |= WS_UPGRADE;
							if (!strcmp(bol, "content-length")) {
								req->attr |= CONTENT_LENGTH;
								req->content_length = atol(k);
							}
							if (!strcmp(bol, "content-type")) {
								char *l = k;
								/* change the content type to lower case,
								   however, stop at ; since training content
								   may be case-sensitive such as multipart-boundary
								   (see #149) */
								while (*l && *l != ';') { if (*l >= 'A' && *l <= 'Z') *l |= 0x20; l++; }
								req->attr |= CONTENT_TYPE;
								if (req->content_type) free(req->content_type);
								req->content_type = strdup(k);
								if (!strncmp(k, "application/x-www-form-urlencoded", 33))
									req->attr |= CONTENT_FORM_UENC;
							}
							if (!strcmp(bol, "host"))
								req->attr |= HOST_HEADER;
							if (!strcmp(bol, "connection")) {
								char *l = k;
								while (*l) { if (*l >= 'A' && *l <= 'Z') *l |= 0x20; l++; }
								if (!strncmp(k, "close", 5))
									req->attr |= CONNECTION_CLOSE;
							}
							if (!strcmp(bol, "sec-websocket-key")) {
								if (req->ws_key) free(req->ws_key);
								req->ws_key = strdup(k);
							}
							if (!strcmp(bol, "sec-websocket-protocol")) {
								if (req->ws_protocol) free(req->ws_protocol);
								req->ws_protocol = strdup(k);
							}
							if (!strcmp(bol, "sec-websocket-version")) {
								if (req->ws_version) free(req->ws_version);
								req->ws_version = strdup(k);
							}
							DBG(printf(" [attr = %x]\n", req->attr));
						}
					}
				}
			}
		}
		if (c->part < PART_BODY) {
			/* we end here if we processed a buffer of exactly one line */
			c->line_pos = 0;
			return;
		}
	}
	if (c->part == PART_BODY && req->body) { /* BODY  - this branch always returns */
		if (c->body_pos < req->content_length) { /* need to receive more ? */
			DBG(printf("BODY: body_pos=%d, content_length=%ld\n", c->body_pos, req->content_length));
			n = c->recv((socket_connection_t*) c, req->body + c->body_pos, req->content_length - c->body_pos);
			DBG(printf("      [recv n=%d - had %u of %lu]\n", n, c->body_pos, req->content_length));
			c->line_pos = 0;
			if (n < 0) { /* error, scrap this worker */
				http_close(c);
				return;
			}
			if (n == 0) { /* connection closed -> try to process and then remove */
				process_request(c);
				http_close(c);
				return;
			}
			c->body_pos += n;
		}
		if (c->body_pos == req->content_length) { /* yay! we got the whole body */
			process_request(c);
			if (req->attr & CONNECTION_CLOSE || c->line_pos) { /* we have to close the connection if there was a double-hit */
				http_close(c);
				return;
			}
			/* keep-alive - reset the worker so it can process a new request */
			clear_http_request(c->request);
			if (c->headers) { free_buffer(c->headers); c->headers = NULL; }
			c->line_pos = 0; c->body_pos = 0;
			c->part = PART_REQUEST;
			return;
		}
	}

	/* we enter here only if recv was used to leave the headers with no body */
	if (c->part == PART_BODY && !req->body) {
		char *s = c->line_buf;
		if (c->line_pos > 0) {
			if ((s[0] != '\r' || s[1] != '\n') && (s[0] != '\n')) {
				send_http_response(c, " 411 length is required for non-empty body\r\nConnection: close\r\n\r\n");
				http_close(c);
				return;
			}
			/* empty body, good */
			process_request(c);
			if (req->attr & CONNECTION_CLOSE) {
				http_close(c);
				return;
			} else { /* keep-alive */
				int sh = 1;
				if (s[0] == '\r') sh++;
				if (c->line_pos <= sh)
					c->line_pos = 0;
				else { /* shift the remaining buffer */
					memmove(c->line_buf, c->line_buf + sh, c->line_pos - sh);
					c->line_pos -= sh;
				}
				/* keep-alive - reset the worker so it can process a new request */
				clear_http_request(c->request);
				if (c->headers) { free_buffer(c->headers); c->headers = NULL; }
				c->body_pos = 0;
				c->part = PART_REQUEST;
				return;
			}
		}
		n = c->recv((socket_connection_t*) c, c->line_buf + c->line_pos, LINE_BUF_SIZE - c->line_pos - 1);
		if (n < 0) { /* error, scrap this worker */
			http_close(c);
			return;
		}
		if (n == 0) { /* connection closed -> try to process and then remove */
			process_request(c);
			http_close(c);
			return;
		}
		if ((s[0] != '\r' || s[1] != '\n') && (s[0] != '\n')) {
			send_http_response(c, " 411 length is required for non-empty body\r\nConnection: close\r\n\r\n");
			http_close(c);
			return;
		}
	}
}

/* this defines which clinets are allowed, for now all */
static int check_tls_client(int verify, const char *cn) {
	return 0;
}

int http_connected(SOCKET s, int flags, http_process_callback process) {
	http_connection_t *c = (http_connection_t*) calloc(1, sizeof(http_connection_t));

	if (!c)
		return -1;

	c->s         = s;
	c->flags     = flags;
	c->process   = process;
	c->recv      = socket_recv;
	c->send      = socket_send;

	if (!(c->line_buf = (char*) malloc(LINE_BUF_SIZE)) ||
		!(c->request = (http_request_t*) calloc(1, sizeof(http_request_t)))) {
		free_http_connection(c);
		return -1;
	}

	if ((c->flags & SRV_TLS) && shared_tls(0)) {
		char cn[256];
		socket_connection_t *sc = (socket_connection_t*) c;
		add_tls(sc, shared_tls(0), 1);
		if (check_tls_client(verify_peer_tls(sc, cn, 256), cn)) {
			close_tls(sc);
			closesocket(sc->s);
			free_http_connection(c);
			return -2;
		}
	}

	while (c->s != -1)
		http_input_iteration(c);

	free_http_connection(c);
	return 0;
}

/* 0 = ok, 1 = connection closed, -1 = error */
int http_send(http_connection_t *c, const void *buf, size_t len) {
	while (len) {
		ssize_t ts = (len > MAX_SEND) ? MAX_SEND : ((ssize_t) len);
		ssize_t n = c->send((socket_connection_t*) c, buf, ts);
		if (n < 1)
			return (n < 0) ? -1 : 1;
		len -= n;
		buf += n;
	}
	return 0;
}

static const char hex[16] = "0123456789abcdef";

int http_send_chunk(http_connection_t *conn, const void *buf, size_t len) {
	char sz[20], *sc;
	int r;
	if (len > 0) {
		int szl = 0;
		size_t l = len;
		/* compute the number of bytes needed for the length */
		while (l > 0) {
			szl++;
			l >>= 4;
		}
		sc = sz + (szl - 1);
		l = len;
		while (l > 0) {
			*(sc--) = hex[l & 15];
			l >>= 4;
		}
		sc = sz + szl;
	} else {
		/* NOTE: we do send the final \r\n so the response is complete,
		   we do not support (optional) trailer headers. */
		strcpy(sz, "0\r\n\r\n");
		return http_send(conn, sz, 5);
	}
	*(sc++) = '\r';
	*(sc++) = '\n';
	/* <size>\r\n */
	if ((r = http_send(conn, sz, sc - sz)))
		return r;
	/* chunk */
	if ((r = http_send(conn, buf, len)))
		return r;
	/* \r\n */
	return http_send(conn, sc - 2, 2);
}

int  http_response(http_connection_t *conn, int code, const char *txt,
				   const char *content_type, int content_length, const char *headers) {
	http_request_t *req = conn->request;
	char buf[512];
	if (content_length < 0)
		snprintf(buf, sizeof(buf), "HTTP/1.%c %d %s\r\nContent-type: %s\r\n%s\r\n",
				 (req->attr & HTTP_1_0) ? '0' : '1', code, txt ? txt : "<NULL>",
				 content_type ? content_type : "text/plain",
				 headers ? headers : "");
	else
		snprintf(buf, sizeof(buf), "HTTP/1.%c %d %s\r\nContent-type: %s\r\nContent-length: %d\r\n%s\r\n",
				 (req->attr & HTTP_1_0) ? '0' : '1', code, txt ? txt : "<NULL>",
				 content_type ? content_type : "text/plain",
				 content_length,
				 headers ? headers : "");
	return http_send(conn, buf, strlen(buf));
}


void http_abort(http_connection_t *c) {
	if (c && c->s != INVALID_SOCKET) {
		if (c->flags & SRV_TLS)
			close_tls((socket_connection_t*) c);
		closesocket(c->s);
		c->s = -1; /* will cause the loop to break */
	}
}

/*--- The following makes the indenting behavior of emacs compatible
      with Xcode's 4/4 setting ---*/
/* Local Variables: */
/* indent-tabs-mode: t */
/* tab-width: 4 */
/* c-basic-offset: 4 */
/* End: */
