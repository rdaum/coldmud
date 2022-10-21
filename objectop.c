/* objectop.c: Function operators acting on the current object. */

#include "x.tab.h"
#include "operator.h"
#include "execute.h"
#include "data.h"
#include "ident.h"
#include "object.h"
#include "grammar.h"
#include "config.h"
#include "cache.h"
#include "dbpack.h"

void op_add_parameter(void)
{
    Data *args;
    long result;

    /* Accept a symbol argument a data value to assign to the variable. */
    if (!func_init_1(&args, SYMBOL))
	return;

    result = object_add_param(cur_frame->object, args[0].u.symbol);
    if (result == paramexists_id) {
	throw(paramexists_id,
	      "Parameter %I already exists.", args[0].u.symbol);
    } else {
	pop(1);
	push_int(1);
    }
}

void op_parameters(void)
{
    List *params;
    Object *obj;
    int i;
    Var *var;
    Data d;

    /* Accept no arguments. */
    if (!func_init_0())
	return;

    /* Construct the list of variable names. */
    obj = cur_frame->object;
    params = list_new(0);
    d.type = SYMBOL;
    for (i = 0; i < obj->vars.size; i++) {
	var = &obj->vars.tab[i];
	if (var->name != -1 && var->class == obj->dbref) {
	    d.u.symbol = var->name;
	    params = list_add(params, &d);
	}
    }

    /* Push the list onto the stack. */
    push_list(params);
    list_discard(params);
}

void op_del_parameter(void)
{
    Data *args;
    long result;

    /* Accept one symbol argument. */
    if (!func_init_1(&args, SYMBOL))
	return;

    result = object_del_param(cur_frame->object, args[0].u.symbol);
    if (result == paramnf_id) {
	throw(paramnf_id, "Parameter %I does not exist.", args[0].u.symbol);
    } else {
	pop(1);
	push_int(1);
    }
}

void op_set_var(void)
{
    Data *args;
    long result;

    /* Accept a symbol for the variable name and a data value of any type. */
    if (!func_init_2(&args, SYMBOL, 0))
	return;

    result = object_assign_var(cur_frame->object, cur_frame->method->object,
			       args[0].u.symbol, &args[1]);
    if (result == paramnf_id) {
	throw(paramnf_id, "No such parameter %I.", args[0].u.symbol);
    } else {
	pop(2);
	push_int(1);
    }
}

void op_get_var(void)
{
    Data *args, d;
    long result;

    /* Accept a symbol argument. */
    if (!func_init_1(&args, SYMBOL))
	return;

    result = object_retrieve_var(cur_frame->object, cur_frame->method->object,
				 args[0].u.symbol, &d);
    if (result == paramnf_id) {
	throw(paramnf_id, "No such parameter %I.", args[0].u.symbol);
    } else {
	pop(1);
	data_dup(&stack[stack_pos], &d);
	stack_pos++;
    }
}

void op_compile(void)
{
    int i, lines;
    Data *args, *code;
    Method *method;
    List *errors;

    /* Accept a list of lines of code and a symbol for the name. */
    if (!func_init_2(&args, LIST, SYMBOL))
	return;

    /* Make sure that every element in the code list is a string. */
    code = data_dptr(&args[0]);
    lines = args[0].u.sublist.span;
    for (i = 0; i < lines; i++) {
	if (code[i].type != STRING) {
	    throw(type_id, "Element %d of code is %D, not a string.", i,
		  &code[i]);
	    return;
	}
    }

    method = compile(cur_frame->object, code, lines, &errors);
    if (method) {
	object_add_method(cur_frame->object, args[1].u.symbol, method);
	method_discard(method);
    }
    pop(2);
    stack[stack_pos].type = LIST;
    sublist_set_to_full_list(&stack[stack_pos].u.sublist, errors);
    stack_pos++;
}

void op_methods(void)
{
    List *methods;
    Object *obj;
    int i, j;

    /* Accept no arguments. */
    if (!func_init_0())
	return;

    /* Construct the list of method names. */
    obj = cur_frame->object;
    methods = list_new(obj->methods.size);
    j = 0;
    for (i = 0; i < obj->methods.size; i++) {
	if (obj->methods.tab[i].m) {
	    methods->el[j].type = SYMBOL;
	    methods->el[j].u.symbol = ident_dup(obj->methods.tab[i].m->name);
	    j++;
	}
    }
    methods->len = j;

    /* Push the list onto the stack. */
    check_stack(1);
    stack[stack_pos].type = LIST;
    sublist_set_to_full_list(&stack[stack_pos].u.sublist, methods);
    stack_pos++;
}

void op_find_method(void)
{
    Data *args;
    Method *method;

    /* Accept a symbol argument giving the method name. */
    if (!func_init_1(&args, SYMBOL))
	return;

    /* Look for the method on the current object. */
    method = object_find_method(cur_frame->object->dbref, args[0].u.symbol);
    pop(1);
    if (method) {
	push_dbref(method->object->dbref);
	cache_discard(method->object);
    } else {
	throw(methodnf_id, "Method %s not found.",
	      ident_name(args[0].u.symbol));
    }
}

void op_find_next_method(void)
{
    Data *args;
    Method *method;

    /* Accept a symbol argument giving the method name, and a dbref giving the
     * object to search past. */
    if (!func_init_2(&args, SYMBOL, DBREF))
	return;

    /* Look for the method on the current object. */
    method = object_find_next_method(cur_frame->object->dbref,
				     args[0].u.symbol, args[1].u.dbref);
    if (method) {
	push_dbref(method->object->dbref);
	cache_discard(method->object);
    } else {
	throw(methodnf_id, "Method %s not found.",
	      ident_name(args[0].u.symbol));
    }
}

void op_list_method(void)
{
    int num_args, indent, parens;
    Data *args;
    List *code;

    /* Accept a symbol for the method name, an optional integer for the
     * indentation, and an optional integer to specify full
     * parenthesization. */
    if (!func_init_1_to_3(&args, &num_args, SYMBOL, INTEGER, INTEGER))
	return;

    indent = (num_args >= 2) ? args[1].u.val : DEFAULT_INDENT;
    indent = (indent < 0) ? 0 : indent;
    parens = (num_args == 3) ? (args[2].u.val != 0) : 0;
    code = object_list_method(cur_frame->object, args[0].u.symbol, indent,
			      parens);

    if (code) {
	pop(num_args);
	push_list(code);
	list_discard(code);
    } else {
	throw(methodnf_id, "Method %s not found.",
	      ident_name(args[0].u.symbol));
    }
}

void op_del_method(void)
{
    Data *args;

    /* Accept a symbol for the method name. */
    if (!func_init_1(&args, SYMBOL))
	return;

    if (!object_del_method(cur_frame->object, args[0].u.symbol)) {
	throw(methodnf_id, "No method named %s was found.",
	      ident_name(args[0].u.symbol));
    } else {
	pop(1);
	push_int(1);
    }
}

void op_parents(void)
{
    /* Accept no arguments. */
    if (!func_init_0())
	return;

    /* Push the parents list onto the stack. */
    push_list(cur_frame->object->parents);
}

void op_children(void)
{
    /* Accept no arguments. */
    if (!func_init_0())
	return;

    /* Push the children list onto the stack. */
    push_list(cur_frame->object->children);
}

void op_ancestors(void)
{
    List *ancestors;

    /* Accept no arguments. */
    if (!func_init_0())
	return;

    /* Get an ancestors list from the object. */
    ancestors = object_ancestors(cur_frame->object->dbref);
    push_list(ancestors);
    list_discard(ancestors);
}

void op_has_ancestor(void)
{
    Data *args;
    int result;

    /* Accept a dbref to check as an ancestor. */
    if (!func_init_1(&args, DBREF))
	return;

    result = object_has_ancestor(cur_frame->object->dbref, args[0].u.dbref);
    pop(1);
    push_int(result);
}

void op_size(void)
{
    /* Accept no arguments. */
    if (!func_init_0())
	return;

    /* Push size of current object. */
    push_int(size_object(cur_frame->object));
}

