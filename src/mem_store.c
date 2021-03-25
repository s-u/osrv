/* Simple Fast Serialisation
   experimental serialiser focused on speed

   (C)2021 Simon Urbanek <simon.urbanek@R-project.org>

   License: MIT

   R-API: SEXP C_mem_store(SEXP sWhat, SEXP sVerb);
   
   Uses dynamic buffers to collect serialisation result.
*/

#include "sfs.h"

typedef struct lbuf {
    sfs_len_t len, cap;
    struct lbuf *next;
    char d[1];
} lbuf_t;

struct store_api {
    store_fn_t store;
    unsigned long cptr;
    lbuf_t *root, *tail;
    int verb;
};

/* -- buffer implementation + debugging -- */

static const char *type_name[] = {
  "NIL", "SYM", "LIST", "CLO", "ENV", "PROM", "LANG",
  "SPEC", "BLTIN", "CHAR", "LGL", "11", "12",
  "INT", "REAL", "CPLX", "STR", "DOT", "SNY", "VEC",
  "EXPR", "BCODE", "EXTPTR", "WEAKREF", "RAW", "S4",
  "26", "27", "28", "29",
  "NEW", "FREE",
  "32", "33", "34", "35", "36", "37", "38", "39", "40", "41", "42", "43",
  "44", "45", "46", "47", "48", "49", "50", "51", "52", "53", "54", "55",
  "56", "57", "58", "59", "60", "61", "62", "63", "64", "65", "66", "67",
  "68", "69", "70", "71", "72", "73", "74", "75", "76", "77", "78", "79",
  "80", "81", "82", "83", "84", "85", "86", "87", "88", "89", "90", "91",
  "92", "93", "94", "95", "96", "97", "98",
  "FUN",
  "100", "101", "102", "103", "104", "105", "106", "107", "108", "109",
  "110", "111", "112", "113", "114", "115", "116", "117", "118", "119",
  "120", "121", "122", "123", "124", "125", "126", "127", "128", "129",
  "130", "131", "132", "133", "134", "135", "136", "137", "138", "139",
  "140", "141", "142", "143", "144", "145", "146", "147", "148", "149",
  "150", "151", "152", "153", "154", "155", "156", "157", "158", "159",
  "160", "161", "162", "163", "164", "165", "166", "167", "168", "169",
  "170", "171", "172", "173", "174", "175", "176", "177", "178", "179",
  "180", "181", "182", "183", "184", "185", "186", "187", "188", "189",
  "190", "191", "192", "193", "194", "195", "196", "197", "198", "199",
  "200", "201", "202", "203", "204", "205", "206", "207", "208", "209",
  "210", "211", "212", "213", "214", "215", "216", "217", "218", "219",
  "220", "221", "222", "223", "224", "225", "226", "227", "228", "229",
  "230", "231", "232", "233", "234", "235", "236", "237", "238", "239",
  "240", "241", "242", "243", "244", "245", "246", "247", "248", "249",
  "250", "251", "252", "253", "254",
  "ATTR" };

static lbuf_t *alloc_buf(sfs_len_t size) {
    lbuf_t *b = (lbuf_t*) malloc(size + sizeof(lbuf_t));
    b->next = 0;
    b->len = 0;
    b->cap = size;
    return b;
}

#define DEF_BUF_SIZE (1024*1024*64)

static void buf_add(store_api_t *api, const void *what, sfs_len_t len) {
    if (api->tail->cap - api->tail->len < len) {
	sfs_len_t in0 = api->tail->cap - api->tail->len;
	if (in0) {
	    memcpy(api->tail->d + api->tail->len, what, in0);
	    api->tail->len += in0;
	}
	api->tail->next = alloc_buf((len > DEF_BUF_SIZE) ? (len + DEF_BUF_SIZE) : DEF_BUF_SIZE);
	api->tail = api->tail->next;
	memcpy(api->tail->d + api->tail->len, what + in0, len - in0);
	api->tail->len += len - in0;
    } else {
	memcpy(api->tail->d + api->tail->len, what, len);
	api->tail->len += len;
    }
}

static SEXP collapse_buf(lbuf_t *buf) {
    sfs_len_t total = 0;
    char *dst;
    SEXP res;
    lbuf_t *c = buf;
    while (c) {
	total += c->len;
	c = c->next;
    }
    c = buf;
    res = allocVector(RAWSXP, total);
    dst = (char*) RAW(res);
    while (c) {
	lbuf_t *nx = c->next;
	memcpy(dst, c->d, c->len);
	free(c);
	c = nx;
    }
    return res;
}

static void add_buf(store_api_t *api, sfs_ts ts, sfs_len_t el, sfs_len_t len, const void *buf) {
    sfs_len_t i = 0;
    sfs_len_t hdr = len;
    hdr <<= 8;
    hdr |= ts;
    buf_add(api, &hdr, sizeof(hdr));
    if (api->verb)
	Rprintf("%06lx: [%6s:%06lx/%02lu] ", api->cptr, type_name[ts], len, el);
    api->cptr += 8;
    if (el > 0)
	len *= el;
    if (api->verb) {
	if (buf)
	    while (i < len) {
		if (i > 16) {
		    Rprintf(" ...");
		    break;
		}
		Rprintf(" %02x", (int)((unsigned char*)buf)[i++]);
	    }
	if (ts == CHARSXP || ts == SYMSXP)
	    Rprintf(" (%s)", buf);
	Rprintf("\n");
    }
    if (buf)
	buf_add(api, buf, len);
    api->cptr += len;
}

SEXP C_mem_store(SEXP sWhat, SEXP sVerb) {
    SEXP res;
    store_api_t api;
    api.store = add_buf;
    api.root = api.tail = alloc_buf(DEF_BUF_SIZE);
    api.cptr = 0;
    api.verb = asInteger(sVerb);
    sfs_store(&api, sWhat);
    res = collapse_buf(api.root);
    return res;
}
