/* cmstring.c: Declarations for string handling. */

#ifndef STRING_H
#define STRING_H

typedef struct string String;

struct string {
    int len;
    int size;
    int refs;
    char s[1];
};

/* string.c */
String *string_new(int len);
String *string_empty(int size);
String *string_from_chars(char *s, int len);
String *string_of_char(int c, int len);
String *string_dup(String *string);
String *string_add(String *string, char *s, int len);
String *string_addc(String *string, int c);
String *string_parse(char **sptr);
String *string_add_unparsed(String *string, char *s, int len);
String *string_truncate(String *string, int len);
String *string_extend(String *string, int len);
void string_discard(String *string);

#endif

