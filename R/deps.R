dep.request <- function(id, keys, msg=0x3a504544) .Call(C_dep_req, id, keys, msg)

dep.queue <- function() .Call(C_dep_queue)

