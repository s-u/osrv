#ifndef TLS_H__
#define TLS_H__

#include "sconn.h"

typedef struct tls tls_t;

/* this is a voluntary standart flag to request TLS support */
#define SRV_TLS       0x0800

/* these flags are global and respected by the default socket server */
#define SRV_IPV6      0x1000 /* use IPv6 */
#define SRV_LOCAL     0x4000 /* bind to local loopback interface only */
#define SRV_KEEPALIVE 0x8000 /* enable keep-alive - note that this is really
				a client option sice inheritance is not
				guaranteed */

/* for set_tls_verify() */
#define TLS_NONE      0 /* default */
#define TLS_REQUIRE   1

/* in case shared tls is not set, it will be set to new_tls
   (which can be NULL) */
tls_t *shared_tls(tls_t *new_tls);

tls_t *new_tls();
int set_tls_pk(tls_t *tls, const char *fn);
int set_tls_cert(tls_t *tls, const char *fn);
int set_tls_ca(tls_t *tls, const char *fn_ca, const char *path_ca);
int set_tls_verify(tls_t *tls, int verify);
void free_tls(tls_t *tls);

int add_tls(socket_connection_t *c, tls_t *tls, int server);
void copy_tls(socket_connection_t *src, socket_connection_t *dst);
void close_tls(socket_connection_t *c);
int verify_peer_tls(socket_connection_t *c, char *cn, int len);

#endif
