#ifndef SCONN_H__
#define SCONN_H__

#include <stddef.h>
#include <sys/types.h>
#include <sys/socket.h>

#define SOCKET int
#define closesocket(X) close(X)
#define INVALID_SOCKET ((SOCKET) -1)

typedef struct socket_connection socket_connection_t;

typedef ssize_t (*recv_fn_t) (socket_connection_t *arg, void *buf, size_t len);
typedef ssize_t (*send_fn_t) (socket_connection_t *arg, const void *buf, size_t len);

struct socket_connection {
    SOCKET s;
    recv_fn_t recv;
    send_fn_t send;
    void *res;
};

/* default send/recv for sockets */
ssize_t socket_send(socket_connection_t *c, const void *buf, size_t len);
ssize_t socket_recv(socket_connection_t *c, void *buf, size_t len);

#endif
