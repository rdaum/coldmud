/* dataop.c: Function operators for miscellaneous data manipulation. */

#define _POSIX_SOURCE

#include <stdlib.h>
#include "x.tab.h"
#include "operator.h"
#include "execute.h"
#include "data.h"
#include "ident.h"
#include "cache.h"
#include "util.h"

void op_type(void)
{
    Data *args;
    int type;

    /* Accept one argument of any type. */
    if (!func_init_1(&args, 0))
	return;

    /* Replace argument with symbol for type name. */
    type = args[0].type;
    pop(1);
    push_symbol(data_type_id(type));
}

void op_class(void)
{
    Data *args;
    long class;

    /* Accept one argument of frob type. */
    if (!func_init_1(&args, FROB))
	return;

    /* Replace argument with class. */
    class = args[0].u.frob.class;
    pop(1);
    push_dbref(class);
}

void op_toint(void)
{
    Data *args;
    long val;

    /* Accept a string or integer to convert into an integer. */
    if (!func_init_1(&args, 0))
	return;

    if (args[0].type == STRING) {
	val = atoln(data_sptr(&args[0]), args[0].u.substr.span);
    } else if (args[0].type == DBREF) {
	val = args[0].u.dbref;
    } else {
	throw(type_id, "The first argument (%D) is not an integer or string.",
	      &args[0]);
    }
    pop(1);
    push_int(val);
}

void op_tostr(void)
{
    Data *args;
    String *str;

    /* Accept one argument of any type. */
    if (!func_init_1(&args, 0))
	return;

    /* Replace the argument with its text version. */
    str = data_tostr(&args[0]);
    pop(1);
    push_string(str);
    string_discard(str);
}

void op_toliteral(void)
{
    Data *args;
    String *str;

    /* Accept one argument of any type. */
    if (!func_init_1(&args, 0))
	return;

    /* Replace the argument with its unparsed version. */
    str = data_to_literal(&args[0]);
    pop(1);
    push_string(str);
    string_discard(str);
}

void op_todbref(void)
{
    Data *args;

    /* Accept an integer to convert into a dbref. */
    if (!func_init_1(&args, INTEGER))
	return;

    args[0].u.dbref = args[0].u.val;
    args[0].type = DBREF;
}

void op_tosym(void)
{
    Data *args;
    long sym;

    /* Accept one string argument. */
    if (!func_init_1(&args, STRING))
	return;

    substring_truncate(&args[0].u.substr);
    sym = ident_get(data_sptr(&args[0]));
    pop(1);
    push_symbol(sym);
}

void op_toerr(void)
{
    Data *args;
    long error;

    /* Accept one string argument. */
    if (!func_init_1(&args, STRING))
	return;

    substring_truncate(&args[0].u.substr);
    error = ident_get(data_sptr(&args[0]));
    pop(1);
    push_error(error);
}

void op_valid(void)
{
    Data *args;
    int is_valid;

    /* Accept one argument of any type (only dbrefs can be valid, though). */
    if (!func_init_1(&args, 0))
	return;

    is_valid = (args[0].type == DBREF && cache_check(args[0].u.dbref));
    pop(1);
    push_int(is_valid);
}

