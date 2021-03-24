/* Simple Fast Serialisation
   experimental serialiser focues on speed */

#include <string.h>
#include <stdlib.h>

#include <Rinternals.h>

typedef unsigned long sfs_len_t;
typedef unsigned char sfs_ts;

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

static unsigned long cptr = 0;

typedef struct lbuf {
    sfs_len_t len, cap;
    struct lbuf *next;
    char d[1];
} lbuf_t;

static lbuf_t *alloc_buf(sfs_len_t size) {
    lbuf_t *b = (lbuf_t*) malloc(size + sizeof(lbuf_t));
    b->next = 0;
    b->len = 0;
    b->cap = size;
    return b;
}

#define DEF_BUF_SIZE (1024*1024*64)

static lbuf_t *root = 0, *tail = 0;

static void buf_add(const void *what, sfs_len_t len) {
    if (tail->cap - tail->len < len) {
	sfs_len_t in0 = tail->cap - tail->len;
	if (in0) {
	    memcpy(tail->d + tail->len, what, in0);
	    tail->len += in0;
	}
	tail->next = alloc_buf((len > DEF_BUF_SIZE) ? (len + DEF_BUF_SIZE) : DEF_BUF_SIZE);
	tail = tail->next;
	memcpy(tail->d + tail->len, what + in0, len - in0);
	tail->len += len - in0;
    } else {
	memcpy(tail->d + tail->len, what, len);
	tail->len += len;
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

static void add(sfs_ts ts, sfs_len_t el, sfs_len_t len, const void *buf) {
    sfs_len_t i = 0;
    sfs_len_t hdr = len;
    hdr <<= 8;
    hdr |= ts;
    printf("%06lx: [%6s:%06lx/%02lu] ", cptr, type_name[ts], len, el);
    buf_add(&hdr, sizeof(hdr));
    cptr += 8;
    if (el > 0)
	len *= el;
    if (buf)
	while (i < len) {
	    if (i > 16) {
		printf(" ...");
		break;
	    }
	    printf(" %02x", (int)((unsigned char*)buf)[i++]);
	}
    printf("\n");
    if (buf)
	buf_add(buf, len);
    cptr += len;
}

static void store(SEXP sWhat) {
    /* store attributes first if present */
    switch (TYPEOF(sWhat)) {
    case LGLSXP:
    case INTSXP:
    case REALSXP:
    case CPLXSXP:
    case STRSXP:
    case VECSXP:
    case RAWSXP:
    case S4SXP:
	if (ATTRIB(sWhat) != R_NilValue) {
	    sfs_len_t l = 0;
	    SEXP x = ATTRIB(sWhat);
	    while (x != R_NilValue) {
		x = CDR(x);
		l++;
	    }
	    add(255, 0, l, 0);
	    x = ATTRIB(sWhat);
	    while (x != R_NilValue) {
		store(TAG(x));
		store(CAR(x));
		x = CDR(x);
	    }
	}
    }

    /* then the object itself */
    switch (TYPEOF(sWhat)) {
    case INTSXP:
    case LGLSXP:
	add(TYPEOF(sWhat), 4, XLENGTH(sWhat), INTEGER(sWhat));
	break;
    case REALSXP:
	add(TYPEOF(sWhat), 8, XLENGTH(sWhat), REAL(sWhat));
	break;
    case CPLXSXP:
	add(TYPEOF(sWhat), 16, XLENGTH(sWhat), COMPLEX(sWhat));
	break;
    case VECSXP:
	{
	    sfs_len_t i = 0 , n = XLENGTH(sWhat);
	    add(TYPEOF(sWhat), 0, n, 0);
	    while (i < n) {
		store(VECTOR_ELT(sWhat, i));
		i++;
	    }
	    break;
	}
    case STRSXP:
	{
	    sfs_len_t i = 0 , n = XLENGTH(sWhat);
	    add(TYPEOF(sWhat), 0, n, 0);
	    while (i < n) {
		const char *c = CHAR(STRING_ELT(sWhat, i));
		add(CHARSXP, 0, strlen(c) + 1, c);
		i++;
	    }
	    break;
	    
	}
    case NILSXP:
	add(TYPEOF(sWhat), 0, 0, 0);
	break;
    case RAWSXP:
	add(TYPEOF(sWhat), 1, XLENGTH(sWhat), RAW(sWhat));
	break;
    case SYMSXP:
	{
	    const char *p = CHAR(PRINTNAME(sWhat));
	    add(TYPEOF(sWhat), 0, strlen(p) + 1, p);
	    break;
	}
    case LISTSXP:
    case LANGSXP:
	{
	    sfs_len_t l = 0;
	    SEXP x = sWhat;
	    while (x != R_NilValue) {
		x = CDR(x);
		l++;
	    }
	    add(TYPEOF(sWhat), 0, l, 0);
	    x = sWhat;
	    while (x != R_NilValue) {
		store(TAG(x));
		store(CAR(x));
		x = CDR(x);
	    }
	    break;
	}
    default:
	add(TYPEOF(sWhat), 0, 0, 0);
    }
}

static char *fbuf;
static sfs_len_t flen;

static void fetch(void *buf, sfs_len_t len) {
    if (flen < len)
	Rf_error("Read error: need %lu, got %lu\n", len, flen);
    memcpy(buf, fbuf, len);
    fbuf += len;
    flen -= len;
}

static SEXP decode();

static char dec_buf[1024]; /* static scrarch buffer for decoding symbols */

static SEXP decode_one(sfs_len_t hdr) {
    sfs_len_t len;
    sfs_ts ts;
    SEXP res = R_NilValue;
    len = hdr;
    ts = (unsigned char)(len & 255);
    len >>= 8;
    Rprintf("[%s:%04lx]\n", type_name[ts], len);
    switch (ts) {
    case INTSXP:
    case LGLSXP:
	res = allocVector(ts, len);
	fetch(INTEGER(res), len * 4);
	break;
    case REALSXP:
	res = allocVector(ts, len);
	fetch(REAL(res), len * 8);
	break;
    case CPLXSXP:
	res = allocVector(ts, len);
	fetch(COMPLEX(res), len * 16);
	break;
    case SYMSXP:
	{
	    if (len < sizeof(dec_buf)) {
		fetch(dec_buf, len);
		res = Rf_install(dec_buf);
	    } else {
		char *buf = (char*) malloc(len);
		if (!buf)
		    Rf_error("Cannot allocate memory for symbol (%lu bytes)", len);
		res = Rf_install(buf);
		free(buf);
	    }
	    break;
	}
    case VECSXP:
	{
	    sfs_len_t i = 0;
	    res = PROTECT(allocVector(ts, len));
	    while (i < len) {
		SET_VECTOR_ELT(res, i, decode());
		i++;
	    }
	    UNPROTECT(1);
	    break;
	}
    case STRSXP:
	{
	    sfs_len_t i = 0;
	    res = PROTECT(allocVector(ts, len));
	    while (i < len) {
		SET_STRING_ELT(res, i, decode());
		i++;
	    }
	    UNPROTECT(1);
	    break;
	}
    case CHARSXP:
	{
	    if (len < sizeof(dec_buf)) {
		fetch(dec_buf, len);
		res = mkChar(dec_buf);
	    } else {
		char *buf = (char*) malloc(len);
		if (!buf)
		    Rf_error("Cannot allocate memory for string (%lu bytes)", len);
		res = mkChar(buf);
		free(buf);
	    }
	    break;
	}
	
    case 255:
    case LISTSXP:
    case LANGSXP:
	{
	    sfs_len_t i = 0;
	    SEXP at = R_NilValue;
	    res = R_NilValue;
	    while (i < len) {
		SEXP tag = PROTECT(decode());
		SEXP val = PROTECT(decode());
		SEXP x = PROTECT((ts == LANGSXP) ?
				 LCONS(val, R_NilValue) :
				 CONS(val, R_NilValue));
		if (tag != R_NilValue)
		    SET_TAG(x, tag);
		if (at == R_NilValue) {
		    res = at = x;
		} else {
		    SETCDR(at, x);
		    UNPROTECT(3);
		    at = x;
		}
		i++;
	    }
	    if (i > 0)
		UNPROTECT(3);
	    break;
	}
    default:
	Rf_error("Unimplemented de-serialisation for %s (%d)", type_name[ts], (int)ts);
    }
    return res;
}

static SEXP decode() {
    sfs_len_t hdr;
    SEXP res, attr = R_NilValue;
    fetch(&hdr, sizeof(hdr));
    if ((hdr & 255) == 255) {
	attr = decode_one(hdr);
	if (attr != R_NilValue)
	    PROTECT(attr);
	fetch(&hdr, sizeof(hdr));
    }
    res = decode_one(hdr);
    if (attr != R_NilValue) {
	PROTECT(res);
	SET_ATTRIB(res, attr);
	UNPROTECT(2);
    }
    return res;
}

SEXP C_store(SEXP sWhat) {
    SEXP res;
    root = tail = alloc_buf(DEF_BUF_SIZE);
    cptr = 0;
    store(sWhat);
    res = collapse_buf(root);
    tail = root = 0;
    return res;
}

SEXP C_restore(SEXP sWhat) {
    SEXP res;
    fbuf = (char*) RAW(sWhat);
    flen = XLENGTH(sWhat);
    return decode();
}
