/* net.h: Declarations for network routines. */

#ifndef NET_H
#define NET_H
#include "io.h"

int get_server_socket(int port);
int io_event_wait(long sec, Connection *connections, Server *servers,
		  Pending *pendings);
long non_blocking_connect(char *addr, int port, int *socket_return);

extern long server_failure_reason;

#endif

