/* grammar.h: Declarations for the parser. */

#ifndef GRAMMAR_H
#define GRAMMAR_H
#include <stdarg.h>
#include "object.h"
#include "data.h"

Method *compile(Object *object, List *code, List **error_ret);
void compiler_error(int lineno, char *fmt, ...);
int no_errors(void);

#endif

