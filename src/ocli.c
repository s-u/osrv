/* Rudimentary osrv TCP client
   
SEXP C_ask(SEXP sHost, SEXP sPort, SEXP sCmd, SEXP sSFS);

 */

#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>

#include <Rinternals.h>

#define SOCKET int
#define closesocket(X) close(X)
#define FETCH_SIZE (512*1024)

/* from sock_restore.c */
SEXP sock_restore(int s, int need_opts);

SEXP C_ask(SEXP sHost, SEXP sPort, SEXP sCmd, SEXP sSFS) {
    SOCKET ss;
    int n, l, i = 1, port = asInteger(sPort), use_sfs = asInteger(sSFS);
    const char *host = 0;
    struct sockaddr_in sin;
    struct hostent *haddr;
    struct timeval tv;

    if (TYPEOF(sHost) != STRSXP || LENGTH(sHost) != 1)
	Rf_error("host must be a string");
    if (TYPEOF(sCmd) != RAWSXP &&
	(TYPEOF(sCmd) != STRSXP || LENGTH(sCmd) != 1))
	Rf_error("command must be string or a raw vector");
    host = CHAR(STRING_ELT(sHost, 0));
    if (port < 0 || port > 65535)
	Rf_error("invalid port");

    ss = socket(AF_INET, SOCK_STREAM, 0);
    if (ss == -1)
	Rf_error("Cannot obtain a socket %s", errno ? strerror(errno) : "");

    sin.sin_family = AF_INET;
    sin.sin_port = htons(port);
    if (host) {
        if (inet_pton(sin.sin_family, host, &sin.sin_addr) != 1) { /* invalid, try DNS */
            if (!(haddr = gethostbyname(host))) { /* DNS failed, */
                closesocket(ss);
                ss = -1;
		Rf_error("Cannot resolve host '%s'", host);
            }
            sin.sin_addr.s_addr = *((uint32_t*) haddr->h_addr); /* pick first address */
        }
    } else
        sin.sin_addr.s_addr = htonl(INADDR_ANY);

    if (connect(ss, (struct sockaddr*)&sin, sizeof(sin)))
	Rf_error("Unable to connect to %s:%d %s", host, port, errno ? strerror(errno) : "");

    /* enable TCP_NODELAY */
    setsockopt(ss, IPPROTO_TCP, TCP_NODELAY, (const void*) &i, sizeof(i));

    /* enable timeout so we can support R-level interrupts */
    tv.tv_sec = 1;
    tv.tv_usec = 0;
    setsockopt(ss, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);

    if (TYPEOF(sCmd) == RAWSXP)
	n = send(ss, RAW(sCmd), l = LENGTH(sCmd), 0);
    else {
	const char *cmd = CHAR(STRING_ELT(sCmd, 0));
	l = strlen(cmd);
	n = send(ss, cmd, l, 0);
    }

    if (n < l) {
	closesocket(ss);
	if (n < 0)
	    Rf_error("Error sending command %s", errno ? strerror(errno) : "");
	Rf_error("Error sending command, could only send %d of %d bytes", n, l);
    }

    {
	char buf[64], *c = 0;
	int p = 0;
	while (1) {
	    int need;
	    buf[p] = 0;
	    if ((c = strchr(buf, '\n'))) {
		*c = 0;
		break;
	    }
	    if (p > sizeof(buf) - 8) {
		closesocket(ss);
		Rf_error("Invalid response: %s", buf);
	    }
	    need = sizeof(buf) - 1 - p;
	    if (use_sfs) { /* we can't allow overflow, so fetch length by one */
		/* valid response must have at least 5 bytes: 'O' 'K' ' ' [0-9] '\n' */
		need = (p < 5) ? (5  - p) : 1;
	    }
	    n = recv(ss, buf + p, need, 0);
	    /* Rprintf("recv(@%d,%d)=%d\n", p, sizeof(buf) - 1 - p, n); */
	    if (n <= 0) {
		/* we have set timeout as to allow interrupts, HOWEVER,
		   the socket will NOT be closed after an interrupt! */
		if (errno == EAGAIN || errno == EWOULDBLOCK) {
		    R_CheckUserInterrupt();
		    continue;
		}
		closesocket(ss);
		if (n == 0) {
		    Rf_error("Connection closed unexpectedly");
		}
		Rf_error("Error while receiving response %s", errno ? strerror(errno) : "");
	    }
	    p += n;
	}
	if (strlen(buf) == 2 || !strcmp(buf, "UNSUPP")) { /* OK/NF alone */
	    closesocket(ss);
	    return mkString(buf);
	}
	if (!strncmp(buf, "OK ", 3)) { /* OK + length */
	    SEXP res;
	    char *dst;
	    unsigned long ii = 0, pl;
	    if (use_sfs) {
		res = sock_restore(ss, 0);
		closesocket(ss);
		return res;
	    }
	    pl = (unsigned long) atol(buf + 3);
	    res = allocVector(RAWSXP, pl);
	    dst = (char*) RAW(res);
	    p -= (c - buf) + 1; /* eat the header */
	    if (p > 0) {
		if (p > pl) /* can't exceed payload */
		    p = pl;
		memcpy(dst, c + 1, p);
		ii += p;
	    }
	    while (ii < pl) {
		unsigned long rq = (pl - ii < FETCH_SIZE) ? pl - ii : FETCH_SIZE;
		n = recv(ss, dst + ii, rq, 0);
		if (n < 0) {
		    if ((errno == EAGAIN || errno == EWOULDBLOCK)) {
			R_CheckUserInterrupt();
			continue;
		    }
		    closesocket(ss);
		    Rf_error("Error while reading, got %lu of %lu bytes %s", ii, pl, errno ? strerror(errno) : "");
		}
		if (n == 0) {
		    closesocket(ss);
		    Rf_error("Connections closed unexpectedly, got %lu of %lu bytes", ii, pl);
		}
		ii += n;
	    }
	    closesocket(ss);
	    return res;
	} else {
	    closesocket(ss);
	    Rf_error("Invalid response: %s", buf);
	}
    }
    /* never reached, but compiler may not know */
    return R_NilValue;
}
