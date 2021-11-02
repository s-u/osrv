os.start <- function(host=NULL, port=9012L, threads=4L)
    .Call(C_start, host, port, threads)

o.put <- function(key, value, sfs=FALSE)
    .Call(C_put, key, value, sfs)

o.get <- function(key, sfs=FALSE, remove=FALSE)
    .Call(C_get, key, sfs, remove)

o.clean <- function()
    .Call(C_clean)

os.ask <- function(cmd, host="127.0.0.1", port=9012L, sfs=FALSE)
    .Call(C_ask, host, port, cmd, sfs)
