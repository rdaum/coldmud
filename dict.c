/* dict.c: Routines for manipulating dictionaries. */

#define _POSIX_SOURCE

#include "x.tab.h"
#include "data.h"
#include "memory.h"
#include "ident.h"

#define MALLOC_DELTA			 5
#define HASHTAB_STARTING_SIZE		(32 - MALLOC_DELTA)

static Dict *prepare_to_modify(Dict *dict);
static void insert_key(Dict *dict, int i);
static int search(Dict *dict, Data *key);
static void double_hashtab_size(Dict *dict);

Dict *dict_new(List *keys, List *values)
{
    Dict *new;
    int i;

    /* Construct a new dictionary. */
    new = EMALLOC(Dict, 1);
    new->keys = list_dup(keys);
    new->values = list_dup(values);

    /* Calculate initial size of chain and hash table. */
    new->hashtab_size = HASHTAB_STARTING_SIZE;
    while (new->hashtab_size < keys->len)
	new->hashtab_size = new->hashtab_size * 2 + MALLOC_DELTA;

    /* Initialize chain entries and hash table. */
    new->links = EMALLOC(int, new->hashtab_size);
    new->hashtab = EMALLOC(int, new->hashtab_size);
    for (i = 0; i < new->hashtab_size; i++) {
	new->links[i] = -1;
	new->hashtab[i] = -1;
    }

    /* Insert the keys into the hash table. */
    for (i = 0; i < new->keys->len; i++) {
	if (search(new, &keys->el[i]) != -1) {
	    new->keys = list_delete(new->keys, i);
	    new->values = list_delete(new->values, i);
	} else {
	    insert_key(new, i);
	}
    }

    new->refs = 1;
    return new;
}

Dict *dict_new_empty(void)
{
    List *l1, *l2;
    Dict *dict;

    l1 = list_new(0);
    l2 = list_new(0);
    dict = dict_new(l1, l2);
    list_discard(l1);
    list_discard(l2);
    return dict;
}

Dict *dict_from_slices(List *slices)
{
    int i;
    List *keys, *values;
    Dict *dict;

    /* Make lists for keys and values. */
    keys = list_new(slices->len);
    values = list_new(slices->len);

    for (i = 0; i < slices->len; i++) {
	if (slices->el[i].type != LIST || slices->el[i].u.sublist.span != 2) {
	    /* Invalid slice.  Throw away what we had and return NULL. */
	    keys->len = values->len = i;
	    list_discard(keys);
	    list_discard(values);
	    return NULL;
	}
	data_dup(&keys->el[i], data_dptr(&slices->el[i]));
	data_dup(&values->el[i], data_dptr(&slices->el[i]) + 1);
    }

    /* Slices were all valid; return new dict. */
    dict = dict_new(keys, values);
    list_discard(keys);
    list_discard(values);
    return dict;
}

Dict *dict_dup(Dict *dict)
{
    dict->refs++;
    return dict;
}

void dict_discard(Dict *dict)
{
    dict->refs--;
    if (!dict->refs) {
	list_discard(dict->keys);
	list_discard(dict->values);
	free(dict->links);
	free(dict->hashtab);
	free(dict);
    }
}

int dict_cmp(Dict *dict1, Dict *dict2)
{
    if (list_cmp(dict1->keys, dict2->keys) == 0 &&
	list_cmp(dict1->values, dict2->values) == 0)
	return 0;
    else
	return 1;
}

Dict *dict_add(Dict *dict, Data *key, Data *value)
{
    int pos;

    dict = prepare_to_modify(dict);

    /* Just replace the value for the key if it already exists. */
    pos = search(dict, key);
    if (pos != -1) {
	dict->values = list_replace(dict->values, pos, value);
	return dict;
    }

    /* Add the key and value to the list. */
    dict->keys = list_add(dict->keys, key);
    dict->values = list_add(dict->values, value);

    /* Check if we should resize the hash table. */
    if (dict->keys->len > dict->hashtab_size)
	double_hashtab_size(dict);
    else
	insert_key(dict, dict->keys->len - 1);
    return dict;
}

Dict *dict_del(Dict *dict, Data *key)
{
    int ind, *ip, i = -1, j;

    /* Search for a pointer to the key, either in the hash table entry or in
     * the chain links. */
    ind = data_hash(key) % dict->hashtab_size;
    for (ip = &dict->hashtab[ind]; *ip != -1; ip = &dict->links[*ip]) {
	i = *ip;
	if (data_cmp(&dict->keys->el[i], key) == 0)
	    break;
    }

    /* If *ip is -1, we didn't find the key; return NULL. */
    if (*ip == -1)
	return NULL;

    /* We found the key; duplicate the dictionary, if necessary, and delete the
     * elements from the keys and values lists. */
    dict = prepare_to_modify(dict);
    dict->keys = list_delete(dict->keys, i);
    dict->values = list_delete(dict->values, i);

    /* Replace the pointer to the key index with the next link. */
    *ip = dict->links[i];

    /* Copy the links beyond ip backward. */
    MEMMOVE(dict->links + i, dict->links + i + 1, int, dict->keys->len - i);
    dict->links[dict->keys->len - 1] = -1;

    /* Since we've renumbered all the elements, we have to check all the links
     * and hash table entries.  If they're greater than i, decrement them.
     * Skip this step if the element we removed was the last one. */
    if (i < dict->keys->len) {
	for (j = 0; j < dict->keys->len; j++) {
	    if (dict->links[j] > i)
		dict->links[j]--;
	}
	for (j = 0; j < dict->hashtab_size; j++) {
	    if (dict->hashtab[j] > i)
		dict->hashtab[j]--;
	}
    }

    return dict;
}

long dict_find(Dict *dict, Data *key, Data *ret)
{
    int pos;

    pos = search(dict, key);
    if (pos == -1)
	return keynf_id;

    data_dup(ret, &dict->values->el[pos]);
    return NOT_AN_IDENT;
}

int dict_contains(Dict *dict, Data *key)
{
    int pos;

    pos = search(dict, key);
    return (pos != -1);
}

List *dict_keys(Dict *dict)
{
    return list_dup(dict->keys);
}

List *dict_key_value_pair(Dict *dict, int i)
{
    List *l;

    if (i >= dict->keys->len)
	return NULL;
    l = list_new(2);
    data_dup(&l->el[0], &dict->keys->el[i]);
    data_dup(&l->el[1], &dict->values->el[i]);
    return l;
}

String *dict_add_literal_to_str(String *str, Dict *dict)
{
    int i;

    str = string_add(str, "#[", 2);
    for (i = 0; i < dict->keys->len; i++) {
	str = string_addc(str, '[');
	str = data_add_literal_to_str(str, &dict->keys->el[i]);
	str = string_add(str, ", ", 2);
	str = data_add_literal_to_str(str, &dict->values->el[i]);
	str = string_addc(str, ']');
	if (i < dict->keys->len - 1)
	    str = string_add(str, ", ", 2);
    }	
    return string_addc(str, ']');
}

static Dict *prepare_to_modify(Dict *dict)
{
    Dict *new;

    if (dict->refs == 1)
	return dict;

    /* Duplicate the old dictionary. */
    new = EMALLOC(Dict, 1);
    new->keys = list_dup(dict->keys);
    new->values = list_dup(dict->values);
    new->hashtab_size = dict->hashtab_size;
    new->links = EMALLOC(int, new->hashtab_size);
    MEMCPY(new->links, dict->links, int, new->hashtab_size);
    new->hashtab = EMALLOC(int, new->hashtab_size);
    MEMCPY(new->hashtab, dict->hashtab, int, new->hashtab_size);
    dict->refs--;
    new->refs = 1;
    return new;
}

static void insert_key(Dict *dict, int i)
{
    int ind;

    ind = data_hash(&dict->keys->el[i]) % dict->hashtab_size;
    dict->links[i] = dict->hashtab[ind];
    dict->hashtab[ind] = i;
}

static int search(Dict *dict, Data *key)
{
    int ind, i;

    ind = data_hash(key) % dict->hashtab_size;
    for (i = dict->hashtab[ind]; i != -1; i = dict->links[i]) {
	if (data_cmp(&dict->keys->el[i], key) == 0)
	    return i;
    }

    return -1;
}

static void double_hashtab_size(Dict *dict)
{
    int i;

    dict->hashtab_size = dict->hashtab_size * 2 + MALLOC_DELTA;
    dict->links = EREALLOC(dict->links, int, dict->hashtab_size);
    dict->hashtab = EREALLOC(dict->hashtab, int, dict->hashtab_size);
    for (i = 0; i < dict->hashtab_size; i++) {
	dict->links[i] = -1;
	dict->hashtab[i] = -1;
    }
    for (i = 0; i < dict->keys->len; i++)
	insert_key(dict, i);
}

