/* decode.c: Routines to decompile a method. */

#define _POSIX_SOURCE

#include <stdio.h>
#include "x.tab.h"
#include "decode.h"
#include "code_prv.h"
#include "data.h"
#include "object.h"
#include "ident.h"
#include "memory.h"
#include "log.h"
#include "util.h"
#include "cmstring.h"
#include "opcodes.h"
#include "config.h"
#include "token.h"

#define TOKEN_SIZE (sizeof(binary_tokens) / sizeof(*binary_tokens))
#define PREC_SIZE (sizeof(precedences) / sizeof(*precedences))
#define A_VERY_LARGE_PRECEDENCE_LEVEL	1000

#define COMPLEX_FLAG			0x1
#define SPANNING_IF_FLAG		0x2
#define COMPLEX_IF_FLAG			0x4

typedef struct context Context;

struct context {
    short type;
    int end;
    Context *enclosure;
};

static int count_lines(int start, int end, unsigned *flags);
static Stmt_list *decompile_stmt_list(int start, int end);
static Stmt *decompile_stmt(int *pos_ptr);
static Stmt *decompile_body(int start, int end);
static Stmt *decompile_until(int *start, int marker);
static Stmt *body_from_stmt_list(Stmt_list *stmts);
static Case_list *decompile_cases(int start, int end);
static Case_entry *decompile_case(int *pos_ptr, int switch_end);
static Expr_list *decompile_case_values(int *pos_ptr, int *end_ret);
static Id_list *make_error_id_list(int which);
static Expr_list *decompile_expressions(int *pos_ptr);
static Expr_list *decompile_expressions_bounded(int *pos_ptr, int end);
static List *unparse_stmt_list(List *output, Stmt_list *stmts, int indent);
static List *unparse_stmt(List *output, Stmt *stmt, int indent, Stmt *last);
static int is_complex_if_else_stmt(Stmt *stmt);
static int is_complex_type(int type);
static List *unparse_body(List *output, Stmt *body, String *str, int indent);
static List *unparse_cases(List *output, Case_list *cases, int indent);
static List *unparse_case(List *output, Case_entry *case_entry, int indent);
static String *unparse_expr(String *str, Expr *expr);
static int is_this(Expr *expr);
static String *unparse_args(String *str, Expr_list *args);
static String *unparse_expr_prec(String *str, Expr *expr, int caller_type,
				 int assoc);
static int prec_level(int opcode);
static char *binary_token(int opcode);
static List *add_and_discard_string(List *output, String *str);
static char *varname(int ind);

/* These globals get set at the start and are never modified. */
static Object *the_object;
static Method *the_method;
static long *the_opcodes;
static int the_increment;
static int the_parens_flag;

static struct {
    int opcode;
    char *token;
} binary_tokens[] = {
    { IN,	"in" },
    { EQ,	"==" },
    { NE,	"!=" },
    { '>',	">" },
    { GE,	">=" },
    { '<',	"<" },
    { LE,	"<=" },
    { '+',	"+" },
    { '-',	"-" },
    { '*',	"*" },
    { '/',	"/" },
    { '%',	"%" },
};

static struct {
    int opcode;
    int level;
} precedences[] = {
    { CONDITIONAL,	 2 },
    { OR,		 3 },
    { AND,		 4 },
    { IN,		 5 },
    { EQ,		 6 },
    { NE,		 6 },
    { '>',		 6 },
    { GE,		 6 },
    { '<',		 6 },
    { LE,		 6 },
    { '+',		 7 },
    { '-',		 7 },
    { '*',		 8 },
    { '/',		 8 },
    { '%',		 8 },
    { '!',		 9 },
    { NEG,		 9 },
    { MESSAGE,		10 },
    { INDEX,		11 }
};

int line_number(Method *method, int pc)
{
    int count = 1;
    unsigned flags;

    /* Count declaration lines. */
    if (!method->overridable)
	count++;
    if (method->num_args || method->rest != -1)
	count++;
    if (method->num_vars)
	count++;
    if (count > 1)
	count++;

    the_opcodes = method->opcodes;
    return count + count_lines(0, pc, &flags);
}

static int count_lines(int start, int end, unsigned *flags)
{
    int count = 0, last = -1, next;

    *flags = 0x0;

    while (start < end) {

	/* Find next opcode. */
	next = start + 1;
	if (op_table[the_opcodes[start]].arg1)
	    next++;
	if (op_table[the_opcodes[start]].arg2)
	    next++;

	/* Check for statement opcodes. */
	switch (the_opcodes[start]) {

	  case POP:
	  case SET_LOCAL:
	  case SET_OBJ_VAR:
	  case BREAK:
	  case CONTINUE:
	  case RETURN:
	  case RETURN_EXPR:
	    /* Count one line. */
	    count++;
	    if (last != -1)
		*flags |= COMPLEX_FLAG;
	    *flags &= ~SPANNING_IF_FLAG;
	    last = the_opcodes[start];
	    break;

	  case COMMENT:
	    /* If last is a comment or this is the beginning of a block,
	     * increment once; otherwise, increment twice. */
	    count += ((last == -1 || last == COMMENT) ? 1 : 2);
	    if (last != -1)
		*flags |= COMPLEX_FLAG;
	    *flags &= ~SPANNING_IF_FLAG;
	    last = COMMENT;
	    break;

	  case IF_ELSE: {
	      int body_end;
	      unsigned if_flags = 0x0, else_flags = 0x0;

	      /* Count "if (expr)" line and consider if body. */
	      count++;
	      body_end = the_opcodes[start + 1] - 2;
	      start = next;

	      if (end < body_end) {
		  /* End is inside if body. */
		  return count + count_lines(start, end, &if_flags);
	      } else {
		  /* Count if body. */
		  count += count_lines(start, body_end, &if_flags);
	      }

	      /* Count "else" line and consider else body. */
	      count++;
	      start = body_end + 2;
	      body_end = the_opcodes[start - 1];

	      if (end < body_end) {
		  /* End is in else body.  Count the entire else body to see if
		   * it contains a spanning if; if so, adjust count backwards
		   * by one to account for the "if (expr)" being on the same
		   * line as our "else".  Then count the body up to the end. */
		  count_lines(start, body_end, &else_flags);
		  if (else_flags & SPANNING_IF_FLAG)
		      count--;
		  return count + count_lines(start, end, &else_flags);
	      }

	      /* Count else body. */
	      count += count_lines(start, body_end, &else_flags);

	      if (else_flags & SPANNING_IF_FLAG) {
		  /* Adjust the count back one, since the "if (expr)" line went
		   * on the same line as our "else".  Also, if the else body
		   * added a line for a closing brace, adjust back one more,
		   * since the responsibility for the closing brace falls on
		   * the outermost if-else. */
		  count--;
		  if (else_flags & COMPLEX_IF_FLAG)
		      count--;
	      }

	      /* Add a line for the closing brace if the if body was complex,
	       * if the else body was complex and not a spanning if, or if the
	       * else body was a spanning if and had complex subclauses.  Set
	       * COMPLEX_IF_FLAG if we do this. */
	      if ((if_flags & COMPLEX_FLAG) ||
		  (else_flags & COMPLEX_FLAG &&
		   !(else_flags & SPANNING_IF_FLAG)) ||
		  (else_flags & SPANNING_IF_FLAG &&
		   else_flags & COMPLEX_IF_FLAG)) {
		  /* Boy, that was a messy condition. */
		  count++;
		  *flags |= COMPLEX_IF_FLAG;
	      }

	      /* Set other flags. */
	      *flags |= COMPLEX_FLAG;
	      if (last == -1)
		  *flags |= SPANNING_IF_FLAG;

	      last = IF_ELSE;
	      next = body_end;
	      break;
	  }

	  case IF:
	  case FOR_RANGE:
	  case FOR_LIST:
	  case WHILE:
	  case LAST_CASE_VALUE:
	  case LAST_CASE_RANGE: {
	      int opcode = the_opcodes[start], body_end;
	      unsigned body_flags;

	      /* Count header line and consider body. */
	      count++;
	      body_end = the_opcodes[start + 1];
	      if (opcode == LAST_CASE_VALUE || opcode == LAST_CASE_RANGE)
		  body_end -= 2;
	      start = next;

	      if (end < body_end) {
		  /* End is in body. */
		  return count + count_lines(start, end, &body_flags);
	      }

	      /* Count body.  If it's complex, count line for closing brace. */
	      count += count_lines(start, body_end, &body_flags);
	      if (opcode != LAST_CASE_VALUE && opcode != LAST_CASE_RANGE) {
		  if (body_flags & COMPLEX_FLAG)
		      count++;
	      }

	      /* For if statements, set COMPLEX_IF_FLAG if body is complex, and
	       * set SPANNING_IF_FLAG if this is the first statement (it may be
	       * unset by a later statement). */
	      if (opcode == IF) {
		  if (body_flags & COMPLEX_FLAG)
		      *flags |= COMPLEX_IF_FLAG;
		  if (last == -1)
		      *flags |= SPANNING_IF_FLAG;
	      } else {
		  *flags &= ~SPANNING_IF_FLAG;
	      }

	      *flags |= COMPLEX_FLAG;
	      last = opcode;
	      next = body_end;
	      break;
	  }

	  case SWITCH: {
	      int body_end;
	      unsigned body_flags;

	      /* Count switch header and consider body. */
	      count++;
	      body_end = the_opcodes[start + 1];
	      start += 2;

	      if (end < body_end) {
		  /* End is in switch body. */
		  return count + count_lines(start, end, &body_flags);
	      }

	      /* Count switch body recursively (so that default case knows
	       * where to stop).  If the default case was complex, count the
	       * closing brace for the default case. */
	      count += count_lines(start, body_end, &body_flags);

	      /* Count closing brace and set flags. */
	      count++;
	      *flags |= COMPLEX_FLAG;
	      *flags &= ~SPANNING_IF_FLAG;
	      last = SWITCH;
	      next = body_end;
	      break;
	  }

	  case DEFAULT: {
	      unsigned body_flags;

	      /* Switch bodies are counted recursively, so the end of the
	       * default case is end.  Count default header and body. */
	      count += 1 + count_lines(next, end, &body_flags);
	      next = end;
	      break;
	  }

	  case CATCH: {
	      int body_end;
	      unsigned body_flags;

	      /* Count catch line and consider body. */
	      count++;
	      body_end = the_opcodes[start + 1];
	      start = next;

	      if (end < body_end) {
		  /* end is inside catch body. */
		  return count + count_lines(start, end, &body_flags);
	      }

	      /* Count body and closing brace. */
	      count += 1 + count_lines(start, body_end, &body_flags);

	      if (the_opcodes[body_end] == HANDLER_END) {
		  /* Skip dummy HANDLER_END instruction. */
		  last = CATCH;
		  next = body_end + 1;
	      } else {
		  /* We don't know where the end of the handler is.  Rather
		   * than counting it recursively, fake it, and count the
		   * closing brace when we hit the HANDLER_END. */
		  last = -1;
		  next = body_end;
	      }

	      *flags &= ~SPANNING_IF_FLAG;
	      *flags |= COMPLEX_FLAG;
	      break;
	  }

	  case HANDLER_END:
	    /* Count closing brace, and set last to CATCH to reflect the
	     * enclosing catch statement. */
	    last = CATCH;
	    count++;
	    break;
	}

	start = next;
    }

    return count;
}

List *decompile(Method *method, Object *object, int increment, int parens)
{
    Stmt_list *body;
    List *output;
    String *str;
    int i;
    char *s;

    /* Set globals so we don't have to pass method and object around. */
    the_object = object;
    the_method = method;
    the_opcodes = method->opcodes;
    the_increment = increment;
    the_parens_flag = parens;

    /* Prepare output list. */
    output = list_new(0);

    /* Add 'disallow_overrides' line if appropriate. */
    if (!method->overridable) {
	str = string_from_chars("disallow_overrides;", 19);
	output = add_and_discard_string(output, str);
    }

    /* Add 'args' line if there are arguments. */
    if (method->num_args || method->rest != -1) {
	str = string_from_chars("arg ", 4);
	for (i = method->num_args - 1; i >= 0; i--) {
	    s = ident_name(object_get_ident(object, method->argnames[i]));
	    str = string_add(str, s, strlen(s));
	    if (i > 0 || method->rest != -1)
		str = string_add(str, ", ", 2);
	}
	if (method->rest != -1) {
	    str = string_addc(str, '[');
	    s = ident_name(object_get_ident(object, method->rest));
	    str = string_add(str, s, strlen(s));
	    str = string_addc(str, ']');
	}
	str = string_addc(str, ';');
	output = add_and_discard_string(output, str);
    }

    /* Add 'vars' line if there are variables. */
    if (method->num_vars) {
	str = string_from_chars("var ", 4);
	for (i = method->num_vars - 1; i >= 0; i--) {
	    s = ident_name(object_get_ident(object, method->varnames[i]));
	    str = string_add(str, s, strlen(s));
	    if (i > 0)
		str = string_add(str, ", ", 2);
	}
	str = string_addc(str, ';');
	output = add_and_discard_string(output, str);
    }

    /* Add blank line if there were declarations and there is code. */
    if (output->len && method->num_opcodes)
	output = add_and_discard_string(output, string_empty(0));

    /* Decompile opcodes into parse tree. */
    body = decompile_stmt_list(0, method->num_opcodes);

    /* The first statement in the body (the last statement in the method) will
     * be a 'return 0' statement.  Ignore this. */
    body = body->next;

    /* Now unparse the body, add it to the list, and return the output. */
    output = unparse_stmt_list(output, body, 0);

    /* Free up all that memory we've been allocating and losing track of. */
    pfree(compiler_pile);

    return output;
}

static Stmt *decompile_stmt(int *pos_ptr)
{
    int pos = *pos_ptr, end;
    Expr_list *exprs;
    Stmt *stmt, *body;
    char *var, *comment;

    /* Most statement opcodes follow at least one expression.  Therefore, the
     * first thing we do is decompile expressions until we hit a statement
     * opcode. */
    exprs = decompile_expressions(&pos);

    /* Now look at the statement opcode. */
    switch (the_opcodes[pos]) {

      case COMMENT:
	/* COMMENT opcode follows no expressions. */
	comment = the_object->strings[the_opcodes[pos + 1]].str->s;
	(*pos_ptr) = pos + 2;
	return comment_stmt(comment);

      case POP:
	/* POP opcode follows one expression. */
	(*pos_ptr) = pos + 1;
	return expr_stmt(exprs->expr);

      case SET_LOCAL:
	/* SET_LOCAL opcode follows one expression. */
	var = varname(the_opcodes[pos + 1]);
	(*pos_ptr) = pos + 2;
	return assign_stmt(var, exprs->expr);

      case SET_OBJ_VAR:
	/* SET_OBJ_VAR opcode follows one expression. */
	var = ident_name(object_get_ident(the_object, the_opcodes[pos + 1]));
	(*pos_ptr) = pos + 2;
	return assign_stmt(var, exprs->expr);

      case IF:
	/* IF opcode follows one expression. */
	end = the_opcodes[pos + 1];
	body = decompile_body(pos + 2, end);
	stmt = if_stmt(exprs->expr, body);
	(*pos_ptr) = end;
	return stmt;

      case IF_ELSE:
	/* IF_ELSE opcode follows one expression.  First get the if part. */
	end = the_opcodes[pos + 1];
	body = decompile_body(pos + 2, end - 2);
	stmt = if_stmt(exprs->expr, body);

	/* Now get the else part. */
	pos = end;
	end = the_opcodes[pos - 1];
	body = decompile_body(pos, end);
	stmt = if_else_stmt(stmt, body);

	(*pos_ptr) = end;
	return stmt;

      case FOR_RANGE:
	/* FOR_RANGE opcode follows two expressions. */
	/* End stored in second opcode argument is after the END opcode, so the
	   body ends at end - 1. */
	end = the_opcodes[pos + 1];
	var = varname(the_opcodes[pos + 2]);
	body = decompile_body(pos + 3, end - 2);
	(*pos_ptr) = end;
	return for_range_stmt(var, exprs->next->expr, exprs->expr, body);

      case FOR_LIST:
	/* FOR_LIST opcode follows two expressions; one is just a zero. */
	/* End stored in second opcode argument is after the END opcode, so the
	 * body ends at end - 1. */
	end = the_opcodes[pos + 1];
	var = varname(the_opcodes[pos + 2]);
	body = decompile_body(pos + 3, end - 2);
	(*pos_ptr) = end;
	return for_list_stmt(var, exprs->next->expr, body);

      case WHILE:
	/* WHILE statement follows one expression. */
	/* End stored in second opcode argument is after the END opcode, so the
	 * body ends at end - 1. */
	end = the_opcodes[pos + 1];
	body = decompile_body(pos + 3, end - 2);
	(*pos_ptr) = end;
	return while_stmt(exprs->expr, body);

      case SWITCH: {
	  Case_list *cases;

	  /* SWITCH statement follows one opcode. */
	  end = the_opcodes[pos + 1];
	  cases = decompile_cases(pos + 2, end);
	  (*pos_ptr) = end;
	  return switch_stmt(exprs->expr, cases);
      }

      case BREAK:
	/* BREAK opcode follows no expressions. */
	(*pos_ptr) = pos + 3;
	return break_stmt();

      case CONTINUE:
	/* CONTINUE opcode follows no expressions. */
	(*pos_ptr) = pos + 3;
	return continue_stmt();

      case RETURN:
	/* RETURN opcode follows no expressions. */
	(*pos_ptr) = pos + 1;
	return return_stmt();

      case RETURN_EXPR:
	/* RETURN_EXPR opcode follows one expression. */
	(*pos_ptr) = pos + 1;
	return return_expr_stmt(exprs->expr);

      case CATCH: {
	  Id_list *errors;

	  /* CATCH opcode follows no expressions. */
	  errors = make_error_id_list(the_opcodes[pos + 2]);
	  end = the_opcodes[pos + 1];
	  body = decompile_body(pos + 3, end - 2);
	  if (the_opcodes[end] == HANDLER_END)
	      stmt = NULL;
	  else
	      stmt = decompile_until(&end, HANDLER_END);
	  (*pos_ptr) = end + 1;
	  return catch_stmt(errors, body, stmt);
      }

      default:
	return NULL;
    }
}

static Stmt_list *decompile_stmt_list(int start, int end)
{
    Stmt_list *stmts = NULL;

    while (start < end)
	stmts = stmt_list(decompile_stmt(&start), stmts);
    return stmts;
}

static Stmt *decompile_until(int *start, int marker)
{
    Stmt_list *stmts = NULL;

    while (the_opcodes[*start] != marker)
	stmts = stmt_list(decompile_stmt(start), stmts);
    return body_from_stmt_list(stmts);
}

static Stmt *decompile_body(int start, int end)
{
    return body_from_stmt_list(decompile_stmt_list(start, end));
}

static Stmt *body_from_stmt_list(Stmt_list *stmts)
{
    if (!stmts)
	return noop_stmt();
    else if (stmts->next)
	return compound_stmt(stmts);
    else
	return stmts->stmt;
}

static Case_list *decompile_cases(int start, int end)
{
    Case_list *cases = NULL;

    while (start < end)
	cases = case_list(decompile_case(&start, end), cases);

    /* If the default case has no action, forget about it. */
    if (!cases->case_entry->stmts)
	cases = cases->next;

    return cases;
}

static Case_entry *decompile_case(int *pos_ptr, int switch_end)
{
    Expr_list *values;
    Stmt_list *stmts;
    int end;

    values = decompile_case_values(pos_ptr, &end);
    if (!values) {
	/* It's a default statement; decompile up to the switch end. */
	stmts = decompile_stmt_list(*pos_ptr, switch_end);
	*pos_ptr = switch_end;
    } else {
	/* Decompile up to the END_CASE instruction at the end of the body. */
	stmts = decompile_stmt_list(*pos_ptr, end - 2);
	*pos_ptr = end;
    }
    return case_entry(values, stmts);
}

static Expr_list *decompile_case_values(int *pos_ptr, int *end_ret)
{
    Expr_list *exprs, *values = NULL;
    Expr *range;
    int pos = *pos_ptr;

    if (the_opcodes[pos] == DEFAULT) {
	(*pos_ptr) = pos + 1;
	return NULL;
    }

    /* Loop until we hit the last case opcode for this case. */
    while (1) {
	/* Get any expressions preceding the case opcode. */
	exprs = decompile_expressions(&pos);

	switch (the_opcodes[pos]) {

	  case CASE_VALUE:
	    pos += 2;
	    values = expr_list(exprs->expr, values);
	    break;

	  case CASE_RANGE:
	    pos += 2;
	    range = range_expr(exprs->next->expr, exprs->expr);
	    values = expr_list(range, values);
	    break;

	  case LAST_CASE_VALUE:
	    *end_ret = the_opcodes[pos + 1];
	    *pos_ptr = pos + 2;
	    return expr_list(exprs->expr, values);

	  case LAST_CASE_RANGE:
	    *end_ret = the_opcodes[pos + 1];
	    *pos_ptr = pos + 2;
	    range = range_expr(exprs->next->expr, exprs->expr);
	    return expr_list(range, values);
	}
    }
}

static Id_list *make_error_id_list(int which)
{
    Error_list *elist;
    Id_list *idl = NULL;
    int i;

    /* A -1 indicates a 'catch all' expression; return a null ID list. */
    if (which == -1)
	return NULL;

    /* Retrieve the error list from the method by its index. */
    elist = &the_method->error_lists[which];

    /* Traverse the error list, building an id list from the id numbers.
     * Since the list gets flipped internally, this list should be read
     * forwards later on. */

    for (i = 0; i < elist->num_errors; i++)
	idl = id_list(ident_name(elist->error_ids[i]), idl);

    return idl;
}

/* Decompile an unbounded list of expressions. */
static Expr_list *decompile_expressions(int *pos_ptr)
{
    return decompile_expressions_bounded(pos_ptr, -1);
}

/* This function constructs the list of expressions that would result from
 * interpreting the opcodes starting at (*pos_ptr).  We stop at end, at a
 * statement token, or at a token which pops an argument list off the stack. */
static Expr_list *decompile_expressions_bounded(int *pos_ptr, int expr_end)
{
    int pos = *pos_ptr, end;
    Expr_list *stack = NULL;
    char *s;

    while (expr_end == -1 || pos < expr_end) {
	switch (the_opcodes[pos]) {

	  case ZERO:
	    stack = expr_list(integer_expr(0), stack);
	    pos++;
	    break;

	  case ONE:
	    stack = expr_list(integer_expr(1), stack);
	    pos++;
	    break;

	  case INTEGER:
	    stack = expr_list(integer_expr(the_opcodes[pos + 1]), stack);
	    pos += 2;
	    break;

	  case STRING:
	    s = the_object->strings[the_opcodes[pos + 1]].str->s;
	    stack = expr_list(string_expr(s), stack);
	    pos += 2;
	    break;

	  case DBREF:
	    stack = expr_list(dbref_expr(the_opcodes[pos + 1]), stack);
	    pos += 2;

	  case SYMBOL:
	    s = ident_name(object_get_ident(the_object, the_opcodes[pos + 1]));
	    stack = expr_list(symbol_expr(s), stack);
	    pos += 2;
	    break;

	  case ERROR:
	    s = ident_name(object_get_ident(the_object, the_opcodes[pos + 1]));
	    stack = expr_list(error_expr(s), stack);
	    pos += 2;
	    break;

	  case NAME:
	    s = ident_name(object_get_ident(the_object, the_opcodes[pos + 1]));
	    stack = expr_list(name_expr(s), stack);
	    pos += 2;
	    break;

	  case GET_LOCAL:
	    s = varname(the_opcodes[pos + 1]);
	    stack = expr_list(var_expr(s), stack);
	    pos += 2;
	    break;

	  case GET_OBJ_VAR:
	    s = ident_name(object_get_ident(the_object, the_opcodes[pos + 1]));
	    stack = expr_list(var_expr(s), stack);
	    pos += 2;
	    break;

	  case START_ARGS: {
	      Expr_list *args;

	      pos++;
	      args = decompile_expressions(&pos);
	      switch(the_opcodes[pos]) {

		case PASS:
		  stack = expr_list(pass_expr(args), stack);
		  pos++;
		  break;

		case MESSAGE:
		  s = ident_name(object_get_ident(the_object,
						  the_opcodes[pos + 1]));
		  stack->expr = message_expr(stack->expr, s, args);
		  pos += 2;
		  break;

		case EXPR_MESSAGE:
		  stack->next->expr = expr_message_expr(stack->next->expr,
							stack->expr, args);
		  stack = stack->next;
		  pos++;
		  break;

		case LIST:
		  stack = expr_list(list_expr(args), stack);
		  pos++;
		  break;

		case DICT:
		  stack = expr_list(dict_expr(args), stack);
		  pos++;
		  break;

		case BUFFER:
		  stack = expr_list(buffer_expr(args), stack);
		  pos++;
		  break;

		default:
		  s = op_table[the_opcodes[pos]].name;
		  if (!s)
		      panic("Invalid expression opcode.");
		  stack = expr_list(function_call_expr(s, args), stack);
		  pos++;
		  break;
	      }
	      break;
	  }

	  case FROB:
	    stack->next->expr = frob_expr(stack->next->expr, stack->expr);
	    stack = stack->next;
	    pos++;
	    break;

	  case INDEX:
	    stack->next->expr = index_expr(stack->next->expr, stack->expr);
	    stack = stack->next;
	    pos++;
	    break;

	  case AND: {
	      Expr_list *rhs;

	      end = the_opcodes[pos + 1];
	      pos += 2;
	      rhs = decompile_expressions_bounded(&pos, end);
	      stack->expr = and_expr(stack->expr, rhs->expr);
	      pos = end;
	      break;
	  }

	  case OR: {
	      Expr_list *rhs;

	      end = the_opcodes[pos + 1];
	      pos += 2;
	      rhs = decompile_expressions_bounded(&pos, end);
	      stack->expr = or_expr(stack->expr, rhs->expr);
	      pos = end;
	      break;
	  }

	  case CONDITIONAL: {
	      Expr_list *true, *false;

	      /* The end stored in opcodes[pos + 1] is after the ELSE
	       * instruction. */
	      /* Get true expression. */
	      end = the_opcodes[pos + 1];
	      pos += 2;
	      true = decompile_expressions_bounded(&pos, end - 2);

	      /* Get false expression. */
	      pos = end;
	      end = the_opcodes[pos - 1];
	      false = decompile_expressions_bounded(&pos, end);

	      stack->expr = cond_expr(stack->expr, true->expr, false->expr);
	      pos = end;
	      break;
	  }

	  case CRITICAL:
	    /* Ignore this, and look for CRITICAL_END instead. */
	    pos += 2;
	    break;

	  case CRITICAL_END:
	    stack->expr = critical_expr(stack->expr);
	    pos++;
	    break;

	  case PROPAGATE:
	    /* Ignore this, and look for PROPAGATE_END instead. */
	    pos += 2;
	    break;

	  case PROPAGATE_END:
	    stack->expr = propagate_expr(stack->expr);
	    pos++;
	    break;

	  case '!':
	  case NEG:
	    stack->expr = unary_expr(the_opcodes[pos], stack->expr);
	    pos++;
	    break;

	  case '*':
	  case '/':
	  case '%':
	  case '+':
	  case '-':
	  case EQ:
	  case NE:
	  case '>':
	  case GE:
	  case '<':
	  case LE:
	  case IN:
	    stack->next->expr = binary_expr(the_opcodes[pos],
					    stack->next->expr, stack->expr);
	    stack = stack->next;
	    pos++;
	    break;

	  case SPLICE_ADD: {
	      Expr_list **elistp = &stack->expr->u.args;

	      /* Find the end of the list expression's argument list. */
	      while (*elistp)
		  elistp = &(*elistp)->next;

	      /* Append the spliced expression to the end. */
	      *elistp = expr_list(splice_expr(stack->next->expr), NULL);

	      /* Shift the new list expression back one on the stack. */
	      stack->next->expr = stack->expr;
	      stack = stack->next;
	      pos++;
	      break;
	  }

	  case SPLICE:
	    stack->expr = splice_expr(stack->expr);
	    pos++;
	    break;

	  default:
	    /* We've hit a statement token, or a token that takes an argument
	     * list.  Stop. */
	    *pos_ptr = pos;
	    return stack;
	}
    }

    /* We hit the end.  Stop. */
    *pos_ptr = pos;
    return stack;
}

/* Write a statement list onto the output list, backwards. */
static List *unparse_stmt_list(List *output, Stmt_list *stmts, int indent)
{
    Stmt *last;

    if (stmts) {
	last = stmts->next ? stmts->next->stmt : NULL;
	output = unparse_stmt_list(output, stmts->next, indent);
	return unparse_stmt(output, stmts->stmt, indent, last);
    } else {
	return output;
    }
}

static List *unparse_stmt(List *output, Stmt *stmt, int indent, Stmt *last)
{
    String *str;

    switch (stmt->type) {

      /* This switch doesn't include no-op statements or RETURN statements,
       * since we never see them here. */

      case COMMENT:
	/* Add a blank line if there is a previous line and it is not a
	 * comment. */
	if (last && last->type != COMMENT)
	    output = add_and_discard_string(output, string_empty(0));
	str = string_of_char(' ', indent);
	str = string_add(str, "//", 2);
	str = string_add(str, stmt->u.comment, strlen(stmt->u.comment));
	return add_and_discard_string(output, str);

      case EXPR:
	str = string_of_char(' ', indent);
	str = unparse_expr(str, stmt->u.expr);
	str = string_addc(str, ';');
	return add_and_discard_string(output, str);

      case COMPOUND:
	/* The calling routine handles the positioning of the braces. */
	return unparse_stmt_list(output, stmt->u.stmt_list, indent);

      case ASSIGN: {
	  char *s;

	  s = stmt->u.assign.var;
	  str = string_of_char(' ', indent);
	  str = string_add(str, s, strlen(s));
	  str = string_add(str, " = ", 3);
	  str = unparse_expr(str, stmt->u.assign.value);
	  str = string_addc(str, ';');
	  return add_and_discard_string(output, str);
      }

      case IF:
	str = string_of_char(' ', indent);
	str = string_add(str, "if (", 4);
	str = unparse_expr(str, stmt->u.if_.cond);
	str = string_addc(str, ')');
	return unparse_body(output, stmt->u.if_.true, str, indent);

      case IF_ELSE: {
	  int complex;

	  /* This is the most complicated unparsing job, because we need to
	   * decide to format both the if-true and if-false statements, and we
	   * also want to correctly format chains of "if ... else if ... ". */

	  /* Determine if either of the statements is complex (will take more
	   * than one line).  Do this just once at the beginning. */
	  complex = is_complex_if_else_stmt(stmt);

	  /* Start with a string of spaces one less than the indent. */
	  str = string_of_char(' ', indent);

	  /* Execute this for stmt as well as any else-ifs. */
	  while (stmt->type == IF_ELSE) {
	      str = string_add(str, "if (", 4);
	      str = unparse_expr(str, stmt->u.if_.cond);
	      str = string_addc(str, ')');

	      if (stmt->u.if_.cond->type == NOOP) {
		  str = string_addc(str, ';');
		  output = add_and_discard_string(output, str);
		  str = string_of_char(' ', indent);
	      } else if (complex) {
		  str = string_add(str, " {", 2);
		  output = add_and_discard_string(output, str);
		  output = unparse_stmt(output, stmt->u.if_.true,
					indent + the_increment, NULL);
		  str = string_of_char(' ', indent);
		  str = string_add(str, "} ", 2);
	      } else {
		  output = add_and_discard_string(output, str);
		  output = unparse_stmt(output, stmt->u.if_.true,
					indent + the_increment, NULL);
		  str = string_of_char(' ', indent);
	      }
	      
	      str = string_add(str, "else ", 5);

	      /* Set stmt to stmt->u.if_.false, so that we will continue the
	       * loop if the false statement is an if statement. */
	      stmt = stmt->u.if_.false;
	  }

	  if (stmt->type == IF) {
	      str = string_add(str, "if (", 4);
	      str = unparse_expr(str, stmt->u.if_.cond);
	      str = string_add(str, ") ", 2);
	      stmt = stmt->u.if_.true;
	  }

	  /* Now unparse the final statement, which is in stmt. */
	  if (stmt->type == NOOP) {
	      /* Replace the trailing space with a semicolon. */
	      str->s[str->len - 1] = ';';
	      return add_and_discard_string(output, str);
	  } else if (complex) {
	      str = string_addc(str, '{');
	      output = add_and_discard_string(output, str);
	      output = unparse_stmt(output, stmt, indent + the_increment,
				    NULL);
	      str = string_of_char(' ', indent);
	      str = string_addc(str, '}');
	      return add_and_discard_string(output, str);
	  } else {
	      /* Just eliminate the trailing space. */
	      str->s[--str->len] = 0;
	      output = add_and_discard_string(output, str);
	      return unparse_stmt(output, stmt, indent + the_increment, NULL);
	  }
      }

      case FOR_RANGE: {
	  char *s;

	  s = stmt->u.for_range.var;
	  str = string_of_char(' ', indent);
	  str = string_add(str, "for ", 4);
	  str = string_add(str, s, strlen(s));
	  str = string_add(str, " in [", 5);
	  str = unparse_expr(str, stmt->u.for_range.lower);
	  str = string_add(str, " .. ", 4);
	  str = unparse_expr(str, stmt->u.for_range.upper);
	  str = string_addc(str, ']');
	  return unparse_body(output, stmt->u.for_range.body, str, indent);
      }

      case FOR_LIST: {
	  char *s;

	  s = stmt->u.for_list.var;
	  str = string_of_char(' ', indent);
	  str = string_add(str, "for ", 4);
	  str = string_add(str, s, strlen(s));
	  str = string_add(str, " in (", 5);
	  str = unparse_expr(str, stmt->u.for_list.list);
	  str = string_addc(str, ')');
	  return unparse_body(output, stmt->u.for_list.body, str, indent);
      }

      case WHILE:
	str = string_of_char(' ', indent);
	str = string_add(str, "while (", 7);
	str = unparse_expr(str, stmt->u.while_.cond);
	str = string_addc(str, ')');
	return unparse_body(output, stmt->u.while_.body, str, indent);

      case SWITCH:
	str = string_of_char(' ', indent);
	str = string_add(str, "switch (", 8);
	str = unparse_expr(str, stmt->u.switch_.expr);
	str = string_add(str, ") {", 3);
	output = add_and_discard_string(output, str);
	output = unparse_cases(output, stmt->u.switch_.cases,
			       indent + the_increment);
	str = string_of_char(' ', indent);
	str = string_addc(str, '}');
	return add_and_discard_string(output, str);

      case BREAK:
	str = string_of_char(' ', indent);
	str = string_add(str, "break;", 6);
	return add_and_discard_string(output, str);

      case CONTINUE:
	str = string_of_char(' ', indent);
	str = string_add(str, "continue;", 9);
	return add_and_discard_string(output, str);

      case RETURN:
	str = string_of_char(' ', indent);
	str = string_add(str, "return;", 7);
	return add_and_discard_string(output, str);
	
      case RETURN_EXPR:
	str = string_of_char(' ', indent);
	str = string_add(str, "return ", 7);
	str = unparse_expr(str, stmt->u.expr);
	str = string_addc(str, ';');
	return add_and_discard_string(output, str);

      case CATCH: {
	  Id_list *errors;

	  str = string_of_char(' ', indent);
	  str = string_add(str, "catch ", 6);
	  errors = stmt->u.catch.errors;
	  if (errors) {
	      for (; errors; errors = errors->next) {
		  str = string_addc(str, '~');
		  str = string_add(str, errors->ident, strlen(errors->ident));
		  if (errors->next)
		      str = string_add(str, ", ", 2);
	      }
	  } else {
	     str = string_add(str, "any", 3);
	  }
	  str = string_add(str, " {", 2);
	  output = add_and_discard_string(output, str);
	  output = unparse_stmt(output, stmt->u.catch.body,
				indent + the_increment, NULL);
	  if (stmt->u.catch.handler) {
	      str = string_of_char(' ', indent);
	      str = string_add(str, "} with handler {", 16);
	      output = add_and_discard_string(output, str);
	      output = unparse_stmt(output, stmt->u.catch.handler,
				    indent + the_increment, NULL);
	  }
	  str = string_of_char(' ', indent);
	  str = string_addc(str, '}');
	  return add_and_discard_string(output, str);
      }

      default:
	return output;
    }
}

/* Returns nonzero if the if-else statment stmt is complex--that is, if we
 * should display its true and false clauses in braces.  If the false clause
 * is an IF_ELSE or an IF statement, then we will be displaying the false
 * clause's if statment on the same line as our own else statement, so we must
 * check to see if the false clause itself is complex.  Apart from that, it's
 * just a matter of testing if either the true or false clause is complex. */
static int is_complex_if_else_stmt(Stmt *stmt)
{
    if (is_complex_type(stmt->u.if_.true->type))
	return 1;
    else if (stmt->u.if_.false->type == IF_ELSE)
	return is_complex_if_else_stmt(stmt->u.if_.false);
    else if (stmt->u.if_.false->type == IF)
	return is_complex_type(stmt->u.if_.false->u.if_.true->type);
    else
	return is_complex_type(stmt->u.if_.false->type);
}

static int is_complex_type(int type)
{
    return (type != NOOP && type != EXPR && type != ASSIGN && type != BREAK &&
	    type != CONTINUE && type != RETURN_EXPR);
}

static List *unparse_body(List *output, Stmt *body, String *str, int indent)
{
    if (is_complex_type(body->type)) {
	str = string_add(str, " {", 2);
	output = add_and_discard_string(output, str);
	output = unparse_stmt(output, body, indent + the_increment, NULL);
	str = string_of_char(' ', indent);
	str = string_addc(str, '}');
	return add_and_discard_string(output, str);
    } else {
	output = add_and_discard_string(output, str);
	return unparse_stmt(output, body, indent + the_increment, NULL);
    }
}

static List *unparse_cases(List *output, Case_list *cases, int indent)
{
    if (cases) {
	output = unparse_cases(output, cases->next, indent);
	output = unparse_case(output, cases->case_entry, indent);
    }
    return output;
}

static List *unparse_case(List *output, Case_entry *case_entry, int indent)
{
    String *str;

    str = string_of_char(' ', indent);
    if (!case_entry->values) {
	str = string_add(str, "default:", 8);
    } else {
	str = string_add(str, "case ", 5);
	str = unparse_args(str, case_entry->values);
	str = string_addc(str, ':');
    }
    output = add_and_discard_string(output, str);
    return unparse_stmt_list(output, case_entry->stmts,
			     indent + the_increment);
}

static String *unparse_expr(String *str, Expr *expr)
{
    char *s;

    switch (expr->type) {

      case INTEGER: {
	  Number_buf nbuf;

	  s = long_to_ascii(expr->u.num, nbuf);
	  return string_add(str, s, strlen(s));
      }

      case STRING:
	return string_add_unparsed(str, expr->u.str, strlen(expr->u.str));

      case DBREF: {
	  Number_buf nbuf;

	  str = string_addc(str, '#');
	  s = long_to_ascii(expr->u.dbref, nbuf);
	  return string_add(str, s, strlen(s));
      }

      case SYMBOL:
	s = expr->u.symbol;
	str = string_addc(str, '\'');
	if (is_valid_ident(expr->u.symbol))
	    return string_add(str, s, strlen(s));
	else
	    return string_add_unparsed(str, s, strlen(s));

      case ERROR:
	s = expr->u.error;
	str = string_addc(str, '~');
	if (is_valid_ident(expr->u.error))
	    return string_add(str, s, strlen(s));
	else
	    return string_add_unparsed(str, s, strlen(s));

      case NAME:
	s = expr->u.name;
	str = string_addc(str, '$');
	if (is_valid_ident(expr->u.name))
	    return string_add(str, s, strlen(s));
	else
	    return string_add_unparsed(str, s, strlen(s));

      case VAR:
	return string_add(str, expr->u.name, strlen(expr->u.name));

      case FUNCTION_CALL:
	s = expr->u.function.name;
	str = string_add(str, s, strlen(s));
	str = string_addc(str, '(');
	str = unparse_args(str, expr->u.function.args);
	return string_addc(str, ')');

      case PASS:
	str = string_add(str, "pass(", 5);
	str = unparse_args(str, expr->u.args);
	return string_addc(str, ')');

      case MESSAGE:
	s = expr->u.message.name;

	/* Only include target if it's not this(). */
	if (!is_this(expr->u.message.to))
	    str = unparse_expr_prec(str, expr->u.message.to, MESSAGE, 0);

	str = string_addc(str, '.');
	str = string_add(str, s, strlen(s));
	str = string_addc(str, '(');
	str = unparse_args(str, expr->u.message.args);
	str = string_addc(str, ')');
	return str;

      case EXPR_MESSAGE:
	/* Only include target if it's not this(). */
	if (!is_this(expr->u.expr_message.to))
	    str = unparse_expr_prec(str, expr->u.message.to, MESSAGE, 0);

	str = string_add(str, ".(", 2);
	str = unparse_expr(str, expr->u.expr_message.message);
	str = string_addc(str, ')');
	str = string_addc(str, '(');
	str = unparse_args(str, expr->u.expr_message.args);
	return string_addc(str, ')');

      case LIST:
	str = string_addc(str, '[');
	str = unparse_args(str, expr->u.args);
	return string_addc(str, ']');

      case DICT:
	str = string_add(str, "#[", 2);
	str = unparse_args(str, expr->u.args);
	return string_addc(str, ']');

      case BUFFER:
	str = string_add(str, "`[", 2);
	str = unparse_args(str, expr->u.args);
	return string_addc(str, ']');

      case FROB:
	str = string_addc(str, '<');
	str = unparse_expr(str, expr->u.frob.class);
	str = string_add(str, ", ", 2);
	str = unparse_expr(str, expr->u.frob.rep);
	return string_addc(str, '>');

      case INDEX:
	str = unparse_expr_prec(str, expr->u.index.list, INDEX, 0);
	str = string_addc(str, '[');
	str = unparse_expr(str, expr->u.index.offset);
	return string_addc(str, ']');

      case UNARY: {
	  int opcode = expr->u.unary.opcode;

	  str = string_addc(str, (opcode == NEG) ? '-' : opcode);
	  return unparse_expr_prec(str, expr->u.unary.expr, opcode, 0);
      }

      case BINARY: {
	  int opcode = expr->u.binary.opcode;

	  s = binary_token(opcode);
	  str = unparse_expr_prec(str, expr->u.binary.left, opcode, 0);
	  str = string_addc(str, ' ');
	  str = string_add(str, s, strlen(s));
	  str = string_addc(str, ' ');
	  return unparse_expr_prec(str, expr->u.binary.right, opcode, 1);
      }

      case AND:
	str = unparse_expr_prec(str, expr->u.and.left, AND, 1);
	str = string_add(str, " && ", 4);
	return unparse_expr_prec(str, expr->u.and.right, AND, 0);

      case OR:
	str = unparse_expr_prec(str, expr->u.or.left, OR, 1);
	str = string_add(str, " || ", 4);
	return unparse_expr_prec(str, expr->u.or.right, OR, 0);

      case CONDITIONAL:
	str = unparse_expr_prec(str, expr->u.cond.cond, CONDITIONAL, 1);
	str = string_add(str, " ? ", 3);
	str = unparse_expr(str, expr->u.cond.true);
	str = string_add(str, " | ", 3);
	return unparse_expr_prec(str, expr->u.cond.false, CONDITIONAL, 0);

      case CRITICAL:
	str = string_add(str, "(| ", 3);
	str = unparse_expr(str, expr->u.expr);
	return string_add(str, " |)", 3);

      case PROPAGATE:
	str = string_add(str, "(> ", 3);
	str = unparse_expr(str, expr->u.expr);
	return string_add(str, " <)", 3);

      case SPLICE:
	str = string_addc(str, '@');
	if (expr->u.expr->type == BINARY) {
	    /* Add parens around binary expressions for readability. */
	    str = string_addc(str, '(');
	    str = unparse_expr(str, expr->u.expr);
	    return string_addc(str, ')');
	} else {
	    return unparse_expr(str, expr->u.expr);
	}

      case RANGE:
	str = unparse_expr(str, expr->u.range.lower);
	str = string_add(str, " .. ", 4);
	return unparse_expr(str, expr->u.range.upper);

      default:
	return str;
    }
}

static int is_this(Expr *expr)
{
    return (expr->type == FUNCTION_CALL && !expr->u.function.args &&
	    strcmp(expr->u.function.name, "this") == 0);
}

/* Unparse an argument list, backwards.  The double check on args and
 * args->next is not redundant, since an argument list may be empty. */
static String *unparse_args(String *str, Expr_list *args)
{
    if (args) {
	if (args->next) {
	    str = unparse_args(str, args->next);
	    str = string_add(str, ", ", 2);
	}
	str = unparse_expr(str, args->expr);
    }
    return str;
}

/* Unparse an expression, inserting parentheses around it if the caller
 * precedence is greater than the expression's precedence level.  If we
 * should put parentheses around the expression if it is at the same
 * precedence level because of assocation (e.g. a / (b * c)) then assoc
 * should be 1. */
static String *unparse_expr_prec(String *str, Expr *expr, int caller_type,
				 int assoc)
{
    int caller_prec = prec_level(caller_type), type, prec;

    type = (expr->type == BINARY) ? expr->u.binary.opcode :
	(expr->type == UNARY) ? expr->u.unary.opcode : expr->type;
    prec = prec_level(type);

    if (prec >= 0 && (the_parens_flag || caller_prec + assoc > prec)) {
	str = string_addc(str, '(');
	str = unparse_expr(str, expr);
	return string_addc(str, ')');
    } else {
	return unparse_expr(str, expr);
    }
}

static int prec_level(int opcode)
{
    int i;

    for (i = 0; i < PREC_SIZE; i++) {
	if (precedences[i].opcode == opcode)
	    return precedences[i].level;
    }

    /* Atomic expressions won't be in the table. */
    return -1;
}

static char *binary_token(int opcode)
{
    int i;

    for (i = 0; i < TOKEN_SIZE; i++) {
	if (binary_tokens[i].opcode == opcode)
	    return binary_tokens[i].token;
    }
    return "??";
}

static List *add_and_discard_string(List *output, String *str)
{
    Data d;

    d.type = STRING;
    substr_set_to_full_string(&d.u.substr, str);
    output = list_add(output, &d);
    string_discard(str);
    return output;
}

/* Get the variable name for an index. */
static char *varname(int ind)
{
    long id;

    if (ind < the_method->num_args) {
	ind = the_method->num_args - ind - 1;
	id = object_get_ident(the_object, the_method->argnames[ind]);
	return ident_name(id);
    }
    ind -= the_method->num_args;

    if (the_method->rest != -1) {
	if (ind == 0) {
	    id = object_get_ident(the_object, the_method->rest);
	    return ident_name(id);
	}
	ind--;
    }

    id = object_get_ident(the_object, the_method->varnames[ind]);
    return ident_name(id);
}

