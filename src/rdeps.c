#include "deps.h"

#include <Rinternals.h>
#include <string.h>

SEXP C_dep_req(SEXP sName, SEXP sKeys, SEXP sMsgID) {
    int n, i = 0, res;
    int slen = 0;
    SEXP sTmp;
    char *c;
    char **dst;
    if (TYPEOF(sName) != STRSXP || TYPEOF(sKeys) != STRSXP)
	Rf_error("name and keys must be character vectors");
    n = LENGTH(sKeys);
    while (i < n)
	slen += strlen(CHAR(STRING_ELT(sKeys, i++))) + 1;

    sTmp = PROTECT(Rf_allocVector(RAWSXP, (n * sizeof(char*)) + slen + 1));
    dst = (char**) RAW(sTmp);
    c = ((char*) RAW(sTmp)) + (n * sizeof(char*));
    i = 0;
    while (i < n) {
	const char *key = CHAR(STRING_ELT(sKeys, i));
	int l = strlen(key) + 1;
	dst[i++] = c;
	memcpy(c, key, l);
	c += l;
    }
    dep_init();
    res = deps_add(CHAR(STRING_ELT(sName, 0)), (const char **) dst, n, Rf_asInteger(sMsgID));
    UNPROTECT(1);
    return Rf_ScalarInteger(res);
}

/* revq.c */
SEXP evq2R(ev_queue_t *q);

SEXP C_dep_queue() {
    return evq2R(deps_queue());
}
