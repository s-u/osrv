start <- function(host=NULL, port=9012L, threads=4L)
    .Call(C_start, host, port, threads)

put <- function(key, value)
    .Call(C_put, key, value)

clean <- function()
    .Call(C_clean)

ask <- function(cmd, host="127.0.0.1", port=9012L)
    .Call(C_ask, host, port, cmd)
