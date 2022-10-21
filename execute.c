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
static List *traceback_add(List *traceback, long ident);
static char *cur_method_name(void);

static Frame *frame_store = NULL;
static int frame_depth;
String *numargs_str;

Frame *cur_frame, *suspend_frame;
Connection *cur_conn;
Data *stack;
int stack_pos, stack_size;
int *arg_starts, arg_pos, arg_size;
long task_id;

void init_execute(void)
{
    stack = EMALLOC(Data, STACK_STARTING_SIZE);
    stack_size = STACK_STARTING_SIZE;
    stack_pos = 0;

    arg_starts = EMALLOC(int, ARG_STACK_STARTING_SIZE);
    arg_size = ARG_STACK_STARTING_SIZE;
    arg_pos = 0;
}

/* Execute a task by sending a message to an object. */
void task(Connection *conn, long dbref, long message, int num_args, ...)
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

long frame_start(Object *obj, Method *method, long sender, long caller,
		 int stack_start, int arg_start)
{
    Frame *frame;
    int i, num_args, rest_start;
    List *rest;
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
	rest_start = arg_start + method->num_args;
	rest = list_new(stack_pos - rest_start);
	MEMCPY(rest->el, &stack[rest_start], stack_pos - rest_start);
	stack_pos = rest_start;
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

long pass_message(int stack_start, int arg_start)
{
    Method *method;
    long result;

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

long send_message(long dbref, long message, int stack_start, int arg_start)
{
    Object *obj;
    Method *method;
    long result, sender, caller;

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
    substr_set_to_full_string(&stack[stack_pos].u.substr, string_dup(str));
    stack_pos++;
}

void push_dbref(long dbref)
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
    sublist_set_to_full_list(&stack[stack_pos].u.sublist, list_dup(list));
    stack_pos++;
}

void push_dict(Dict *dict)
{
    check_stack(1);
    stack[stack_pos].type = DICT;
    stack[stack_pos].u.dict = dict_dup(dict);
    stack_pos++;
}

void push_symbol(long id)
{
    check_stack(1);
    stack[stack_pos].type = SYMBOL;
    stack[stack_pos].u.symbol = ident_dup(id);
    stack_pos++;
}

void push_error(long id)
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

void throw(long id, char *fmt, ...)
{
    String *str;
    va_list arg;

    va_start(arg, fmt);
    str = vformat(fmt, arg);
    va_end(arg);
    interp_error(id, str);
    string_discard(str);
}

void interp_error(long id, String *str)
{
    List *traceback;
    String *errstr;
    char *opname;

    traceback = list_new(2);

    /* Set first line of traceback to be explanation string. */
    errstr = format("ERROR: %s", str->s);
    traceback->el[0].type = STRING;
    substr_set_to_full_string(&traceback->el[0].u.substr, errstr);

    /* Set second line to be source of error. */
    opname = op_table[cur_frame->last_opcode].name;
    if (islower(*opname))
	errstr = format("Thrown by function %s().", opname);
    else
	errstr = format("Thrown by interpreter opcode %s.", opname);
    traceback->el[1].type = STRING;
    substr_set_to_full_string(&traceback->el[1].u.substr, errstr);

    /* Propagate the error. */
    propagate_error(traceback, id, NULL);
}

void user_error(long id, String *str, Data *arg)
{
    List *traceback;
    String *errstr;

    traceback = list_new(2);

    /* Set first line of traceback to be explanation string. */
    errstr = format("ERROR: %s", str->s);
    traceback->el[0].type = STRING;
    substr_set_to_full_string(&traceback->el[0].u.substr, errstr);

    /* Set second line to be source of error. */
    errstr = format("Thrown by #%l.%s (defined #%l), line %d.",
		    cur_frame->object->dbref, cur_method_name(),
		    cur_frame->method->object->dbref,
		    line_number(cur_frame->method, cur_frame->pc));
    traceback->el[1].type = STRING;
    substr_set_to_full_string(&traceback->el[1].u.substr, errstr);

    /* Propagate the error after exiting this frame. */
    frame_return();
    propagate_error(traceback, id, arg);
}

static void out_of_ticks_error(void)
{
    List *traceback;
    String *errstr;

    traceback = list_new(2);

    /* Set first line of traceback to be explanation string. */
    errstr = string_from_chars("ERROR: Out of ticks.", 20);
    traceback->el[0].type = STRING;
    substr_set_to_full_string(&traceback->el[0].u.substr, errstr);

    /* Set second line to be source of error. */
    errstr = string_from_chars("Thrown by interpreter.", 22);
    traceback->el[1].type = STRING;
    substr_set_to_full_string(&traceback->el[1].u.substr, errstr);

    /* Propagate the error after exiting this frame. */
    traceback = traceback_add(traceback, ticks_id);
    frame_return();
    propagate_error(traceback, methoderr_id, NULL);
}

/* Requires:	traceback is a list of strings containing the traceback
 *			information to date.  THIS FUNCTION CONSUMES TRACEBACK.
 *		id is an error id.  This function accounts for an error id
 *			which is "owned" by a data stack frame that we will
 *			nuke in the course of unwinding the call stack.
 *		str is a string containing an explanation of the error. */
void propagate_error(List *traceback, long id, Data *arg)
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
    traceback = traceback_add(traceback, id);

    /* Look for an appropriate specifier in this frame. */
    for (; cur_frame->specifiers; pop_error_action_specifier()) {

	spec = cur_frame->specifiers;
	switch (spec->type) {

	  case CRITICAL:

	    /* We're in a critical expression.  Make a copy of the error id,
	     * since it may currently be living in the region of the stack
	     * we're about to nuke. */
	    id = ident_dup(id);

	    /* Nuke the stack back to where we were at the beginning of the
	     * critical expression. */
	    pop(stack_pos - spec->stack_pos);

	    /* Jump to the end of the critical expression. */
	    cur_frame->pc = spec->u.critical.end;

	    /* Push the error id on the stack, and discard our copy of it. */
	    push_error(id);
	    ident_discard(id);

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
		    if (errors->error_ids[i] == id)
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
	    hinfo->id = ident_dup(id);
	    if (arg) {
		data_dup(&hinfo->arg, arg);
	    } else {
		hinfo->arg.type = INTEGER;
		hinfo->arg.u.val = 0;
	    }
	    hinfo->next = cur_frame->handler_info;
	    cur_frame->handler_info = hinfo;

	    /* Pop the stack down to where we were at the beginning of the
	     * catch statement.  This may nuke our copy of id, but we don't
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
    propagate_error(traceback, (propagate) ? id : methoderr_id, arg);
}

static List *traceback_add(List *traceback, long id)
{
    Data d;
    String *errstr;

    errstr = format("~%I in #%l.%s (defined on #%l), line %d",
		    id, cur_frame->object->dbref, cur_method_name(),
		    cur_frame->method->object->dbref,
		    line_number(cur_frame->method, cur_frame->pc));
    d.type = STRING;
    substr_set_to_full_string(&d.u.substr, errstr);
    traceback = list_add(traceback, &d);
    string_discard(errstr);
    return traceback;
}

void pop_error_action_specifier(void)
{ 
    Error_action_specifier *old;

    /* Pop the first error action specifier off that stack. */
    old = cur_frame->specifiers;
    cur_frame->specifiers = old->next;
    free(old);
}

void pop_handler_info(void)
{
    Handler_info *old;

    /* Free the data in the first handler info specifier, and pop it off that
     * stack. */
    old = cur_frame->handler_info;
    list_discard(old->traceback);
    ident_discard(old->id);
    data_discard(&old->arg);
    cur_frame->handler_info = old->next;
    free(old);
}

static char *cur_method_name(void)
{
    if (cur_frame->method->name == -1)
	return "<eval>";
    else
	return ident_name(cur_frame->method->name);
}

