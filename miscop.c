/* miscop.c: Miscellaneous operations. */

#define _POSIX_SOURCE

#include <stdlib.h>
#include <time.h>
#include "x.tab.h"
#include "operator.h"
#include "execute.h"
#include "data.h"
#include "util.h"
#include "config.h"
#include "ident.h"

static void find_extreme(int which);

void op_version(void)
{
    List *version;

    /* Take no arguments. */
    if (!func_init_0())
	return;

    /* Construct a list of the version numbers and push it. */
    version = list_new(3);
    version->el[0].type = version->el[1].type = version->el[2].type = INTEGER;
    version->el[0].u.val = VERSION_MAJOR;
    version->el[1].u.val = VERSION_MINOR;
    version->el[2].u.val = VERSION_BUGFIX;
    push_list(version);
    list_discard(version);
}

void op_random(void)
{
    Data *args;

    /* Take one integer argument. */
    if (!func_init_1(&args, INTEGER))
	return;

    /* Replace argument on stack with a random number. */
    args[0].u.val = random_number(args[0].u.val);
}

void op_time(void)
{
    /* Take no arguments. */
    if (!func_init_0())
	return;

    push_int(time(NULL));
}

void op_ctime(void)
{
    Data *args;
    int num_args;
    time_t tval;
    char *timestr;
    String *str;

    /* Take an optional integer argument. */
    if (!func_init_0_or_1(&args, &num_args, INTEGER))
	return;

    tval = (num_args) ? args[0].u.val : time(NULL);
    timestr = ctime(&tval);
    str = string_from_chars(timestr, 24);

    pop(num_args);
    push_string(str);
    string_discard(str);
}

/* which is 1 for max, -1 for min. */
static void find_extreme(int which)
{
    int arg_start, num_args, i, type;
    Data *args, *extreme, d;

    arg_start = arg_starts[--arg_pos];
    args = &stack[arg_start];
    num_args = stack_pos - arg_start;

    if (!num_args) {
	throw(numargs_id, "Called with no arguments, requires at least one.");
	return;
    }

    type = args[0].type;
    if (type != INTEGER && type != STRING) {
	throw(type_id, "First argument (%D) not an integer or string.",
	      &args[0]);
	return;
    }

    extreme = &args[0];
    for (i = 1; i < num_args; i++) {
	if (args[i].type != type) {
	    throw(type_id, "Arguments are not all of same type.");
	    return;
	}
	if (data_cmp(&args[i], extreme) * which > 0)
	    extreme = &args[i];
    }

    /* Replace args[0] with extreme, and pop other arguments. */
    data_dup(&d, extreme);
    data_discard(&args[0]);
    args[0] = d;
    pop(num_args - 1);
}

void op_max(void)
{
    find_extreme(1);
}

void op_min(void)
{
    find_extreme(-1);
}

void op_abs(void)
{
    Data *args;

    if (!func_init_1(&args, INTEGER))
	return;

    if (args[0].u.val < 0)
	args[0].u.val = -args[0].u.val;
}

