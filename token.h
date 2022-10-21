/* token.h: Declarations for the lexer. */

#ifndef TOKEN_H
#define TOKEN_H
#include <stdio.h>
#include "data.h"

void init_token(void);
void lex_start(List *code_lines);
int yylex(void);
int is_valid_ident(char *s);
int cur_lineno(void);

#endif

