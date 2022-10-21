/* string.c: String-handling routines.
 * This code is not ANSI-conformant, because it allocates memory at the end
 * of String structure and references it with a one-element array. */

#define _POSIX_SOURCE

#include <string.h>
#include "cmstring.h"
#include "memory.h"

/* Note that we number string elements [0..(len - 1)] internally, while the
 * user sees string elements as numbered [1..len]. */

/* Many implementations of malloc() deal best with blocks eight or sixteen
 * bytes less than a power of two.  MALLOC_DELTA and STRING_STARTING_SIZE take
 * this into account.  We start with a string MALLOC_DELTA bytes less than
 * a power of two.  When we enlarge a string, we double it and add MALLOC_DELTA
 * so that we're still MALLOC_DELTA less than a power of two.  When we
 * allocate, we add in sizeof(String), leaving us 32 bytes short of a power of
 * two, as desired. */

#define MALLOC_DELTA	(sizeof(String) + 32)
#define STARTING_SIZE	(128 - MALLOC_DELTA)

static String *prepare_to_modify(String *string, int len);

String *string_new(int len)
{
    String *new;
    int size;

    size = STARTING_SIZE;
    while (size < len)
	size = size * 2 + MALLOC_DELTA;
    new = (String *) emalloc(sizeof(String) + sizeof(char) * size);
    new->len = len;
    new->size = size;
    new->refs = 1;
    new->reg = NULL;
    return new;
}

String *string_empty(int size)
{
    String *new = string_new(size);

    new->s[0] = 0;
    new->len = 0;
    return new;
}

String *string_from_chars(char *s, int len)
{
    String *new = string_new(len);

    MEMCPY(new->s, s, char, len);
    new->s[len] = 0;
    return new;
}

String *string_of_char(int c, int len)
{
    String *new = string_new(len);

    memset(new->s, c, len);
    new->s[len] = 0;
    return new;
}

String *string_dup(String *string)
{
    string->refs++;
    return string;
}

String *string_add(String *string, char *s, int len)
{
    string = prepare_to_modify(string, string->len + len);
    MEMCPY(string->s + string->len, s, char, len);
    string->s[string->len + len] = 0;
    string->len += len;
    return string;
}

String *string_addc(String *string, int c)
{
    string = prepare_to_modify(string, string->len + 1);
    string->s[string->len] = c;
    string->s[++string->len] = 0;
    return string;
}

String *string_parse(char **sptr)
{
    String *str;
    char *s = *sptr, *p;

    str = string_new(0);
    s++;
    while (1) {
	for (p = s; *p && *p != '"' && *p != '\\'; p++);
	str = string_add(str, s, p - s);
	s = p + 1;
	if (!*p || *p == '"')
	    break;
	if (*s)
	    str = string_addc(str, *s++);
    }
    *sptr = s;
    return str;
}

String *string_add_unparsed(String *string, char *s, int len)
{
    int i;

    string = string_addc(string, '"');

    /* Add characters to string, escaping quotes and backslashes. */
    while (1) {
	for (i = 0; i < len && s[i] != '"' && s[i] != '\\'; i++);
	string = string_add(string, s, i);
	if (i < len) {
	    string = string_addc(string, '\\');
	    string = string_addc(string, s[i]);
	    s += i + 1;
	    len -= i + 1;
	} else {
	    break;
	}
    }

    return string_addc(string, '"');
}

String *string_truncate(String *string, int len)
{
    len = (len > string->len) ? string->len : len;
    string = prepare_to_modify(string, len);
    string->s[len] = 0;
    string->len = len;
    return string;
}

String *string_extend(String *string, int len)
{
    return prepare_to_modify(string, len);
}

void string_discard(String *string)
{
    if (!--string->refs) {
	if (string->reg)
	    free(string->reg);
	free(string);
    }
}

static String *prepare_to_modify(String *string, int len)
{
    String *new;

    if (string->refs > 1 || string->size < len) {
	new = string_new(len);
	new->len = string->len;
	MEMCPY(new->s, string->s, char, string->len + 1);
	new->reg = NULL;
	string_discard(string);
	return new;
    } else {
	if (string->reg) {
	    free(string->reg);
	    string->reg = NULL;
	}
	return string;
    }
}

