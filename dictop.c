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

