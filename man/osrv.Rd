\name{osrv}
\alias{osrv}
\alias{start}
\alias{put}
\alias{clean}
\title{
  Manage Object Server
}
\description{
  \code{start} starts the threaded TCP object server.

  \code{put} puts objects into the object store that will be served to
  clients connecting via TCP.

  \code{clean} does the equivalent of a garbage collection on any
  objects that were released by the serving threads.
}
\usage{
start(host = NULL, port = 9012L, threads = 4L)
put(key, value)
clean()
}
\arguments{
  \item{host}{string or \code{NULL}, IP address or host name of the
    interface to bind to. If set to \code{NULL} then all interfaces are bound.}
  \item{port}{integer, TCP port number to bind to}
  \item{threads}{integer, number of worker threads to start}
  \item{key}{string, key to use for retrieval}
  \item{value}{raw vector, payload to serve}
}
\details{
  The current implementation allows only one server in the process at a
  time so \code{start()} can be used only once. All threads are started
  as detached, so they are not expected to join when finished. Requests
  are served by worker threads. \code{start} returns immediately after
  the socket is successfully bound and connections are accepted on a
  separate thread.
}
\value{
  \code{TRUE} on success and \code{FALSE} on failure.
}
%\references{
%}
\author{
  Simon Urbanek
}
\examples{
 ## start the TCP server
 start()
 ## put an object under the key "foo"
 put("foo", charToRaw("bar"))

 s <- socketConnection("127.0.0.1", port=9012, open="a+b")

 ## very rudimentary client using sockets in R
 cmd <- function(s, cmd) {
   writeBin(charToRaw(cmd), s)
   while (!length(a <- readBin(s, raw(), 32))) {}
   i <- which(a == as.raw(10))
   if (!length(i)) stop("Invalid answer")
   res <- gsub("[\r\n]+","",rawToChar(a[1:i[1]]))
   sr <- strsplit(res, " ", TRUE)[[1]]
   ## object found
   if (sr[1] == "OK" && length(sr) > 1) {
     len <- as.numeric(sr[2])
     p <- if (i[1] < length(a)) a[-(1:i[1])] else raw()
     ## read the rest of the object
     while (length(p) < len)
       p <- c(p, readBin(s, raw(), len - length(p)))
     p
   } else if (sr[1] == "OK") {
     TRUE
   } else stop("Answer: ", sr[1])
 }

 ## retrieve "foo"
 v <- cmd(s, "GET foo\n")
 print(rawToChar(v))

 ## delete "foo"
 cmd(s, "DEL foo\n")

 ## it should be removed now so NF (not found)
 print(tryCatch(cmd(s, "GET foo\n"),
   error=function(e) e))
}
\keyword{system}
