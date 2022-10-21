/* errorop.c: Error handling operations. */

#include "x.tab.h"
#include "operator.h"
#include "execute.h"
#include "ident.h"

void op_error_func(void)
{
    if (!func_init_0())
	return;

    if (!cur_frame->handler_info) {
	throw(error_id, "Request for handler info outside handler.");
	return;
    }

    push_error(cur_frame->handler_info->id);
}

void op_traceback(void)
{
    if (!func_init_0())
	return;

    if (!cur_frame->handler_info) {
	throw(error_id, "Request for handler info outside handler.");
	return;
    }

    push_list(cur_frame->handler_info->traceback);
}

void op_error_str(void)
{
    String *str;

    if (!func_init_0())
	return;

    if (!cur_frame->handler_info) {
	throw(error_id, "Request for handler info outside handler.");
	return;
    }

    str = cur_frame->handler_info->traceback->el[0].u.substr.str;
    push_string(str);
    stack[stack_pos - 1].u.substr.start = 7;
    stack[stack_pos - 1].u.substr.span -= 7;
    string_discard(str);
}

void op_error_arg(void)
{
    if (!func_init_0())
	return;

    if (!cur_frame->handler_info) {
	throw(error_id, "Request for handler info outside handler.");
	return;
    }

    check_stack(1);
    data_dup(&stack[stack_pos++], &cur_frame->handler_info->arg);
}

void op_throw(void)
{
    Data *args, error_arg;
    int num_args;
    String *str;

    if (!func_init_2_or_3(&args, &num_args, ERROR, STRING, 0))
	return;

    /* Convert args[1]'s substring into a string. */
    if (args[1].u.substr.start == 0 && (args[1].u.substr.span
					== args[1].u.substr.str->len))
	str = string_dup(args[1].u.substr.str);
    else
	str = string_from_chars(data_sptr(&args[1]), args[1].u.substr.span);

    /* Throw the error. */
    if (num_args == 3) {
	data_dup(&error_arg, &args[2]);
	user_error(args[0].u.error, str, &error_arg);
	data_discard(&error_arg);
    } else {
	user_error(args[0].u.error, str, NULL);
    }

    string_discard(str);
}

void op_rethrow(void)
{
    Data *args, error_arg;
    List *traceback;

    if (!func_init_1(&args, ERROR))
	return;

    if (!cur_frame->handler_info) {
	throw(error_id, "Request for handler info outside handler.");
	return;
    }

    /* Abort the current frame and propagate an error in the caller. */
    traceback = list_dup(cur_frame->handler_info->traceback);
    data_dup(&error_arg, &cur_frame->handler_info->arg);
    frame_return();
    propagate_error(traceback, args[0].u.error, &error_arg);
    data_discard(&error_arg);
}

