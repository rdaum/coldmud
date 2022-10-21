/* syntaxop.c: Operators for opcodes generated by language syntax. */

#define _POSIX_SOURCE

#include <time.h>
#include "x.tab.h"
#include "operator.h"
#include "execute.h"
#include "data.h"
#include "memory.h"
#include "ident.h"
#include "cache.h"
#include "cmstring.h"
#include "lookup.h"

void op_comment(void)
{
    /* Do nothing, just increment the program counter past the comment. */
    cur_frame->pc++;
}

void op_pop(void)
{
    pop(1);
}

void op_set_local(void)
{
    Data *var;

    /* Move data in top of stack to variable. */
    var = &stack[cur_frame->var_start + cur_frame->opcodes[cur_frame->pc++]];
    data_discard(var);
    *var = stack[stack_pos - 1];
    stack_pos--;
    /* Transfers control of reference count from top of stack to var. */
}

void op_set_obj_var(void)
{
    long ind, id, result;
    Data *val;

    ind = cur_frame->opcodes[cur_frame->pc++];
    id = object_get_ident(cur_frame->method->object, ind);
    val = &stack[stack_pos - 1];
    result = object_assign_var(cur_frame->object, cur_frame->method->object,
			       id, val);
    if (result == paramnf_id)
	throw(paramnf_id, "No such parameter %I.", id);
    else
	pop(1);
}

void op_if(void)
{
    /* Jump if the condition is false. */
    if (!data_true(&stack[stack_pos - 1]))
	cur_frame->pc = cur_frame->opcodes[cur_frame->pc];
    else
	cur_frame->pc++;
    pop(1);
}

void op_else(void)
{
    cur_frame->pc = cur_frame->opcodes[cur_frame->pc];
}

void op_for_range(void)
{
    int var;
    Data *range;

    var = cur_frame->var_start + cur_frame->opcodes[cur_frame->pc + 1];
    range = &stack[stack_pos - 2];

    /* Make sure we have an integer range. */
    if (range[0].type != INTEGER || range[1].type != INTEGER) {
	throw(type_id, "Range bounds (%D, %D) are not both integers.",
	      &range[0], &range[1]);
	return;
    }

    if (range[0].u.val > range[1].u.val) {
	/* We're finished; pop the range and jump to the end. */
	pop(2);
	cur_frame->pc = cur_frame->opcodes[cur_frame->pc];
    } else {
	/* Replace the index variable with the lower range bound, increment the
	 * range, and continue. */
	data_discard(&stack[var]);
	stack[var] = range[0];
	range[0].u.val++;
	cur_frame->pc += 2;
    }
}

void op_for_list(void)
{
    Data *counter;
    Data *domain;
    int var, len;
    List *pair;

    counter = &stack[stack_pos - 1];
    domain = &stack[stack_pos - 2];
    var = cur_frame->var_start + cur_frame->opcodes[cur_frame->pc + 1];

    /* Make sure we're iterating over a list.  We know the counter is okay. */
    if (domain->type != LIST && domain->type != DICT) {
	throw(type_id, "Domain (%D) is not a list or dictionary.", domain);
	return;
    }

    len = (domain->type == LIST) ? list_length(domain->u.list)
				 : dict_size(domain->u.dict);

    if (counter->u.val >= len) {
	/* We're finished; pop the list and counter and jump to the end. */
	pop(2);
	cur_frame->pc = cur_frame->opcodes[cur_frame->pc];
	return;
    }

    /* Replace the index variable with the next list element and increment
     * the counter. */
    data_discard(&stack[var]);
    if (domain->type == LIST) {
	data_dup(&stack[var], list_elem(domain->u.list, counter->u.val));
    } else {
	pair = dict_key_value_pair(domain->u.dict, counter->u.val);
	stack[var].type = LIST;
	stack[var].u.list = pair;
    }
    counter->u.val++;
    cur_frame->pc += 2;
}

void op_while(void)
{
    if (!data_true(&stack[stack_pos - 1])) {
	/* The condition expression is false.  Jump to the end of the loop. */
	cur_frame->pc = cur_frame->opcodes[cur_frame->pc];
    } else {
	/* The condition expression is true; continue. */
	cur_frame->pc += 2;
    }
    pop(1);
}

void op_switch(void)
{
    /* This opcode doesn't actually do anything; it just provides a place-
     * holder for a break statement. */
    cur_frame->pc++;
}

void op_case_value(void)
{
    /* There are two expression values on the stack: the controlling expression
     * for the switch statement, and the value for this case.  If they are
     * equal, pop them off the stack and jump to the body of this case.
     * Otherwise, just pop the value for this case, and go on. */
    if (data_cmp(&stack[stack_pos - 2], &stack[stack_pos - 1]) == 0) {
	pop(2);
	cur_frame->pc = cur_frame->opcodes[cur_frame->pc];
    } else {
	pop(1);
	cur_frame->pc++;
    }
}

void op_case_range(void)
{
    Data *switch_expr, *range;
    int is_match;

    switch_expr = &stack[stack_pos - 3];
    range = &stack[stack_pos - 2];

    /* Verify that range[0] and range[1] make a value type. */
    if (range[0].type != range[1].type) {
	throw(type_id, "%D and %D are not of the same type.",
	      &range[0], &range[1]);
	return;
    } else if (range[0].type != INTEGER && range[0].type != STRING) {
	throw(type_id, "%D and %D are not integers or strings.", &range[0],
	      &range[1]);
	return;
    }

    /* Decide if this is a match.  In order for it to be a match, switch_expr
     * must be of the same type as the range expressions, must be greater than
     * or equal to the lower bound of the range, and must be less than or equal
     * to the upper bound of the range. */
    is_match = (switch_expr->type == range[0].type);
    is_match = (is_match) && (data_cmp(switch_expr, &range[0]) >= 0);
    is_match = (is_match) && (data_cmp(switch_expr, &range[1]) <= 0);

    /* If it's a match, pop all three expressions and jump to the case body.
     * Otherwise, just pop the range and go on. */
    if (is_match) {
	pop(3);
	cur_frame->pc = cur_frame->opcodes[cur_frame->pc];
    } else {
	pop(2);
	cur_frame->pc++;
    }
}

void op_last_case_value(void)
{
    /* There are two expression values on the stack: the controlling expression
     * for the switch statement, and the value for this case.  If they are
     * equal, pop them off the stack and go on.  Otherwise, just pop the value
     * for this case, and jump to the next case. */
    if (data_cmp(&stack[stack_pos - 2], &stack[stack_pos - 1]) == 0) {
	pop(2);
	cur_frame->pc++;
    } else {
	pop(1);
	cur_frame->pc = cur_frame->opcodes[cur_frame->pc];
    }
}

void op_last_case_range(void)
{
    Data *switch_expr, *range;
    int is_match;

    switch_expr = &stack[stack_pos - 3];
    range = &stack[stack_pos - 2];

    /* Verify that range[0] and range[1] make a value type. */
    if (range[0].type != range[1].type) {
	throw(type_id, "%D and %D are not of the same type.",
	      &range[0], &range[1]);
	return;
    } else if (range[0].type != INTEGER && range[0].type != STRING) {
	throw(type_id, "%D and %D are not integers or strings.", &range[0],
	      &range[1]);
	return;
    }

    /* Decide if this is a match.  In order for it to be a match, switch_expr
     * must be of the same type as the range expressions, must be greater than
     * or equal to the lower bound of the range, and must be less than or equal
     * to the upper bound of the range. */
    is_match = (switch_expr->type == range[0].type);
    is_match = (is_match) && (data_cmp(switch_expr, &range[0]) >= 0);
    is_match = (is_match) && (data_cmp(switch_expr, &range[1]) <= 0);

    /* If it's a match, pop all three expressions and go on.  Otherwise, just
     * pop the range and jump to the next case. */
    if (is_match) {
	pop(3);
	cur_frame->pc++;
    } else {
	pop(2);
	cur_frame->pc = cur_frame->opcodes[cur_frame->pc];
    }
}

void op_end_case(void)
{
    /* Jump to end of switch statement. */
    cur_frame->pc = cur_frame->opcodes[cur_frame->pc];
}

void op_default(void)
{
    /* Pop the controlling switch expression. */
    pop(1);
}

void op_end(void)
{
    /* Jump to the beginning of the loop or condition expression. */
    cur_frame->pc = cur_frame->opcodes[cur_frame->pc];
}

void op_break(void)
{
    int n, op;

    /* Get loop instruction from argument. */
    n = cur_frame->opcodes[cur_frame->pc];

    /* If it's a for loop, pop the loop information on the stack (either a list
     * and an index, or two range bounds. */
    op = cur_frame->opcodes[n];
    if (op == FOR_LIST || op == FOR_RANGE)
	pop(2);

    /* Jump to the end of the loop. */
    cur_frame->pc = cur_frame->opcodes[n + 1];
}

void op_continue(void)
{
    /* Jump back to the beginning of the loop.  If it's a WHILE loop, jump back
     * to the beginning of the condition expression. */
    cur_frame->pc = cur_frame->opcodes[cur_frame->pc];
    if (cur_frame->opcodes[cur_frame->pc] == WHILE)
	cur_frame->pc = cur_frame->opcodes[cur_frame->pc + 2];
}

void op_return(void)
{
    long dbref;

    dbref = cur_frame->object->dbref;
    frame_return();
    if (cur_frame)
	push_dbref(dbref);
}

void op_return_expr(void)
{
    Data *val;

    /* Return, and push frame onto caller stack.  Transfers reference count to
     * caller stack.  Assumes (correctly) that there is space on the caller
     * stack. */
    val = &stack[--stack_pos];
    frame_return();
    if (cur_frame) {
	stack[stack_pos] = *val;
	stack_pos++;
    } else {
	data_discard(val);
    }
}

void op_catch(void)
{
    Error_action_specifier *spec;

    /* Make a new error action specifier and push it onto the stack. */
    spec = EMALLOC(Error_action_specifier, 1);
    spec->type = CATCH;
    spec->stack_pos = stack_pos;
    spec->u.catch.handler = cur_frame->opcodes[cur_frame->pc++];
    spec->u.catch.error_list = cur_frame->opcodes[cur_frame->pc++];
    spec->next = cur_frame->specifiers;
    cur_frame->specifiers = spec;
}

void op_catch_end(void)
{
    /* Pop the error action specifier for the catch statement, and jump past
     * the handler. */
    pop_error_action_specifier();
    cur_frame->pc = cur_frame->opcodes[cur_frame->pc];
}

void op_handler_end(void)
{
    pop_handler_info();
}

void op_zero(void)
{
    /* Push a zero. */
    push_int(0);
}

void op_one(void)
{
    /* Push a one. */
    push_int(1);
}

void op_integer(void)
{
    push_int(cur_frame->opcodes[cur_frame->pc++]);
}

void op_string(void)
{
    String *str;
    int ind = cur_frame->opcodes[cur_frame->pc++];

    str = object_get_string(cur_frame->method->object, ind);
    push_string(str);
}

void op_dbref(void)
{
    int id;

    id = cur_frame->opcodes[cur_frame->pc++];
    push_dbref(id);
}

void op_symbol(void)
{
    int ind, id;

    ind = cur_frame->opcodes[cur_frame->pc++];
    id = object_get_ident(cur_frame->method->object, ind);
    push_symbol(id);
}

void op_error(void)
{
    int ind, id;

    ind = cur_frame->opcodes[cur_frame->pc++];
    id = object_get_ident(cur_frame->method->object, ind);
    push_error(id);
}

void op_name(void)
{
    int ind, id;
    long dbref;

    ind = cur_frame->opcodes[cur_frame->pc++];
    id = object_get_ident(cur_frame->method->object, ind);
    if (lookup_retrieve_name(id, &dbref))
	push_dbref(dbref);
    else
	throw(namenf_id, "Can't find object name %I.", id);
}

void op_get_local(void)
{
    int var;

    /* Push value of local variable on stack. */
    var = cur_frame->var_start + cur_frame->opcodes[cur_frame->pc++];
    check_stack(1);
    data_dup(&stack[stack_pos], &stack[var]);
    stack_pos++;
}

void op_get_obj_var(void)
{
    long ind, id, result;
    Data val;

    /* Look for variable, and push it onto the stack if we find it. */
    ind = cur_frame->opcodes[cur_frame->pc++];
    id = object_get_ident(cur_frame->method->object, ind);
    result = object_retrieve_var(cur_frame->object, cur_frame->method->object,
				 id, &val);
    if (result == paramnf_id) {
	throw(paramnf_id, "No such parameter %I.", id);
    } else {
	check_stack(1);
	stack[stack_pos] = val;
	stack_pos++;
    }
}

void op_start_args(void)
{
    /* Resize argument stack if necessary. */
    if (arg_pos == arg_size) {
	arg_size = arg_size * 2 + ARG_STACK_MALLOC_DELTA;
	arg_starts = EREALLOC(arg_starts, int, arg_size);
    }

    /* Push stack position onto argument start stack. */
    arg_starts[arg_pos] = stack_pos;
    arg_pos++;
}

void op_pass(void)
{
    int arg_start, result;

    arg_start = arg_starts[--arg_pos];

    /* Attempt to pass the message we're processing. */
    result = pass_message(arg_start, arg_start);

    if (result == numargs_id)
	interp_error(result, numargs_str);
    else if (result == methodnf_id)
	throw(result, "No next method found.");
    else if (result == maxdepth_id)
	throw(result, "Maximum call depth exceeded.");
}

void op_message(void)
{
    int arg_start, result, ind;
    Data *target;
    long message, dbref;
    Frob *frob;

    ind = cur_frame->opcodes[cur_frame->pc++];
    message = object_get_ident(cur_frame->method->object, ind);

    arg_start = arg_starts[--arg_pos];
    target = &stack[arg_start - 1];

    if (target->type == DBREF) {
	dbref = target->u.dbref;
    } else if (target->type == FROB) {
	/* Convert the frob to its rep and pass as first argument. */
	frob = target->u.frob;
	dbref = frob->class;
	*target = frob->rep;
	arg_start--;
	TFREE(frob, 1);
    } else {
	throw(type_id, "Target (%D) is not a dbref or frob.", target);
	return;
    }

    /* Attempt to send the message. */
    result = send_message(dbref, message, target - stack, arg_start);

    if (result == numargs_id)
	interp_error(result, numargs_str);
    else if (result == objnf_id)
	throw(result, "Target (#%l) not found.", dbref);
    else if (result == methodnf_id)
	throw(result, "Method %I not found.", message);
    else if (result == maxdepth_id)
	throw(result, "Maximum call depth exceeded.");
}

void op_expr_message(void)
{
    int arg_start, result;
    Data *target, *message_data;
    long dbref, message;

    arg_start = arg_starts[--arg_pos];
    target = &stack[arg_start - 2];
    message_data = &stack[arg_start - 1];

    if (message_data->type != SYMBOL) {
	throw(type_id, "Message (%D) is not a symbol.", message_data);
	return;
    }
    message = ident_dup(message_data->u.symbol);

    if (target->type == DBREF) {
	dbref = target->u.dbref;
    } else if (target->type == FROB) {
	dbref = target->u.frob->class;

	/* Pass frob rep as first argument (where the message data is now). */
	data_discard(message_data);
	*message_data = target->u.frob->rep;
	arg_start--;

	/* Discard the frob and replace it with a dummy value. */
	TFREE(target->u.frob, 1);
	target->type = INTEGER;
	target->u.val = 0;
    } else {
	ident_discard(message);
	throw(type_id, "Target (%D) is not a dbref or frob.", target);
	return;
    }

    /* Attempt to send the message. */
    result = send_message(dbref, message, target - stack, arg_start);
    ident_discard(message);

    if (result == numargs_id)
	interp_error(result, numargs_str);
    else if (result == objnf_id)
	throw(result, "Target (#%l) not found.", dbref);
    else if (result == methodnf_id)
	throw(result, "Method %I not found.", message);
    else if (result == maxdepth_id)
	throw(result, "Maximum call depth exceeded.");
}

void op_list(void)
{
    int start, len;
    List *list;
    Data *d;

    start = arg_starts[--arg_pos];
    len = stack_pos - start;

    /* Move the elements into a list. */
    list = list_new(len);
    d = list_empty_spaces(list, len);
    MEMCPY(d, &stack[start], len);
    stack_pos = start;

    /* Push the list onto the stack where elements began. */
    push_list(list);
    list_discard(list);
}

void op_dict(void)
{
    int start, len;
    List *list;
    Data *d;
    Dict *dict;

    start = arg_starts[--arg_pos];
    len = stack_pos - start;

    /* Move the elements into a list. */
    list = list_new(len);
    d = list_empty_spaces(list, len);
    MEMCPY(d, &stack[start], len);
    stack_pos = start;

    /* Construct a dictionary from the list. */
    dict = dict_from_slices(list);
    list_discard(list);
    if (!dict) {
	throw(type_id, "Arguments were not all two-element lists.");
    } else {
	push_dict(dict);
	dict_discard(dict);
    }
}

void op_buffer(void)
{
    int start, len, i;
    Buffer *buf;

    start = arg_starts[--arg_pos];
    len = stack_pos - start;
    for (i = 0; i < len; i++) {
	if (stack[start + i].type != INTEGER) {
	    throw(type_id, "Element %d (%D) is not an integer.", i + 1,
		  &stack[start + i]);
	    return;
	}
    }
    buf = buffer_new(len);
    for (i = 0; i < len; i++)
	buf->s[i] = ((unsigned long) stack[start + i].u.val) % (1 << 8);
    stack_pos = start;
    push_buffer(buf);
    buffer_discard(buf);
}

void op_frob(void)
{
    Data *class, *rep;

    class = &stack[stack_pos - 2];
    rep = &stack[stack_pos - 1];
    if (class->type != DBREF) {
	throw(type_id, "Class (%D) is not a dbref.", class);
    } else if (rep->type != LIST && rep->type != DICT) {
	throw(type_id, "Rep (%D) is not a list or dictionary.", rep);
    } else {
	class->type = FROB;
	class->u.frob = TMALLOC(Frob, 1);
	class->u.frob->class = class->u.dbref;
	data_dup(&class->u.frob->rep, rep);
	pop(1);
    }
}

void op_index(void)
{
    Data *d, *ind, element;
    int i, len;
    String *str;

    d = &stack[stack_pos - 2];
    ind = &stack[stack_pos - 1];
    if (d->type != LIST && d->type != STRING && d->type != DICT) {
	throw(type_id, "Array (%D) is not a list, string, or dictionary.", d);
	return;
    } else if (d->type != DICT && ind->type != INTEGER) {
	throw(type_id, "Offset (%D) is not an integer.", ind);
	return;
    } 

    if (d->type == DICT) {
	/* Get the value corresponding to a key. */
	if (dict_find(d->u.dict, ind, &element) == keynf_id) {
	    throw(keynf_id, "Key (%D) is not in the dictionary.", ind);
	} else {
	    pop(1);
	    data_discard(d);
	    *d = element;
	}
	return;
    }

    /* It's not a dictionary.  Make sure ind is within bounds. */
    len = (d->type == LIST) ? list_length(d->u.list) : string_length(d->u.str);
    i = ind->u.val - 1;
    if (i < 0) {
	throw(range_id, "Index (%d) is less than one.", i + 1);
    } else if (i > len - 1) {
	throw(range_id, "Index (%d) is greater than length (%d)",
	      i + 1, len);
    } else {
	/* Replace d with the element of d numbered by ind. */
	if (d->type == LIST) {
	    data_dup(&element, list_elem(d->u.list, i));
	    pop(2);
	    stack[stack_pos] = element;
	    stack_pos++;
	} else {
	    str = string_from_chars(string_chars(d->u.str) + i, 1);
	    pop(2);
	    push_string(str);
	    string_discard(str);
	}
    }
}

void op_and(void)
{
    /* Short-circuit if left side is false; otherwise discard. */
    if (!data_true(&stack[stack_pos - 1])) {
	cur_frame->pc = cur_frame->opcodes[cur_frame->pc];
    } else {
	cur_frame->pc++;
	pop(1);
    }
}

void op_or(void)
{
    /* Short-circuit if left side is true; otherwise discard. */
    if (data_true(&stack[stack_pos - 1])) {
	cur_frame->pc = cur_frame->opcodes[cur_frame->pc];
    } else {
	cur_frame->pc++;
	pop(1);
    }
}

void op_splice(void)
{
    int i;
    List *list;
    Data *d;

    if (stack[stack_pos - 1].type != LIST) {
	throw(type_id, "%D is not a list.", &stack[stack_pos - 1]);
	return;
    }
    list = stack[stack_pos - 1].u.list;

    /* Splice the list onto the stack, overwriting the list. */
    check_stack(list_length(list) - 1);
    for (d = list_first(list); d; d = list_next(list, d))
	data_dup(&stack[stack_pos - 1 + i], d);
    stack_pos += list_length(list) - 1;

    list_discard(list);
}

void op_critical(void)
{
    Error_action_specifier *spec;

    /* Make an error action specifier for the critical expression, and push it
     * onto the stack. */
    spec = EMALLOC(Error_action_specifier, 1);
    spec->type = CRITICAL;
    spec->stack_pos = stack_pos;
    spec->u.critical.end = cur_frame->opcodes[cur_frame->pc++];
    spec->next = cur_frame->specifiers;
    cur_frame->specifiers = spec;
}

void op_critical_end(void)
{
    pop_error_action_specifier();
}

void op_propagate(void)
{
    Error_action_specifier *spec;

    /* Make an error action specifier for the critical expression, and push it
     * onto the stack. */
    spec = EMALLOC(Error_action_specifier, 1);
    spec->type = PROPAGATE;
    spec->stack_pos = stack_pos;
    spec->u.propagate.end = cur_frame->opcodes[cur_frame->pc++];
    spec->next = cur_frame->specifiers;
    cur_frame->specifiers = spec;
}

void op_propagate_end(void)
{
    pop_error_action_specifier();
}

