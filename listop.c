/* listop.c: Function operators for list manipulation. */

#define _POSIX_SOURCE

#include "x.tab.h"
#include "operator.h"
#include "execute.h"
#include "data.h"
#include "ident.h"
#include "memory.h"

void op_listlen(void)
{
    Data *args;
    int len;

    /* Accept a list to take the length of. */
    if (!func_init_1(&args, LIST))
	return;

    /* Replace the argument with its length. */
    len = args[0].u.sublist.span;
    pop(1);
    push_int(len);
}

void op_sublist(void)
{
    int num_args, start, span;
    Data *args;

    /* Accept a list, an integer, and an optional integer. */
    if (!func_init_2_or_3(&args, &num_args, LIST, INTEGER, INTEGER))
	return;

    /* Make sure range is in bounds. */
    start = args[1].u.val - 1;
    span = (num_args == 3) ? args[2].u.val : args[0].u.sublist.span - start;
    if (start < 0) {
	throw(range_id, "Start (%d) less than one", start + 1);
    } else if (span < 0) {
	throw(range_id, "Sublist length (%d) less than zero", span);
    } else if (start + span > args[0].u.sublist.span) {
	throw(range_id, "Sublist extends to %d, past end of list (length %d)",
	      start + span, args[0].u.sublist.span);
    } else {
	/* Replace first argument with sublist, and pop other arguments. */
	args[0].u.sublist.start += start;
	args[0].u.sublist.span = span;
	pop(num_args - 1);
    }
}

void op_insert(void)
{
    int pos;
    Data *args;
    Sublist *sublist;

    /* Accept a list, an integer offset, and a data value of any type. */
    if (!func_init_3(&args, LIST, INTEGER, 0))
	return;

    pos = args[1].u.val - 1;
    sublist = &args[0].u.sublist;

    if (pos < 0) {
	throw(range_id, "Position (%d) less than one", pos + 1);
    } else if (pos > sublist->span) {
	throw(range_id, "Position (%d) beyond end of list (length %d)",
	      pos + 1, sublist->span);
    } else {
	/* Modify the list and discard the offset and data. */
	anticipate_assignment();
	sublist->list = list_insert(sublist->list, sublist->start + pos,
				    &args[2]);
	sublist->span++;
	pop(2);
    }
}

void op_replace(void)
{
    int pos;
    Data *args;
    Sublist *sublist;
    List *list;

    /* Accept a list, an integer offset, and a data value of any type. */
    if (!func_init_3(&args, LIST, INTEGER, 0))
	return;

    pos = args[1].u.val - 1;
    sublist = &args[0].u.sublist;

    if (pos < 0) {
	throw(range_id, "Position (%d) less than one", pos + 1);
    } else if (pos > sublist->span - 1) {
	throw(range_id, "Position (%d) greater than length of list (%d)",
	      pos + 1, sublist->span);
    } else {
	anticipate_assignment();

	/* If we don't own the list, make a copy of the sublist. */
	if (sublist->list->refs != 1) {
	    list = sublist->list;
	    sublist->list = list_from_data(list->el + sublist->start,
					   sublist->span);
	    list_discard(list);
	    sublist->start = 0;
	}

	/* Replace the list element. */
	data_discard(&sublist->list->el[sublist->start + pos]);
	data_dup(&sublist->list->el[sublist->start + pos], &args[2]);

	/* Discard the second and third argument. */
	pop(2);
    }
}

void op_delete(void)
{
    int pos;
    Data *args, *dptr;
    Sublist *sublist;

    /* Accept a list and an integer offset. */
    if (!func_init_2(&args, LIST, INTEGER))
	return;

    pos = args[1].u.val - 1;
    sublist = &args[0].u.sublist;

    if (pos < 0) {
	throw(range_id, "Position (%d) less than one", pos + 1);
    } else if (pos > sublist->span - 1) {
	throw(range_id, "Position (%d) greater than length of list (%d)",
	      pos + 1, sublist->span);
    } else {
	anticipate_assignment();

	/* Remove the list element. */
	sublist_truncate(sublist);
	dptr = &sublist->list->el[sublist->start + pos];
	data_discard(dptr);
	MEMMOVE(dptr, dptr + 1, sublist->span - 1 - pos);
	sublist->list->len--;
	sublist->span--;

	/* Pop the second argument. */
	pop(1);
    }
}

void op_setadd(void)
{
    int i;
    Data *args;
    Sublist *sublist;

    /* Accept a list and a data value of any type. */
    if (!func_init_2(&args, LIST, 0))
	return;

    /* If args[1] is in args[0], then just pop args[1] and return. */
    for (i = 0; i < args[0].u.sublist.span; i++) {
	if (data_cmp(data_dptr(&args[0]) + i, &args[1]) == 0) {
	    pop(1);
	    return;
	}
    }

    /* Add args[1] to args[0] and pop args[1]. */
    anticipate_assignment();
    sublist = &args[0].u.sublist;
    sublist_truncate(sublist);
    sublist->list = list_add(sublist->list, &args[1]);
    sublist->span++;
    pop(1);
}

void op_setremove(void)
{
    int i;
    Data *args, *d;
    Sublist *sublist;

    /* Accept a list and a data value of any type. */
    if (!func_init_2(&args, LIST, 0))
	return;

    /* Look for args[1] in args[0]. */
    for (i = 0; i < args[0].u.sublist.span; i++) {
	if (data_cmp(data_dptr(&args[0]) + i, &args[1]) == 0)
	    break;
    }

    /* Nothing to do if args[1] wasn't there. */
    if (i == args[0].u.sublist.span) {
	pop(1);
	return;
    }

    /* Remove the list element. */
    anticipate_assignment();
    sublist = &args[0].u.sublist;
    sublist_truncate(sublist);
    d = &sublist->list->el[sublist->start + i];
    data_discard(d);
    MEMCPY(d, d + 1, sublist->span - sublist->start - i - 1);
    sublist->list->len--;
    sublist->span--;

    /* Pop the second argument. */
    pop(1);
}

void op_union(void)
{
    int i, j;
    Data *args, *base1, *base2;
    Sublist *sublist;

    /* Accept two lists. */
    if (!func_init_2(&args, LIST, LIST))
	return;

    anticipate_assignment();

    /* Prepare to add elements to left list. */
    sublist = &args[0].u.sublist;
    sublist_truncate(sublist);
    base1 = sublist->list->el + sublist->start;
    base2 = data_dptr(&args[1]);

    /* Iterate over elements in second list. */
    for (i = 0; i < args[1].u.sublist.span; i++) {
	/* Look for data in first sublist. */
	for (j = 0; j < sublist->span; j++) {
	    if (data_cmp(&base1[j], &base2[i]) == 0)
		break;
	}
	if (j == sublist->span) {
	    /* Data wasn't in first sublist; add it. */
	    sublist->list = list_add(sublist->list, &base2[i]);
	    sublist->span++;
	}
    }

    /* Pop the second list. */
    pop(1);
}

