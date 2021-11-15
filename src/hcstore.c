/* SFS store into a HTTP chunked-encoding connection */

#include "sfs.h"
#include "http.h"

/* size of the buffer to keep for accumulation */
#define MAX_CHUNK_SIZE (1024*1024*16)
/* this must be at most half of the above */
/* objects >= this size will be sent directly instead of buffering */
#define MIN_INPLACE_SIZE (1024*1024*2)

struct store_api {
    store_fn_t store;
    http_connection_t *conn;
    int res;
    size_t len, pos;
    char buf[1];
};

static void buf_add(store_api_t *api, const void *what, sfs_len_t len) {
    if (api->res) /* if there was any error earlier, don't bother */
	return;
    /* too big to fit -- and either buffer or payload is big enough
       flush the buffeer */
    if (len > (api->len - api->pos) &&
	(api->pos >= MIN_INPLACE_SIZE || len >= MIN_INPLACE_SIZE)) {
	if ((api->res = http_send_chunk(api->conn, api->buf, api->pos)))
	    return;
	api->pos = 0;
    }
    /* nothing in the buffer and len big enough - send directly */
    if (!api->pos && len >= MIN_INPLACE_SIZE) {
	api->res = http_send_chunk(api->conn, what, len);
	return; /* we're done */
    }
    /* fill the buffer */
    /* NOTE: now len must fit in the buffer, because if it didn't
       we would have sent either it or the buffer (or both) already */
    memcpy(api->buf + api->pos, what, len);
    api->pos += len;
}

static void add(store_api_t *api, sfs_ts ts, sfs_len_t el, sfs_len_t len, const void *buf) {
    sfs_len_t hdr = len;
    hdr <<= 8;
    hdr |= ts;
    buf_add(api, &hdr, sizeof(hdr));
    len *= el;
    if (buf)
	buf_add(api, buf, len);
}

int http_store(http_connection_t *conn, SEXP sWhat) {
    store_api_t *api = (store_api_t*) malloc(sizeof(store_api_t) + MAX_CHUNK_SIZE);
    int res;
    if (!api) return -1;

    api->store = add;
    api->conn = conn;
    api->res = 0;
    sfs_store(api, sWhat);
    /* send last non-zero chunk */
    if (!api->res && api->pos)
	api->res = http_send_chunk(conn, api->buf, api->pos);
    res = api->res;
    /* send final empty chunk */
    if (!res)
	res = http_send_chunk(conn, 0, 0);
    free(api);
    return res;
}
