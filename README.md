[![RForge](https://rforge.net/do/versvg/osrv)](https://RForge.net/osrv)
[![tiff check](https://github.com/s-u/osrv/actions/workflows/check.yml/badge.svg)](https://github.com/s-u/osrv/actions/workflows/check.yml)

# Object Server and Simple Fast Serialisation

This packages provides two concepts: the ability to serve objects from an R process asynchronously using threads (no forking necessary) and a simple fast serialisation (SFS) format for transfer of objects between R processes.

## Asynchronous, Threaded TCP Object Server

The server allows the R session to register R objects under a name (key) which can be retrieved via TCP ansynchronously, i.e., while the R session is performing other tasks. The main intended use is to provide in-memory "hot" storage of objects that can be both computed on and sent to other computing nodes on demand.

The `start` function starts a TCP server on a separate thread from the R process. It then responds to any queries to fetch or remove objects from this session's object cache.

`put(key, value)` registers the object `value` to be served under the name `key`. Because the server is asynchronous, any removals of objects are not performed immediately, but instead R can issue `clean()` command to perform any removal of R objects that were requested by the clients in the meantime.

In addition to the server, the package also provides a simple client which can request objects from another R process via TCP using the `ask()` function. It is more efficient and faster than using R's socket functions.

## Simple Fast Serialisation

In order to avoid the necessity to store serialised form of objects which are already in the R session this package also introduces a Simple Fast Serialisation format. It is designed to be fast and streamable, i.e., it requires no memory buffer to serialise or unserialise (technically, it requires 8 bytes). The implementation supports direct (un-)serialisation to/from memory, file or socket. Currently, most but not all object types are supported.
