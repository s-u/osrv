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
       os.start())
assert("Store",
       o.put("t1", as.raw(1:10)))
assert("Store II",
       o.put("t2", as.raw(5:15)))
assert("Ask",
       os.ask("GET t1\n"), as.raw(1:10))
assert("Ask II",
       os.ask("GET t2\n"), as.raw(5:15))
assert("Delete",
       os.ask("DEL t1\n"), "OK")
assert("Ask deleted",
       os.ask("GET t1\n"), "NF")
assert("DEL regression",
       os.ask("GET t2\n"), as.raw(5:15))
assert("Unsupported",
       os.ask("FOO BAR\n"), "UNSUPP")
assert("PUT",
       os.ask("PUT t3\n3\n123"), "OK")
assert("Local get",
       o.get("t3"), charToRaw("123"))
assert("Local rm",
       o.get("t3", remove=TRUE), charToRaw("123"))
assert("Local rm",
       o.get("t3"), NULL)
assert("Clean",
       o.clean())

section("SFS")

assert("Mem store/restore",
{
    x <- createSFS(iris)
    restoreSFS(x)
}, iris)
assert("osrv SFS support",
{
    o.put("iris", x)
    os.ask("GET iris\n", sfs=TRUE)
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

assert("SFS with debugging info",
       createSFS(iris, TRUE), x)

unlink(tmp)

section("Object Server with SFS")

demo <- rbind(iris, iris)

assert("Store in server with SFS",
       o.put("demo", demo, sfs=TRUE))
assert("Retrieve with SFS",
       os.ask("GET demo\n", sfs=TRUE), demo)

assert("Clean up",
       os.ask("DEL demo\n") == "OK" && o.clean())

r <- createSFS(demo)
assert("Remote PUT with SFS",
       os.ask(c(charToRaw(
         paste0("PUT demo2\n", length(r), "\n")), r)), "OK")

assert("Local SFS get",
       o.get("demo2", sfs=TRUE), demo)

assert("Clean up",
       identical(o.get("demo2", sfs=TRUE, remove=TRUE), demo) && o.clean())

assert("Removal Check",
       o.get("demo2"), NULL)

section("Large data / memory management")

base.mem <- gc()[2,2]
cat("  Memory usage before:", base.mem, "Mb\n")
x <- rnorm(2e7)
store.mem <- gc()[2,2]
cat("  With rnorm(2e7)    :", store.mem, "Mb\n")
assert("Store ~152.6Mb",
       o.put("x", x, sfs=TRUE))
put.mem <- gc()[2,2]
cat("  After put          :", put.mem, "Mb\n")
assert("Check memory",
       put.mem - base.mem < 154)
assert("Retrieve",
       identical(os.ask("GET x\n", sfs=TRUE), x))
get.mem <- gc()[2,2]
cat("  After get          :", get.mem, "Mb\n")
rm(x)
assert("Clean up",
       os.ask("DEL x\n") == "OK" && o.clean())
clean.mem <- gc()[2,2]
cat("  After clean        :", clean.mem, "Mb\n")

assert("Check for memory leaks",
       clean.mem - base.mem < 1)

section("HTTP Server")

if (requireNamespace("httr", quietly=TRUE)) {

assert("Start http",
       os.start(port=8089, protocol="http"))

library(httr)

assert("PUT",
       status_code(PUT("http://127.0.0.1:8089/data/foo", body=charToRaw("bar"), encode="raw")),
       200L)

assert("GET", {
  r <- GET("http://127.0.0.1:8089/data/foo")
  identical(status_code(r), 200L) &&
  identical(content(r), charToRaw("bar")) })

assert("o.get", o.get("foo"), charToRaw("bar"))

assert("HEAD", {
  r <- GET("http://127.0.0.1:8089/data/foo")
  identical(status_code(r), 200L) &&
  identical(as.numeric(headers(r)$`content-length`), 3) })

assert("DELETE",
       status_code(DELETE("http://127.0.0.1:8089/data/foo")),
       200L)

assert("HEAD on non-existent",
       status_code(HEAD("http://127.0.0.1:8089/data/foo")),
       404L)

assert("GET on non-existent",
       status_code(GET("http://127.0.0.1:8089/data/foo")),
       404L)

assert("local get", o.get("foo"), NULL)

assert("local put", o.put("foo2", charToRaw("bar2")))

assert("GET on local put", {
  r <- GET("http://127.0.0.1:8089/data/foo2")
  identical(status_code(r), 200L) &&
  identical(content(r), charToRaw("bar2")) })

assert("local get + remove", o.get("foo2", remove=TRUE), charToRaw("bar2"))

} else {
  cat("WARNING: httr not found, cannot perfrom HTTP tests.\n\n")
}

cat("\nDONE\n\n")
