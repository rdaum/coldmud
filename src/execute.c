/* execute.c: Routines for executing C-- tasks. */

#define _POSIX_SOURCE

#include <stdio.h>
#include <stdarg.h>
#include <ctype.h>
#include "x.tab.h"
#include "execute.h"
#include "memory.h"
#include "config.h"
#include "ident.h"
#include "io.h"
#include "object.h"
#include "cache.h"
#include "util.h"
#include "opcodes.h"
#include "cmstring.h"
#include "log.h"
#include "decode.h"

#define STACK_STARTING_SIZE		(256 - STACK_MALLOC_DELTA)
#define ARG_STACK_STARTING_SIZE		(32 - ARG_STACK_MALLOC_DELTA)

extern int running;

static void execute(void);
static void out_of_ticks_error(void);
static void start_error(Ident error, String *explanation, Data *arg,
			List *location);
static List *traceback_add(List *traceback, Ident error);
static void fill_in_method_info(Data *d);

static Frame *frame_store = NULL;
static int frame_depth;
String *numargs_str;

Frame *cur_frame, *suspend_frame;
Connection *cur_conn;
Data *stack;
int stack_pos, stack_size;
int *arg_starts, arg_pos, arg_size;
long task_id;

void init_execute()
{
    stack = EMALLOC(Data, STACK_STARTING_SIZE);
    stack_size = STACK_STARTING_SIZE;
    stack_pos = 0;

    arg_starts = EMALLOC(int, ARG_STACK_STARTING_SIZE);
    arg_size = ARG_STACK_STARTING_SIZE;
    arg_pos = 0;
}

/* Execute a task by sending a message to an object. */
void task(Connection *conn, Dbref dbref, long message, int num_args, ...)
{
    va_list arg;

    /* Don't execute if a shutdown() has occured. */
    if (!running) {
	va_end(arg);
	return;
    }

    /* Set global variables. */
    cur_conn = conn;
    frame_depth = 0;

    va_start(arg, num_args);
    check_stack(num_args);
    while (num_args--)
	data_dup(&stack[stack_pos++], va_arg(arg, Data *));
    va_end(arg);

    /* Send the message.  If this is succesful, start the task by calling
     * execute(). */
    if (send_message(dbref, message, 0, 0) == NOT_AN_IDENT) {
	execute();
	if (stack_pos != 0)
	    panic("Stack not empty after interpretation.");
	task_id++;
    } else {
	pop(stack_pos);
    }
}

/* Execute a task by evaluating a method on an object. */
void task_method(Connection *conn, Object *obj, Method *method)
{
    cur_conn = conn;
    frame_start(obj, method, NOT_AN_IDENT, NOT_AN_IDENT, 0, 0);
    execute();

    if (stack_pos != 0)
	panic("Stack not empty after interpretation.");
}

long frame_start(Object *obj, Method *method, Dbref sender, Dbref caller,
		 int stack_start, int arg_start)
{
    Frame *frame;
    int i, num_args, num_rest_args;
    List *rest;
    Data *d;
    Number_buf nbuf1, nbuf2;

    num_args = stack_pos - arg_start;
    if (num_args < method->num_args || (num_args > method->num_args &&
					method->rest == -1)) {
	if (numargs_str)
	    string_discard(numargs_str);
	numargs_str = format("#%l.%s called with %s argument%s, requires %s%s",
			     obj->dbref, ident_name(method->name),
			     english_integer(num_args, nbuf1),
			     (num_args == 1) ? "" : "s",
			     (method->num_args == 0) ? "none" :
			     english_integer(method->num_args, nbuf2),
			     (method->rest == -1) ? "." : " or more.");
	return numargs_id;
    }

    if (frame_depth > MAX_CALL_DEPTH)
	return maxdepth_id;
    frame_depth++;

    if (method->rest != -1) {
	/* Make a list for the remaining arguments. */
	num_rest_args = stack_pos - (arg_start + method->num_args);
	rest = list_new(num_rest_args);

	/* Move aforementioned remaining arguments into the list. */
	d = list_empty_spaces(rest, num_rest_args);
	MEMCPY(d, &stack[stack_pos - num_rest_args], num_rest_args);
	stack_pos -= num_rest_args;

	/* Push the list onto the stack. */
	push_list(rest);
	list_discard(rest);
    }

    if (frame_store) {
	frame = frame_store;
	frame_store = frame_store->caller_frame;
    } else {
	frame = EMALLOC(Frame, 1);
    }

    frame->object = cache_grab(obj);
    frame->sender = sender;
    frame->caller = caller;
    frame->method = method_grab(method);
    cache_grab(method->object);
    frame->opcodes = method->opcodes;
    frame->pc = 0;
    frame->ticks = METHOD_TICKS;

    frame->specifiers = NULL;
    frame->handler_info = NULL;

    /* Set up stack indices. */
    frame->stack_start = stack_start;
    frame->var_start = arg_start;

    /* Initialize local variables to 0. */
    check_stack(method->num_vars);
    for (i = 0; i < method->num_vars; i++) {
	stack[stack_pos + i].type = INTEGER;
	stack[stack_pos + i].u.val = 0;
    }
    stack_pos += method->num_vars;

    frame->caller_frame = cur_frame;
    cur_frame = frame;

    return NOT_AN_IDENT;
}

void frame_return(void)
{
    int i;
    Frame *caller_frame = cur_frame->caller_frame;

    /* Free old data on stack. */
    for (i = cur_frame->stack_start; i < stack_pos; i++)
	data_discard(&stack[i]);
    stack_pos = cur_frame->stack_start;

    /* Let go of method and objects. */
    method_discard(cur_frame->method);
    cache_discard(cur_frame->object);
    cache_discard(cur_frame->method->object);

    /* Discard any error action specifiers. */
    while (cur_frame->specifiers)
	pop_error_action_specifier();

    /* Discard any handler information. */
    while (cur_frame->handler_info)
	pop_handler_info();

    /* Append frame to frame store for later reuse. */
    cur_frame->caller_frame = frame_store;
    frame_store = cur_frame;

    /* Return to the caller frame. */
    cur_frame = caller_frame;

    frame_depth--;
}

static void execute(void)
{
    int opcode;

    while (cur_frame) {
	if (!--(cur_frame->ticks)) {
	    out_of_ticks_error();
	} else {
	    opcode = cur_frame->opcodes[cur_frame->pc];
	    cur_frame->last_opcode = opcode;
	    cur_frame->pc++;
	    (*op_table[opcode].func)();
	}
    }
}

/* Requires cur_frame->pc to be the current instruction.  Do NOT call this
 * function if there is any possibility of the assignment failing before the
 * current instruction finishes. */
void anticipate_assignment(void)
{
    int opcode, ind;
    long id;
    Data *dp, d;

    opcode = cur_frame->opcodes[cur_frame->pc];
    if (opcode == SET_LOCAL) {
	/* Zero out local variable value. */
	dp = &stack[cur_frame->var_start +
		    cur_frame->opcodes[cur_frame->pc + 1]];
	data_discard(dp);
	dp->type = INTEGER;
	dp->u.val = 0;
    } else if (opcode == SET_OBJ_VAR) {
	/* Zero out the object variable, if it exists. */
	ind = cur_frame->opcodes[cur_frame->pc + 1];
	id = object_get_ident(cur_frame->method->object, ind);
	d.type = INTEGER;
	d.u.val = 0;
	object_assign_var(cur_frame->object, cur_frame->method->object,
			  id, &d);
    }
}

Ident pass_message(int stack_start, int arg_start)
{
    Method *method;
    Ident result;

    if (cur_frame->method->name == -1)
	return methodnf_id;

    /* Find the next method to handle the message. */
    method = object_find_next_method(cur_frame->object->dbref,
				     cur_frame->method->name,
				     cur_frame->method->object->dbref);
    if (!method)
	return methodnf_id;

    /* Start the new frame. */
    result = frame_start(cur_frame->object, method, cur_frame->sender,
			 cur_frame->caller, stack_start, arg_start);
    cache_discard(method->object);
    return result;
}

Ident send_message(Dbref dbref, Ident message, int stack_start, int arg_start)
{
    Object *obj;
    Method *method;
    Ident result;
    Dbref sender, caller;

    /* Get the target object from the cache. */
    obj = cache_retrieve(dbref);
    if (!obj)
	return objnf_id;

    /* Find the method to run. */
    method = object_find_method(obj->dbref, message);
    if (!method) {
	cache_discard(obj);
	return methodnf_id;
    }

    /* Start the new frame. */
    sender = (cur_frame) ? cur_frame->object->dbref : NOT_AN_IDENT;
    caller = (cur_frame) ? cur_frame->method->object->dbref : NOT_AN_IDENT;
    result = frame_start(obj, method, sender, caller, stack_start, arg_start);

    cache_discard(obj);
    cache_discard(method->object);
    return result;
}

void pop(int n)
{
    while (n--)
	data_discard(&stack[--stack_pos]);
}

void check_stack(int n)
{
    while (stack_pos + n > stack_size) {
	stack_size = stack_size * 2 + STACK_MALLOC_DELTA;
	stack = EREALLOC(stack, Data, stack_size);
    }
}

void push_int(long n)
{
    check_stack(1);
    stack[stack_pos].type = INTEGER;
    stack[stack_pos].u.val = n;
    stack_pos++;
}

void push_string(String *str)
{
    check_stack(1);
    stack[stack_pos].type = STRING;
    stack[stack_pos].u.str = string_dup(str);
    stack_pos++;
}

void push_dbref(Dbref dbref)
{
    check_stack(1);
    stack[stack_pos].type = DBREF;
    stack[stack_pos].u.dbref = dbref;
    stack_pos++;
}

void push_list(List *list)
{
    check_stack(1);
    stack[stack_pos].type = LIST;
    stack[stack_pos].u.list = list_dup(list);
    stack_pos++;
}

void push_dict(Dict *dict)
{
    check_stack(1);
    stack[stack_pos].type = DICT;
    stack[stack_pos].u.dict = dict_dup(dict);
    stack_pos++;
}

void push_symbol(Ident id)
{
    check_stack(1);
    stack[stack_pos].type = SYMBOL;
    stack[stack_pos].u.symbol = ident_dup(id);
    stack_pos++;
}

void push_error(Ident id)
{
    check_stack(1);
    stack[stack_pos].type = ERROR;
    stack[stack_pos].u.error = ident_dup(id);
    stack_pos++;
}

void push_buffer(Buffer *buf)
{
    check_stack(1);
    stack[stack_pos].type = BUFFER;
    stack[stack_pos].u.buffer = buffer_dup(buf);
    stack_pos++;
}

int func_init_0(void)
{
    int arg_start = arg_starts[--arg_pos];
    int num_args = stack_pos - arg_start;

    if (num_args)
	func_num_error(num_args, "none");
    else
	return 1;
    return 0;
}

int func_init_1(Data **args, int type1)
{
    int arg_start = arg_starts[--arg_pos];
    int num_args = stack_pos - arg_start;

    *args = &stack[arg_start];
    if (num_args != 1)
	func_num_error(num_args, "one");
    else if (type1 && stack[arg_start].type != type1)
	func_type_error("first", &stack[arg_start], english_type(type1));
    else
	return 1;
    return 0;
}

int func_init_2(Data **args, int type1, int type2)
{
    int arg_start = arg_starts[--arg_pos];
    int num_args = stack_pos - arg_start;

    *args = &stack[arg_start];
    if (num_args != 2)
	func_num_error(num_args, "two");
    else if (type1 && stack[arg_start].type != type1)
	func_type_error("first", &stack[arg_start], english_type(type1));
    else if (type2 && stack[arg_start + 1].type != type2)
	func_type_error("second", &stack[arg_start + 1], english_type(type2));
    else
	return 1;
    return 0;
}

int func_init_3(Data **args, int type1, int type2, int type3)
{
    int arg_start = arg_starts[--arg_pos];
    int num_args = stack_pos - arg_start;

    *args = &stack[arg_start];
    if (num_args != 3)
	func_num_error(num_args, "three");
    else if (type1 && stack[arg_start].type != type1)
	func_type_error("first", &stack[arg_start], english_type(type1));
    else if (type2 && stack[arg_start + 1].type != type2)
	func_type_error("second", &stack[arg_start + 1], english_type(type2));
    else if (type3 && stack[arg_start + 2].type != type3)
	func_type_error("third", &stack[arg_start + 2], english_type(type3));
    else
	return 1;
    return 0;
}

int func_init_0_or_1(Data **args, int *num_args, int type1)
{
    int arg_start = arg_starts[--arg_pos];

    *args = &stack[arg_start];
    *num_args = stack_pos - arg_start;
    if (*num_args > 1)
	func_num_error(*num_args, "at most one");
    else if (type1 && *num_args == 1 && stack[arg_start].type != type1)
	func_type_error("first", &stack[arg_start], english_type(type1));
    else
	return 1;
    return 0;
}

int func_init_1_or_2(Data **args, int *num_args, int type1, int type2)
{
    int arg_start = arg_starts[--arg_pos];

    *args = &stack[arg_start];
    *num_args = stack_pos - arg_start;
    if (*num_args < 1 || *num_args > 2)
	func_num_error(*num_args, "one or two");
    else if (type1 && stack[arg_start].type != type1)
	func_type_error("first", &stack[arg_start], english_type(type1));
    else if (type2 && *num_args == 2 && stack[arg_start + 1].type != type2)
	func_type_error("second", &stack[arg_start + 1], english_type(type2));
    else
	return 1;
    return 0;
}

int func_init_2_or_3(Data **args, int *num_args, int type1, int type2,
		     int type3)
{
    int arg_start = arg_starts[--arg_pos];

    *args = &stack[arg_start];
    *num_args = stack_pos - arg_start;
    if (*num_args < 2 || *num_args > 3)
	func_num_error(*num_args, "two or three");
    else if (type1 && stack[arg_start].type != type1)
	func_type_error("first", &stack[arg_start], english_type(type1));
    else if (type2 && stack[arg_start + 1].type != type2)
	func_type_error("second", &stack[arg_start + 1], english_type(type2));
    else if (type3 && *num_args == 3 && stack[arg_start + 2].type != type3)
	func_type_error("third", &stack[arg_start + 2], english_type(type3));
    else
	return 1;
    return 0;
}

int func_init_1_to_3(Data **args, int *num_args, int type1, int type2,
		     int type3)
{
    int arg_start = arg_starts[--arg_pos];

    *args = &stack[arg_start];
    *num_args = stack_pos - arg_start;
    if (*num_args < 1 || *num_args > 3)
	func_num_error(*num_args, "one to three");
    else if (type1 && stack[arg_start].type != type1)
	func_type_error("first", &stack[arg_start], english_type(type1));
    else if (type2 && *num_args >= 2 && stack[arg_start + 1].type != type2)
	func_type_error("second", &stack[arg_start + 1], english_type(type2));
    else if (type3 && *num_args == 3 && stack[arg_start + 2].type != type3)
	func_type_error("third", &stack[arg_start + 2], english_type(type3));
    else
	return 1;
    return 0;
}

void func_num_error(int num_args, char *required)
{
    Number_buf nbuf;

    throw(numargs_id, "Called with %s argument%s, requires %s.",
	  english_integer(num_args, nbuf),
	  (num_args == 1) ? "" : "s", required);
}

void func_type_error(char *which, Data *wrong, char *required)
{
    throw(type_id, "The %s argument (%D) is not %s.", which, wrong, required);
}

void throw(Ident error, char *fmt, ...)
{
    String *str;
    va_list arg;

    va_start(arg, fmt);
    str = vformat(fmt, arg);
    va_end(arg);
    interp_error(error, str);
    string_discard(str);
}

void interp_error(Ident error, String *explanation)
{
    List *location;
    Ident location_type;
    Data *d;
    char *opname;

    /* Get the opcode name and decide whether it's a function or not. */
    opname = op_table[cur_frame->last_opcode].name;
    location_type = (islower(*opname)) ? function_id : opcode_id;

    /* Construct a two-element list giving the location. */
    location = list_new(2);
    d = list_empty_spaces(location, 2);

    /* The first element is 'function or 'opcode. */
    d->type = SYMBOL;
    d->u.symbol = ident_dup(location_type);
    d++;

    /* The second element is the symbol for the opcode. */
    d->type = SYMBOL;
    d->u.symbol = ident_dup(op_table[cur_frame->last_opcode].symbol);

    start_error(error, explanation, NULL, location);
    list_discard(location);
}

void user_error(Ident error, String *explanation, Data *arg)
{
    List *location;
    Data *d;

    /* Construct a list giving the location. */
    location = list_new(5);
    d = list_empty_spaces(location, 5);

    /* The first element is 'method. */
    d->type = SYMBOL;
    d->u.symbol = ident_dup(method_id);
    d++;

    /* The second through fifth elements are the current method info. */
    fill_in_method_info(d);

    /* Return from the current method, and propagate the error. */
    frame_return();
    start_error(error, explanation, arg, location);
    list_discard(location);
}

static void out_of_ticks_error(void)
{
    static String *explanation;
    List *location;
    Data *d;

    /* Construct a list giving the location. */
    location = list_new(5);
    d = list_empty_spaces(location, 5);

    /* The first element is 'interpreter. */
    d->type = SYMBOL;
    d->u.symbol = ident_dup(interpreter_id);
    d++;

    /* The second through fifth elements are the current method info. */
    fill_in_method_info(d);

    /* Don't give the topmost frame a chance to return. */
    frame_return();

    if (!explanation)
	explanation = string_from_chars("Out of ticks", 12);
    start_error(methoderr_id, explanation, NULL, location);
    list_discard(location);
}

static void start_error(Ident error, String *explanation, Data *arg,
			List *location)
{
    List *error_condition, *traceback;
    Data *d;

    /* Construct a three-element list for the error condition. */
    error_condition = list_new(3);
    d = list_empty_spaces(error_condition, 3);

    /* The first element is the error code. */
    d->type = ERROR;
    d->u.error = ident_dup(error);
    d++;

    /* The second element is the explanation string. */
    d->type = STRING;
    d->u.str = string_dup(explanation);
    d++;

    /* The third element is the error arg, or 0 if there is none. */
    if (arg) {
	data_dup(d, arg);
    } else {
	d->type = INTEGER;
	d->u.val = 0;
    }

    /* Now construct a traceback, starting as a two-element list. */
    traceback = list_new(2);
    d = list_empty_spaces(traceback, 2);

    /* The first element is the error condition. */
    d->type = LIST;
    d->u.list = error_condition;
    d++;

    /* The second argument is the location. */
    d->type = LIST;
    d->u.list = list_dup(location);

    /* Start the error propagating.  This consumes traceback. */
    propagate_error(traceback, error);
}

/* Requires:	traceback is a list of strings containing the traceback
 *			information to date.  THIS FUNCTION CONSUMES TRACEBACK.
 *		id is an error id.  This function accounts for an error id
 *			which is "owned" by a data stack frame that we will
 *			nuke in the course of unwinding the call stack.
 *		str is a string containing an explanation of the error. */
void propagate_error(List *traceback, Ident error)
{
    int i, ind, propagate = 0;
    Error_action_specifier *spec;
    Error_list *errors;
    Handler_info *hinfo;

    /* If there's no current frame, drop all this on the floor. */
    if (!cur_frame) {
	list_discard(traceback);
	return;
    }

    /* Add message to traceback. */
    traceback = traceback_add(traceback, error);

    /* Look for an appropriate specifier in this frame. */
    for (; cur_frame->specifiers; pop_error_action_specifier()) {

	spec = cur_frame->specifiers;
	switch (spec->type) {

	  case CRITICAL:

	    /* We're in a critical expression.  Make a copy of the error,
	     * since it may currently be living in the region of the stack
	     * we're about to nuke. */
	    error = ident_dup(error);

	    /* Nuke the stack back to where we were at the beginning of the
	     * critical expression. */
	    pop(stack_pos - spec->stack_pos);

	    /* Jump to the end of the critical expression. */
	    cur_frame->pc = spec->u.critical.end;

	    /* Push the error on the stack, and discard our copy of it. */
	    push_error(error);
	    ident_discard(error);

	    /* Pop this error spec, discard the traceback, and continue
	     * processing. */
	    pop_error_action_specifier();
	    list_discard(traceback);
	    return;

	  case PROPAGATE:

	    /* We're in a propagate expression.  Set the propagate flag and
	     * keep going. */
	    propagate = 1;
	    break;

	  case CATCH:

	    /* We're in a catch statement.  Get the error list index. */
	    ind = spec->u.catch.error_list;

	    /* If the index is -1, this was a 'catch any' statement.
	     * Otherwise, check if this error code is in the error list. */
	    if (spec->u.catch.error_list != -1) {
		errors = &cur_frame->method->error_lists[ind];
		for (i = 0; i < errors->num_errors; i++) {
		    if (errors->error_ids[i] == error)
			break;
		}

		/* Keep going if we didn't find the error. */
		if (i == errors->num_errors)
		    break;
	    }

	    /* We catch this error.  Make a handler info structure and push it
	     * onto the stack. */
	    hinfo = EMALLOC(Handler_info, 1);
	    hinfo->traceback = traceback;
	    hinfo->error = ident_dup(error);
	    hinfo->next = cur_frame->handler_info;
	    cur_frame->handler_info = hinfo;

	    /* Pop the stack down to where we were at the beginning of the
	     * catch statement.  This may nuke our copy of error, but we don't
	     * need it any more. */
	    pop(stack_pos - spec->stack_pos);

	    /* Jump to the handler expression, pop this specifier, and continue
	     * processing. */
	    cur_frame->pc = spec->u.catch.handler;
	    pop_error_action_specifier();
	    return;

	}
    }

    /* There was no handler in the current frame. */
    frame_return();
    propagate_error(traceback, (propagate) ? error : methoderr_id);
}

static List *traceback_add(List *traceback, Ident error)
{
    List *frame;
    Data *d, frame_data;

    /* Construct a list giving information about this stack frame. */
    frame = list_new(5);
    d = list_empty_spaces(frame, 5);

    /* First element is the error code. */
    d->type = ERROR;
    d->u.error = ident_dup(error);
    d++;

    /* Second through fifth elements are the current method info. */
    fill_in_method_info(d);

    /* Add the frame to the list. */
    frame_data.type = LIST;
    frame_data.u.list = frame;
    traceback = list_add(traceback, &frame_data);
    list_discard(frame);

    return traceback;
}

void pop_error_action_specifier()
{ 
    Error_action_specifier *old;

    /* Pop the first error action specifier off that stack. */
    old = cur_frame->specifiers;
    cur_frame->specifiers = old->next;
    free(old);
}

void pop_handler_info()
{
    Handler_info *old;

    /* Free the data in the first handler info specifier, and pop it off that
     * stack. */
    old = cur_frame->handler_info;
    list_discard(old->traceback);
    ident_discard(old->error);
    cur_frame->handler_info = old->next;
    free(old);
}

static void fill_in_method_info(Data *d)
{
    Ident method_name;

    /* The method name, or 0 for eval. */
    method_name = cur_frame->method->name;
    if (method_name == NOT_AN_IDENT) {
	d->type = INTEGER;
	d->u.val = 0;
    } else {
	d->type = SYMBOL;
	d->u.val = method_name;
    }
    d++;

    /* The current object. */
    d->type = DBREF;
    d->u.dbref = cur_frame->object->dbref;
    d++;

    /* The defining object. */
    d->type = DBREF;
    d->u.dbref = cur_frame->method->object->dbref;
    d++;

    /* The line number. */
    d->type = INTEGER;
    d->u.val = line_number(cur_frame->method, cur_frame->pc);
}

