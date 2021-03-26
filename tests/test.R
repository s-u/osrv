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

section("Object Server with SFS")

demo <- rbind(iris, iris)

assert("Store in server with SFS",
       put("demo", demo, sfs=TRUE))
assert("Retrieve with SFS",
       ask("GET demo\n", sfs=TRUE), demo)

assert("Clean up",
       ask("DEL demo\n") == "OK" && clean())

section("Large data / memory management")

base.mem <- gc()[2,2]
cat("  Memory usage before:", base.mem, "Mb\n")
x <- rnorm(2e7)
store.mem <- gc()[2,2]
cat("  With rnorm(2e7)    :", store.mem, "Mb\n")
assert("Store ~152.6Mb",
       put("x", x, sfs=TRUE))
put.mem <- gc()[2,2]
cat("  After put          :", put.mem, "Mb\n")
assert("Check memory",
       put.mem - base.mem < 154)
assert("Retrieve",
       identical(ask("GET x\n", sfs=TRUE), x))
get.mem <- gc()[2,2]
cat("  After get          :", get.mem, "Mb\n")
rm(x)
assert("Clean up",
       ask("DEL x\n") == "OK" && clean())
clean.mem <- gc()[2,2]
cat("  After clean        :", clean.mem, "Mb\n")

assert("Check for memory leaks",
       clean.mem - base.mem < 1)

cat("\nDONE\n\n")
