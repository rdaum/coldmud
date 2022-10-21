/* io.h: Declarations for input/output management. */

#ifndef IO_H
#define IO_H
#include "cmstring.h"
#include "data.h"

typedef struct connection Connection;
typedef struct server Server;
typedef struct pending Pending;

struct connection {
    int fd;			/* File descriptor for input and output. */
    Buffer *write_buf;		/* Buffer for network output. */
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
    unsigned short port;
    long dbref;
    int dead;
    int client_socket;
    char client_addr[20];
    unsigned short client_port;
    Server *next;
};

struct pending {
    int fd;
    long task_id;
    long dbref;
    long error;
    int finished;
    Pending *next;
};

void flush_defunct(void);
void handle_io_events(long sec);
void tell(long dbref, Buffer *buf);
int boot(long dbref);
int add_server(int port, long dbref);
int remove_server(int port);
long make_connection(char *addr, int port, long dbref);
void flush_output(void);

#endif

