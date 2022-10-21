/* object.h: Declarations for objects. */

#ifndef OBJECT_H
#define OBJECT_H
#include <stdio.h>
#include "data.h"

typedef struct object		Object;
typedef struct string_entry	String_entry;
typedef struct ident_entry	Ident_entry;
typedef struct var		Var;
typedef struct method		Method;
typedef struct error_list	Error_list;

struct object {
    List *parents;
    List *children;

    /* Variables are stored in a table, with index threads starting at the
     * hash table entries.  There is also an index thread for blanks.  This
     * way we can write the hash table to disk easily. */
    struct {
	Var *tab;
	int *hashtab;
	int blanks;
	int size;
    } vars;

    /* Methods are also stored in a table.  Since methods are fairly big, we
     * store a table of pointers to methods so that we don't waste lots of
     * space. */
    struct {
	struct mptr {
	    Method *m;
	    int next;
	} *tab;
	int *hashtab;
	int blanks;
	int size;
    } methods;

    /* Table for string references in methods. */
    String_entry *strings;
    int num_strings;
    int strings_size;

    /* Table for identifier references in methods. */
    Ident_entry	*idents;
    int num_idents;
    int idents_size;

    /* Information for the cache. */
    long dbref;
    int refs;
    char dirty;			/* Flag: Object has been modified. */
    char dead;			/* Flag: Object has been destroyed. */

    long search;		/* Last search to visit object. */

    /* Pointers to next and previous objects in cache chain. */
    Object *next;
    Object *prev;
};

/* We keep a ref count on object string entries because we have to know when
 * to delete it from the object.  As far as the string is concerned, all these
 * references are really just one reference, since we only duplicate or discard
 * when we're adding or removing a string from an object. */
struct string_entry {
    String *str;
    int refs;
};

/* Similar logic for identifier references. */
struct ident_entry {
    long id;
    int refs;
};

struct var {
    long name;
    long class;
    Data val;
    int next;
};

struct method {
    int name;
    Object *object;
    int num_args;
    int *argnames;
    long rest;
    int num_vars;
    int *varnames;
    int num_opcodes;
    long *opcodes;
    int num_error_lists;
    Error_list *error_lists;
    int overridable;
    int refs;
};

struct error_list {
    int num_errors;
    int *error_ids;
};

Object *object_new(long dbref, List *parents);
void object_free(Object *object);
void object_destroy(Object *object);

void object_construct_ancprec(Object *object);

int object_change_parents(Object *object, Sublist *parents);
List *object_ancestors(long dbref);
int object_has_ancestor(long dbref, long ancestor);
void object_reconstruct_descendent_ancprec(long dbref);

int object_add_string(Object *object, String *string);
void object_discard_string(Object *object, int ind);
String *object_get_string(Object *object, int ind);

int object_add_ident(Object *object, char *ident);
void object_discard_ident(Object *object, int ind);
long object_get_ident(Object *object, int ind);

long object_add_param(Object *object, long name);
long object_del_param(Object *object, long name);
long object_assign_var(Object *object, Object *class, long name, Data *val);
long object_retrieve_var(Object *object, Object *class, long name, Data *ret);
void object_put_var(Object *object, long class, long name, Data *val);

Method *object_find_method(long dbref, long name);
Method *object_find_next_method(long dbref, long name, long after);
void object_add_method(Object *object, long name, Method *method);
int object_del_method(Object *object, long name);
List *object_list_method(Object *object, long name, int indent, int parens);
void method_free(Method *method);
Method *method_grab(Method *method);
void method_discard(Method *method);

void object_text_dump(long dbref, FILE *fp);

#endif

