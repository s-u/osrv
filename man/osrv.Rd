\name{osrv}
\alias{osrv}
\alias{start}
\alias{put}
\alias{clean}
\alias{ask}
\title{
  Manage Object Server
}
\description{
  \code{start} starts the threaded TCP object server.

  \code{put} puts objects into the object store that will be served to
  clients connecting via TCP.

  \code{clean} does the equivalent of a garbage collection on any
  objects that were released by the serving threads.

  \code{ask} is a rudimentary object server client that sends a command,
  awaits a response and closes the connection.
}
\usage{
start(host = NULL, port = 9012L, threads = 4L)
put(key, value, sfs = FALSE)
clean()

ask(cmd, host = "127.0.0.1", port = 9012L, sfs = FALSE)
}
\arguments{
  \item{host}{string or \code{NULL}, IP address or host name of the
    interface to bind to. If set to \code{NULL} then all interfaces are
    bound.}
  \item{port}{integer, TCP port number to bind to}
  \item{threads}{integer, number of worker threads to start}
  \item{key}{string, key to use for retrieval}
  \item{value}{payload to serve. If \code{sfs=FALSE} then it must be a
    raw vector.}
  \item{cmd}{string, command to send}
  \item{sfs}{if \code{TRUE} then SFS serialisation on-the-fly is used
    (see details)}
}
\details{
  The current implementation allows only one server in the process at a
  time so \code{start()} can be used only once. All threads are started
  as detached, so they are not expected to join when finished. Requests
  are served by worker threads. \code{start} returns immediately after
  the socket is successfully bound and connections are accepted on a
  separate thread.

  If \code{sfs=TRUE} then SFS serialisation is used. For \code{put()}
  this means that objects other than raw vectors can be served and the
  object is serialised when retrieved on the fly. For \code{ask()} it
  means the payload is expected to the SFS-serialised and will be
  unserialised automatically (again, without any extra memory usage).
  Note that only "safe" native R objects or ALTREP objects with
  thread-safe implementation of \code{const DATAPTR()} are supported.
}
\value{
  \code{TRUE} on success and \code{FALSE} on failure.

  \code{ask} returns either the status as a string for commands that do
  not return payload (typically \code{"OK"} or \code{"NF"}) or the
  payload - which is eaither a raw vector (\code{sfs=FALSE}) or the
  unserialised R object (\code{sfs=TRUE}).
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

 (r <- ask("GET foo\n"))
 rawToChar(r)

 ## delete "foo"
 ask("DEL foo\n")

 ## it should be removed now so NF (not found)
 ask("GET foo\n")

 ## SFS
 put("iris", iris, sfs=TRUE)

 ## check if it's the same
 identical(ask("GET iris\n", sfs=TRUE), iris)
}
\keyword{system}
