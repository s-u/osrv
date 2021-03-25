/* Simple Fast Serialisation
   experimental serialiser focused on speed

   (C)2021 Simon Urbanek <simon.urbanek@R-project.org>

   License: MIT
 */

#include "sfs.h"

/* this is a "virtual" type, not defined by R, but used
   in our protocol to denote that an object has attributes
   and they are stored first */
#define ATTRSXP 255

struct store_api {
    store_fn_t store;
    /* implementations can add anything here... */
};

struct fetch_api {
    fetch_fn_t fetch;
    /* implementations can add anything here... */
};

static void store(store_api_t *api, SEXP sWhat) {
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
	    api->store(api, ATTRSXP, 0, l, 0);
	    x = ATTRIB(sWhat);
	    while (x != R_NilValue) {
		store(api, TAG(x));
		store(api, CAR(x));
		x = CDR(x);
	    }
	}
    }

    /* then the object itself */
    switch (TYPEOF(sWhat)) {
    case INTSXP:
    case LGLSXP:
	api->store(api, TYPEOF(sWhat), 4, XLENGTH(sWhat), INTEGER(sWhat));
	break;
    case REALSXP:
	api->store(api, TYPEOF(sWhat), 8, XLENGTH(sWhat), REAL(sWhat));
	break;
    case CPLXSXP:
	api->store(api, TYPEOF(sWhat), 16, XLENGTH(sWhat), COMPLEX(sWhat));
	break;
    case VECSXP:
	{
	    sfs_len_t i = 0 , n = XLENGTH(sWhat);
	    api->store(api, TYPEOF(sWhat), 0, n, 0);
	    while (i < n) {
		store(api, VECTOR_ELT(sWhat, i));
		i++;
	    }
	    break;
	}
    case STRSXP:
	{
	    sfs_len_t i = 0 , n = XLENGTH(sWhat);
	    api->store(api, TYPEOF(sWhat), 0, n, 0);
	    while (i < n) {
		const char *c = CHAR(STRING_ELT(sWhat, i));
		api->store(api, CHARSXP, 0, strlen(c) + 1, c);
		i++;
	    }
	    break;
	    
	}
    case NILSXP:
	api->store(api, TYPEOF(sWhat), 0, 0, 0);
	break;
    case RAWSXP:
	api->store(api, TYPEOF(sWhat), 1, XLENGTH(sWhat), RAW(sWhat));
	break;
    case SYMSXP:
	{
	    const char *p = CHAR(PRINTNAME(sWhat));
	    api->store(api, TYPEOF(sWhat), 0, *p ? strlen(p) + 1 : 0, p);
	    break;
	}
    case CLOSXP:
	api->store(api, TYPEOF(sWhat), 3, 0, 0);
	store(api, FORMALS(sWhat));
	/* FIXME: until we know how to handle byte code
	   we store the expression, not the byte code */
	if (TYPEOF(BODY(sWhat)) == BCODESXP)
	    store(api, BODY_EXPR(sWhat));
	else
	    store(api, BODY(sWhat));
	store(api, CLOENV(sWhat));
	break;
	
	/*case BCODESXP:
	  FIXME: how to handle byte code? */
	
    case LISTSXP:
    case LANGSXP:
	{
	    sfs_len_t l = 0;
	    SEXP x = sWhat;
	    while (x != R_NilValue) {
		x = CDR(x);
		l++;
	    }
	    api->store(api, TYPEOF(sWhat), 0, l, 0);
	    x = sWhat;
	    while (x != R_NilValue) {
		store(api, TAG(x));
		store(api, CAR(x));
		x = CDR(x);
	    }
	    break;
	}
    default:
	api->store(api, TYPEOF(sWhat), 0, 0, 0);
    }
}

/* static scratch buffer for decoding symbols and strings */
static char dec_buf[8192];

static SEXP decode_one(fetch_api_t *api, sfs_len_t hdr) {
    sfs_len_t len;
    sfs_ts ts;
    SEXP res = R_NilValue;
    len = hdr;
    ts = (unsigned char)(len & 255);
    len >>= 8;
    switch (ts) {
    case NILSXP:
	return R_NilValue;
    case INTSXP:
    case LGLSXP:
	res = allocVector(ts, len);
	api->fetch(api, INTEGER(res), len * 4);
	break;
    case REALSXP:
	res = allocVector(ts, len);
	api->fetch(api, REAL(res), len * 8);
	break;
    case CPLXSXP:
	res = allocVector(ts, len);
	api->fetch(api, COMPLEX(res), len * 16);
	break;
    case SYMSXP:
	{
	    if (len == 0) {
		res = R_MissingArg;
		break;
	    }
	    if (len < sizeof(dec_buf)) {
		api->fetch(api, dec_buf, len);
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
		SET_VECTOR_ELT(res, i, sfs_load(api));
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
		SET_STRING_ELT(res, i, sfs_load(api));
		i++;
	    }
	    UNPROTECT(1);
	    break;
	}
    case CHARSXP:
	{
	    if (len < sizeof(dec_buf)) {
		api->fetch(api, dec_buf, len);
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

    case CLOSXP:
	{
	    SEXP v;
	    res = PROTECT(allocSExp(CLOSXP));
	    v = sfs_load(api);
	    if (v != R_NilValue)
		SET_FORMALS(res, v);
	    v = sfs_load(api);
	    if (v != R_NilValue)
		SET_BODY(res, v);
	    v = sfs_load(api);
	    if (v != R_NilValue)
		SET_CLOENV(res, v);
	    UNPROTECT(1);
	    break;
	}

    case ATTRSXP:
    case LISTSXP:
    case LANGSXP:
	{
	    sfs_len_t i = 0;
	    SEXP at = R_NilValue;
	    res = R_NilValue;
	    while (i < len) {
		SEXP tag = PROTECT(sfs_load(api));
		SEXP val = PROTECT(sfs_load(api));
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
    case ENVSXP:
	Rf_warning("Environments are not serialized.");
	break;
    default:
	Rf_error("Unimplemented de-serialisation for type %d", (int)ts);
    }
    return res;
}

/* API entry for store */
void sfs_store(store_api_t *api, SEXP sWhat) {
    store(api, sWhat);
}

/* API entry for load */
SEXP sfs_load(fetch_api_t *api) {
    sfs_len_t hdr;
    SEXP res, attr = R_NilValue;
    api->fetch(api, &hdr, sizeof(hdr));
    if ((hdr & 255) == ATTRSXP) {
	attr = decode_one(api, hdr);
	if (attr != R_NilValue)
	    PROTECT(attr);
	api->fetch(api, &hdr, sizeof(hdr));
    }
    res = decode_one(api, hdr);
    if (attr != R_NilValue) {
	SEXP c = attr;
	/* check for the "class" attribute
	   so we can flag the object */
	while (c != R_NilValue) {
	    if (TAG(c) == R_ClassSymbol) {
		SET_OBJECT(res, 1);
		break;
	    }
	    c = CDR(c);
	}
	PROTECT(res);
	SET_ATTRIB(res, attr);
	UNPROTECT(2);
    }
    return res;
}
