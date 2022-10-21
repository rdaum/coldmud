/* sig.c: Coldmud signal handling. */

#define _POSIX_SOURCE

#include <stdio.h>
#include <signal.h>
#include "sig.h"

void init_sig(void)
{
    struct sigaction act;

    sigemptyset(&act.sa_mask);
    act.sa_flags = 0;

    /* Ignore SIGPIPE, since we may write to a closed socket due to
     * unpreventable race conditions. */
    act.sa_handler = SIG_IGN;
    sigaction(SIGPIPE, &act, NULL);
}

