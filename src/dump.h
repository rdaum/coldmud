/* dump.h: Declarations for binary and text database dumps. */

#ifndef DUMP_H
#define DUMP_H
#include <stdio.h>

int binary_dump(void);
int text_dump(void);
void text_dump_read(FILE *fp);

#endif

