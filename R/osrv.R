start <- function(host=NULL, port=9012L, threads=4L)
    .Call(C_start, host, port, threads)

put <- function(key, value, sfs=FALSE)
    .Call(C_put, key, value, sfs)

clean <- function()
    .Call(C_clean)

ask <- function(cmd, host="127.0.0.1", port=9012L, sfs=FALSE)
    .Call(C_ask, host, port, cmd, sfs)
