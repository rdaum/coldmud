/* io.h: Declarations for input/output management. */

#ifndef IO_H
#define IO_H
#include "cmstring.h"

typedef struct connection Connection;
typedef struct server Server;
typedef struct pending Pending;

struct connection {
    int fd;			/* File descriptor for input and output. */
    String *read_buf;		/* Buffer for network input. */
    String *write_buf;		/* Buffer for network output. */
    long dbref;			/* The player, usually. */
    struct {
	char readable;		/* Connection has new data pending. */
	char writable;		/* Connection can be written to. */
	char dead;		/* Connection is defunct. */
    } flags;
    Connection *next;
};

struct server {
    int server_socket;
    int client_socket;
    unsigned short port;
    long dbref;
    int dead;
    Server *next;
};

struct pending {
    int fd;
    long dbref;
    long error;
    int finished;
    Pending *next;
};

void flush_defunct(void);
void handle_io_events(long sec);
void tell(long dbref, char *s, int len);
int boot(long dbref);
int add_server(int port, long dbref);
int remove_server(int port);
long make_connection(char *addr, int port, long dbref);

#endif

