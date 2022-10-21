%{
/* grammar.y: C-- grammar. */

#define _POSIX_SOURCE
    
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include "grammar.h"
#include "token.h"
#include "codegen.h"
#include "object.h"
#include "memory.h"
#include "util.h"
#include "data.h"

int yyparse(void);
static void yyerror(char *s);

static Prog *prog;
static List *errors;

extern Pile *compiler_pile;	/* We free this pile after compilation. */

%}

%union {
	long			 num;
	char			*s;
	struct arguments	*args;
	struct stmt		*stmt;
	struct expr		*expr;
	struct id_list		*id_list;
	struct stmt_list	*stmt_list;
	struct expr_list	*expr_list;
	struct case_list	*case_list;
	struct case_entry	*case_entry;
};

%type	<num>		ovdecl
%type	<args>		argdecl
%type	<stmt>		stmt if
%type	<stmt_list>	compound stmts stmtlist
%type	<case_entry>	case_ent
%type	<case_list>	caselist cases
%type	<expr>		expr sexpr rexpr
%type	<expr_list>	args arglist cvals
%type	<s>		for
%type	<id_list>	vars idlist errors errlist

/* The following tokens are terminals for the parser. */

%token	<num>	INTEGER DBREF
%token	<s>	COMMENT STRING SYMBOL NAME IDENT ERROR
%token		DISALLOW_OVERRIDES ARG VAR
%token		IF FOR IN UPTO WHILE SWITCH CASE DEFAULT
%token		BREAK CONTINUE RETURN
%token		CATCH ANY HANDLER
%token		FORK
%token		PASS CRITLEFT CRITRIGHT PROPLEFT PROPRIGHT

%left	TO
%right	'?' '|'
%right	OR
%right	AND
%left	IN
%left	EQ NE '>' GE '<' LE
%left	'+' '-'
%left	'*' '/' '%'
%left	'!'
%left	'[' ']' START_DICT START_BUFFER
%left	'.'

/* Declarations to shut up shift/reduce conflicts. */
%nonassoc LOWER_THAN_ELSE
%nonassoc ELSE
%nonassoc LOWER_THAN_WITH
%nonassoc WITH

/* The parser does not use the following tokens.  I define them here so
 * that I can use them, along with the above tokens, as statement and
 * expression types for the code generator, and as opcodes for the
 * interpreter. */

%token NOOP EXPR COMPOUND ASSIGN IF_ELSE FOR_RANGE FOR_LIST END RETURN_EXPR
%token CASE_VALUE CASE_RANGE LAST_CASE_VALUE LAST_CASE_RANGE END_CASE RANGE
%token FUNCTION_CALL MESSAGE EXPR_MESSAGE LIST DICT BUFFER FROB INDEX UNARY
%token BINARY CONDITIONAL SPLICE NEG SPLICE_ADD POP START_ARGS ZERO ONE
%token SET_LOCAL SET_OBJ_VAR GET_LOCAL GET_OBJ_VAR CATCH_END HANDLER_END
%token CRITICAL CRITICAL_END PROPAGATE PROPAGATE_END JUMP

%token TYPE CLASS TOINT TOSTR TOLITERAL TODBREF TOSYM TOERR VALID
%token STRLEN SUBSTR EXPLODE STRSUB PAD MATCH_BEGIN MATCH_TEMPLATE
%token MATCH_PATTERN MATCH_REGEXP CRYPT UPPERCASE LOWERCASE STRCMP
%token LISTLEN SUBLIST INSERT REPLACE DELETE SETADD SETREMOVE UNION
%token DICT_KEYS DICT_ADD DICT_DEL DICT_ADD_ELEM DICT_DEL_ELEM DICT_CONTAINS
%token BUFFER_LEN BUFFER_RETRIEVE BUFFER_APPEND BUFFER_REPLACE BUFFER_ADD
%token BUFFER_TRUNCATE BUFFER_TO_STRINGS BUFFER_FROM_STRINGS
%token VERSION RANDOM TIME CTIME MIN MAX ABS GET_NAME
%token THIS DEFINER SENDER CALLER TASK_ID
%token ERROR_FUNC TRACEBACK ERROR_STR ERROR_ARG THROW RETHROW
%token ECHO_FUNC ECHO_FILE DISCONNECT
%token ADD_PARAMETER PARAMETERS DEL_PARAMETER SET_VAR GET_VAR RMVAR COMPILE
%token METHODS FIND_METHOD FIND_NEXT_METHOD LIST_METHOD DEL_METHOD PARENTS
%token CHILDREN ANCESTORS HAS_ANCESTOR SIZE
%token CREATE CHPARENTS DESTROY LOG CONN_ASSIGN BINARY_DUMP TEXT_DUMP
%token RUN_SCRIPT SHUTDOWN BIND UNBIND CONNECT SET_HEARTBEAT_FREQ DATA SET_NAME
%token DEL_NAME DB_TOP

/* Reserved for future use. */
%token FORK ATOMIC NON_ATOMIC

/* LAST_TOKEN tells opcodes.c how much space to allocate for the opcodes
 * table. */
%token LAST_TOKEN

%%

method	: ovdecl argdecl vars stmtlist	{ prog = make_prog($1, $2, $3, $4); }
	;

ovdecl	: /* nothing */			{ $$ = 1; }
	| DISALLOW_OVERRIDES ';'	{ $$ = 0; }
	;

argdecl	: /* nothing */			{ $$ = arguments(NULL, NULL); }
	| ARG '[' IDENT ']' ';'		{ $$ = arguments(NULL, $3); }
	| ARG idlist ';'		{ $$ = arguments($2, NULL); }
	| ARG idlist ',' '[' IDENT ']' ';'
					{ $$ = arguments($2, $5); }
	;

vars	: /* nothing */			{ $$ = NULL; }
	| VAR idlist ';'		{ $$ = $2; }
	;

idlist	: IDENT				{ $$ = id_list($1, NULL); }
	| idlist ',' IDENT		{ $$ = id_list($3, $1); }
	;

errors	: ANY				{ $$ = NULL; }
	| errlist			{ $$ = $1; }
	;

errlist	: ERROR				{ $$ = id_list($1, NULL); }
	| errlist ',' ERROR		{ $$ = id_list($3, $1); }
	;

compound: '{' stmtlist '}'		{ $$ = $2; }
	;

stmtlist: /* nothing */			{ $$ = NULL; }
	| stmts				{ $$ = $1; }
	;

stmts	: stmt				{ $$ = stmt_list($1, NULL); }
	| stmts stmt			{ $$ = stmt_list($2, $1); }
	;

stmt	: COMMENT			{ $$ = comment_stmt($1); }
	| ';'				{ $$ = noop_stmt(); }
	| expr ';'			{ $$ = expr_stmt($1); }
	| compound			{ $$ = compound_stmt($1); }
	| IDENT '=' expr ';'		{ $$ = assign_stmt($1, $3); }
	| if %prec LOWER_THAN_ELSE	{ $$ = $1; }
	| if ELSE stmt			{ $$ = if_else_stmt($1, $3); }
	| for '[' expr UPTO expr ']' stmt
					{ $$ = for_range_stmt($1, $3, $5,$7); }
	| for '(' expr ')' stmt		{ $$ = for_list_stmt($1, $3, $5); }
	| WHILE '(' expr ')' stmt	{ $$ = while_stmt($3, $5); }
	| SWITCH '(' expr ')' caselist	{ $$ = switch_stmt($3, $5); }
	| BREAK ';'			{ $$ = break_stmt(); }
	| CONTINUE ';'			{ $$ = continue_stmt(); }
	| RETURN ';'			{ $$ = return_stmt(); }
	| RETURN expr ';'		{ $$ = return_expr_stmt($2); }
	| CATCH errors stmt %prec LOWER_THAN_WITH
					{ $$ = catch_stmt($2, $3, NULL); }
	| CATCH errors stmt WITH HANDLER stmt
					{ $$ = catch_stmt($2, $3, $6); }
	| error ';'			{ yyerrok; $$ = NULL; }
	;

if	: IF '(' expr ')' stmt		{ $$ = if_stmt($3, $5); }
	;

for	: FOR IDENT IN			{ $$ = $2; }
	;

caselist: '{' '}'			{ $$ = NULL; }
	| '{' cases '}'			{ $$ = $2; }
	;

cases	: case_ent			{ $$ = case_list($1, NULL); }
	| cases case_ent		{ $$ = case_list($2, $1); }
	;

case_ent: CASE cvals ':' stmtlist	{ $$ = case_entry($2, $4); }
	| DEFAULT ':' stmtlist		{ $$ = case_entry(NULL, $3); }
	;

expr	: INTEGER			{ $$ = integer_expr($1); }
	| STRING			{ $$ = string_expr($1); }
	| DBREF				{ $$ = dbref_expr($1); }
	| SYMBOL			{ $$ = symbol_expr($1); }
	| ERROR				{ $$ = error_expr($1); }
	| NAME				{ $$ = name_expr($1); }
	| IDENT				{ $$ = var_expr($1); }
	| IDENT '(' args ')'		{ $$ = function_call_expr($1, $3); }
	| PASS '(' args ')'		{ $$ = pass_expr($3); }
	| expr '.' IDENT '(' args ')'	{ $$ = message_expr($1, $3, $5); }
	| '.' IDENT '(' args ')'	{ $$ = message_expr(NULL, $2, $4); }
	| expr '.' '(' expr ')' '(' args ')'
					{ $$ = expr_message_expr($1, $4, $7); }
	| '.' '(' expr ')' '(' args ')'
				{ $$ = expr_message_expr(NULL, $3, $6); }
	| '[' args ']'			{ $$ = list_expr($2); }
	| START_DICT args ']'		{ $$ = dict_expr($2); }
	| START_BUFFER args ']'		{ $$ = buffer_expr($2); }
	| '<' expr ',' expr '>'		{ $$ = frob_expr($2, $4); }
	| expr '[' expr ']'		{ $$ = index_expr($1, $3); }
	| '!' expr			{ $$ = unary_expr('!', $2); }
	| '-' expr %prec '!'		{ $$ = unary_expr(NEG, $2); }
	| '+' expr %prec '!'		{ $$ = $2; }
	| expr '*' expr			{ $$ = binary_expr('*', $1, $3); }
	| expr '/' expr			{ $$ = binary_expr('/', $1, $3); }
	| expr '%' expr			{ $$ = binary_expr('%', $1, $3); }
	| expr '+' expr			{ $$ = binary_expr('+', $1, $3); }
	| expr '-' expr			{ $$ = binary_expr('-', $1, $3); }
	| expr EQ expr			{ $$ = binary_expr(EQ, $1, $3); }
	| expr NE expr			{ $$ = binary_expr(NE, $1, $3); }
	| expr '>' expr			{ $$ = binary_expr('>', $1, $3); }
	| expr GE expr			{ $$ = binary_expr(GE, $1, $3); }
	| expr '<' expr			{ $$ = binary_expr('<', $1, $3); }
	| expr LE expr			{ $$ = binary_expr(LE, $1, $3); }
	| expr IN expr			{ $$ = binary_expr(IN, $1, $3); }
	| expr AND expr			{ $$ = and_expr($1, $3); }
	| expr OR expr			{ $$ = or_expr($1, $3); }
	| expr '?' expr '|' expr	{ $$ = cond_expr($1, $3, $5); }
	| '(' expr ')'			{ $$ = $2; }
        | CRITLEFT expr CRITRIGHT       { $$ = critical_expr($2); }
	| PROPLEFT expr PROPRIGHT	{ $$ = propagate_expr($2); }
	;

sexpr	: expr				{ $$ = $1; }
	| '@' expr			{ $$ = splice_expr($2); }
	;

args	: /* nothing */			{ $$ = NULL; }
	| arglist			{ $$ = $1; }
	;

arglist	: sexpr				{ $$ = expr_list($1, NULL); }
	| arglist ',' sexpr		{ $$ = expr_list($3, $1); }
	;

rexpr	: expr				{ $$ = $1; }
	| expr UPTO expr		{ $$ = range_expr($1, $3); }
	;

cvals	: rexpr				{ $$ = expr_list($1, NULL); }
	| cvals ',' rexpr		{ $$ = expr_list($3, $1); }
	;

%%

Method *compile(Object *object, List *code, List **error_ret)
{
    Method *method = NULL;

    /* Initialize compiler globals. */
    errors = list_new(0);
    lex_start(code);

    /* Parse text.  This sets prog if successful. */
    yyparse();

    if (!errors->len) {
	/* No errors in parsing.  Compile to linear code.  method will be
	 * NULL if unsuccessful. */
	method = generate_method(prog, object);
    }

    /* Free up all temporary storage we allocated during compilation. */
    pfree(compiler_pile);

    /* error_ret gets reference count on errors. */
    *error_ret = errors;
    return method;
}

void compiler_error(int lineno, char *fmt, ...)
{
    va_list arg;
    String *errstr, *line;
    Data d;

    va_start(arg, fmt);
    errstr = vformat(fmt, arg);

    if (lineno == -1) {
	line = errstr;
    } else {
	line = format("Line %d: %s", lineno, errstr->s);
	string_discard(errstr);
    }

    d.type = STRING;
    d.u.str = line;
    errors = list_add(errors, &d);

    string_discard(line);
    va_end(arg);
}

int no_errors(void)
{
    return (errors->len == 0);
}

static void yyerror(char *s)
{
    compiler_error(cur_lineno(), s);
}

