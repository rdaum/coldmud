/* main.c: The main program. */

#define _POSIX_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include "x.tab.h"
#include "codegen.h"
#include "opcodes.h"
#include "object.h"
#include "match.h"
#include "cache.h"
#include "sig.h"
#include "db.h"
#include "util.h"
#include "io.h"
#include "data.h"
#include "log.h"
#include "dump.h"
#include "execute.h"
#include "ident.h"
#include "cmstring.h"
#include "token.h"
#include "config.h"

int running = 1;
long heartbeat_freq = -1;
time_t last_heartbeat;

static void initialize(int argc, char **argv);
static void main_loop(void);

int main(int argc, char **argv)
{
    initialize(argc, argv);
    main_loop();

    /* We get this far after a C-- shutdown().  Sync the cache, flush output
     * buffers, and exit normally. */
    cache_sync();
    db_close();
    flush_output();
    return 0;
}

static void initialize(int argc, char **argv)
{
    FILE *fp;
    Object *obj;
    List *parents, *args;
    int i, use_text_dump;
    String *str;
    Data d;

    /* Ditch stdin and stdout, so we can reuse the file descriptors. */
    fclose(stdin);
    fclose(stdout);

    /* Initialize internal tables and variables. */
    init_codegen();
    init_op_table();
    init_match();
    init_util();
    init_sig();
    init_execute();
    init_ident();
    init_scratch_file();
    init_token();

    /* Make sure we have enough arguments. */
    if (argc < 2) {
	fprintf(stderr, "Usage: %s <database> <db args>\n",  argv[0]);
	exit(1);
    }

    /* Switch into database direectory. */
    if (chdir(argv[1]) == -1) {
	fprintf(stderr, "Couldn't change to directory %s.\n", argv[1]);
	exit(1);
    }

    /* Build argument list from arguments. */
    args = list_new(argc);
    for (i = 0; i < argc; i++) {
	args->el[i].type = STRING;
	str = string_from_chars(argv[i], strlen(argv[i]));
	substr_set_to_full_string(&args->el[i].u.substr, str);
    }

    /* Initialize database and network modules. */
    init_cache();
    use_text_dump = init_db();

    /* Order of operations note: it might seem like we'd want to read the text
     * dump (if we're going to) before making sure there's a root and system
     * object.  However, this way is correct, since the textdump reader can
     * evaluate arbitrary C-- code and thus should start with a consistent
     * database. */

    /* Make sure there is a root object. */
    obj = cache_retrieve(ROOT_DBREF);
    if (!obj) {
	parents = list_new(0);
	obj = object_new(ROOT_DBREF, parents);
	list_discard(parents);
    }
    cache_discard(obj);

    /* Make sure there is a system object. */
    obj = cache_retrieve(SYSTEM_DBREF);
    if (!obj) {
	parents = list_new(1);
	parents->el[0].type = DBREF;
	parents->el[0].u.dbref = ROOT_DBREF;
	obj = object_new(SYSTEM_DBREF, parents);
	list_discard(parents);
    }
    cache_discard(obj);

    /* Read a text dump if there was no existing binary database. */
    if (use_text_dump) {
	fp = fopen("textdump", "r");
	if (!fp) {
	    fail_to_start("Couldn't open text dump file.");
	} else {
	    text_dump_read(fp);
	    fclose(fp);
	}
    }

    /* Send a startup message to the system object. */
    d.type = LIST;
    sublist_set_to_full_list(&d.u.sublist, args);
    task(NULL, SYSTEM_DBREF, startup_id, 1, &d);
    list_discard(args);
}

static void main_loop(void)
{
    int seconds;
    time_t next_heartbeat = 0, t;

    while (running) {
	/* Delete any defunct connection or server records.  This sends a
	 * "disconnect"* message to the system object for each connection done
	 * away with. */
	flush_defunct();

	/* Sanity check: make sure there are no objects in active chains. */
	cache_sanity_check();

	/* Find number of seconds before next heartbeat. */
	if (heartbeat_freq == -1) {
	    seconds = -1;
	} else {
	    next_heartbeat = (last_heartbeat -
			      (last_heartbeat % heartbeat_freq)
			      ) + heartbeat_freq;
	    time(&t);
	    seconds = (t >= next_heartbeat) ? 0 : next_heartbeat - t;
	}

	/* Handle any I/O events waiting. */
	handle_io_events(seconds);

	if (heartbeat_freq != -1) {
	    time(&t);
	    if (t >= next_heartbeat) {
		last_heartbeat = t;
		task(NULL, SYSTEM_DBREF, heartbeat_id, 0);
	    }
	}
    }
}

