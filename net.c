/* net.c: Network routines. */
/* This stuff is not POSIX, and thus must be ported separately to each
 * network interface.  This code is for a BSD interface. */

/* RFC references: inverse name resolution--1293, 903
 * 1035 - domain name system */

#define _BSD 44 /* For RS6000s. */

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>
#include <fcntl.h>
#include "net.h"
#include "io.h"
#include "log.h"
#include "util.h"
#include "ident.h"

extern int socket(), bind(), listen(), getdtablesize(), select(), accept();
extern int connect(), getpeername(), getsockopt();
extern void bzero(), memset();

static long translate_connect_error(int error);

static struct sockaddr_in sin;		/* An internet address. */
static int addr_size = sizeof(sin);	/* Size of sin. */

long server_failure_reason;

int get_server_socket(int port)
{
    int fd;

    /* Create a socket. */
    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
	server_failure_reason = socket_id;
	return -1;
    }

    /* Bind the socket to port. */
    sin.sin_family = AF_INET;
    sin.sin_port = htons((unsigned short) port);
    if (bind(fd, (struct sockaddr *) &sin, sizeof(sin)) < 0) {
	server_failure_reason = bind_id;
	return -1;
    }

    /* Start listening on port.  This shouldn't return an error under any
     * circumstances. */
    listen(fd, 8);

    return fd;
}

/* Wait for I/O events.  sec is the number of seconds we can wait before
 * returning, or -1 if we can wait forever.  Returns nonzero if an I/O event
 * happened. */
int io_event_wait(long sec, Connection *connections, Server *servers,
		  Pending *pendings)
{
    struct timeval tv, *tvp;
    Connection *conn;
    Server *serv;
    Pending *pend;
    fd_set read_fds, write_fds;
    int flags, nfds, count, result, error, dummy = sizeof(int);

    /* Set time structure according to sec. */
    if (sec == -1) {
	tvp = NULL;
    } else {
	tv.tv_sec = sec;
	tv.tv_usec = 0;
	tvp = &tv;
    }

    /* Begin with blank file descriptor masks and an nfds of 0. */
    FD_ZERO(&read_fds);
    FD_ZERO(&write_fds);
    nfds = 0;

    /* Listen for new data on connections, and also check for ability to write
     * to them if we have data to write. */
    for (conn = connections; conn; conn = conn->next) {
	if (!conn->flags.dead)
	    FD_SET(conn->fd, &read_fds);
	if (conn->write_buf->len)
	    FD_SET(conn->fd, &write_fds);
	if (conn->fd >= nfds)
	    nfds = conn->fd + 1;
    }

    /* Listen for connections on the server sockets. */
    for (serv = servers; serv; serv = serv->next) {
	FD_SET(serv->server_socket, &read_fds);
	if (serv->server_socket >= nfds)
	    nfds = serv->server_socket + 1;
    }

    /* Check pending connections for ability to write. */
    for (pend = pendings; pend; pend = pend->next) {
	if (pend->error != NOT_AN_IDENT) {
	    /* The connect has already failed; just set the finished bit. */
	    pend->finished = 1;
	} else {
	    FD_SET(pend->fd, &write_fds);
	    if (pend->fd >= nfds)
		nfds = pend->fd + 1;
	}
    }

    /* Call select(). */
    count = select(nfds, &read_fds, &write_fds, NULL, tvp);

    /* Lose horribly if select() fails on anything but an interrupted system
     * call.  On EINTR, we'll return 0. */
    if (count == -1 && errno != EINTR)
	panic("select() failed");

    /* Stop and return zero if no I/O events occurred. */
    if (count <= 0)
	return 0;

    /* Check if any connections are readable or writable. */
    for (conn = connections; conn; conn = conn->next) {
	if (FD_ISSET(conn->fd, &read_fds))
	    conn->flags.readable = 1;
	if (FD_ISSET(conn->fd, &write_fds))
	    conn->flags.writable = 1;
    }

    /* Check if any server sockets have new connections. */
    for (serv = servers; serv; serv = serv->next) {
	if (FD_ISSET(serv->server_socket, &read_fds)) {
	    serv->client_socket = accept(serv->server_socket,
					 (struct sockaddr *) &sin, &addr_size);
	    if (serv->client_socket < 0)
		continue;

	    /* Set the CLOEXEC flag on socket so that it will be closed for a
	     * run_script() operation. */
#ifdef FD_CLOEXEC
	    flags = fcntl(serv->client_socket, F_GETFD);
	    flags |= FD_CLOEXEC;
	    fcntl(serv->client_socket, F_SETFD, flags);
#endif
	}
    }

    /* Check if any pending connections have succeeded or failed. */
    for (pend = pendings; pend; pend = pend->next) {
	if (FD_ISSET(pend->fd, &write_fds)) {
	    result = getpeername(pend->fd, (struct sockaddr *) &sin,
				 &addr_size);
	    if (result == 0) {
		pend->error = NOT_AN_IDENT;
	    } else {
		getsockopt(pend->fd, SOL_SOCKET, SO_ERROR, (char *) &error,
			   &dummy);
		pend->error = translate_connect_error(error);
	    }
	    pend->finished = 1;
	}
    }

    /* Return nonzero, indicating that at least one I/O event occurred. */
    return 1;
}

long non_blocking_connect(char *addr, int port, int *socket_return)
{
    int fd, result, flags;
    struct in_addr inaddr;
    struct sockaddr_in saddr;

    /* Convert address to struct in_addr. */
    inaddr.s_addr = inet_addr(addr);
    if (inaddr.s_addr == -1)
	return address_id;

    /* Get a socket for the connection. */
    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == -1)
	return socket_id;

    /* Set the socket non-blocking. */
    flags = fcntl(fd, F_GETFL);
#ifdef FNDELAY
    flags |= FNDELAY;
#else
#ifdef O_NDELAY
    flags |= O_NDELAY;
#endif
#endif
    fcntl(fd, F_SETFL, flags);

    /* Make the connection. */
    memset(&saddr, 0, sizeof(saddr));
    saddr.sin_family = AF_INET;
    saddr.sin_port = htons((unsigned short) port);
    saddr.sin_addr = inaddr;
    do {
	result = connect(fd, (struct sockaddr *) &saddr, sizeof(saddr));
    } while (result == -1 && errno == EINTR);

    *socket_return = fd;
    if (result != -1 || errno == EINPROGRESS)
	return NOT_AN_IDENT;
    else
	return translate_connect_error(errno);
}

static long translate_connect_error(int error)
{
    switch (error) {

      case ECONNREFUSED:
	return refused_id;

      case ENETUNREACH:
	return net_id;

      case ETIMEDOUT:
	return timeout_id;

      default:
	return other_id;
    }
}

