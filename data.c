/* data.c: Routines for C-- data manipulation. */

#define _POSIX_SOURCE

#include <stdlib.h>
#include <ctype.h>
#include "x.tab.h"
#include "data.h"
#include "object.h"
#include "ident.h"
#include "util.h"
#include "cache.h"
#include "cmstring.h"
#include "memory.h"
#include "token.h"
#include "log.h"

/* Multiplicative hashing constants.  These constants are *not* machine-
 * dependent.  ANSI guarantees us that longs are at least 32 bits.  In order
 * for the binary db format to be machine-independent, our hash routine must
 * produce the same result even if longs are, say, 64 bits. */
#define MULTIPLIER	2654435769UL
#define WORD_BITS	32
#define FULL_MASK	0xffffffffUL

static int sublist_cmp(Sublist *s1, Sublist *s2);

/* Effects: Returns 0 if and only if d1 and d2 are equal according to C--
 *	    conventions.  If d1 and d2 are of the same type and are integers or
 *	    strings, returns greater than 0 if d1 is greater than d2 according
 *	    to C-- conventions, and less than 0 if d1 is less than d2. */
int data_cmp(Data *d1, Data *d2)
{
    int l1, l2, l, val;

    if (d1->type != d2->type) {
	return 1;
    } else {
	switch (d1->type) {

	  case INTEGER:
	    return d1->u.val - d2->u.val;

	  case STRING:
	    l1 = d1->u.substr.span;
	    l2 = d2->u.substr.span;
	    l = (l1 < l2) ? l1 : l2;
	    val = strnccmp(data_sptr(d1), data_sptr(d2), l);
	    if (val)
		return val;
	    else if (l1 > l2)
		return data_sptr(d1)[l2];
	    else if (l1 < l2)
		return -data_sptr(d2)[l1];
	    else
		return 0;

	  case DBREF:
	    return (d1->u.dbref != d2->u.dbref);

	  case LIST:
	    return sublist_cmp(&d1->u.sublist, &d2->u.sublist);

	  case SYMBOL:
	    return (d1->u.symbol != d2->u.symbol);

	  case ERROR:
	    return (d1->u.error != d2->u.error);

	  case FROB:
	    return !(d1->u.frob.class == d2->u.frob.class &&
		     dict_cmp(d1->u.frob.rep, d2->u.frob.rep) == 0);

	  case DICT:
	    return dict_cmp(d1->u.dict, d2->u.dict);

	  default:
	    return 1;
	}
    }
}

/* Effects: Returns 1 if data is true according to C-- conventions, or 0 if
 *	    data is false. */
int data_true(Data *data)
{
    switch (data->type) {

      case INTEGER:
	return (data->u.val != 0);

      case STRING:
	return (data->u.substr.span != 0);

      case DBREF:
	return 1;

      case LIST:
	return (data->u.sublist.span != 0);

      case SYMBOL:
	return 1;

      case ERROR:
	return 0;

      case FROB:
	return 1;

      case DICT:
	return (data->u.dict->keys->len != 0);

      default:
	return 0;
    }
}

unsigned long data_hash(Data *d)
{
    List *values;
    Sublist *sub;

    switch (d->type) {

      case INTEGER:
	return d->u.val;

      case STRING:
	return hash_case(d->u.substr.str->s + d->u.substr.start,
			 d->u.substr.span);

      case DBREF:
	return hash(ident_name(d->u.dbref));

      case LIST:
	sub = &d->u.sublist;
	if (sub->span)
	    return data_hash(&sub->list->el[sub->start + sub->span - 1]);
	else
	    return 100;

      case SYMBOL:
	return hash(ident_name(d->u.symbol));

      case ERROR:
	return hash(ident_name(d->u.error));

      case FROB:
	values = d->u.frob.rep->values;
	if (values->len)
	    return d->u.frob.class + data_hash(&values->el[values->len - 1]);
	else
	    return d->u.frob.class;

      case DICT:
	values = d->u.dict->values;
	if (values->len)
	    return data_hash(&values->el[values->len - 1]);
	else
	    return 200;

      default:
	return -1;
    }
}

/* Modifies: dest.
 * Effects: Copies src into dest, updating reference counts as necessary. */
void data_dup(Data *dest, Data *src)
{
    dest->type = src->type;
    switch (src->type) {

      case INTEGER:
	dest->u.val = src->u.val;
	break;

      case STRING:
	dest->u.substr.start = src->u.substr.start;
	dest->u.substr.span = src->u.substr.span;
	dest->u.substr.str = string_dup(src->u.substr.str);
	break;

      case DBREF:
	dest->u.dbref = ident_dup(src->u.dbref);
	break;

      case LIST:
	dest->u.sublist.start = src->u.sublist.start;
	dest->u.sublist.span = src->u.sublist.span;
	dest->u.sublist.list = list_dup(src->u.sublist.list);
	break;

      case SYMBOL:
	dest->u.symbol = ident_dup(src->u.symbol);
	break;

      case ERROR:
	dest->u.error = ident_dup(src->u.error);
	break;

      case FROB:
	dest->u.frob.class = ident_dup(src->u.frob.class);
	dest->u.frob.rep = dict_dup(src->u.frob.rep);
	break;

      case DICT:
	dest->u.dict = dict_dup(src->u.dict);
	break;
    }
}

/* Modifies: The value referred to by data.
 * Effects: Updates the reference counts for the value referred to by data
 *	    when we are no longer using it. */
void data_discard(Data *data)
{
    switch (data->type) {

      case STRING:
	string_discard(data->u.substr.str);
	break;

      case DBREF:
	ident_discard(data->u.dbref);
	break;

      case LIST:
	list_discard(data->u.sublist.list);
	break;

      case SYMBOL:
	ident_discard(data->u.symbol);
	break;

      case ERROR:
	ident_discard(data->u.error);
	break;

      case FROB:
	ident_discard(data->u.frob.class);
	dict_discard(data->u.frob.rep);
	break;

      case DICT:
	dict_discard(data->u.dict);
	break;
    }
}

String *data_tostr(Data *data)
{
    char *s;
    Number_buf nbuf;

    switch (data->type) {

      case INTEGER:
	s = long_to_ascii(data->u.val, nbuf);
	return string_from_chars(s, strlen(s));

      case STRING:
	if (data->u.substr.span == data->u.substr.str->len)
	    return string_dup(data->u.substr.str);
	else
	    return string_from_chars(data_sptr(data), data->u.substr.span);

      case DBREF:
	s = ident_name(data->u.dbref);
	return string_from_chars(s, strlen(s));

      case LIST:
	return string_from_chars("<list>", 6);

      case SYMBOL:
	s = ident_name(data->u.symbol);
	return string_from_chars(s, strlen(s));

      case ERROR:
	s = ident_name(data->u.error);
	return string_from_chars(s, strlen(s));

      case FROB:
	return string_from_chars("<frob>", 6);

      case DICT:
	return string_from_chars("<dict>", 9);

      default:
	panic("Unrecognized data type.");
	return NULL;
    }
}

/* Effects: Returns a string containing a printed representation of data. */
String *data_to_literal(Data *data)
{
    String *str = string_empty(0);

    return data_add_literal_to_str(str, data);
}

/* Modifies: str (mutator, claims reference count).
 * Effects: Returns a string with the printed representation of data added to
 *	    it. */
String *data_add_literal_to_str(String *str, Data *data)
{
    char *s;
    int i;
    Number_buf nbuf;

    switch(data->type) {

      case INTEGER:
	s = long_to_ascii(data->u.val, nbuf);
	return string_add(str, s, strlen(s));

      case STRING:
	return string_add_unparsed(str, data_sptr(data), data->u.substr.span);

      case DBREF:
	str = string_addc(str, '$');
	s = ident_name(data->u.dbref);
	if (is_valid_ident(s))
	    return string_add(str, s, strlen(s));
	else
	    return string_add_unparsed(str, s, strlen(s));

      case LIST:
	str = string_addc(str, '[');
	for (i = 0; i < data->u.sublist.span; i++) {
	    str = data_add_literal_to_str(str, data_dptr(data) + i);
	    if (i < data->u.sublist.span - 1)
		str = string_add(str, ", ", 2);
	}
	return string_addc(str, ']');

      case SYMBOL:
	str = string_addc(str, '\'');
	s = ident_name(data->u.dbref);
	if (is_valid_ident(s))
	    return string_add(str, s, strlen(s));
	else
	    return string_add_unparsed(str, s, strlen(s));

      case ERROR:
	str = string_addc(str, '~');
	s = ident_name(data->u.dbref);
	if (is_valid_ident(s))
	    return string_add(str, s, strlen(s));
	else
	    return string_add_unparsed(str, s, strlen(s));

      case FROB:
	str = string_add(str, "<$", 2);
	s = ident_name(data->u.frob.class);
	if (is_valid_ident(s))
	    str = string_add(str, s, strlen(s));
	else
	    str = string_add_unparsed(str, s, strlen(s));
	str = string_add(str, ", ", 2);
	str = dict_add_literal_to_str(str, data->u.frob.rep);
	return string_addc(str, '>');

      case DICT:
	return dict_add_literal_to_str(str, data->u.dict);

      default:
	return str;
    }
}

char *data_from_literal(Data *d, char *s)
{
    while (isspace(*s))
	s++;
    d->type = -1;

    if (isdigit(*s)) {
	d->type = INTEGER;
	d->u.val = atol(s);
	while (isdigit(*++s));
	return s;
    } else if (*s == '"') {
	d->type = STRING;
	substr_set_to_full_string(&d->u.substr, string_parse(&s));
	return s;
    } else if (*s == '$') {
	s++;
	d->type = DBREF;
	d->u.dbref = parse_ident(&s);
	return s;
    } else if (*s == '[') {
	List *list;

	list = list_new(0);
	s++;
	while (*s && *s != ']') {
	    s = data_from_literal(d, s);
	    list = list_add(list, d);
	    data_discard(d);
	    while (isspace(*s))
		s++;
	    if (*s == ',')
		s++;
	    while (isspace(*s))
		s++;
	}
	d->type = LIST;
	sublist_set_to_full_list(&d->u.sublist, list);
	return (*s) ? s + 1 : s;
    } else if (*s == '#') {
	Data assocs;

	/* Get associations. */
	s = data_from_literal(&assocs, s + 1);
	if (assocs.type != LIST) {
	    if (assocs.type != -1)
		data_discard(&assocs);
	    d->type = -1;
	    return s;
	}

	/* Make a dict from the associations. */
	d->type = DICT;
	d->u.dict = dict_from_slices(assocs.u.sublist.list);
	data_discard(&assocs);
	if (!d->u.dict)
	    d->type = -1;
	return s;
    } else if (*s == '\'') {
	s++;
	d->type = SYMBOL;
	d->u.symbol = parse_ident(&s);
	return s;
    } else if (*s == '~') {
	s++;
	d->type = ERROR;
	d->u.symbol = parse_ident(&s);
	return s;
    } else if (*s == '<') {
	Data class, rep;

	s = data_from_literal(&class, s + 1);
	if (class.type == DBREF) {
	    while (isspace(*s))
		s++;
	    if (*s == ',')
		s++;
	    while (isspace(*s))
		s++;
	    s = data_from_literal(&rep, s);
	    if (rep.type == DICT) {
		d->type = FROB;
		d->u.frob.class = class.u.dbref;
		d->u.frob.rep = rep.u.dict;
		return (*s) ? s + 1 : s;
	    } else if (rep.type != -1) {
		data_discard(&rep);
	    }
	} else if (class.type != -1) {
	    data_discard(&class);
	}
	return (*s) ? s + 1 : s;
    } else {
	return (*s) ? s + 1 : s;
    }
}

/* Effects: Returns an id (without updating reference count) for the name of
 *	    the type given by type. */
long data_type_id(int type)
{
    switch (type) {
      case INTEGER:	return integer_id;
      case STRING:	return string_id;
      case DBREF:	return dbref_id;
      case LIST:	return list_id;
      case SYMBOL:	return symbol_id;
      case ERROR:	return error_id;
      case FROB:	return frob_id;
      case DICT:	return dictionary_id;
      default:		panic("Unrecognized data type."); return 0;
    }
}

/* Effects: Returns 0 if the sublists s1 and s2 are equivalent, or 1 if not. */
static int sublist_cmp(Sublist *s1, Sublist *s2)
{
    int i;
    List *l1, *l2;

    /* Lists can only be equal if they're of the same length. */
    if (s1->span != s2->span)
	return 1;

    l1 = s1->list;
    l2 = s2->list;

    /* If they're pointing to the same actual list, then they're obviously
     * equal. */
    if (l1 == l2 && s1->start == s2->start)
	return 0;

    /* See if any elements differ. */
    for (i = 0; i < s1->span; i++) {
	if (data_cmp(&l1->el[s1->start + i], &l2->el[s2->start + i]) != 0)
	    return 1;
    }

    /* No elements differ, so the lists are the same. */
    return 0;
}

/* Modifies: sublist and sublist->list.
 * Effects: Makes sure that sublist is pointing to a list which we can add
 *	    to or otherwise modify. */
void sublist_truncate(Sublist *sublist)
{
    List *list;

    if (sublist->list->refs == 1) {
	/* Since we own the list, then we can just throw away anything past the
	 * end of the sublist. */
	while (sublist->list->len > sublist->start + sublist->span)
	    data_discard(&sublist->list->el[--sublist->list->len]);
    } else {
	/* Make a copy of the list containing just the sublist. */
	list = sublist->list;
	sublist->list = list_from_data(list->el + sublist->start,
				       sublist->span);
	list_discard(list);
	sublist->start = 0;
    }
}

/* Modifies: substr and substr->str.
 * Effects: Makes sure that substr is pointing to a string which we can add
 *	    to. */
void substring_truncate(Substring *substr)
{
    String *str;

    if (substr->str->refs == 1) {
	/* Since we own the string, then we can just throw away anything past
	 * the end of the substring. */
	substr->str->len = substr->start + substr->span;
	substr->str->s[substr->str->len] = 0;
    } else {
	/* Make a copy of the string containing just the substring. */
	str = string_from_chars(substr->str->s + substr->start, substr->span);
	string_discard(substr->str);
	substr->str = str;
	substr->start = 0;
    }
}

/* Requires: data contains a string value.
 * Effects: Returns a pointer to the first character in the string.  This may
 *	    not be null-terminated if we don't use substring_truncate()
 *	    first. */
char *data_sptr(Data *data)
{
    return data->u.substr.str->s + data->u.substr.start;
}

/* Effects: Returns a pointer to the first data element in the list. */
Data *data_dptr(Data *data)
{
    return data->u.sublist.list->el + data->u.sublist.start;
}

/* Modifies: target.
 * Effects: Assigns the full range of str to target.  Does not update the
 *	    reference count on str. */
void substr_set_to_full_string(Substring *target, String *str)
{
    target->str = str;
    target->start = 0;
    target->span = str->len;
}

/* Modifies: target.
 * Effects: Assigns the full range of list to target.  Does not update the
 *	    reference count on list. */
void sublist_set_to_full_list(Sublist *target, List *list)
{
    target->list = list;
    target->start = 0;
    target->span = list->len;
}

