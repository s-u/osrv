start <- function(host=NULL, port=9012L, threads=4L)
    .Call(C_start, host, port, threads)

put <- function(key, value)
    .Call(C_put, key, value)

clean <- function()
    .Call(C_clean)
