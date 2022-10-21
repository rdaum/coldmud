/* arithop.c: Arithmetic and relational operators. */

#define _POSIX_SOURCE

#include <string.h>
#include "x.tab.h"
#include "operator.h"
#include "execute.h"
#include "data.h"
#include "ident.h"
#include "cmstring.h"
#include "util.h"

/* All functions in this file are interpreter opcodes, so they require that the
 * interpreter data (the globals in execute.c) be in a state consistent with
 * interpretation.  They may modify the interpreter data by pushing and popping
 * the data stack or by throwing exceptions. */

/* Effects: Pops the top value on the stack and pushes its logical inverse. */
void op_not(void)
{
    Data *d = &stack[stack_pos - 1];
    int val = !data_true(d);

    /* Replace d with the inverse of its truth value. */
    data_discard(d);
    d->type = INTEGER;
    d->u.val = val;
}

/* Effects: If the top value on the stack is an integer, pops it and pushes its
 *	    its arithmetic inverse. */
void op_negate(void)
{
    Data *d = &stack[stack_pos - 1];

    /* Make sure we're taking the negative of an integer. */
    if (d->type != INTEGER) {
	throw(type_id, "Argument (%D) is not an integer.", d);
    } else {
	/* Replace d with -d. */
	d->u.val *= -1;
    }
}

/* Effects: If the top two values on the stack are integers, pops them and
 *	    pushes their product. */
void op_multiply(void)
{
    Data *d1 = &stack[stack_pos - 2];
    Data *d2 = &stack[stack_pos - 1];

    /* Make sure we're multiplying two integers. */
    if (d1->type != INTEGER) {
	throw(type_id, "Left side (%D) is not an integer.", d1);
    } else if (d2->type != INTEGER) {
	throw(type_id, "Right side (%D) is not an integer.", d2);
    } else {
	/* Replace d1 with d1 * d2, and pop d2. */
	d1->u.val *= d2->u.val;
	pop(1);
    }
}

/* Effects: If the top two values on the stack are integers and the second is
 *	    not zero, pops them, divides the first by the second, and pushes
 *	    the quotient. */
void op_divide(void)
{
    Data *d1 = &stack[stack_pos - 2];
    Data *d2 = &stack[stack_pos - 1];

    /* Make sure we're multiplying two integers. */
    if (d1->type != INTEGER) {
	throw(type_id, "Left side (%D) is not an integer.", d1);
    } else if (d2->type != INTEGER) {
	throw(type_id, "Right side (%D) is not an integer.", d2);
    } else if (d2->u.val == 0) {
	throw(div_id, "Attempt to divide %D by zero.", d1);
    } else {
	/* Replace d1 with d1 / d2, and pop d2. */
	d1->u.val /= d2->u.val;
	pop(1);
    }
}

/* Effects: If the top two values on the stack are integers and the second is
 *	    not zero, pops them, divides the first by the second, and pushes
 *	    the remainder. */
void op_modulo(void)
{
    Data *d1 = &stack[stack_pos - 2];
    Data *d2 = &stack[stack_pos - 1];

    /* Make sure we're multiplying two integers. */
    if (d1->type != INTEGER) {
	throw(type_id, "Left side (%D) is not an integer.", d1);
    } else if (d2->type != INTEGER) {
	throw(type_id, "Right side (%D) is not an integer.", d2);
    } else if (d2->u.val == 0) {
	throw(div_id, "Attempt to divide %D by zero.", d1);
    } else {
	/* Replace d1 with d1 % d2, and pop d2. */
	d1->u.val %= d2->u.val;
	pop(1);
    }
}

/* Effects: If the top two values on the stack are integers, pops them and
 *	    pushes their sum.  If the top two values are strings, pops them,
 *	    concatenates the second onto the first, and pushes the result. */
void op_add(void)
{
    Data *d1 = &stack[stack_pos - 2];
    Data *d2 = &stack[stack_pos - 1];

    /* If we're adding two integers or two strings, replace d1 with d1+d2 and
     * discard d2. */
    if (d1->type == INTEGER && d2->type == INTEGER) {
	/* Replace d1 with d1 + d2, and pop d2. */
	d1->u.val += d2->u.val;
    } else if (d1->type == STRING && d2->type == STRING) {
	anticipate_assignment();
	d1->u.str = string_add(d1->u.str, d2->u.str);
    } else if (d1->type == LIST && d2->type == LIST) {
	anticipate_assignment();
	d1->u.list = list_append(d1->u.list, d2->u.list);
    } else {
	throw(type_id, "Cannot add %D and %D.", d1, d2);
	return;
    }
    pop(1);
}

/* Effects: Adds two lists.  (This is used for [@foo, ...];) */
void op_splice_add(void)
{
    Data *d1 = &stack[stack_pos - 2];
    Data *d2 = &stack[stack_pos - 1];

    /* No need to check if d2 is a list, due to code generation. */
    if (d1->type != LIST) {
	throw(type_id, "%D is not a list.", d1);
	return;
    }

    anticipate_assignment();
    d1->u.list = list_append(d1->u.list, d2->u.list);
    pop(1);
}

/* Effects: If the top two values on the stack are integers, pops them and
 *	    pushes their difference. */
void op_subtract(void)
{
    Data *d1 = &stack[stack_pos - 2];
    Data *d2 = &stack[stack_pos - 1];

    /* Make sure we're multiplying two integers. */
    if (d1->type != INTEGER) {
	throw(type_id, "Left side (%D) is not an integer.", d1);
    } else if (d2->type != INTEGER) {
	throw(type_id, "Right side (%D) is not an integer.", d2);
    } else {
	/* Replace d1 with d1 - d2, and pop d2. */
	d1->u.val -= d2->u.val;
	pop(1);
    }
}

/* Effects: Pops the top two values on the stack and pushes 1 if they are
 *	    equal, 0 if not. */
void op_equal(void)
{
    Data *d1 = &stack[stack_pos - 2];
    Data *d2 = &stack[stack_pos - 1];
    int val = (data_cmp(d1, d2) == 0);

    pop(2);
    push_int(val);
}

/* Effects: Pops the top two values on the stack and returns 1 if they are
 *	    unequal, 0 if they are equal. */   
void op_not_equal(void)
{
    Data *d1 = &stack[stack_pos - 2];
    Data *d2 = &stack[stack_pos - 1];
    int val = (data_cmp(d1, d2) != 0);

    pop(2);
    push_int(val);
}

/* Definition: Two values are comparable if they are of the same type and that
 * 	       type is integer or string. */

/* Effects: If the top two values on the stack are comparable, pops them and
 *	    pushes 1 if the first is greater than the second, 0 if not. */
void op_greater(void)
{
    Data *d1 = &stack[stack_pos - 2];
    Data *d2 = &stack[stack_pos - 1];
    int val, t = d1->type;

    if (d1->type != d2->type) {
	throw(type_id, "%D and %D are not of the same type.", d1, d2);
    } else if (t != INTEGER && t != STRING) {
	throw(type_id, "%D and %D are not integers or strings.", d1, d2);
    } else {
	/* Discard d1 and d2 and push the appropriate truth value. */
	val = (data_cmp(d1, d2) > 0);
	pop(2);
	push_int(val);
    }
}

/* Effects: If the top two values on the stack are comparable, pops them and
 *	    pushes 1 if the first is greater than or equal to the second, 0 if
 *	    not. */
void op_greater_or_equal(void)
{
    Data *d1 = &stack[stack_pos - 2];
    Data *d2 = &stack[stack_pos - 1];
    int val, t = d1->type;

    if (d1->type != d2->type) {
	throw(type_id, "%D and %D are not of the same type.", d1, d2);
    } else if (t != INTEGER && t != STRING) {
	throw(type_id, "%D and %D are not integers or strings.", d1, d2);
    } else {
	/* Discard d1 and d2 and push the appropriate truth value. */
	val = (data_cmp(d1, d2) >= 0);
	pop(2);
	push_int(val);
    }
}

/* Effects: If the top two values on the stack are comparable, pops them and
 *	    pushes 1 if the first is less than the second, 0 if not. */
void op_less(void)
{
    Data *d1 = &stack[stack_pos - 2];
    Data *d2 = &stack[stack_pos - 1];
    int val, t = d1->type;

    if (d1->type != d2->type) {
	throw(type_id, "%D and %D are not of the same type.", d1, d2);
    } else if (t != INTEGER && t != STRING) {
	throw(type_id, "%D and %D are not integers or strings.", d1, d2);
    } else {
	/* Discard d1 and d2 and push the appropriate truth value. */
	val = (data_cmp(d1, d2) < 0);
	pop(2);
	push_int(val);
    }
}

/* Effects: If the top two values on the stack are comparable, pops them and
 *	    pushes 1 if the first is greater than or equal to the second, 0 if
 *	    not. */
void op_less_or_equal(void)
{
    Data *d1 = &stack[stack_pos - 2];
    Data *d2 = &stack[stack_pos - 1];
    int val, t = d1->type;

    if (d1->type != d2->type) {
	throw(type_id, "%D and %D are not of the same type.", d1, d2);
    } else if (t != INTEGER && t != STRING) {
	throw(type_id, "%D and %D are not integers or strings.", d1, d2);
    } else {
	/* Discard d1 and d2 and push the appropriate truth value. */
	val = (data_cmp(d1, d2) <= 0);
	pop(2);
	push_int(val);
    }
}

/* Effects: If the top value on the stack is a string or a list, pops the top
 *	    two values on the stack and pushes the location of the first value
 *	    in the second (where the first element is 1), or 0 if the first
 *	    value does not exist in the second. */
void op_in(void)
{
    Data *d1 = &stack[stack_pos - 2];
    Data *d2 = &stack[stack_pos - 1];
    int pos;
    char *s;

    if (d2->type == LIST) {
	pos = list_search(d2->u.list, d1);
	pop(2);
	push_int(pos + 1);
	return;
    }

    if (d1->type != STRING || d2->type != STRING) {
	throw(type_id, "Cannot search for %D in %D.", d1, d2);
	return;
    }

    s = strcstr(string_chars(d1->u.str), string_chars(d2->u.str));
    if (s) {
	pos = s - string_chars(d1->u.str);
    } else {
	pos = -1;
    }

    pop(2);
    push_int(pos + 1);
}

