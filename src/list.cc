/* list.c: Routines for list manipulation.
 * This code is not ANSI-conformant, because it allocates memory at the end
 * of List structure and references it with a one-element array. */

#define _POSIX_SOURCE

#include "x.tab.h"
#include "list.h"
#include "memory.h"

/* Note that we number list elements [0..(len - 1)] internally, while the
 * user sees list elements as numbered [1..len]. */

/* We use MALLOC_DELTA to keep our blocks about 32 bytes less than a power of
 * two.  We also have to account for the size of a List (16 bytes) which gets
 * added in before we allocate.  This works if a Data is sixteen bytes. */

#define MALLOC_DELTA	3
#define STARTING_SIZE	(16 - MALLOC_DELTA)

static List *prepare_to_modify(List *str, int start, int len);

List *list_new(int len)
{
    List *new;
    int size;

    size = STARTING_SIZE;
    while (size < len)
	size = size * 2 + MALLOC_DELTA;
    new = emalloc(sizeof(List) + (size * sizeof(Data)));
    new->len = 0;
    new->size = size;
    new->refs = 1;
    return new;
}

List *list_dup(List *list)
{
    list->refs++;
    return list;
}

int list_length(List *list)
{
    return list->len;
}

Data *list_first(List *list)
{
    return (list->len != 0) ? list->el + list->start : NULL;
}

Data *list_next(List *list, Data *d)
{
    return (d < list->el + list->start + list->len - 1) ? d + 1 : NULL;
}

Data *list_last(List *list)
{
    return list->el + list->start + list->len - 1;
}

Data *list_prev(List *list, Data *d)
{
    return (d > list->el + list->start) ? d - 1 : NULL;
}

Data *list_elem(List *list, int i)
{
    return list->el + list->start + i;
}

/* This is a horrible abstraction-breaking function.  Call it just after you
 * make a list with list_new(<spaces>).  Then fill in the data slots yourself.
 * Don't manipulate <list> until you're done. */
Data *list_empty_spaces(List *list, int spaces)
{
    list->len += spaces;
    return list->el + list->start + list->len - spaces;
}

int list_search(List *list, Data *data)
{
    Data *d, *start, *end;

    start = list->el + list->start;
    end = start + list->len;
    for (d = start; d < end; d++) {
	if (data_cmp(data, d) == 0)
	    return d - start;
    }
    return -1;
}

/* Effects: Returns 0 if the lists l1 and l2 are equivalent, or 1 if not. */
int list_cmp(List *l1, List *l2)
{
    int i;

    /* They're obviously the same if they're the same list. */
    if (l1 == l2)
	return 0;

    /* Lists can only be equal if they're of the same length. */
    if (l1->len != l2->len)
	return 1;

    /* See if any elements differ. */
    for (i = 0; i < l1->len; i++) {
	if (data_cmp(&l1->el[l1->start + i], &l2->el[l2->start + i]) != 0)
	    return 1;
    }

    /* No elements differ, so the lists are the same. */
    return 0;
}

/* Error-checking on pos is the job of the calling function. */
List *list_insert(List *list, int pos, Data *elem)
{
    list = prepare_to_modify(list, list->start, list->len + 1);
    pos += list->start;
    MEMMOVE(list->el + pos + 1, list->el + pos, list->len - 1 - pos);
    data_dup(&list->el[pos], elem);
    return list;
}

List *list_add(List *list, Data *elem)
{
    list = prepare_to_modify(list, list->start, list->len + 1);
    data_dup(&list->el[list->start + list->len - 1], elem);
    return list;
}

/* Error-checking on pos is the job of the calling function. */
List *list_replace(List *list, int pos, Data *elem)
{
    list = prepare_to_modify(list, list->start, list->len);
    pos += list->start;
    data_discard(&list->el[pos]);
    data_dup(&list->el[pos], elem);
    return list;
}

/* Error-checking on pos is the job of the calling function. */
List *list_delete(List *list, int pos)
{
    Data last_elem;

    /* Special-case deletion of last element. */
    if (pos == list->len - 1)
	return prepare_to_modify(list, list->start, list->len - 1);

    data_dup(&last_elem, &list->el[list->len - 1]);
    list = prepare_to_modify(list, list->start, list->len - 1);
    pos += list->start;
    data_discard(&list->el[pos]);
    MEMMOVE(list->el + pos, list->el + pos + 1, list->len - pos - 1);
    list->el[list->len - 1] = last_elem;
    return list;
}

/* This routine will crash if elem is not in list. */
List *list_delete_element(List *list, Data *elem)
{
    int pos;

    pos = list_search(list, elem);
    return list_delete(list, pos);
}

List *list_append(List *list1, List *list2)
{
    int i;
    Data *p, *q;

    list1 = prepare_to_modify(list1, list1->start, list1->len + list2->len);
    p = list1->el + list1->start + list1->len - list2->len;
    q = list2->el + list2->start;
    for (i = 0; i < list2->len; i++)
	data_dup(&p[i], &q[i]);
    return list1;
}

List *list_reverse(List *list)
{
    Data *d, tmp;
    int i;

    list = prepare_to_modify(list, list->start, list->len);
    d = list->el + list->start;
    for (i = 0; i < list->len / 2; i++) {
	tmp = d[i];
	d[i] = d[list->len - i - 1];
	d[list->len - i - 1] = tmp;
    }
    return list;
}

List *list_setadd(List *list, Data *d)
{
    if (list_search(list, d) == -1)
	return list;
    return list_add(list, d);
}

List *list_setremove(List *list, Data *d)
{
    int pos;

    pos = list_search(list, d);
    if (pos == -1)
	return list;
    return list_delete(list, pos);
}

List *list_union(List *list1, List *list2)
{
    Data *start, *end, *d;

    /* Simplistic O(len1 * len2) implementation for now.  Later, use lengths to
     * decide whether to use a O(len1 + len2) hash table algorithm. */
    start = list2->el + list2->start;
    end = start + list2->len;
    for (d = start; d < end; d++) {
	if (list_search(list1, d) == -1)
	    list1 = list_add(list1, d);
    }
    return list1;
}

List *list_sublist(List *list, int start, int len)
{
    return prepare_to_modify(list, start, len);
}

/* Warning: do not discard a list before initializing its data elements. */
void list_discard(List *list)
{
    int i;

    if (!--list->refs) {
	for (i = list->start; i < list->start + list->len; i++)
	    data_discard(&list->el[i]);
	free(list);
    }
}

/* Input to this routine should be a list you want to modify, a start, and a
 * length.  The start gives the offset from list->el at which you start being
 * interested in data; the length is the amount of data there will be in the
 * list after that point after you finish modifying it.
 *
 * The return value of this routine is a list whose contents can be freely
 * modified, containing at least the information you claimed was interesting.
 * list->start will be set to the beginning of the interesting data; list->len
 * will be set to len, even though this will make some data invalid if
 * len > list->len upon input.  Also, the returned string may not be null-
 * terminated.
 *
 * If start is increased or len is decreased by this function, and list->refs
 * is 1, the uninteresting data will be discarded by this function.
 *
 * In general, modifying start and len is the responsibility of this routine;
 * modifying the contents is the responsibility of the calling routine. */
static List *prepare_to_modify(List *list, int start, int len)
{
    List *new;
    int i, need_to_move, need_to_resize, size;

    /* Figure out if we need to resize the list or move its contents.  Moving
     * contents takes precedence. */
    need_to_resize = (len - start) * 4 < list->size;
    need_to_resize = need_to_resize && list->size > STARTING_SIZE;
    need_to_resize = need_to_resize || (list->size < len);
    need_to_move = (list->refs > 1) || (need_to_resize && start > 0);

    if (need_to_move) {
	/* Move the list contents into a new list. */
	new = list_new(len);
	new->len = len;
	len = (list->len < len) ? list->len : len;
	for (i = 0; i < len; i++)
	    data_dup(&new->el[i], &list->el[start + i]);
	list_discard(list);
	return new;
    } else if (need_to_resize) {
	/* Resize the list.  We can assume that list->start == start == 0. */
	for (; list->len > len; list->len--)
	    data_discard(&list->el[list->len - 1]);
	list->len = len;
	size = STARTING_SIZE;
	while (size < len)
	    size = size * 2 + MALLOC_DELTA;
	list = erealloc(list, sizeof(List) + (size * sizeof(Data)));
	list->size = size;
	return list;
    } else {
	for (; list->start < start; list->start++)
	    data_discard(&list->el[list->start]);
	for (; list->len > len; list->len--)
	    data_discard(&list->el[list->start + list->len - 1]);
	list->start = start;
	list->len = len;
	return list;
    }
}

