/* util.h: Declarations for utility functions. */

#ifndef UTIL_H
#define UTIL_H
#include <stdio.h>
#include <stdarg.h>
#include "cmstring.h"

#define NUMBER_BUF_SIZE 32
#define LCASE(c) lowercase[(int) c]
#define UCASE(c) uppercase[(int) c]

typedef char Number_buf[NUMBER_BUF_SIZE];

void init_util(void);
unsigned long hash(char *s);
unsigned long hash_case(char *s, int n);
long atoln(char *s, int n);
char *long_to_ascii(long num, Number_buf nbuf);
int strccmp(char *s1, char *s2);
int strnccmp(char *s1, char *s2, int n);
char *strcchr(char *s, int c);
long random_number(long n);
char *crypt_string(char *key, char *salt);
String *vformat(char *fmt, va_list arg);
String *format(char *fmt, ...);
void fformat(FILE *fp, char *fmt, ...);
String *fgetstring(FILE *fp);
char *english_type(int type);
char *english_integer(int n, Number_buf nbuf);
long parse_ident(char **sptr);
FILE *open_scratch_file(char *name, char *type);
void close_scratch_file(FILE *fp);
void init_scratch_file(void);

extern char lowercase[128];
extern char uppercase[128];

#endif

