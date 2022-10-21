/* cmstring.c: Declarations for string handling. */

#ifndef STRING_H
#define STRING_H

typedef struct string String;

#include <stdio.h>
#include "regexp.h"

struct string {
    int start;
    int len;
    int size;
    int refs; 
    regexp *reg;
    char s[1];
};

/* string.c */
String *string_new(int len);
String *string_empty(int size);
String *string_from_chars(char *s, int len);
String *string_of_char(int c, int len);
String *string_dup(String *str);
int string_length(String *str);
char *string_chars(String *str);
void string_pack(String *str, FILE *fp);
String *string_unpack(FILE *fp);
int string_packed_size(String *str);
int string_cmp(String *str1, String *str2);
String *string_add(String *str1, String *str2);
String *string_add_chars(String *str, char *s, int len);
String *string_addc(String *str, int c);
String *string_add_padding(String *str, char *filler, int len, int padding);
String *string_truncate(String *str, int len);
String *string_substring(String *str, int start, int len);
String *string_uppercase(String *str);
String *string_lowercase(String *str);
regexp *string_regexp(String *str);
void string_discard(String *str);
String *string_parse(char **sptr);
String *string_add_unparsed(String *str, char *s, int len);
char *regerror(char *msg);

#endif

