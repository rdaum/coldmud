/* dictop.c: Function operators for dictionary manipulation. */

#define _POSIX_SOURCE

#include "x.tab.h"
#include "operator.h"
#include "execute.h"
#include "data.h"
#include "ident.h"
#include "memory.h"

void op_dict_keys(void)
{
    Data *args;
    List *keys;

    if (!func_init_1(&args, DICT))
	return;

    keys = dict_keys(args[0].u.dict);
    pop(1);
    push_list(keys);
    list_discard(keys);
}

void op_dict_add(void)
{
    Data *args;

    if (!func_init_3(&args, DICT, 0, 0))
	return;

    anticipate_assignment();
    args[0].u.dict = dict_add(args[0].u.dict, &args[1], &args[2]);
    pop(2);
}

void op_dict_del(void)
{
    Data *args;

    if (!func_init_2(&args, DICT, 0))
	return;

    if (!dict_contains(args[0].u.dict, &args[1])) {
	throw(keynf_id, "Key (%D) is not in the dictionary.", &args[1]);
    } else {
	anticipate_assignment();
	args[0].u.dict = dict_del(args[0].u.dict, &args[1]);
	pop(1);
    }
}

void op_dict_add_elem(void)
{
    Data *args, oldval, d;
    Sublist *sublist;

    if (!func_init_3(&args, DICT, 0, 0))
	return;

    if (dict_find(args[0].u.dict, &args[1], &oldval) == keynf_id) {
	oldval.type = LIST;
	sublist_set_to_full_list(&oldval.u.sublist, list_new(0));
    } else if (oldval.type != LIST) {
	throw(type_id, "Value for %D (%D) is not a list.", &args[0], &oldval);
	data_discard(&oldval);
	return;
    }

    /* Anticipate assignment, and also temporarily replace the value for the
     * key args[1] with 0 to get rid of the dictionary's reference count on
     * the list. */
    anticipate_assignment();
    d.type = INTEGER;
    d.u.val = 0;
    args[0].u.dict = dict_add(args[0].u.dict, &args[1], &d);

    /* Add the element to the list. */
    sublist = &oldval.u.sublist;
    sublist_truncate(sublist);
    sublist->list = list_add(sublist->list, &args[2]);
    sublist->span++;

    /* Replace the dictionary's value for args[1] with the updated list. */
    args[0].u.dict = dict_add(args[0].u.dict, &args[1], &oldval);
    data_discard(&oldval);

    pop(2);
}

void op_dict_del_elem(void)
{
    Data *args, *elem, oldval, d;
    Sublist *sublist;
    int pos;

    if (!func_init_3(&args, DICT, 0, 0))
	return;

    if (dict_find(args[0].u.dict, &args[1], &oldval) == keynf_id) {
	throw(keynf_id, "Key (%D) is not in the dictionary.", &args[1]);
	return;
    }
    if (oldval.type != LIST) {
	throw(type_id, "Value for %D (%D) is not a list.", &args[1], &oldval);
	data_discard(&oldval);
	return;
    }

    sublist = &oldval.u.sublist;
    pos = sublist_search(sublist, &args[2]);
    if (pos == -1) {
	/* The key's not there; make no modifications. */
	data_discard(&oldval);
	pop(2);
	return;
    }

    anticipate_assignment();

    if (sublist->span == 1) {
	/* args[2] is the only element in the list; delete the key from the
	 * dictionary. */
	args[0].u.dict = dict_del(args[0].u.dict, &args[1]);
	data_discard(&oldval);
	pop(2);
	return;
    }

    /* Temporarily replace the dictionary's value for the key args[1] with 0
     * to get rid of the dictionary's reference count the list. */
    d.type = INTEGER;
    d.u.val = 0;
    args[0].u.dict = dict_add(args[0].u.dict, &args[1], &d);

    /* Remove the list element corresponding to args[2]. */
    sublist_truncate(sublist);
    elem = &sublist->list->el[sublist->start + pos];
    data_discard(elem);
    MEMMOVE(elem, elem + 1, sublist->span - 1 - pos);
    sublist->list->len--;
    sublist->span--;

    /* Reassign the key in the dictionary. */
    args[0].u.dict = dict_add(args[0].u.dict, &args[1], &oldval);
    data_discard(&oldval);

    pop(2);
}

void op_dict_contains(void)
{
    Data *args;
    int val;

    if (!func_init_2(&args, DICT, 0))
	return;

    val = dict_contains(args[0].u.dict, &args[1]);
    pop(2);
    push_int(val);
}

