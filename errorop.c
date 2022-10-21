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

    push_error(cur_frame->handler_info->error);
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

void op_throw(void)
{
    Data *args, error_arg;
    int num_args;
    String *str;

    if (!func_init_2_or_3(&args, &num_args, ERROR, STRING, 0))
	return;

    /* Throw the error. */
    str = string_dup(args[1].u.str);
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
    Data *args;
    List *traceback;

    if (!func_init_1(&args, ERROR))
	return;

    if (!cur_frame->handler_info) {
	throw(error_id, "Request for handler info outside handler.");
	return;
    }

    /* Abort the current frame and propagate an error in the caller. */
    traceback = list_dup(cur_frame->handler_info->traceback);
    frame_return();
    propagate_error(traceback, args[0].u.error);
}

