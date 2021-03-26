\name{sfs}
\alias{sfs}
\alias{SFS}
\alias{createSFS}
\alias{restoreSFS}
\alias{readSFS}
\alias{saveSFS}
\alias{statSFS}
\title{
  Simple Fast Serialisation Tools
}
\description{
  All functions here handle Simple Fast Serialisation (SFS) format. This
  format is optimised to be fast and streamable (no seeking needed). Its
  main purpose is to allow asynchronous in-flight serialisation of R
  objects for large-data applications.

  Serialisation (store) functions:

  \code{createSFS} stores into a raw vector.

  \code{saveSFS} stores into a file.

  \code{statSFS} performs the storage algorithm without creating any
  output bytes, but returns statistics on object counts and sizes.

  Un-serialization (restore) functions:

  \code{restoreSFS} restores from a raw vector.

  \code{readSFS} restores from a file.

  C-level code also supports store/restore from sockets.
}
\usage{
createSFS(object, debug = FALSE)
restoreSFS(data)

saveSFS(object, filename)
readSFS(filename)

statSFS(object)
}
\arguments{
  \item{object}{object to serialise}
  \item{debug}{if \code{TRUE} then the serialiser prints one line for
    each object that is serialised with information on size, type and
    up to first 15 bytes.}
  \item{data}{raw vector with the serialised SFS format}
  \item{filename}{string, filename (will be expanded)}
}
%\details{
%}
\value{
  \code{createSFS} returns a raw fector for the serialised SFS form.

  \code{restoreSFS} and \code{readSFS} return the un-serialised object.

  \code{saveSFS} has undefined return value (currently \code{NULL}).

  \code{statSFS} return a matrix with columns \code{"count"} and
  \code{"size"}. The rows repesent R types.
}
%\references{
%}
\author{
  Simon Urbanek
}
%\note{
%}
\examples{
x <- createSFS(iris)
str(x)
y <- restoreSFS(x)
str(y)
identical(y, iris)

tmp <- tempfile("test",, ".sfs")

saveSFS(function(x) x^2, tmp)
f <- readSFS(tmp)
f(1:4)
unlink(tmp)

str(createSFS(iris, TRUE))

statSFS(iris)
}
\keyword{manip}
