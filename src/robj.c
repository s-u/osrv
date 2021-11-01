/* Interface from R to the object store */

#include "obj.h"

SEXP C_put(SEXP sKey, SEXP sWhat, SEXP sSFS) {
    int use_sfs = asInteger(sSFS);
    if (TYPEOF(sKey) != STRSXP || LENGTH(sKey) != 1)
	Rf_error("Invalid key, must be a string");
    if (!use_sfs && TYPEOF(sWhat) != RAWSXP)
	Rf_error("Value must be a raw vector unless SFS is used");
    obj_init();
    obj_add(CHAR(STRING_ELT(sKey, 0)), sWhat, use_sfs ? 0 : RAW(sWhat), use_sfs ? 0 : XLENGTH(sWhat));
    return ScalarLogical(1);
}

SEXP C_clean() {
    obj_init();
    obj_gc();
    return ScalarLogical(1);
}
