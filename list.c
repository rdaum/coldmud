/* list.c: Routines for list manipulation.
 * This code is not ANSI-conformant, because it allocates memory at the end
 * of List structure and references it with a one-element array. */

#define _POSIX_SOURCE

#include "data.h"
#include "memory.h"

/* Note that we number list elements [0..(len - 1)] internally, while the
 * user sees list elements as numbered [1..len]. */

/* We use MALLOC_DELTA to keep our blocks about 32 bytes less than a power of
 * two.  We also have to account for the size of a List (16 bytes) which gets
 * added in before we allocate.  This works if a Data is sixteen bytes. */

#define MALLOC_DELTA	3
#define STARTING_SIZE	(16 - MALLOC_DELTA)

static List *prepare_to_modify(List *list, int len);

List *list_new(int len)
{
    List *new;
    int size;

    size = STARTING_SIZE;
    while (size < len)
	size = size * 2 + MALLOC_DELTA;
    new = emalloc(sizeof(List) + (size * sizeof(Data)));
    new->len = len;
    new->size = size;
    new->refs = 1;
    return new;
}

List *list_dup(List *list)
{
    list->refs++;
    return list;
}

List *list_from_data(Data *data, int len)
{
    List *list;
    int i;

    list = list_new(len);
    for (i = 0; i < len; i++)
	data_dup(&list->el[i], &data[i]);
    return list;
}

List *list_from_sublist(Sublist *sublist)
{
    List *list = sublist->list;

    if (sublist->span == list->len) {
	return list_dup(list);
    } else {
	return list_from_data(list->el + sublist->start, sublist->span);
    }
}

int list_search(List *list, Data *data)
{
    int i;

    for (i = 0; i < list->len; i++) {
	if (data_cmp(data, &list->el[i]) == 0)
	    return i;
    }
    return -1;
}

/* Effects: Returns 0 if the lists l1 and l2 are equivalent, or 1 if not. */
int list_cmp(List *l1, List *l2)
{
    int i;

    if (l1 == l2)
	return 0;

    /* Lists can only be equal if they're of the same length. */
    if (l1->len != l2->len)
	return 1;

    /* See if any elements differ. */
    for (i = 0; i < l1->len; i++) {
	if (data_cmp(&l1->el[i], &l2->el[i]) != 0)
	    return 1;
    }

    /* No elements differ, so the lists are the same. */
    return 0;
}

/* Error-checking on pos is the job of the calling function. */
List *list_insert(List *list, int pos, Data *elem)
{
    list = prepare_to_modify(list, list->len + 1);
    MEMMOVE(list->el + pos + 1, list->el + pos, list->len - pos);
    data_dup(&list->el[pos], elem);
    list->len++;
    return list;
}

List *list_add(List *list, Data *elem)
{
    list = prepare_to_modify(list, list->len + 1);
    data_dup(&list->el[list->len++], elem);
    return list;
}

/* Error-checking on pos is the job of the calling function. */
List *list_replace(List *list, int pos, Data *elem)
{
    list = prepare_to_modify(list, list->len);
    data_discard(&list->el[pos]);
    data_dup(&list->el[pos], elem);
    return list;
}

/* Error-checking on pos is the job of the calling function. */
List *list_delete(List *list, int pos)
{
    list = prepare_to_modify(list, list->len - 1);
    data_discard(&list->el[pos]);
    MEMMOVE(list->el + pos, list->el + pos + 1, list->len - pos - 1);
    list->len--;
    return list;
}

/* This routine will crash if elem is not in list. */
List *list_delete_element(List *list, Data *elem)
{
    int pos;

    pos = list_search(list, elem);
    return list_delete(list, pos);
}

List *list_append(List *list, Data *d, int len)
{
    int i;

    list = prepare_to_modify(list, list->len + len);
    for (i = 0; i < len; i++)
	data_dup(&list->el[list->len + i], &d[i]);
    list->len += len;
    return list;
}

/* Warning: do not discard a list before initializing its data elements. */
void list_discard(List *list)
{
    int i;

    if (!--list->refs) {
	for (i = 0; i < list->len; i++)
	    data_discard(&list->el[i]);
	free(list);
    }
}

static List *prepare_to_modify(List *list, int len)
{
    List *new;
    int i;

    if (list->refs == 1 && list->size < len) {
	do {
	    list->size = list->size * 2 + MALLOC_DELTA;
	} while (list->size < len);
	list = erealloc(list, sizeof(List) + (list->size * sizeof(Data)));
	return list;
    } else if (list->refs > 1) {
	new = list_new(len);
	new->len = list->len;
	for (i = 0; i < list->len; i++)
	    data_dup(&new->el[i], &list->el[i]);
	list_discard(list);
	return new;
    } else {
	return list;
    }
}

