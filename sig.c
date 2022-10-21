/* sig.c: Coldmud signal handling. */

#define _POSIX_SOURCE

#include <stdio.h>
#include <signal.h>
#include "sig.h"

void init_sig(void)
{
    signal(SIGPIPE, SIG_IGN);
}

