#include "sconn.h"

ssize_t socket_send(socket_connection_t *c, const void *buf, size_t len) {
    return (ssize_t) send(c->s, buf, len, 0);
}

ssize_t socket_recv(socket_connection_t *c, void *buf, size_t len) {
    return (ssize_t) recv(c->s, buf, len, 0);
}

