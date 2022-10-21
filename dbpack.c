/* dbpack.c: Write and retrieve objects to disk. */

#define _POSIX_SOURCE

#include <stdio.h>
#include <string.h>
#include "x.tab.h"
#include "dbpack.h"
#include "object.h"
#include "data.h"
#include "memory.h"
#include "cmstring.h"
#include "ident.h"

static void pack_list(Data *base, int len, FILE *fp);
static void pack_dict(Dict *dict, FILE *fp);
static void pack_data(Data *data, FILE *fp);
static void pack_vars(Object *obj, FILE *fp);
static void pack_methods(Object *obj, FILE *fp);
static void pack_method(Method *method, FILE *fp);
static void pack_strings(Object *obj, FILE *fp);
static void pack_idents(Object *obj, FILE *fp);

static List *unpack_list(FILE *fp);
static Dict *unpack_dict(FILE *fp);
static void unpack_data(Data *data, FILE *fp);
static void unpack_vars(Object *obj, FILE *fp);
static void unpack_methods(Object *obj, FILE *fp);
static Method *unpack_method(FILE *fp);
static void unpack_strings(Object *obj, FILE *fp);
static void unpack_idents(Object *obj, FILE *fp);

static int size_list(Data *base, int len);
static int size_dict(Dict *dict);
static int size_data(Data *data);
static int size_vars(Object *obj);
static int size_methods(Object *obj);
static int size_method(Method *method);
static int size_strings(Object *obj);
static int size_idents(Object *obj);

static void write_ident(long id, FILE *fp);
static long read_ident(FILE *fp);
static long size_ident(long id);

static void write_long(long n, FILE *fp);
static long read_long(FILE *fp);
static int size_long(long n);

void pack_object(Object *obj, FILE *fp)
{
    pack_list(obj->parents->el, obj->parents->len, fp);
    pack_list(obj->children->el, obj->children->len, fp);
    pack_vars(obj, fp);
    pack_methods(obj, fp);
    pack_strings(obj, fp);
    pack_idents(obj, fp);
    write_long(obj->search, fp);
}

static void pack_list(Data *base, int len, FILE *fp)
{
    int i;

    write_long(len, fp);
    for (i = 0; i < len; i++)
	pack_data(&base[i], fp);
}

static void pack_dict(Dict *dict, FILE *fp)
{
    int i;

    pack_list(dict->keys->el, dict->keys->len, fp);
    pack_list(dict->values->el, dict->values->len, fp);
    write_long(dict->hashtab_size, fp);
    for (i = 0; i < dict->hashtab_size; i++) {
	write_long(dict->links[i], fp);
	write_long(dict->hashtab[i], fp);
    }
}

static void pack_data(Data *data, FILE *fp)
{
    write_long(data->type, fp);
    switch (data->type) {

      case INTEGER:
	write_long(data->u.val, fp);
	break;

      case STRING:
	write_long(data->u.substr.span, fp);
	fwrite(data_sptr(data), sizeof(char), data->u.substr.span, fp);
	break;

      case DBREF:
	write_ident(data->u.dbref, fp);
	break;

      case LIST:
	pack_list(data_dptr(data), data->u.sublist.span, fp);
	break;

      case SYMBOL:
	write_ident(data->u.symbol, fp);
	break;

      case ERROR:
	write_ident(data->u.error, fp);
	break;

      case FROB:
	write_ident(data->u.frob.class, fp);
	write_long(data->u.frob.rep_type, fp);
	if (data->u.frob.rep_type == LIST) {
	    pack_list(data->u.frob.rep.list->el, data->u.frob.rep.list->len,
		      fp);
	} else {
	    pack_dict(data->u.frob.rep.dict, fp);
	}
	break;

      case DICT:
	pack_dict(data->u.dict, fp);
	break;
    }
}

static void pack_vars(Object *obj, FILE *fp)
{
    int i;

    write_long(obj->vars.size, fp);
    write_long(obj->vars.blanks, fp);

    for (i = 0; i < obj->vars.size; i++) {
	write_long(obj->vars.hashtab[i], fp);
	if (obj->vars.tab[i].name != NOT_AN_IDENT) {
	    write_ident(obj->vars.tab[i].name, fp);
	    write_ident(obj->vars.tab[i].class, fp);
	    pack_data(&obj->vars.tab[i].val, fp);
	} else {
	    write_long(NOT_AN_IDENT, fp);
	}
	write_long(obj->vars.tab[i].next, fp);
    }
}

static void pack_methods(Object *obj, FILE *fp)
{
    int i;

    write_long(obj->methods.size, fp);
    write_long(obj->methods.blanks, fp);

    for (i = 0; i < obj->methods.size; i++) {
	write_long(obj->methods.hashtab[i], fp);
	if (obj->methods.tab[i].m) {
	    pack_method(obj->methods.tab[i].m, fp);
	} else {
	    /* Method begins with name identifier; write NOT_AN_IDENT. */
	    write_long(NOT_AN_IDENT, fp);
	}
	write_long(obj->methods.tab[i].next, fp);
    }
}

static void pack_method(Method *method, FILE *fp)
{
    int i, j;

    write_ident(method->name, fp);

    write_long(method->num_args, fp);
    for (i = 0; i < method->num_args; i++)
	write_long(method->argnames[i], fp);
    write_long(method->rest, fp);

    write_long(method->num_vars, fp);
    for (i = 0; i < method->num_vars; i++)
	write_long(method->varnames[i], fp);

    write_long(method->num_opcodes, fp);
    for (i = 0; i < method->num_opcodes; i++)
	write_long(method->opcodes[i], fp);

    write_long(method->num_error_lists, fp);
    for (i = 0; i < method->num_error_lists; i++) {
	write_long(method->error_lists[i].num_errors, fp);
	for (j = 0; j < method->error_lists[i].num_errors; j++)
	    write_ident(method->error_lists[i].error_ids[j], fp);
    }

    write_long(method->overridable, fp);
}

static void pack_strings(Object *obj, FILE *fp)
{
    int i;
    String *str;

    write_long(obj->strings_size, fp);
    write_long(obj->num_strings, fp);
    for (i = 0; i < obj->num_strings; i++) {
	str = obj->strings[i].str;
	if (str) {
	    write_long(str->len, fp);
	    fwrite(str->s, sizeof(char), str->len, fp);
	    write_long(obj->strings[i].refs, fp);
	} else {
	    write_long(-1, fp);
	}
    }
}

static void pack_idents(Object *obj, FILE *fp)
{
    int i;

    write_long(obj->idents_size, fp);
    write_long(obj->num_idents, fp);
    for (i = 0; i < obj->num_idents; i++) {
	if (obj->idents[i].id != NOT_AN_IDENT) {
	    write_ident(obj->idents[i].id, fp);
	    write_long(obj->idents[i].refs, fp);
	} else {
	    write_long(NOT_AN_IDENT, fp);
	}
    }
}

void unpack_object(Object *obj, FILE *fp)
{
    obj->parents = unpack_list(fp);
    obj->children = unpack_list(fp);
    unpack_vars(obj, fp);
    unpack_methods(obj, fp);
    unpack_strings(obj, fp);
    unpack_idents(obj, fp);
    obj->search = read_long(fp);
}

static List *unpack_list(FILE *fp)
{
    int len, i;
    List *list;

    len = read_long(fp);
    list = list_new(len);
    for (i = 0; i < len; i++)
	unpack_data(&list->el[i], fp);
    return list;
}

static Dict *unpack_dict(FILE *fp)
{
    Dict *dict;
    int i;

    dict = EMALLOC(Dict, 1);
    dict->keys = unpack_list(fp);
    dict->values = unpack_list(fp);
    dict->hashtab_size = read_long(fp);
    dict->links = EMALLOC(int, dict->hashtab_size);
    dict->hashtab = EMALLOC(int, dict->hashtab_size);
    for (i = 0; i < dict->hashtab_size; i++) {
	dict->links[i] = read_long(fp);
	dict->hashtab[i] = read_long(fp);
    }
    dict->refs = 1;
    return dict;
}

static void unpack_data(Data *data, FILE *fp)
{
    data->type = read_long(fp);
    switch (data->type) {

      case INTEGER:
	data->u.val = read_long(fp);
	break;

      case STRING: {
	  String *str;
	  int len;

	  len = read_long(fp);
	  str = string_new(len);
	  fread(str->s, sizeof(char), len, fp);
	  str->s[len] = 0;
	  substr_set_to_full_string(&data->u.substr, str);
	  break;
      }

      case DBREF:
	data->u.dbref = read_ident(fp);
	break;

      case LIST:
	sublist_set_to_full_list(&data->u.sublist, unpack_list(fp));
	break;

      case SYMBOL:
	data->u.symbol = read_ident(fp);
	break;

      case ERROR:
	data->u.error = read_ident(fp);
	break;

      case FROB:
	data->u.frob.class = read_ident(fp);
	data->u.frob.rep_type = read_long(fp);
	if (data->u.frob.rep_type == LIST)
	    data->u.frob.rep.list = unpack_list(fp);
	else
	    data->u.frob.rep.dict = unpack_dict(fp);
	break;

      case DICT:
	data->u.dict = unpack_dict(fp);
	break;
    }
}

static void unpack_vars(Object *obj, FILE *fp)
{
    int i;

    obj->vars.size = read_long(fp);
    obj->vars.blanks = read_long(fp);

    obj->vars.hashtab = EMALLOC(int, obj->vars.size);
    obj->vars.tab = EMALLOC(Var, obj->vars.size);

    for (i = 0; i < obj->vars.size; i++) {
	obj->vars.hashtab[i] = read_long(fp);
	obj->vars.tab[i].name = read_ident(fp);
	if (obj->vars.tab[i].name != NOT_AN_IDENT) {
	    obj->vars.tab[i].class = read_ident(fp);
	    unpack_data(&obj->vars.tab[i].val, fp);
	}
	obj->vars.tab[i].next = read_long(fp);
    }
}

static void unpack_methods(Object *obj, FILE *fp)
{
    int i;

    obj->methods.size = read_long(fp);
    obj->methods.blanks = read_long(fp);

    obj->methods.hashtab = EMALLOC(int, obj->methods.size);
    obj->methods.tab = EMALLOC(struct mptr, obj->methods.size);

    for (i = 0; i < obj->methods.size; i++) {
	obj->methods.hashtab[i] = read_long(fp);
	obj->methods.tab[i].m = unpack_method(fp);
	if (obj->methods.tab[i].m)
	    obj->methods.tab[i].m->object = obj;
	obj->methods.tab[i].next = read_long(fp);
    }
}

static Method *unpack_method(FILE *fp)
{
    int name, i, j, n;
    Method *method;

    /* Read in the name.  If this is -1, it was a marker for a blank entry. */
    name = read_ident(fp);
    if (name == NOT_AN_IDENT)
	return NULL;

    method = EMALLOC(Method, 1);

    method->name = name;

    method->num_args = read_long(fp);
    if (method->num_args) {
	method->argnames = TMALLOC(int, method->num_args);
	for (i = 0; i < method->num_args; i++)
	    method->argnames[i] = read_long(fp);
    }
    method->rest = read_long(fp);

    method->num_vars = read_long(fp);
    if (method->num_vars) {
	method->varnames = TMALLOC(int, method->num_vars);
	for (i = 0; i < method->num_vars; i++)
	    method->varnames[i] = read_long(fp);
    }

    method->num_opcodes = read_long(fp);
    method->opcodes = TMALLOC(long, method->num_opcodes);
    for (i = 0; i < method->num_opcodes; i++)
	method->opcodes[i] = read_long(fp);

    method->num_error_lists = read_long(fp);
    if (method->num_error_lists) {
	method->error_lists = TMALLOC(Error_list, method->num_error_lists);
	for (i = 0; i < method->num_error_lists; i++) {
	    n = read_long(fp);
	    method->error_lists[i].num_errors = n;
	    method->error_lists[i].error_ids = TMALLOC(int, n);
	    for (j = 0; j < n; j++)
		method->error_lists[i].error_ids[j] = read_ident(fp);
	}
    }

    method->overridable = read_long(fp);

    method->refs = 1;
    return method;
}

static void unpack_strings(Object *obj, FILE *fp)
{
    int len, i;

    obj->strings_size = read_long(fp);
    obj->num_strings = read_long(fp);
    obj->strings = EMALLOC(String_entry, obj->strings_size);
    for (i = 0; i < obj->num_strings; i++) {
	len = read_long(fp);
	if (len == -1) {
	    obj->strings[i].str = NULL;
	} else {
	    obj->strings[i].str = string_new(len);
	    fread(obj->strings[i].str->s, sizeof(char), len, fp);
	    obj->strings[i].str->s[len] = 0;
	    obj->strings[i].refs = read_long(fp);
	}
    }
}

static void unpack_idents(Object *obj, FILE *fp)
{
    int i;

    obj->idents_size = read_long(fp);
    obj->num_idents = read_long(fp);
    obj->idents = EMALLOC(Ident_entry, obj->idents_size);
    for (i = 0; i < obj->num_idents; i++) {
	obj->idents[i].id = read_ident(fp);
	if (obj->idents[i].id != NOT_AN_IDENT)
	    obj->idents[i].refs = read_long(fp);
    }
}

int size_object(Object *obj)
{
    int size = 0;

    size = size_list(obj->parents->el, obj->parents->len);
    size += size_list(obj->children->el, obj->children->len);
    size += size_vars(obj);
    size += size_methods(obj);
    size += size_strings(obj);
    size += size_idents(obj);
    size += size_long(obj->search);
    return size;
}

static int size_list(Data *base, int len)
{
    int size = 0, i;

    size += size_long(len);
    for (i = 0; i < len; i++)
	size += size_data(&base[i]);
    return size;
}

static int size_dict(Dict *dict)
{
    int size = 0, i;

    size += size_list(dict->keys->el, dict->keys->len);
    size += size_list(dict->values->el, dict->values->len);
    size += size_long(dict->hashtab_size);
    for (i = 0; i < dict->hashtab_size; i++) {
	size += size_long(dict->links[i]);
	size += size_long(dict->hashtab[i]);
    }
    return size;
}

static int size_data(Data *data)
{
    int size = 0;

    size += size_long(data->type);
    switch (data->type) {

      case INTEGER:
	size += size_long(data->u.val);
	break;

      case STRING:
	size += size_long(data->u.substr.span);
	size += sizeof(char) * data->u.substr.span;
	break;

      case DBREF:
	size += size_ident(data->u.dbref);
	break;

      case LIST:
	size += size_list(data_dptr(data), data->u.sublist.span);
	break;

      case SYMBOL:
	size += size_ident(data->u.symbol);
	break;

      case ERROR:
	size += size_ident(data->u.error);
	break;

      case FROB:
	size += size_ident(data->u.frob.class);
	size += size_long(data->u.frob.rep_type);
	if (data->u.frob.rep_type == LIST) {
	    size += size_list(data->u.frob.rep.list->el,
			      data->u.frob.rep.list->len);
	} else {
	    size += size_dict(data->u.frob.rep.dict);
	}
	break;

      case DICT:
	size += size_dict(data->u.dict);
	break;
    }

    return size;
}

static int size_vars(Object *obj)
{
    int size = 0, i;

    size += size_long(obj->vars.size);
    size += size_long(obj->vars.blanks);

    for (i = 0; i < obj->vars.size; i++) {
	size += size_long(obj->vars.hashtab[i]);
	if (obj->vars.tab[i].name != NOT_AN_IDENT) {
	    size += size_ident(obj->vars.tab[i].name);
	    size += size_ident(obj->vars.tab[i].class);
	    size += size_data(&obj->vars.tab[i].val);
	} else {
	    size += size_long(NOT_AN_IDENT);
	}
	size += size_long(obj->vars.tab[i].next);
    }

    return size;
}

static int size_methods(Object *obj)
{
    int size = 0, i;

    size += size_long(obj->methods.size);
    size += size_long(obj->methods.blanks);

    for (i = 0; i < obj->methods.size; i++) {
	size += size_long(obj->methods.hashtab[i]);
	if (obj->methods.tab[i].m)
	    size += size_method(obj->methods.tab[i].m);
	else
	    size += size_long(NOT_AN_IDENT);
	size += size_long(obj->methods.tab[i].next);
    }

    return size;
}

static int size_method(Method *method)
{
    int size = 0, i, j;

    size += size_ident(method->name);

    size += size_long(method->num_args);
    for (i = 0; i < method->num_args; i++)
	size += size_long(method->argnames[i]);
    size += size_long(method->rest);

    size += size_long(method->num_vars);
    for (i = 0; i < method->num_vars; i++)
	size += size_long(method->varnames[i]);

    size += size_long(method->num_opcodes);
    for (i = 0; i < method->num_opcodes; i++)
	size += size_long(method->opcodes[i]);

    size += size_long(method->num_error_lists);
    for (i = 0; i < method->num_error_lists; i++) {
	size += size_long(method->error_lists[i].num_errors);
	for (j = 0; j < method->error_lists[i].num_errors; j++)
	    size += size_ident(method->error_lists[i].error_ids[j]);
    }

    size += size_long(method->overridable);
    return size;
}

static int size_strings(Object *obj)
{
    int size = 0, i;
    String *str;

    size += size_long(obj->strings_size);
    size += size_long(obj->num_strings);
    for (i = 0; i < obj->num_strings; i++) {
	str = obj->strings[i].str;
	if (str) {
	    size += size_long(str->len);
	    size += sizeof(char) * str->len;
	    size += size_long(obj->strings[i].refs);
	} else {
	    size += size_long(-1);
	}
    }

    return size;
}

static int size_idents(Object *obj)
{
    int size = 0, i;

    size += size_long(obj->idents_size);
    size += size_long(obj->num_idents);
    for (i = 0; i < obj->num_idents; i++) {
	if (obj->idents[i].id != NOT_AN_IDENT) {
	    size += size_ident(obj->idents[i].id);
	    size += size_long(obj->idents[i].refs);
	} else {
	    size += size_long(NOT_AN_IDENT);
	}
    }

    return size;
}

static void write_ident(long id, FILE *fp)
{
    char *s;
    int len;

    s = ident_name(id);
    len = strlen(s);
    write_long(len, fp);
    fwrite(s, sizeof(char), len, fp);
}

static long read_ident(FILE *fp)
{
    int len;
    char *s;
    long id;

    /* Read the length of the identifier. */
    len = read_long(fp);

    /* If the length is -1, it's not really an identifier, but a -1 signalling
     * a blank variable or method. */
    if (len == NOT_AN_IDENT)
	return NOT_AN_IDENT;

    /* Otherwise, it's an identifier.  Read it into temporary storage. */
    s = TMALLOC(char, len + 1);
    fread(s, sizeof(char), len, fp);
    s[len] = 0;

    /* Get the index for the identifier and free the temporary memory. */
    id = ident_get(s);
    tfree_chars(s);

    return id;
}

static long size_ident(long id)
{
    int len = strlen(ident_name(id));

    return size_long(len) + (len * sizeof(char));
}

/* Write a four-byte number to fp in a consistent byte-order. */
static void write_long(long n, FILE *fp)
{
    /* Since first byte is special, special-case 0 as well. */
    if (!n) {
	putc(96, fp);
	return;
    }

    /* First byte depends on sign. */
    putc((n > 0) ? 64 + (n % 32) : 32 + (-n % 32), fp);
    n = (n > 0) ? n / 32 : -n / 32;

    while (n) {
	putc(32 + (n % 64), fp);
	n /= 64;
    }

    putc(96, fp);
}

/* Read a four-byte number in a consistent byte-order. */
static long read_long(FILE *fp)
{
    int c;
    long n, place;

    /* Check for initial terminator, meaning 0. */
    c = getc(fp);
    if (c == 96)
	return 0;

    /* Initial byte determines sign. */
    n = (c < 64) ? -((c - 32) % 32) : ((c - 64) % 32);
    place = (c < 64) ? -32 : 32;

    while (1) {
	c = getc(fp);
	if (c == 96)
	    return n;
	n += place * (c - 32);
	place *= 64;
    }
}

static int size_long(long n)
{
    int count = 2;

    if (!n)
	return 1;
    n /= 32;
    while (n) {
	n /= 64;
	count++;
    }
    return count;
}

