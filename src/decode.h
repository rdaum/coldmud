/* decode.h: Declarations for decompiling methods. */

#ifndef DECODE_H
#define DECODE_H
#include "data.h"
#include "object.h"

int line_number(Method *method, int pc);
List *decompile(Method *method, Object *object, int increment, int parens);

#endif

