/* bufferop.c: Function operators for buffer manipulation. */

#include "x.tab.h"
#include "execute.h"
#include "ident.h"

void op_buffer_len(void)
{
    Data *args;
    int len;

    if (!func_init_1(&args, BUFFER))
	return;
    len = buffer_len(args[0].u.buffer);
    pop(1);
    push_int(len);
}

void op_buffer_retrieve(void)
{
    Data *args;
    int c, pos;

    if (!func_init_2(&args, BUFFER, INTEGER))
	return;
    pos = args[1].u.val - 1;
    if (pos < 0) {
	throw(range_id, "Position (%d) is less than one.", pos + 1);
    } else if (pos >= buffer_len(args[0].u.buffer)) {
	throw(range_id, "Position (%d) is greater than buffer length (%d).",
	      pos + 1, buffer_len(args[0].u.buffer));
    } else {
	c = buffer_retrieve(args[0].u.buffer, pos);
	pop(2);
	push_int(c);
    }
}

void op_buffer_append(void)
{
    Data *args;

    if (!func_init_2(&args, BUFFER, BUFFER))
	return;
    args[0].u.buffer = buffer_append(args[0].u.buffer, args[1].u.buffer);
    pop(1);
}

void op_buffer_replace(void)
{
    Data *args;
    int pos;

    if (!func_init_3(&args, BUFFER, INTEGER, INTEGER))
	return;
    pos = args[1].u.val - 1;
    if (pos < 0) {
	throw(range_id, "Position (%d) is less than one.", pos + 1);
	return;
    } else if (pos >= buffer_len(args[0].u.buffer)) {
	throw(range_id, "Position (%d) is greater than buffer length (%d).",
	      pos + 1, buffer_len(args[0].u.buffer));
	return;
    }
    args[0].u.buffer = buffer_replace(args[0].u.buffer, pos, args[1].u.val);
    pop(2);
}

void op_buffer_add(void)
{
    Data *args;

    if (!func_init_2(&args, BUFFER, INTEGER))
	return;
    args[0].u.buffer = buffer_add(args[0].u.buffer, args[1].u.val);
    pop(1);
}

void op_buffer_truncate(void)
{
    Data *args;
    int pos;

    if (!func_init_2(&args, BUFFER, INTEGER))
	return;
    pos = args[1].u.val;
    if (pos < 0) {
	throw(range_id, "Position (%d) is less than zero.", pos);
	return;
    } else if (pos > buffer_len(args[0].u.buffer)) {
	throw(range_id, "Position (%d) is greater than buffer length (%d).",
	      pos, buffer_len(args[0].u.buffer));
	return;
    }
    args[0].u.buffer = buffer_truncate(args[0].u.buffer, pos);
    pop(1);
}

void op_buffer_to_strings(void)
{
    Data *args;
    int num_args;
    List *list;
    Buffer *sep;

    if (!func_init_1_or_2(&args, &num_args, BUFFER, BUFFER))
	return;
    sep = (num_args == 2) ? args[1].u.buffer : NULL;
    list = buffer_to_strings(args[0].u.buffer, sep);
    pop(num_args);
    push_list(list);
    list_discard(list);
}

void op_buffer_from_strings(void)
{
    Data *args, *base;
    int num_args, i;
    Buffer *buf, *sep;

    if (!func_init_1_or_2(&args, &num_args, LIST, BUFFER))
	return;
    sep = (num_args == 2) ? args[1].u.buffer : NULL;
    base = data_dptr(&args[0]);
    for (i = 0; i < args[0].u.substr.span; i++) {
	if (base[i].type != STRING) {
	    throw(type_id, "List element %d (%D) not a string.", i + 1,
		  &base[i]);
	    return;
	}
    }
    buf = buffer_from_strings(base, args[0].u.substr.span, sep);
    pop(num_args);
    push_buffer(buf);
    buffer_discard(buf);
}

