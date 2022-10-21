/* object.c: Object manipulation routines. */

#define _POSIX_SOURCE

#include <stdio.h>
#include <string.h>
#include "x.tab.h"
#include "object.h"
#include "data.h"
#include "memory.h"
#include "opcodes.h"
#include "cache.h"
#include "io.h"
#include "ident.h"
#include "cmstring.h"
#include "decode.h"
#include "util.h"
#include "log.h"

/* We use MALLOC_DELTA to keep table sizes to 32 bytes less than a power of
 * two, if pointers and longs are four bytes. */
#define MALLOC_DELTA		8
#define ANCTEMP_STARTING_SIZE	(32 - MALLOC_DELTA)
#define VAR_STARTING_SIZE	(16 - MALLOC_DELTA - 1)
#define METHOD_STARTING_SIZE	(16 - MALLOC_DELTA - 1)
#define STRING_STARTING_SIZE	(16 - MALLOC_DELTA)
#define IDENTS_STARTING_SIZE	(16 - MALLOC_DELTA)
#define METHOD_CACHE_SIZE	503

/* Data for method searches. */
typedef struct search_params Search_params;

struct search_params {
    unsigned long name;
    long stop_at;
    int done;
    Method *last_method_found;
};

struct {
    long stamp;
    long dbref;
    long name;
    long after;
    long loc;
} method_cache[METHOD_CACHE_SIZE];

static void object_update_parents(Object *object,
				  List *(*list_op)(List *, Data *));
static List *object_ancestors_aux(long dbref, List *ancestors);
static int object_has_ancestor_aux(long dbref, long ancestor);
static Var *object_create_var(Object *object, long class, long name);
static Var *object_find_var(Object *object, long class, long name);
static Method *object_find_method_local(Object *object, long name);
static Method *method_cache_check(long dbref, long name, long after);
static void method_cache_set(long dbref, long name, long after, long loc);
static void search_object(long dbref, Search_params *params);
static void method_delete_code_refs(Method *method);
static void object_text_dump_aux(Object *obj, FILE *fp);

/* Count for keeping track of of already-searched objects during searches. */
int cur_search;

/* Validity count for method cache (incrementing this count invalidates all
 * cache entries. */
static int cur_stamp = 1;

/* Error-checking on parents is the job of the calling function. */
Object *object_new(long dbref, List *parents)
{
    Object *new;
    int i;

    new = cache_get_holder(dbref);
    new->parents = list_dup(parents);
    new->children = list_new(0);

    /* Initialize variables table and hash table. */
    new->vars.tab = EMALLOC(Var, VAR_STARTING_SIZE);
    new->vars.hashtab = EMALLOC(int, VAR_STARTING_SIZE);
    new->vars.blanks = 0;
    new->vars.size = VAR_STARTING_SIZE;
    for (i = 0; i < VAR_STARTING_SIZE; i++) {
	new->vars.hashtab[i] = -1;
	new->vars.tab[i].name = -1;
	new->vars.tab[i].next = i + 1;
    }
    new->vars.tab[VAR_STARTING_SIZE - 1].next = -1;

    /* Initialize methods table and hash table. */
    new->methods.tab = EMALLOC(struct mptr, METHOD_STARTING_SIZE);
    new->methods.hashtab = EMALLOC(int, METHOD_STARTING_SIZE);
    new->methods.blanks = 0;
    new->methods.size = METHOD_STARTING_SIZE;
    for (i = 0; i < METHOD_STARTING_SIZE; i++) {
	new->methods.hashtab[i] = -1;
	new->methods.tab[i].m = NULL;
	new->methods.tab[i].next = i + 1;
    }
    new->methods.tab[METHOD_STARTING_SIZE - 1].next = -1;

    /* Initialize string table. */
    new->strings = EMALLOC(String_entry, STRING_STARTING_SIZE);
    new->strings_size = STRING_STARTING_SIZE;
    new->num_strings = 0;

    /* Initialize identifier table. */
    new->idents = EMALLOC(Ident_entry, IDENTS_STARTING_SIZE);
    new->idents_size = IDENTS_STARTING_SIZE;
    new->num_idents = 0;

    /* Add this object to the children list of parents. */
    object_update_parents(new, list_add);

    return new;
}

/* Free the data on an object, as when we're swapping it out.  Since the object
 * probably still exists on disk, we don't free parent and child references;
 * also, we don't free code references on methods because that would make
 * swapping too difficult. */
void object_free(Object *object)
{
    int i;

    /* Free parents and children list. */
    list_discard(object->parents);
    list_discard(object->children);

    /* Free variable names and contents. */
    for (i = 0; i < object->vars.size; i++) {
	if (object->vars.tab[i].name != -1) {
	    ident_discard(object->vars.tab[i].name);
	    data_discard(&object->vars.tab[i].val);
	}
    }
    free(object->vars.tab);
    free(object->vars.hashtab);

    /* Free methods. */
    for (i = 0; i < object->methods.size; i++) {
	if (object->methods.tab[i].m)
	    method_free(object->methods.tab[i].m);
    }
    free(object->methods.tab);
    free(object->methods.hashtab);

    /* Discard strings. */
    for (i = 0; i < object->num_strings; i++) {
	if (object->strings[i].str)
	    string_discard(object->strings[i].str);
    }
    free(object->strings);
}

/* Free everything on the object, update parents and descendents, etc.  The
 * object is really going to be gone.  We don't want anything left, except for
 * the structure it came in, which belongs to the cache. */
void object_destroy(Object *object)
{
    List *children;
    Object *kid;
    int i;
    Data d;

    /* Invalidate the method cache. */
    cur_stamp++;

    /* Tell parents we don't exist any more. */
    object_update_parents(object, list_delete_element);

    /* Tell children the same thing (no function for this, just do it).
     * Also, check if any kid hits zero parents, and reparent it to our
     * parents if it does. */
    children = object->children;
    d.type = DBREF;
    d.u.dbref = object->dbref;
    for (i = 0; i < children->len; i++) {
	kid = cache_retrieve(children->el[i].u.dbref);
	kid->parents = list_delete_element(kid->parents, &d);
	if (!kid->parents->len) {
	    list_discard(kid->parents);
	    kid->parents = list_dup(object->parents);
	    object_update_parents(kid, list_add);
	}
	kid->dirty = 1;
	cache_discard(kid);
    }

    /* Boot all connections on this object. */
    boot(object->dbref);

    /* Having freed all the stuff we don't normally free, free the stuff that
     * we do normally free. */
    object_free(object);
}

static void object_update_parents(Object *object,
				  List *(*list_op)(List *, Data *))
{
    int i;
    Object *p;
    List *parents;
    Data d;

    /* Make a data structure for the children list. */
    d.type = DBREF;
    d.u.dbref = object->dbref;

    parents = object->parents;
    for (i = 0; i < parents->len; i++) {
	p = cache_retrieve(parents->el[i].u.dbref);
	p->children = (*list_op)(p->children, &d);
	p->dirty = 1;
	cache_discard(p);
    }
}

List *object_ancestors(long dbref)
{
    List *ancestors;
    int i, len;
    long id;

    /* Get the ancestor list, backwards. */
    ancestors = list_new(0);
    cur_search++;
    ancestors = object_ancestors_aux(dbref, ancestors);

    /* Reverse the list. */
    len = ancestors->len;
    for (i = 0; i < len / 2; i++) {
	id = ancestors->el[i].u.dbref;
	ancestors->el[i].u.dbref = ancestors->el[len - i - 1].u.dbref;
	ancestors->el[len - i - 1].u.dbref = id;
    }

    return ancestors;
}

/* Modifies ancestors.  Returns a backwards list. */
static List *object_ancestors_aux(long dbref, List *ancestors)
{
    Object *object;
    List *parents;
    Data d;
    int i;

    object = cache_retrieve(dbref);
    if (object->search == cur_search) {
	cache_discard(object);
	return ancestors;
    }
    object->dirty = 1;
    object->search = cur_search;

    parents = list_dup(object->parents);
    cache_discard(object);

    for (i = parents->len - 1; i >= 0; i--)
	ancestors = object_ancestors_aux(parents->el[i].u.dbref, ancestors);
    list_discard(parents);

    d.type = DBREF;
    d.u.dbref = dbref;
    return list_add(ancestors, &d);
}

int object_has_ancestor(long dbref, long ancestor)
{
    cur_search++;
    return object_has_ancestor_aux(dbref, ancestor);
}

static int object_has_ancestor_aux(long dbref, long ancestor)
{
    Object *object;
    List *parents;
    int i;

    object = cache_retrieve(dbref);

    /* Don't search an object twice. */
    if (object->search == cur_search) {
	cache_discard(object);
	return 0;
    }
    object->dirty = 1;
    object->search = cur_search;

    parents = list_dup(object->parents);
    cache_discard(object);

    for (i = 0; i < parents->len; i++) {
	if (parents->el[i].u.dbref == ancestor) {
	    list_discard(parents);
	    return 1;
	}
    }

    for (i = 0; i < parents->len; i++) {
	if (object_has_ancestor_aux(dbref, ancestor)) {
	    list_discard(parents);
	    return 1;
	}
    }

    list_discard(parents);
    return 0;
}

int object_change_parents(Object *object, Sublist *parents)
{
    int i;
    long pdbref;

    /* Make sure that all parents are valid objects, and that they don't create
     * any cycles.  If something is wrong, return the index of the parent that
     * caused the problem. */
    for (i = parents->start; i < parents->start + parents->span; i++) {
	if (parents->list->el[i].type != DBREF)
	    return i;
	pdbref = parents->list->el[i].u.dbref;
	if (pdbref == object->dbref || !cache_check(pdbref)
	    || object_has_ancestor(pdbref, object->dbref))
	    return i;
    }

    /* Invalidate the method cache. */
    cur_stamp++;

    /* Tell our old parents that we're no longer a kid, and discard the old
     * parents list. */
    object_update_parents(object, list_delete_element);
    list_discard(object->parents);

    /* Set the object's parents list to a copy of the new list, and tell all
     * our new parents that we're a kid. */
    object->parents = list_from_sublist(parents);
    object_update_parents(object, list_add);

    /* Return -1, meaning that all the parents were okay. */
    return -1;
}

int object_add_string(Object *object, String *string)
{
    int i, blank = -1;

    /* Get the object dirty now, so we can return with a clean conscience. */
    object->dirty = 1;

    /* Look for blanks while checking for an equivalent string. */
    for (i = 0; i < object->num_strings; i++) {
	if (!object->strings[i].str) {
	    blank = i;
	} else if (strcmp(string->s, object->strings[i].str->s) == 0) {
	    object->strings[i].refs++;
	    return i;
	}
    }

    /* Fill in a blank if we found one. */
    if (blank != -1) {
	object->strings[blank].str = string_dup(string);
	object->strings[blank].refs = 1;
	return blank;
    }

    /* Check to see if we have to enlarge the table. */
    if (i >= object->strings_size) {
	object->strings_size = object->strings_size * 2 + MALLOC_DELTA;
	object->strings = EREALLOC(object->strings, String_entry,
				   object->strings_size);
    }

    /* Add the string to the end of the table. */
    object->strings[i].str = string_dup(string);
    object->strings[i].refs = 1;
    object->num_strings++;

    return i;
}

void object_discard_string(Object *object, int ind)
{
    object->strings[ind].refs--;
    if (!object->strings[ind].refs) {
	string_discard(object->strings[ind].str);
	object->strings[ind].str = NULL;
    }

    object->dirty = 1;
}

String *object_get_string(Object *object, int ind)
{
    return object->strings[ind].str;
}

int object_add_ident(Object *object, char *ident)
{
    int i, blank = -1;
    long id;

    /* Mark the object dirty, since we will modify it in all cases. */
    object->dirty = 1;

    /* Get an identifier for the identifier string. */
    id = ident_get(ident);

    /* Look for blanks while checking for an equivalent identifier. */
    for (i = 0; i < object->num_idents; i++) {
	if (object->idents[i].id == -1) {
	    blank = i;
	} else if (object->idents[i].id == id) {
	    /* We already have this id.  Up the reference count on the object's
	     * copy if it, discard this function's copy of it, and return the
	     * index into the object's identifier table. */
	    object->idents[i].refs++;
	    ident_discard(id);
	    return i;
	}
    }

    /* Fill in a blank if we found one. */
    if (blank != -1) {
	object->idents[blank].id = id;
	object->idents[blank].refs = 1;
	return blank;
    }

    /* Check to see if we have to enlarge the table. */
    if (i >= object->idents_size) {
	object->idents_size = object->idents_size * 2 + MALLOC_DELTA;
	object->idents = EREALLOC(object->idents, Ident_entry,
				  object->idents_size);
    }

    /* Add the string to the end of the table. */
    object->idents[i].id = id;
    object->idents[i].refs = 1;
    object->num_idents++;

    return i;
}

void object_discard_ident(Object *object, int ind)
{
    object->idents[ind].refs--;
    if (!object->idents[ind].refs) {
	ident_discard(object->idents[ind].id);
	object->idents[ind].id = -1;
    }

    object->dirty = 1;
}

long object_get_ident(Object *object, int ind)
{
    return object->idents[ind].id;
}

long object_add_param(Object *object, long name)
{
    if (object_find_var(object, object->dbref, name))
	return paramexists_id;
    object_create_var(object, object->dbref, name);
    return NOT_AN_IDENT;
}

long object_del_param(Object *object, long name)
{
    int *indp;
    Var *var;

    /* This is the index-thread equivalent of double pointers in a standard
     * linked list.  We traverse the list using pointers to the ->next element
     * of the variables. */
    indp = &object->vars.hashtab[hash(ident_name(name)) % object->vars.size];
    for (; *indp != -1; indp = &object->vars.tab[*indp].next) {
	var = &object->vars.tab[*indp];
	if (var->name == name && var->class == object->dbref) {
	    ident_discard(var->name);
	    ident_discard(var->class);
	    data_discard(&var->val);
	    var->name = -1;

	    /* Remove ind from hash table thread, and add it to blanks
	     * thread. */
	    *indp = var->next;
	    var->next = object->vars.blanks;
	    object->vars.blanks = var - object->vars.tab;

	    object->dirty = 1;
	    return NOT_AN_IDENT;
	}
    }

    return paramnf_id;
}

long object_assign_var(Object *object, Object *class, long name, Data *val)
{
    Var *var;

    /* Make sure variable exists in class (method object). */
    if (!object_find_var(class, class->dbref, name))
	return paramnf_id;

    /* Get variable slot on object, creating it if necessary. */
    var = object_find_var(object, class->dbref, name);
    if (!var)
	var = object_create_var(object, class->dbref, name);

    data_discard(&var->val);
    data_dup(&var->val, val);

    return NOT_AN_IDENT;
}

long object_retrieve_var(Object *object, Object *class, long name, Data *ret)
{
    Var *var;

    /* Make sure variable exists on class. */
    if (!object_find_var(class, class->dbref, name))
	return paramnf_id;

    var = object_find_var(object, class->dbref, name);
    if (var) {
	data_dup(ret, &var->val);
    } else {
	ret->type = INTEGER;
	ret->u.val = 0;
    }

    return NOT_AN_IDENT;
}

/* Only the text dump reader calls this function; it assigns or creates a
 * variable as needed, and always succeeds. */
void object_put_var(Object *object, long class, long name, Data *val)
{
    Var *var;

    var = object_find_var(object, class, name);
    if (!var)
	var = object_create_var(object, class, name);
    data_discard(&var->val);
    data_dup(&var->val, val);
}

/* Add a variable to an object. */
static Var *object_create_var(Object *object, long class, long name)
{
    Var *new;
    int ind;

    /* If the variable table is full, expand it and its corresponding hash
     * table. */
    if (object->vars.blanks == -1) {
	int new_size, i;

	/* Compute new size and resize tables. */
	new_size = object->vars.size * 2 + MALLOC_DELTA + 1;
	object->vars.tab = EREALLOC(object->vars.tab, Var, new_size);
	object->vars.hashtab = EREALLOC(object->vars.hashtab, int, new_size);

	/* Refill hash table. */
	for (i = 0; i < new_size; i++)
	    object->vars.hashtab[i] = -1;
	for (i = 0; i < object->vars.size; i++) {
	    ind = hash(ident_name(object->vars.tab[i].name)) % new_size;
	    object->vars.tab[i].next = object->vars.hashtab[ind];
	    object->vars.hashtab[ind] = i;
	}

	/* Create new thread of blanks, setting names to -1. */
	for (i = object->vars.size; i < new_size; i++) {
	    object->vars.tab[i].name = -1;
	    object->vars.tab[i].next = i + 1;
	}
	object->vars.tab[new_size - 1].next = -1;
	object->vars.blanks = object->vars.size;

	object->vars.size = new_size;
    }

    /* Add variable at first blank. */
    new = &object->vars.tab[object->vars.blanks];
    object->vars.blanks = new->next;

    /* Fill in new variable. */
    new->name = ident_dup(name);
    new->class = ident_dup(class);
    new->val.type = INTEGER;
    new->val.u.val = 0;

    /* Add variable to hash table thread. */
    ind = hash(ident_name(name)) % object->vars.size;
    new->next = object->vars.hashtab[ind];
    object->vars.hashtab[ind] = new - object->vars.tab;

    object->dirty = 1;
    return new;
}

/* Look for a variable on an object. */
static Var *object_find_var(Object *object, long class, long name)
{
    int ind;
    Var *var;

    /* Traverse hash table thread, stopping if we get a match on the name. */
    ind = object->vars.hashtab[hash(ident_name(name)) % object->vars.size];
    for (; ind != -1; ind = object->vars.tab[ind].next) {
	var = &object->vars.tab[ind];
	if (var->name == name && var->class == class)
	    return var;
    }

    return NULL;
}

/* Reference-counting kludge: on return, the method's object field has an extra
 * reference count, in order to keep it in cache.  dbref must be valid. */
Method *object_find_method(long dbref, long name)
{
    Search_params params;
    Object *object;
    Method *method, *local_method;
    List *parents;
    int i;

    /* Look for cached value. */
    method = method_cache_check(dbref, name, -1);
    if (method)
	return method;

    object = cache_retrieve(dbref);
    parents = list_dup(object->parents);
    cache_discard(object);

    /* If the object has only one parent, call this function recursively. */
    if (parents->len == 1) {
	method = object_find_method(parents->el[0].u.dbref, name);
    } else {
	/* We've hit a bulge; resort to the reverse depth-first search. */
	cur_search++;
	params.name = name;
	params.stop_at = -1;
	params.done = 0;
	params.last_method_found = NULL;
	for (i = parents->len - 1; i >= 0 && !params.done; i--)
	    search_object(parents->el[i].u.dbref, &params);
	method = params.last_method_found;
    }

    list_discard(parents);

    if (!method || method->overridable) {
	/* We didn't find a non-overridable method; check for a method on the
	 * current object. */
	object = cache_retrieve(dbref);
	local_method = object_find_method_local(object, name);
	if (local_method) {
	    if (method)
		cache_discard(method->object);
	    method = local_method;
	} else {
	    cache_discard(object);
	}
    }

    if (method)
	method_cache_set(dbref, name, -1, method->object->dbref);
    return method;
}

/* Reference-counting kludge: on return, the method's object field has an extra
 * reference count, in order to keep it in cache.  dbref must be valid. */
Method *object_find_next_method(long dbref, long name, long after)
{
    Search_params params;
    Object *object;
    Method *method;
    long parent;
    int i;

    /* Check cache. */
    method = method_cache_check(dbref, name, after);
    if (method)
	return method;

    object = cache_retrieve(dbref);

    if (object->parents->len == 1) {
	/* Object has only one parent; search recursively. */
	parent = object->parents->el[0].u.dbref;
	cache_discard(object);
	if (dbref == after)
	    method = object_find_method(parent, name);
	else
	    method = object_find_next_method(parent, name, after);
    } else {
	/* Object has more than one parent; use complicated search. */
	cur_search++;
	params.name = name;
	params.stop_at = (dbref == after) ? -1 : after;
	params.done = 0;
	params.last_method_found = NULL;
	for (i = object->parents->len - 1; i >= 0 && !params.done; i--)
	    search_object(object->parents->el[i].u.dbref, &params);

	cache_discard(object);
	method = params.last_method_found;
    }

    if (method)
	method_cache_set(dbref, name, after, method->object->dbref);
    return method;
}

/* Perform a reverse depth-first traversal of this object and its ancestors
 * with no repeat visits, thus searching ancestors before children and
 * searching parents right-to-left.  We will take the last method we find,
 * possibly stopping at a method if we were looking for the next method after
 * a given method. */
static void search_object(long dbref, Search_params *params)
{
    Object *object;
    Method *method;
    List *parents;
    int i;

    object = cache_retrieve(dbref);

    /* Don't search an object twice. */
    if (object->search == cur_search) {
	cache_discard(object);
	return;
    }
    object->dirty = 1;
    object->search = cur_search;

    /* Grab the parents list and discard the object. */
    parents = list_dup(object->parents);
    cache_discard(object);

    /* Traverse the parents list backwards. */
    for (i = parents->len - 1; i >= 0 && !params->done; i--)
	search_object(parents->el[i].u.dbref, params);
    list_discard(object->parents);

    /* If the search is done, don't visit this object. */
    if (params->done)
	return;

    /* If we were searching for a next method after a given object, then this
     * might be the given object, in which case we should stop. */
    if (dbref == params->stop_at) {
	params->done = 1;
	return;
    }

    /* Visit this object.  First, get it back from the cache. */
    object = cache_retrieve(dbref);
    method = object_find_method_local(object, params->name);
    if (method) {
	/* We found a method on this object.  Discard the reference count on
	 * the last method found's object, if we have one, and set this method
	 * as the last one found.  Leave object's reference count there, since
	 * we don't want it to get swapped out. */
	if (params->last_method_found)
	    cache_discard(params->last_method_found->object);
	params->last_method_found = method;

	/* If this method is non-overridable, the search is done. */
	if (!method->overridable)
	    params->done = 1;
    } else {
	cache_discard(object);
    }
}

/* Look for a method on an object. */
static Method *object_find_method_local(Object *object, long name)
{
    int ind, method;

    /* Traverse hash table thread, stopping if we get a match on the name. */
    ind = hash(ident_name(name)) % object->methods.size;
    method = object->methods.hashtab[ind];
    for (; method != -1; method = object->methods.tab[method].next) {
	if (object->methods.tab[method].m->name == name)
	    return object->methods.tab[method].m;
    }

    return NULL;
}

static Method *method_cache_check(long dbref, long name, long after)
{
    Object *object;
    int i;

    i = (dbref * name + after) % METHOD_CACHE_SIZE;
    if (method_cache[i].stamp == cur_stamp && method_cache[i].dbref == dbref &&
	method_cache[i].name == name && method_cache[i].after == after &&
	method_cache[i].loc != -1) {
	object = cache_retrieve(method_cache[i].loc);
	return object_find_method_local(object, name);
    } else {
	return NULL;
    }
}

static void method_cache_set(long dbref, long name, long after, long loc)
{
    int i;

    i = (dbref * name + after) % METHOD_CACHE_SIZE;
    if (method_cache[i].stamp != 0) {
	ident_discard(method_cache[i].dbref);
	ident_discard(method_cache[i].name);
	if (method_cache[i].after != -1)
	    ident_discard(method_cache[i].after);
	ident_discard(method_cache[i].loc);
    }
    method_cache[i].stamp = cur_stamp;
    method_cache[i].dbref = ident_dup(dbref);
    method_cache[i].name = ident_dup(name);
    method_cache[i].after = (after == -1) ? -1 : ident_dup(after);
    method_cache[i].loc = ident_dup(loc);
}

void object_add_method(Object *object, long name, Method *method)
{
    int ind, hval;

    /* Invalidate the method cache. */
    cur_stamp++;

    /* Delete the method if it previous existed. */
    object_del_method(object, name);

    /* If the method table is full, expand it and its corresponding hash
     * table. */
    if (object->methods.blanks == -1) {
	int new_size, i, ind;

	/* Compute new size and resize tables. */
	new_size = object->methods.size * 2 + MALLOC_DELTA + 1;
	object->methods.tab = EREALLOC(object->methods.tab, struct mptr,
				       new_size);
	object->methods.hashtab = EREALLOC(object->methods.hashtab, int,
					   new_size);

	/* Refill hash table. */
	for (i = 0; i < new_size; i++)
	    object->methods.hashtab[i] = -1;
	for (i = 0; i < object->methods.size; i++) {
	    ind = hash(ident_name(object->methods.tab[i].m->name)) % new_size;
	    object->methods.tab[i].next = object->methods.hashtab[ind];
	    object->methods.hashtab[ind] = i;
	}

	/* Create new thread of blanks and set method pointers to null. */
	for (i = object->methods.size; i < new_size; i++) {
	    object->methods.tab[i].m = NULL;
	    object->methods.tab[i].next = i + 1;
	}
	object->methods.tab[new_size - 1].next = -1;
	object->methods.blanks = object->methods.size;

	object->methods.size = new_size;
    }

    method->object = object;
    method->name = ident_dup(name);

    /* Add method at first blank. */
    ind = object->methods.blanks;
    object->methods.blanks = object->methods.tab[ind].next;
    object->methods.tab[ind].m = method_grab(method);

    /* Add method to hash table thread. */
    hval = hash(ident_name(name)) % object->methods.size;
    object->methods.tab[ind].next = object->methods.hashtab[hval];
    object->methods.hashtab[hval] = ind;

    object->dirty = 1;
}

int object_del_method(Object *object, long name)
{
    int *indp, ind;

    /* Invalidate the method cache. */
    cur_stamp++;

    /* This is the index-thread equivalent of double pointers in a standard
     * linked list.  We traverse the list using pointers to the ->next element
     * of the method pointers. */
    ind = hash(ident_name(name)) % object->methods.size;
    indp = &object->methods.hashtab[ind];
    for (; *indp != -1; indp = &object->methods.tab[*indp].next) {
	ind = *indp;
	if (object->methods.tab[ind].m->name == name) {
	    /* We found the method; discard it. */
	    method_discard(object->methods.tab[ind].m);
	    object->methods.tab[ind].m = NULL;

	    /* Remove ind from the hash table thread, and add it to the blanks
	     * thread. */
	    *indp = object->methods.tab[ind].next;
	    object->methods.tab[ind].next = object->methods.blanks;
	    object->methods.blanks = ind;

	    object->dirty = 1;

	    /* Return one, meaning the method was successfully deleted. */
	    return 1;
	}
    }

    /* Return zero, meaning no method was found to delete. */
    return 0;
}

List *object_list_method(Object *object, long name, int indent, int parens)
{
    Method *method;

    method = object_find_method_local(object, name);
    return (method) ? decompile(method, object, indent, parens) : NULL;
}

/* Destroys a method.  Does not delete references from the method's code. */
void method_free(Method *method)
{
    int i, j;
    Error_list *elist;

    if (method->name != -1)
	ident_discard(method->name);
    if (method->num_args)
	TFREE(method->argnames, method->num_args);
    if (method->num_vars)
	TFREE(method->varnames, method->num_vars);
    TFREE(method->opcodes, method->num_opcodes);
    if (method->num_error_lists) {
	/* Discard identifiers held in the method's error lists. */
	for (i = 0; i < method->num_error_lists; i++) {
	    elist = &method->error_lists[i];
	    for (j = 0; j < elist->num_errors; j++)
		ident_discard(elist->error_ids[j]);
	}
	TFREE(method->error_lists, method->num_error_lists);
    }
    free(method);
}

/* Delete references to object variables and strings in a method's code. */
void method_delete_code_refs(Method *method)
{
    int i, j, arg_type, opcode;
    Op_info *info;

    for (i = 0; i < method->num_args; i++)
	object_discard_ident(method->object, method->argnames[i]);
    if (method->rest != -1)
	object_discard_ident(method->object, method->rest);

    for (i = 0; i < method->num_vars; i++)
	object_discard_ident(method->object, method->varnames[i]);

    i = 0;
    while (i < method->num_opcodes) {
	opcode = method->opcodes[i];

	/* Use opcode info table for anything else. */
	info = &op_table[opcode];
	for (j = 0; j < 2; j++) {
	    arg_type = (j == 0) ? info->arg1 : info->arg2;
	    if (arg_type) {
		i++;
		switch (arg_type) {

		  case STRING:
		    object_discard_string(method->object, method->opcodes[i]);
		    break;

		  case IDENT:
		    object_discard_ident(method->object, method->opcodes[i]);
		    break;

		}
	    }
	}
	i++;
    }

}

Method *method_grab(Method *method)
{
    method->refs++;
    return method;
}

void method_discard(Method *method)
{
    method->refs--;
    if (!method->refs) {
	method_delete_code_refs(method);
	method_free(method);
    }
}

void object_text_dump(long dbref, FILE *fp)
{
    Object *obj;
    List *parents;
    int i;

    obj = cache_retrieve(dbref);

    /* Don't dump an object twice. */
    if (obj->search == cur_search) {
	cache_discard(obj);
	return;
    }
    obj->dirty = 1;
    obj->search = cur_search;

    /* Pick up a copy of the dbref and parents list, and forget the object. */
    parents = list_dup(obj->parents);
    cache_discard(obj);

    /* Dump any parents which haven't already been dumped. */
    for (i = 0; i < parents->len; i++)
	object_text_dump(parents->el[i].u.dbref, fp);

    /* Now discard the parents list and retrieve the object again. */
    list_discard(parents);
    obj = cache_retrieve(dbref);

    /* Write the object out, finally. */
    object_text_dump_aux(obj, fp);

    cache_discard(obj);
}

static void object_text_dump_aux(Object *obj, FILE *fp)
{
    String *str;
    List *code;
    int i, j;
    Var *var;

    /* Output parents. */
    for (i = 0; i < obj->parents->len; i++)
	fformat(fp, "parent %s\n", ident_name(obj->parents->el[i].u.dbref));

    /* Output creation command. */
    fformat(fp, "object %s\n", ident_name(obj->dbref));

    /* Output commands to set obj variables. */
    for (i = 0; i < obj->vars.size; i++) {
	var = &obj->vars.tab[i];
	if (var->name == -1)
	    continue;
	str = data_to_literal(&var->val);
	fformat(fp, "var %I %I %s\n", var->class, var->name, str->s);
	string_discard(str);
    }

    /* Output method definitions. */
    for (i = 0; i < obj->methods.size; i++) {
	if (!obj->methods.tab[i].m)
	    continue;
	fformat(fp, "method %s\n", ident_name(obj->methods.tab[i].m->name));
	code = decompile(obj->methods.tab[i].m, obj, 4, 1);
	for (j = 0; j < code->len; j++) {
	    fputs(code->el[j].u.substr.str->s, fp);
	    putc('\n', fp);
	}
	list_discard(code);
	fputs(".\n", fp);
    }
}

