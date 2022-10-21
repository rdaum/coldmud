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
    len = list_length(args[0].u.list);
    pop(1);
    push_int(len);
}

void op_sublist(void)
{
    int num_args, start, span, list_len;
    Data *args;

    /* Accept a list, an integer, and an optional integer. */
    if (!func_init_2_or_3(&args, &num_args, LIST, INTEGER, INTEGER))
	return;

    list_len = list_length(args[0].u.list);
    start = args[1].u.val - 1;
    span = (num_args == 3) ? args[2].u.val : list_len - start;

    /* Make sure range is in bounds. */
    if (start < 0) {
	throw(range_id, "Start (%d) less than one", start + 1);
    } else if (span < 0) {
	throw(range_id, "Sublist length (%d) less than zero", span);
    } else if (start + span > list_len) {
	throw(range_id, "Sublist extends to %d, past end of list (length %d)",
	      start + span, list_len);
    } else {
	/* Replace first argument with sublist, and pop other arguments. */
	anticipate_assignment();
	args[0].u.list = list_sublist(args[0].u.list, start, span);
	pop(num_args - 1);
    }
}

void op_insert(void)
{
    int pos, list_len;
    Data *args;

    /* Accept a list, an integer offset, and a data value of any type. */
    if (!func_init_3(&args, LIST, INTEGER, 0))
	return;

    pos = args[1].u.val - 1;
    list_len = list_length(args[0].u.list);

    if (pos < 0) {
	throw(range_id, "Position (%d) less than one", pos + 1);
    } else if (pos > list_len) {
	throw(range_id, "Position (%d) beyond end of list (length %d)",
	      pos + 1, list_len);
    } else {
	/* Modify the list and pop the offset and data. */
	anticipate_assignment();
	args[0].u.list = list_insert(args[0].u.list, pos, &args[2]);
	pop(2);
    }
}

void op_replace(void)
{
    int pos, list_len;
    Data *args;

    /* Accept a list, an integer offset, and a data value of any type. */
    if (!func_init_3(&args, LIST, INTEGER, 0))
	return;

    list_len = list_length(args[0].u.list);
    pos = args[1].u.val - 1;

    if (pos < 0) {
	throw(range_id, "Position (%d) less than one", pos + 1);
    } else if (pos > list_len - 1) {
	throw(range_id, "Position (%d) greater than length of list (%d)",
	      pos + 1, list_len);
    } else {
	/* Modify the list and pop the offset and data. */
	anticipate_assignment();
	args[0].u.list = list_replace(args[0].u.list, pos, &args[2]);
	pop(2);
    }
}

void op_delete(void)
{
    int pos, list_len;
    Data *args;

    /* Accept a list and an integer offset. */
    if (!func_init_2(&args, LIST, INTEGER))
	return;

    list_len = list_length(args[0].u.list);
    pos = args[1].u.val - 1;

    if (pos < 0) {
	throw(range_id, "Position (%d) less than one", pos + 1);
    } else if (pos > list_len - 1) {
	throw(range_id, "Position (%d) greater than length of list (%d)",
	      pos + 1, list_len);
    } else {
	/* Modify the list and pop the offset. */
	anticipate_assignment();
	args[0].u.list = list_delete(args[0].u.list, pos);
	pop(1);
    }
}

void op_setadd(void)
{
    Data *args;

    /* Accept a list and a data value of any type. */
    if (!func_init_2(&args, LIST, 0))
	return;

    /* Add args[1] to args[0] and pop args[1]. */
    anticipate_assignment();
    args[0].u.list = list_setadd(args[0].u.list, &args[1]);
    pop(1);
}

void op_setremove(void)
{
    Data *args;

    /* Accept a list and a data value of any type. */
    if (!func_init_2(&args, LIST, 0))
	return;

    /* Remove args[1] from args[0] and pop args[1]. */
    anticipate_assignment();
    args[0].u.list = list_setremove(args[0].u.list, &args[1]);
    pop(1);
}

void op_union(void)
{
    Data *args;

    /* Accept two lists. */
    if (!func_init_2(&args, LIST, LIST))
	return;

    /* Union args[1] into args[0] and pop args[1]. */
    anticipate_assignment();
    args[0].u.list = list_union(args[0].u.list, args[1].u.list);
    pop(1);
}

