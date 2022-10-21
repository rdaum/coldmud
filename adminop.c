/* adminop.c: Operators for administrative functions. */

#define _POSIX_SOURCE

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include "x.tab.h"
#include "operator.h"
#include "execute.h"
#include "data.h"
#include "object.h"
#include "dump.h"
#include "io.h"
#include "log.h"
#include "cache.h"
#include "util.h"
#include "config.h"
#include "ident.h"
#include "memory.h"
#include "net.h"
#include "lookup.h"

#ifdef BSD_FEATURES
/* vfork() is not POSIX. */
extern pid_t vfork(void);
#endif

extern int running;
extern long heartbeat_freq, db_top;

/* All of the functions in this file are interpreter function operators, so
 * they require that the interpreter data (the globals in execute.c) be in a
 * state consistent with interpretation, and that a stack position has been
 * pushed onto the arg_starts stack using op_start_args().  They will pop a
 * value off the argument starts stack, and may affect the interpreter data by
 * popping and pushing the data stack or throwing exceptions. */

void op_create(void)
{
    Data *args;
    List *parents;
    Object *obj;
    int i;

    /* Accept a list of parents. */
    if (!func_init_1(&args, LIST))
	return;

    if (cur_frame->object->dbref != SYSTEM_DBREF) {
	throw(perm_id, "Current object (#%l) is not the system object.",
	      cur_frame->object->dbref);
	return;
    }

    /* Get parents list from second argument. */
    if (args[0].u.sublist.span == args[0].u.sublist.list->len)
	parents = list_dup(args[0].u.sublist.list);
    else
	parents = list_from_data(data_dptr(&args[0]), args[0].u.sublist.span);

    /* Verify that all parents are dbrefs. */
    for (i = 0; i < parents->len; i++) {
	if (parents->el[i].type != DBREF) {
	    throw(type_id, "Element %d of parents list (%D) is not a dbref.",
		  i, &parents->el[i]);
	    list_discard(parents);
	    return;
	} else if (!cache_check(parents->el[i].u.dbref)) {
	    throw(objnf_id, "Parent dbref %D does not refer to an object.",
		  &parents->el[i]);
	    list_discard(parents);
	    return;
	}
    }

    /* Create the new object. */
    obj = object_new(-1, parents);
    list_discard(parents);

    pop(1);
    push_dbref(obj->dbref);
    cache_discard(obj);
}

void op_chparents(void)
{
    Data *args, *d;
    Object *obj;
    int wrong;

    /* Accept a dbref and a list of parents to change to. */
    if (!func_init_2(&args, DBREF, LIST))
	return;

    if (cur_frame->object->dbref != SYSTEM_DBREF) {
	throw(perm_id, "Current object (#%l) is not the system object.",
	      cur_frame->object->dbref);
	return;
    }

    if (args[0].u.dbref == ROOT_DBREF) {
	throw(perm_id, "You cannot change the root object's parents.");
	return;
    }

    obj = cache_retrieve(args[0].u.dbref);
    if (!obj) {
	throw(objnf_id, "Object #%l not found.", args[0].u.dbref);
	return;
    }

    if (!args[1].u.sublist.span) {
	throw(perm_id, "You must specify at least one parent.");
	return;
    }

    /* Call object_change_parents().  This will return the number of a
     * parent which was invalid, or -1 if they were all okay. */
    wrong = object_change_parents(obj, &args[1].u.sublist);
    if (wrong >= 0) {
	d = data_dptr(&args[1]) + wrong;
	if (d->type != DBREF) {
	    throw(type_id, "New parent %D is not a dbref.", d);
	} else if (d->u.dbref == args[0].u.dbref) {
	    throw(parent_id, "New parent %D is the same as %D.", d, &args[0]);
	} else if (!cache_check(d->u.dbref)) {
	    throw(objnf_id, "New parent %D does not exist.", d);
	} else {
	    throw(parent_id, "New parent %D is a descendent of %D.", d,
		  &args[0]);
	}
    } else {
	pop(2);
	push_int(1);
    }

    cache_discard(obj);
}

void op_destroy(void)
{
    Data *args;
    Object *obj;

    /* Accept a dbref to destroy. */
    if (!func_init_1(&args, DBREF))
	return;

    if (cur_frame->object->dbref != SYSTEM_DBREF) {
	throw(perm_id, "Current object (#%l) is not the system object.",
	      cur_frame->object->dbref);
    } else if (args[0].u.dbref == ROOT_DBREF) {
	throw(perm_id, "You can't destroy the root object.");
    } else if (args[0].u.dbref == SYSTEM_DBREF) {
	throw(perm_id, "You can't destroy the system object.");
    } else {
	obj = cache_retrieve(args[0].u.dbref);
	if (!obj) {
	    throw(objnf_id, "Object #%l not found.", args[0].u.dbref);
	    return;
	}
	/* Set the object dead, so it will go away when nothing is holding onto
	 * it.  cache_discard() will notice the dead flag, and call
	 * object_destroy(). */
	obj->dead = 1;
	cache_discard(obj);
	pop(1);
	push_int(1);
    }
}

/* Effects: If called by the system object with a string argument, logs it to
 * standard error using write_log(), and returns 1. */
void op_log(void)
{
    Data *args;

    /* Accept a string. */
    if (!func_init_1(&args, STRING))
	return;

    if (cur_frame->object->dbref != SYSTEM_DBREF) {
	throw(perm_id, "Current object (#%l) is not the system object.",
	      cur_frame->object->dbref);
    } else {
	write_log("> %S", data_sptr(&args[0]), args[0].u.substr.span);
	pop(1);
	push_int(1);
    }
}

/* Modifies: cur_player, contents of cur_conn.
 * Effects: If called by the system object with a dbref argument, assigns that
 * 	    dbref to cur_conn->dbref and to cur_player and returns 1, unless
 *	    there is no current connection, in which case it returns 0. */
void op_conn_assign(void)
{
    Data *args;

    /* Accept a dbref. */
    if (!func_init_1(&args, DBREF))
	return;

    if (cur_frame->object->dbref != SYSTEM_DBREF) {
	throw(perm_id, "Current object (#%l) is not the system object.",
	      cur_frame->object->dbref);
    } else if (cur_conn) {
	cur_conn->dbref = args[0].u.dbref;
	pop(1);
	push_int(1);
    } else {
	pop(1);
	push_int(0);
    }
}

/* Modifies: The object cache, identifier table, and binary database files via
 *	     cache_sync() and ident_dump().
 * Effects: If called by the sytem object with no arguments, performs a binary
 *	    dump, ensuring that the files db and db.* are consistent.  Returns
 *	    1 if the binary dump succeeds, or 0 if it fails. */
void op_binary_dump(void)
{
    /* Accept no arguments. */
    if (!func_init_0())
	return;

    if (cur_frame->object->dbref != SYSTEM_DBREF) {
	throw(perm_id, "Current object (#%l) is not the system object.",
	      cur_frame->object->dbref);
    } else {
	push_int(binary_dump());
    }
}

/* Modifies: The object cache and binary database files via cache_sync() and
 *	     two sweeps through the database.  Modifies the internal dbm state
 *	     use by dbm_firstkey() and dbm_nextkey().
 * Effects: If called by the system object with no arguments, performs a text
 *	    dump, creating a file 'textdump' which contains a representation
 *	    of the database in terms of a few simple commands and the C--
 *	    language.  Returns 1 if the text dump succeeds, or 0 if it
 *	    fails.*/
void op_text_dump(void)
{
    /* Accept no arguments. */
    if (!func_init_0())
	return;

    if (cur_frame->object->dbref != SYSTEM_DBREF) {
	throw(perm_id, "Current object (#%l) is not the system object.",
	      cur_frame->object->dbref);
    } else {
	push_int(text_dump());
    }
}

void op_run_script(void)
{
    Data *args, *base;
    int num_args, i, fd, status;
    pid_t pid;
    char *fname, **argv;

    /* Accept a name of a script to run, a list of arguments to give it, and
     * an optional flag signifying that we should not wait for completion. */
    if (!func_init_2_or_3(&args, &num_args, STRING, LIST, INTEGER))
	return;

    /* Verify that all items in argument list are strings. */
    base = data_dptr(&args[1]);
    for (i = 0; i < args[1].u.sublist.span; i++) {
	if (base[i].type != STRING) {
	    throw(type_id, "Argument %d (%D) is not a string.",
		  i + 1, data_dptr(&args[1]) + i);
	    return;
	}
    }

    /* Restrict to system object. */
    if (cur_frame->object->dbref != SYSTEM_DBREF) {
	throw(perm_id, "Current object (#%l) is not the system object.",
	      cur_frame->object->dbref);
	return;
    }

    /* Construct the name of the script. */
    fname = TMALLOC(char, args[0].u.substr.span + 9);
    memcpy(fname, "scripts/", 8);
    memcpy(fname + 8, data_sptr(&args[0]), args[0].u.substr.span);
    fname[args[0].u.substr.span + 8] = 0;

    /* Don't walking back up the directory tree. */
    if (strstr(fname, "../")) {
	tfree_chars(fname);
	throw(perm_id, "Filename %D is not legal.", &args[0]);
	return;
    }

    /* Build an argument list. */
    argv = TMALLOC(char *, args[1].u.substr.span + 2);
    argv[0] = tstrdup(fname);
    for (i = 0; i < args[1].u.sublist.span; i++)
	argv[1] = tstrndup(data_sptr(&base[i]), base[i].u.substr.span);
    argv[args[1].u.sublist.span + 1] = NULL;

    pop(num_args);

    /* Fork off a process using vfork() (not POSIX). */
#ifdef BSD_FEATURES
    pid = vfork();
#else
    pid = fork();
#endif
    if (pid == 0) {
	/* Pipe stdin and stdout to /dev/null, keep stderr. */
	fd = open("/dev/null", O_RDWR);
	if (fd == -1) {
	    write_log("EXEC: Failed to open /dev/null: %s.", strerror(errno));
	    exit(-1);
	}
	dup2(fd, STDIN_FILENO);
	dup2(fd, STDOUT_FILENO);
	execv(fname, argv);
	write_log("EXEC: Failed to exec \"%s\": %s.", fname, strerror(errno));
	exit(-1);
    } else if (pid > 0) {
	if (num_args == 3 && args[2].u.val) {
	    if (waitpid(pid, &status, WNOHANG) == 0)
		status = 0;
	} else {
	    waitpid(pid, &status, 0);
	}
    } else {
	write_log("EXEC: Failed to vfork: %s.", strerror(errno));
	status = -1;
    }

    push_int(status);
}

/* Modifies: The 'running' global may be set to 0.
 * Effects: If called by the system object with no arguments, sets 'running'
 *	    to 0, causing the program to exit after this iteration of the main
 *	    loop finishes.  Returns 1. */
void op_shutdown(void)
{
    /* Accept no arguments. */
    if (!func_init_0())
	return;

    if (cur_frame->object->dbref != SYSTEM_DBREF) {
	throw(perm_id, "Current object (#%l) is not the system object.",
	      cur_frame->object->dbref);
    } else {
	running = 0;
	push_int(1);
    }
}

void op_bind(void)
{
    Data *args;

    /* Accept a port to bind to, and a dbref to handle connections. */
    if (!func_init_2(&args, INTEGER, DBREF))
	return;

    if (cur_frame->object->dbref != SYSTEM_DBREF) {
	throw(perm_id, "Current object (#%l) is not the system object.",
	      cur_frame->object->dbref);
	return;
    }

    if (add_server(args[0].u.val, args[1].u.dbref))
	push_int(1);
    else if (server_failure_reason == socket_id)
	throw(socket_id, "Couldn't create server socket.");
    else /* (server_failure_reason == bind_id) */
	throw(bind_id, "Couldn't bind to port %d.", args[0].u.val);
}

void op_unbind(void)
{
    Data *args;

    /* Accept a port number. */
    if (!func_init_1(&args, INTEGER))
	return;

    if (cur_frame->object->dbref != SYSTEM_DBREF) {
	throw(perm_id, "Current object (#%l) is not the system object.",
	      cur_frame->object->dbref);
	return;
    }

    if (!remove_server(args[0].u.val))
	throw(servnf_id, "No server socket on port %d.", args[0].u.val);
    else
	push_int(1);
}

void op_connect(void)
{
    Data *args;
    long r;

    if (!func_init_3(&args, STRING, INTEGER, DBREF))
	return;

    if (cur_frame->object->dbref != SYSTEM_DBREF) {
	throw(perm_id, "Current object (#%l) is not the system object.",
	      cur_frame->object->dbref);
	return;
    }

    substring_truncate(&args[0].u.substr);
    r = make_connection(data_sptr(&args[0]), args[1].u.val, args[2].u.dbref);
    if (r == address_id)
	throw(address_id, "Invalid address");
    else if (r == socket_id)
	throw(socket_id, "Couldn't create socket for connection");
    pop(3);
    push_int(1);
}

void op_set_heartbeat_freq(void)
{
    Data *args;

    if (!func_init_1(&args, INTEGER))
	return;

    if (cur_frame->object->dbref != SYSTEM_DBREF) {
	throw(perm_id, "Current object (#%l) is not the system object.",
	      cur_frame->object->dbref);
	return;
    }

    if (args[0].u.val <= 0)
	args[0].u.val = -1;
    heartbeat_freq = args[0].u.val;
    pop(1);
}

void op_data(void)
{
    Data *args, key, value;
    Object *obj;
    Dict *dict;
    int i;

    if (!func_init_1(&args, DBREF))
	return;

    if (cur_frame->object->dbref != SYSTEM_DBREF) {
	throw(perm_id, "Current object (#%l) is not the system object.",
	      cur_frame->object->dbref);
	return;
    }

    obj = cache_retrieve(args[0].u.dbref);
    if (!obj) {
	throw(objnf_id, "No such object #%l", args[0].u.dbref);
	return;
    }

    /* Construct the dictionary. */
    dict = dict_new_empty();
    for (i = 0; i < obj->vars.size; i++) {
	if (obj->vars.tab[i].name == -1)
	    continue;
	key.type = DBREF;
	key.u.dbref = obj->vars.tab[i].class;
	if (dict_find(dict, &key, &value) == keynf_id) {
	    value.type = DICT;
	    value.u.dict = dict_new_empty();
	    dict = dict_add(dict, &key, &value);
	}

	key.type = SYMBOL;
	key.u.symbol = obj->vars.tab[i].name;
	value.u.dict = dict_add(value.u.dict, &key, &obj->vars.tab[i].val);

	key.type = DBREF;
	key.u.dbref = obj->vars.tab[i].class;
	dict = dict_add(dict, &key, &value);
	dict_discard(value.u.dict);
    }

    cache_discard(obj);
    pop(1);
    push_dict(dict);
    dict_discard(dict);
}

void op_set_name(void)
{
    Data *args;
    int result;

    if (!func_init_2(&args, SYMBOL, DBREF))
	return;

    if (cur_frame->object->dbref != SYSTEM_DBREF) {
	throw(perm_id, "Current object (#%l) is not the system object.",
	      cur_frame->object->dbref);
	return;
    }

    result = lookup_store_name(args[0].u.symbol, args[1].u.dbref);
    pop(2);
    push_int(result);
}

void op_del_name(void)
{
    Data *args;

    if (!func_init_1(&args, SYMBOL))
	return;

    if (cur_frame->object->dbref != SYSTEM_DBREF) {
	throw(perm_id, "Current object (#%l) is not the system object.",
	      cur_frame->object->dbref);
	return;
    }

    if (!lookup_remove_name(args[0].u.symbol)) {
	throw(namenf_id, "Can't find object name %I.", args[0].u.symbol);
	return;
    }

    pop(1);
    push_int(1);
}

void op_db_top(void)
{
    if (!func_init_0())
	return;
    push_int(db_top);
}

