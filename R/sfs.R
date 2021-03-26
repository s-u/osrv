createSFS <- function(object, debug=FALSE)
    .Call(C_mem_store, object, debug)

restoreSFS <- function(data)
    .Call(C_mem_restore, data)

statSFS <- function(object) {
    m <- .Call(C_stat_store, object)
    colnames(m) <-  c("count", "size")
    q  <-  1:256
    q[1:26] <- c(
        "NIL", "SYM", "LIST", "CLO", "ENV", "PROM", "LANG",
        "SPEC", "BLTIN", "CHAR", "LGL", "11", "12",
        "INT", "REAL", "CPLX", "STR", "DOT", "ANY", "VEC",
        "EXPR", "BCODE", "EXTPTR", "WEAKREF", "RAW", "S4")
    q[100] <- "FUN"
    q[256] <- "ATTR"
    colnames(m) <- c("count", "size")
    rownames(m) <- q
    m[m[,1] > 0,]
}

readSFS <- function(filename)
    .Call(C_file_restore, path.expand(filename))

saveSFS <- function(object, filename)
    invisible(.Call(C_file_store, object, path.expand(filename)))
