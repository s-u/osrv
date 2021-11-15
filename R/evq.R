evq.new <- function() .Call(C_evq_new)

evq.pop <- function(queue, timeout=0, reference=FALSE) {
  if (!is.finite(timeout)) timeout <- 31536000 ## use a year ... wonder if anyone will complain that it is finite ;)
  .Call(C_evq_pop, queue, timeout, reference)
}

evq.push <- function(C_evq_push, queue, what, front=FALSE) .Call(queue, what, front)


