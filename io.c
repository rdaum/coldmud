/* io.c: Network routines. */

#define _POSIX_SOURCE

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include "x.tab.h"
#include "io.h"
#include "net.h"
#include "execute.h"
#include "memory.h"
#include "grammar.h"
#include "cmstring.h"
#include "data.h"
#include "util.h"
#include "ident.h"

#define BUF_SIZE 1024

static void connection_read(Connection *conn);
static void connection_write(Connection *conn);
static Connection *connection_add(int fd, long dbref);
static void connection_discard(Connection *conn);
static void pend_discard(Pending *pend);
static void server_discard(Server *serv);
static void handle_incoming_lines(Connection *conn);
static void dispatch_line(Connection *conn, String *line);

static Connection *connections;		/* List of client connections. */
static Server *servers;			/* List of server sockets. */
static Pending *pendings;		/* List of pending connections. */

/* Notify the system object of any dead connections and delete them. */
void flush_defunct(void)
{
    Connection **connp, *conn;
    Server **servp, *serv;
    Pending **pendp, *pend;

    connp = &connections;
    while (*connp) {
	conn = *connp;
	if (conn->flags.dead && conn->write_buf->len == 0) {
	    *connp = conn->next;
	    connection_discard(conn);
	} else {
	    connp = &conn->next;
	}
    }

    servp = &servers;
    while (*servp) {
	serv = *servp;
	if (serv->dead) {
	    *servp = serv->next;
	    server_discard(serv);
	} else {
	    servp = &serv->next;
	}
    }

    pendp = &pendings;
    while (*pendp) {
	pend = *pendp;
	if (pend->finished) {
	    *pendp = pend->next;
	    pend_discard(pend);
	} else {
	    pendp = &pend->next;
	}
    }
}

/* Handle any I/O events.  sec is the number of seconds we get to wait for
 * something to happen before the timer wants the thread back, or -1 if we can
 * wait forever. */
void handle_io_events(long sec)
{
    Connection *conn;
    Server *serv;
    Pending *pend;
    String *str;
    Data d1, d2;

    /* Call io_event_wait() to wait for something to happen.  The return value
     * is nonzero if an I/O event occurred.  If thre is a new connection, then
     * *fd will be set to the descriptor of the new connection; otherwise, it
     * is set to -1. */
    if (!io_event_wait(sec, connections, servers, pendings))
	return;

    /* Deal with any events on our existing connections. */
    for (conn = connections; conn; conn = conn->next) {
	if (conn->flags.readable && !conn->flags.dead)
	    connection_read(conn);
	if (conn->flags.writable)
	    connection_write(conn);
    }

    /* Look for new connections on the server sockets. */
    for (serv = servers; serv; serv = serv->next) {
	if (serv->client_socket == -1)
	    continue;
	conn = connection_add(serv->client_socket, serv->dbref);
	serv->client_socket = -1;
	str = string_from_chars(serv->client_addr, strlen(serv->client_addr));
	d1.type = STRING;
	substr_set_to_full_string(&d1.u.substr, str);
	d2.type = INTEGER;
	d2.u.val = serv->client_port;
	task(conn, conn->dbref, connect_id, 2, &d1, &d2);
	string_discard(str);
    }

    /* Look for pending connections succeeding or failing. */
    for (pend = pendings; pend; pend = pend->next) {
	if (pend->finished) {
	    if (pend->error == NOT_AN_IDENT) {
		conn = connection_add(pend->fd, pend->dbref);
		d1.type = INTEGER;
		d1.u.val = pend->task_id;
		task(conn, conn->dbref, connect_id, 1, &d1);
	    } else {
		close(pend->fd);
		d1.type = INTEGER;
		d1.u.val = pend->task_id;
		d2.type = ERROR;
		d2.u.error = pend->error;
		task(NULL, pend->dbref, failed_id, 2, &d1, &d2);
	    }
	}
    }
}

void tell(long dbref, char *s, int len)
{
    Connection *conn;

    for (conn = connections; conn; conn = conn->next) {
	if (conn->dbref == dbref && !conn->flags.dead) {
	    conn->write_buf = string_add(conn->write_buf, s, len);
	    conn->write_buf = string_add(conn->write_buf, "\r\n", 2);
	}
    }
}

int boot(long dbref)
{
    Connection *conn;
    int count = 0;

    for (conn = connections; conn; conn = conn->next) {
	if (conn->dbref == dbref) {
	    conn->flags.dead = 1;
	    count++;
	}
    }
    return count;
}

int add_server(int port, long dbref)
{
    Server *new;
    int server_socket;

    /* Check if a server already exists for this port. */
    for (new = servers; new; new = new->next) {
	if (new->port == port) {
	    ident_discard(new->dbref);
	    new->dbref = ident_dup(dbref);
	    new->dead = 0;
	    return 1;
	}
    }

    /* Get a server socket for the port. */
    server_socket = get_server_socket(port);
    if (server_socket < 0)
	return 0;

    new = EMALLOC(Server, 1);
    new->server_socket = server_socket;
    new->client_socket = -1;
    new->port = port;
    new->dbref = ident_dup(dbref);
    new->dead = 0;
    new->next = servers;
    servers = new;

    return 1;
}

int remove_server(int port)
{
    Server **servp;

    for (servp = &servers; *servp; servp = &((*servp)->next)) {
	if ((*servp)->port == port) {
	    (*servp)->dead = 1;
	    return 1;
	}
    }

    return 0;
}

static void connection_read(Connection *conn)
{
    String *buf;
    int len;

    conn->flags.readable = 0;

    /* Make sure there's a significant amount of space in the read buffer. */
    buf = conn->read_buf = string_extend(conn->read_buf, BUF_SIZE);

    len = read(conn->fd, buf->s + buf->len, buf->size - buf->len - 1);
    if (len < 0 && errno == EINTR) {
	/* We were interrupted; deal with this next time around. */
	return;
    }

    if (len <= 0) {
	/* The connection closed. */
	conn->flags.dead = 1;
	return;
    }

    /* We successfully read some data.  Handle it. */
    buf->len += len;
    buf->s[buf->len] = 0;
    handle_incoming_lines(conn);
}

static void connection_write(Connection *conn)
{
    String *buf = conn->write_buf;
    int len;

    conn->flags.writable = 0;

    len = write(conn->fd, buf->s, buf->len);
    if (len <= 0) {
	/* We lost the connection. */
	conn->flags.dead = 1;
    } else  {
	conn->write_buf->len -= len;
	MEMMOVE(buf->s, buf->s + len, char, buf->len);
	buf->s[buf->len] = 0;
    }
}

static Connection *connection_add(int fd, long dbref)
{
    Connection *conn;

    conn = EMALLOC(Connection, 1);
    conn->fd = fd;
    conn->read_buf = string_empty(BUF_SIZE);
    conn->write_buf = string_empty(BUF_SIZE);
    conn->dbref = ident_dup(dbref);
    conn->flags.readable = 0;
    conn->flags.writable = 0;
    conn->flags.dead = 0;
    conn->next = connections;
    connections = conn;
    return conn;
}

static void connection_discard(Connection *conn)
{
    /* Notify system object that the connection is gone. */
    task(conn, conn->dbref, disconnect_id, 0);

    /* Free the data associated with the connection. */
    close(conn->fd);
    ident_discard(conn->dbref);
    string_discard(conn->read_buf);
    string_discard(conn->write_buf);
    free(conn);
}

static void pend_discard(Pending *pend)
{
    ident_discard(pend->dbref);
    free(pend);
}

static void server_discard(Server *serv)
{
    close(serv->server_socket);
    ident_discard(serv->dbref);
}

static void handle_incoming_lines(Connection *conn)
{
    char *s = conn->read_buf->s, *p, *q;
    String *line;
    int len;

    /* Loop while there is a newline in the read buffer. */
    len = conn->read_buf->len;
    while ((p = memchr(s, '\n', len))) {

	/* Decrement len by the number of characters in the line. */
	len -= p + 1 - s;

	/* Create a new string for the line; we will change the length after
	 * we copy in all the printable characters. */
	line = string_new(p - s);

	/* Copy in all the printable characters from s to p. */
	q = line->s;
	while (s < p) {
	    if (isprint(*s))
		*q++ = *s;
	    s++;
	}

	/* Null-terminate line, and set the length to the number of characters
	 * we actually copied. */
	*q = 0;
	line->len = q - line->s;

	/* Dispatch line. */
	dispatch_line(conn, line);
	string_discard(line);

	/* Stop if the connection has disappeared out from under us. */
	if (conn->flags.dead)
	    return;

	/* Increment s past the newline, and start again. */
	s++;
    }

    /* Copy whatever is left back into read_buf. */
    MEMMOVE(conn->read_buf->s, s, char, len + 1);
    conn->read_buf->len = len;
}

/* Process a line. */
static void dispatch_line(Connection *conn, String *line)
{
    Data d;

    /* Run parse method on object with line as an argument. */
    d.type = STRING;
    substr_set_to_full_string(&d.u.substr, line);
    task(conn, conn->dbref, parse_id, 1, &d);
}

long make_connection(char *addr, int port, long dbref)
{
    Pending *new;
    int socket;
    long result;

    result = non_blocking_connect(addr, port, &socket);
    if (result == address_id || result == socket_id)
	return result;
    new = TMALLOC(Pending, 1);
    new->fd = socket;
    new->task_id = task_id;
    new->dbref = dbref;
    new->finished = 0;
    new->error = result;
    new->next = pendings;
    pendings = new;
    return NOT_AN_IDENT;
}

/* Write out everything in connections' write buffers.  Called by main()
 * before exiting; does not modify the write buffers to reflect writing. */
void flush_output(void)
{
    Connection *conn;
    char *s;
    int len, r;

    for (conn = connections; conn; conn = conn->next) {
	s = conn->write_buf->s;
	len = conn->write_buf->len;
	while (len) {
	    r = write(conn->fd, s, len);
	    if (r <= 0)
		break;
	    len -= r;
	    s += r;
	}
    }
}

