/* match.h: Declarations for the pattern matcher. */

#ifndef MATCH_H
#define MATCH_H
#include "data.h"

void init_match(void);
List *match_template(char *template, char *s);
List *match_pattern(char *pattern, char *s);

#endif

