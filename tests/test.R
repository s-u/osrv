library(osrv)

section <- function(...) cat("\n===", ..., "===\n\n")
assert <- function(test, val, expected=TRUE) {
    txt <- deparse(code <- substitute(val))
    cat("-", test, "... ")
    if (!identical(val, expected)) {
        message("\n== TEST FAIL ==")
        cat("Expected:\n")
        str(expected)
        cat("Actual:\n")
        str(val)
        cat("Code:\n", txt, "\n")
        .expected <<- expected
        .value <<- val
        .code <<- code
        stop("Test FAILED")
    }
    cat("OK\n")
}

section("Object Server")

assert("Start service",
       start())
assert("Store",
       put("t1", as.raw(1:10)))
assert("Ask",
       ask("GET t1\n"), as.raw(1:10))
assert("Delete",
       ask("DEL t1\n"), "OK")
assert("Ask deleted",
       ask("GET t1\n"), "NF")
assert("Unsupported",
       ask("FOO BAR\n"), "UNSUPP")
assert("Clean",
       clean())

section("SFS")

assert("Mem store/restore",
{
    x <- createSFS(iris)
    restoreSFS(x)
}, iris)
assert("osrv SFS support",
{
    put("iris", x)
    ask("GET iris\n", sfs=TRUE)
}, iris)
assert("Closure serialisation",
{
    f <- restoreSFS(createSFS(function(x) x))
    f(iris)
}, iris)

tmp <- tempfile("test",, ".sfs")
assert("saveSFS",
{
    saveSFS(iris, tmp)
    readBin(tmp, raw(), length(x) * 2)
}, x)
assert("readSFS", readSFS(tmp), iris)

assert("statSFS", statSFS(iris),
       structure(c(5, 10, 2, 4, 4, 1, 2, 35, 104, 608, 4800, 0, 0, 0),
                 .Dim = c(7L, 2L),
                 .Dimnames = list(
                     c("SYM", "CHAR", "INT", "REAL", "STR", "VEC", "ATTR"),
                     c("count", "size")))

       )

unlink(tmp)

cat("\nDONE\n\n")
