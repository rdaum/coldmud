/* opcodes.c: Information about opcodes. */

#define _POSIX_SOURCE

#include <ctype.h>
#include "x.tab.h"
#include "opcodes.h"
#include "operator.h"
#include "util.h"

#define NUM_OPERATORS (sizeof(op_info) / sizeof(*op_info))

Op_info op_table[LAST_TOKEN];

static int first_function;

static Op_info op_info[] = {

    /* Opcodes generated by language syntax (syntaxop.c). */
    { COMMENT,		"COMMENT",	op_comment,	STRING,	0 },
    { POP,		"POP",		op_pop,		0,	0 },
    { SET_LOCAL,	"SET_LOCAL",	op_set_local,	VAR,	0 },
    { SET_OBJ_VAR,	"SET_OBJ_VAR",	op_set_obj_var,	IDENT,	0 },
    { IF,		"IF",		op_if,		JUMP,	0 },
    { IF_ELSE,		"IF_ELSE",	op_if,		JUMP,	0 },
    { ELSE,		"ELSE",		op_else,	JUMP,	0 },
    { FOR_RANGE,	"FOR_RANGE",	op_for_range,	JUMP,	INTEGER },
    { FOR_LIST,		"FOR_LIST",	op_for_list,	JUMP,	INTEGER },
    { WHILE,		"WHILE",	op_while,	JUMP,	JUMP },
    { SWITCH,		"SWITCH",	op_switch,	JUMP,	0 },
    { CASE_VALUE,	"CASE_VALUE",	op_case_value,	JUMP,	0 },
    { CASE_RANGE,	"CASE_RANGE",	op_case_range,	JUMP,	0 },
    { LAST_CASE_VALUE,	"LAST_CASE_VALUE",op_last_case_value,JUMP,0},
    { LAST_CASE_RANGE,	"LAST_CASE_RANGE",op_last_case_range,JUMP,0},
    { END_CASE,		"END_CASE",	op_end_case,	JUMP,	0 },
    { DEFAULT,		"DEFAULT",	op_default,	0,	0 },
    { END,		"END",		op_end,		JUMP,	0 },
    { BREAK,		"BREAK",	op_break,	JUMP,	INTEGER },
    { CONTINUE,		"CONTINUE",	op_continue,	JUMP,	INTEGER },
    { RETURN,		"RETURN",	op_return,	0,	0 },
    { RETURN_EXPR,	"RETURN_EXPR",	op_return_expr,	0,	0 },
    { CATCH,		"CATCH",	op_catch,	JUMP,	ERROR },
    { CATCH_END,	"CATCH_END",	op_catch_end,	JUMP,	0 },
    { HANDLER_END,	"HANDLER_END",	op_handler_end,	0,	0 },

    { ZERO,		"ZERO",		op_zero,	0,	0 },
    { ONE,		"ONE",		op_one,		0,	0 },
    { INTEGER,		"INTEGER",	op_integer,	INTEGER,0 },
    { STRING,		"STRING",	op_string,	STRING,	0 },
    { DBREF,		"DBREF",	op_dbref,	IDENT,	0 },
    { SYMBOL,		"SYMBOL",	op_symbol,	IDENT,	0 },
    { ERROR,		"ERROR",	op_error,	IDENT,	0 },
    { GET_LOCAL,	"GET_LOCAL",	op_get_local,	VAR,	0 },
    { GET_OBJ_VAR,	"GET_OBJ_VAR",	op_get_obj_var,	IDENT,	0 },
    { START_ARGS,	"START_ARGS",	op_start_args,	0,	0 },
    { PASS,		"PASS",		op_pass,	0,	0 },
    { MESSAGE,		"MESSAGE",	op_message,	IDENT,	0 },
    { EXPR_MESSAGE,	"EXPR_MESSAGE",	op_expr_message,0,	0 },
    { LIST,		"LIST",		op_list,	0,	0 },
    { DICT,		"DICT",		op_dict,	0,	0 },
    { FROB,		"FROB",		op_frob,	0,	0 },
    { INDEX,		"INDEX",	op_index,	0,	0 },
    { AND,		"AND",		op_and,		JUMP,	0 },
    { OR,		"OR",		op_or,		JUMP,	0 },
    { CONDITIONAL,	"CONDITIONAL",	op_if,		JUMP,	0 },
    { SPLICE,		"SPLICE",	op_splice,	0,	0 },
    { CRITICAL,		"CRITICAL",	op_critical,	JUMP,	0 },
    { CRITICAL_END,	"CRITICAL_END", op_critical_end,0,	0 },
    { PROPAGATE,	"PROPAGATE",	op_propagate,	JUMP,	0 },
    { PROPAGATE_END,	"PROPAGATE_END",op_propagate_end,0,	0 },

    /* Arithmetic and relational operators (arithop.c). */
    { '!',		"!",		op_not,		0,	0 },
    { NEG,		"NEG",		op_negate,	0,	0 },
    { '*',		"*",		op_multiply,	0,	0 },
    { '/',		"/",		op_divide,	0,	0 },
    { '%',		"%",		op_modulo,	0,	0 },
    { '+',		"+",		op_add,		0,	0 },
    { SPLICE_ADD,	"SPLICE_ADD",	op_splice_add,	0,	0 },
    { '-',		"-",		op_subtract,	0,	0 },
    { EQ,		"EQ",		op_equal,	0,	0 },
    { NE,		"NE",		op_not_equal,	0,	0 },
    { '>',		">",		op_greater,	0, 	0 },
    { GE,		">=",		op_greater_or_equal,0,	0 },
    { '<',		"<",		op_less,	0,	0 },
    { LE,		"<=",		op_less_or_equal,0,	0 },
    { IN,		"IN",		op_in,		0,	0 },

    /* Generic data manipulation (dataop.c). */
    { TYPE,		"type",		op_type,	0,	0 },
    { CLASS,		"class",	op_class,	0,	0 },
    { TOINT,		"toint",	op_toint,	0,	0 },
    { TOSTR,		"tostr",	op_tostr,	0,	0 },
    { TOLITERAL,	"toliteral",	op_toliteral,	0,	0 },
    { TODBREF,		"todbref",	op_todbref,	0,	0 },
    { TOSYM,		"tosym",	op_tosym,	0,	0 },
    { TOERR,		"toerr",	op_toerr,	0,	0 },
    { VALID,		"valid",	op_valid,	0,	0 },

    /* Operations on strings (stringop.c). */
    { STRLEN,		"strlen",	op_strlen,	0,	0 },
    { SUBSTR,		"substr",	op_substr,	0,	0 },
    { EXPLODE,		"explode",	op_explode,	0,	0 },
    { STRSUB,		"strsub",	op_strsub,	0,	0 },
    { PAD,		"pad",		op_pad,		0,	0 },
    { MATCH_BEGIN,	"match_begin",	op_match_begin,	0,	0 },
    { MATCH_TEMPLATE,	"match_template",op_match_template,0,	0 },
    { MATCH_PATTERN,	"match_pattern",op_match_pattern,0,	0 },
    { MATCH_REGEXP,	"match_regexp",	op_match_regexp,0,	0 },
    { CRYPT,		"crypt",	op_crypt,	0,	0 },
    { UPPERCASE,	"uppercase",	op_uppercase,	0,	0 },
    { LOWERCASE,	"lowercase",	op_lowercase,	0,	0 },
    { STRCMP,		"strcmp",	op_strcmp,	0,	0 },

    /* List manipulation (listop.c). */
    { LISTLEN,		"listlen",	op_listlen,	0,	0 },
    { SUBLIST,		"sublist",	op_sublist,	0,	0 },
    { INSERT,		"insert",	op_insert,	0,	0 },
    { REPLACE,		"replace",	op_replace,	0,	0 },
    { DELETE,		"delete",	op_delete,	0,	0 },
    { SETADD,		"setadd",	op_setadd,	0,	0 },
    { SETREMOVE,	"setremove",	op_setremove,	0,	0 },
    { UNION,		"union",	op_union,	0,	0 },

    /* Dictionary manipulation (dictop.c). */
    { DICT_KEYS,	"dict_keys",	op_dict_keys,	0,	0 },
    { DICT_ADD,		"dict_add",	op_dict_add,	0,	0 },
    { DICT_DEL,		"dict_del",	op_dict_del,	0,	0 },
    { DICT_CONTAINS,	"dict_contains",op_dict_contains,0,	0 },

    /* Miscellaneous operations (miscop.c). */
    { VERSION,		"version",	op_version,	0,	0 },
    { RANDOM,		"random",	op_random,	0,	0 },
    { TIME,		"time",		op_time,	0,	0 },
    { CTIME,		"ctime",	op_ctime,	0,	0 },
    { MIN,		"min",		op_min,		0,	0 },
    { MAX,		"max",		op_max,		0,	0 },
    { ABS,		"abs",		op_abs,		0,	0 },

    /* Current method information operations (methodop.c). */
    { THIS,		"this",		op_this,	0,	0 },
    { DEFINER,		"definer",	op_definer,	0,	0 },
    { SENDER,		"sender",	op_sender,	0,	0 },
    { CALLER,		"caller",	op_caller,	0,	0 },
    { TASK_ID,		"task_id",	op_task_id,	0,	0 },

    /* Error handling operations (errorop.c). */
    { ERROR_FUNC,	"error",	op_error_func,	0,	0 },
    { TRACEBACK,	"traceback",	op_traceback,	0,	0 },
    { ERROR_ARG,	"error_arg",	op_error_arg,	0,	0 },
    { THROW,		"throw",	op_throw,	0,	0 },
    { RETHROW,		"rethrow",	op_rethrow,	0,	0 },

    /* Input and output (ioop.c). */
    { ECHO_FUNC,	"echo",		op_echo,	0,	0 },
    { ECHO_FILE,	"echo_file",	op_echo_file,	0,	0 },
    { DISCONNECT,	"disconnect",	op_disconnect,	0,	0 },

    /* Operations on the current object (objectop.c). */
    { ADD_PARAMETER,	"add_parameter",op_add_parameter,0,	0 },
    { PARAMETERS,	"parameters",	op_parameters,	0,	0 },
    { DEL_PARAMETER,	"del_parameter",op_del_parameter,0,	0 },
    { SET_VAR,		"set_var",	op_set_var,	0,	0 },
    { GET_VAR,		"get_var",	op_get_var,	0,	0 },
    { COMPILE,		"compile",	op_compile,	0,	0 },
    { METHODS,		"methods",	op_methods,	0,	0 },
    { FIND_METHOD,	"find_method",	op_find_method,	0,	0 },
    { FIND_NEXT_METHOD,	"find_next_method",op_find_next_method,0,0},
    { LIST_METHOD,	"list_method",	op_list_method,	0,	0 },
    { DEL_METHOD,	"del_method",	op_del_method,	0,	0 },
    { PARENTS,		"parents",	op_parents,	0,	0 },
    { CHILDREN,		"children",	op_children,	0,	0 },
    { ANCESTORS,	"ancestors",	op_ancestors,	0,	0 },
    { HAS_ANCESTOR,	"has_ancestor",	op_has_ancestor,0,	0 },
    { SIZE,		"size",		op_size,	0,	0 },

    /* Administrative operations (adminop.c). */
    { CREATE,		"create",	op_create,	0,	0 },
    { CHPARENTS,	"chparents",	op_chparents,	0,	0 },
    { DESTROY,		"destroy",	op_destroy,	0,	0 },
    { LOG,		"log",		op_log,		0,	0 },
    { CONN_ASSIGN,	"conn_assign",	op_conn_assign,	0,	0 },
    { BINARY_DUMP,	"binary_dump",	op_binary_dump,	0,	0 },
    { TEXT_DUMP,	"text_dump",	op_text_dump,	0,	0 },
    { RUN_SCRIPT,	"run_script",	op_run_script,	0,	0 },
    { SHUTDOWN,		"shutdown",	op_shutdown,	0,	0 },
    { BIND,		"bind",		op_bind,	0,	0 },
    { UNBIND,		"unbind",	op_unbind,	0,	0 },
    { CONNECT,		"connect",	op_connect,	0,	0 },
    { SET_HEARTBEAT_FREQ, "set_heartbeat_freq", op_set_heartbeat_freq, 0, 0 },
    { DATA,		"data",		op_data,	0,	0 }

};

void init_op_table(void)
{
    int i;

    for (i = 0; i < NUM_OPERATORS; i++)
	op_table[op_info[i].opcode] = op_info[i];

    /* Look for first opcode with a lowercase name to find the first
     * function. */
    for (i = 0; i < NUM_OPERATORS; i++) {
	if (islower(*op_info[i].name))
	    break;
    }
    first_function = i;
}

int find_function(char *name)
{
    int i;

    for (i = first_function; i < NUM_OPERATORS; i++) {
	if (strcmp(op_info[i].name, name) == 0)
	    return op_info[i].opcode;
    }

    return -1;
}

