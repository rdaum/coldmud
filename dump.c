/* dump.c: Routines to handle binary and text database dumps. */
#define _POSIX_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include "x.tab.h"
#include "dump.h"
#include "cache.h"
#include "object.h"
#include "log.h"
#include "data.h"
#include "config.h"
#include "util.h"
#include "execute.h"
#include "grammar.h"
#include "db.h"
#include "ident.h"
#include "lookup.h"

static Method *text_dump_get_method(FILE *fp, Object *obj, char *name);
static long get_dbref(char **sptr);

extern int cur_search;

/* Binary dump.  This dump must not allocate any memory, since we may be
 * performing it under low-memory conditions. */
int binary_dump(void)
{
    cache_sync();
    return 1;
}

/* Text dump.  This dump can allocate memory, and thus shouldn't be used as a
 * panic dump for low-memory situations. */
int text_dump(void)
{
    FILE *fp;
    Object *obj;
    long name, dbref;

    /* Open the output file. */
    fp = open_scratch_file("textdump.new", "w");
    if (!fp)
	return 0;

    /* Dump the names. */
    name = lookup_first_name();
    while (name != NOT_AN_IDENT) {
	if (!lookup_retrieve_name(name, &dbref))
	    panic("Name index is inconsistent.");
	fformat(fp, "name %I %d\n", name, dbref);
	ident_discard(name);
	name = lookup_next_name();
    }

    /* Dump the objects. */
    cur_search++;
    for (obj = cache_first(); obj; obj = cache_next()) {
	object_text_dump(obj->dbref, fp);
	cache_discard(obj);
    }

    close_scratch_file(fp);
    unlink("textdump");
    if (rename("textdump.new", "textdump") == -1)
	return 0;

    return 1;
}

void text_dump_read(FILE *fp)
{
    String *line;
    Object *obj = NULL;
    List *parents;
    Data d;
    long dbref = -1, name;
    char *p, *q;
    Method *method;

    /* Initialize parents to an empty list. */
    parents = list_new(0);

    while ((line = fgetstring(fp))) {

	/* Strip trailing spaces from the line. */
	while (line->len && isspace(line->s[line->len - 1]))
	    line->len--;
	line->s[line->len] = 0;

	/* Strip unprintables from the line. */
	for (p = q = line->s; *p; p++, q++) {
	    while (*p && !isprint(*p))
		p++;
	    *q = *p;
	}
	*q = 0;
	line->len = q - line->s;

	if (!strnccmp(line->s, "parent", 6) && isspace(line->s[6])) {
	    for (p = line->s + 7; isspace(*p); p++);

	    /* Add this parent to the parents list. */
	    q = p;
	    dbref = get_dbref(&q);
	    if (cache_check(dbref)) {
		d.type = DBREF;
		d.u.dbref = dbref;
		parents = list_add(parents, &d);
	    } else {
		write_log("Parent %s does not exist.", p);
	    }

	} else if (!strnccmp(line->s, "object", 6) && isspace(line->s[6])) {
	    for (p = line->s + 7; isspace(*p); p++);
	    q = p;
	    dbref = get_dbref(&q);

	    /* If the parents list is empty, and this isn't "root", parent it
	     * to root. */
	    if (!parents->len && dbref != ROOT_DBREF) {
		write_log("Orphan object %s parented to root", p);
		if (!cache_check(ROOT_DBREF))
		    fail_to_start("Root object not first in text dump.");
		d.type = DBREF;
		d.u.dbref = ROOT_DBREF;
		parents = list_add(parents, &d);
	    }

	    /* Discard the old object if we had one.  Also see if dbref already
	     * exists, and delete it if it does. */
	    if (obj)
		cache_discard(obj);
	    obj = cache_retrieve(dbref);
	    if (obj) {
		obj->dead = 1;
		cache_discard(obj);
	    }

	    /* Create the new object. */
	    obj = object_new(dbref, parents);
	    list_discard(parents);
	    parents = list_new(0);

	} else if (!strnccmp(line->s, "var", 3) && isspace(line->s[3])) {
	    for (p = line->s + 4; isspace(*p); p++);

	    /* Get variable owner. */
	    dbref = get_dbref(&p);

	    /* Skip spaces and get variable name. */
	    while (isspace(*p))
		p++;
	    name = parse_ident(&p);

	    /* Skip spaces and get variable value. */
	    while (isspace(*p))
		p++;
	    data_from_literal(&d, p);

	    /* Create the variable. */
	    object_put_var(obj, dbref, name, &d);

	    ident_discard(name);
	    data_discard(&d);

	} else if (!strccmp(line->s, "eval")) {
	    method = text_dump_get_method(fp, obj, "<eval>");
	    if (method) {
		method->name = NOT_AN_IDENT;
		method->object = obj;
		task_method(NULL, obj, method);
		method_discard(method);
	    }

	} else if (!strnccmp(line->s, "method", 6) && isspace(line->s[6])) {
	    for (p = line->s + 7; isspace(*p); p++);
	    name = parse_ident(&p);
	    method = text_dump_get_method(fp, obj, ident_name(name));
	    if (method) {
		object_add_method(obj, name, method);
		method_discard(method);
	    }
	    ident_discard(name);

	} else if (!strnccmp(line->s, "name", 4) && isspace(line->s[4])) {
	    /* Skip spaces and get name. */
	    for (p = line->s + 5; isspace(*p); p++);
	    name = parse_ident(&p);

	    /* Skip spaces and get dbref. */
	    while (isspace(*p))
		p++;
	    dbref = atol(p);

	    /* Store the name. */
	    if (!lookup_store_name(name, dbref))
		fail_to_start("Can't store name--disk full?");

	    ident_discard(name);
	}

	string_discard(line);
    }

    if (obj)
	cache_discard(obj);
    list_discard(parents);
}

/* Get a dbref.  Use some intuition. */
static long get_dbref(char **sptr)
{
    char *s = *sptr;
    long dbref, name;
    int result;

    if (isdigit(*s)) {
	/* Looks like the user wants to specify an object number. */
	dbref = atol(s);
	while (isdigit(*++s));
	*sptr = s;
	return dbref;
    } else if (*s == '#') {
	/* Looks like the user really wants to specify an object number. */
	dbref = atol(s + 1);
	while (isdigit(*++s));
	*sptr = s;
	return dbref;
    } else {
	/* It's a name.  If there's a dollar sign (which might be there to make
	 * sure that it's not interpreted as a number), skip it. */
	if (*s == '$')
	    s++;
	name = parse_ident(&s);
	*sptr = s;
	result = lookup_retrieve_name(name, &dbref);
	ident_discard(name);
	return (result) ? dbref : -1;
    }
}

static Method *text_dump_get_method(FILE *fp, Object *obj, char *name)
{
    Method *method;
    List *code, *errors;
    String *line;
    Data d;
    int i;

    code = list_new(0);
    d.type = STRING;
    while ((line = fgetstring(fp))) {
	if (line->len == 1 && *line->s == '.') {
	    /* End of the code.  Compile the method, display any error
	     * messages we may have received, and return the method. */
	    string_discard(line);
	    method = compile(obj, code->el, code->len, &errors);
	    list_discard(code);
	    for (i = 0; i < errors->len; i++) {
		write_log("#%l %s: %S", obj->dbref, name,
			  data_sptr(&errors->el[i]),
			  errors->el[i].u.substr.span);
	    }
	    list_discard(errors);
	    return method;
	}

	substr_set_to_full_string(&d.u.substr, line);
	code = list_add(code, &d);
	string_discard(line);
    }

    /* We ran out of lines.  This wasn't supposed to happen. */
    write_log("Text dump ended inside method.");
    return NULL;
}

