/* dump.c: Routines to handle binary and text database dumps. */
#define _POSIX_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
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

static Method *text_dump_get_method(FILE *fp, Object *obj, char *name);

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

    /* Open the output file. */
    fp = open_scratch_file("textdump", "w");
    if (!fp)
	return 0;

    /* Now dump the database. */
    cur_search++;
    for (obj = cache_first(); obj; obj = cache_next()) {
	object_text_dump(obj->dbref, fp);
	cache_discard(obj);
    }

    close_scratch_file(fp);
    return 1;
}

void text_dump_read(FILE *fp)
{
    String *line;
    Object *obj = NULL;
    List *parents;
    Data d;
    long dbref = -1, name;
    char *p;
    Method *method;

    /* Initialize parents to an empty list. */
    parents = list_new(0);

    while ((line = fgetstring(fp))) {

	if (!strnccmp(line->s, "parent", 6) && isspace(line->s[6])) {
	    for (p = line->s + 7; isspace(*p); p++);

	    /* Add this parent to the parents list. */
	    d.type = DBREF;
	    d.u.dbref = ident_get(p);
	    if (cache_check(d.u.dbref))
		parents = list_add(parents, &d);
	    else
		write_log("Invalid parent %s", ident_name(d.u.dbref));
	    ident_discard(d.u.dbref);

	} else if (!strnccmp(line->s, "object", 6) && isspace(line->s[6])) {
	    for (p = line->s + 7; isspace(*p); p++);

	    /* If the parents list is empty, and this isn't "root", parent it
	     * to root. */
	    if (!parents->len && strcmp(p, "root") != 0) {
		write_log("Orphan object %s parented to root", p);
		if (!cache_check(root_id))
		    fail_to_start("Root object not first in text dump.");
		d.type = DBREF;
		d.u.dbref = root_id;
		parents = list_add(parents, &d);
	    }

	    /* Get the dbref. */
	    dbref = ident_get(p);

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
	    dbref = parse_ident(&p);

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

	    ident_discard(dbref);
	    ident_discard(name);
	    data_discard(&d);

	} else if (!strccmp(line->s, "eval")) {
	    method = text_dump_get_method(fp, obj, "<eval>");
	    if (method) {
		task_eval(NULL, obj, method);
		method_discard(method);
	    }

	} else if (!strnccmp(line->s, "method", 6) && isspace(line->s[6])) {
	    for (p = line->s + 7; *p == ' '; p++);
	    method = text_dump_get_method(fp, obj, p);
	    if (method) {
		name = ident_get(p);
		object_add_method(obj, name, method);
		method_discard(method);
		ident_discard(name);
	    }
	}
	string_discard(line);
    }

    if (obj)
	cache_discard(obj);
    list_discard(parents);
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
		write_log("$%I %s: %S", obj->dbref, name,
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

