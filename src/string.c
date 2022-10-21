/* string.c: String-handling routines.
 * This code is not ANSI-conformant, because it allocates memory at the end
 * of String structure and references it with a one-element array. */

#define _POSIX_SOURCE

#include <stdio.h>
#include <string.h>
#include "cmstring.h"
#include "memory.h"
#include "dbpack.h"
#include "util.h"

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

static String *prepare_to_modify(String *str, int start, int len);

String *string_new(int size_needed)
{
    String *new;
    int size;

    size = STARTING_SIZE;
    while (size < size_needed)
	size = size * 2 + MALLOC_DELTA;
    new = (String *) emalloc(sizeof(String) + sizeof(char) * size);
    new->start = 0;
    new->len = 0;
    new->size = size;
    new->refs = 1;
    new->reg = NULL;
    *new->s = 0;
    return new;
}

String *string_from_chars(char *s, int len)
{
    String *new = string_new(len);

    MEMCPY(new->s, s, len);
    new->s[len] = 0;
    new->len = len;
    return new;
}

String *string_of_char(int c, int len)
{
    String *new = string_new(len);

    memset(new->s, c, len);
    new->s[len] = 0;
    new->len = len;
    return new;
}

String *string_dup(String *str)
{
    str->refs++;
    return str;
}

int string_length(String *str)
{
    return str->len;
}

char *string_chars(String *str)
{
    return str->s + str->start;
}

void string_pack(String *str, FILE *fp)
{
    if (str) {
	write_long(str->len, fp);
	fwrite(str->s + str->start, sizeof(char), str->len, fp);
    } else {
	write_long(-1, fp);
    }
}

String *string_unpack(FILE *fp)
{
    String *str;
    int len;

    len = read_long(fp);
    if (len == -1)
	return NULL;
    str = string_new(len);
    fread(str->s, sizeof(char), str->len, fp);
    return str;
}

int string_packed_size(String *str)
{
    if (str)
	return size_long(str->len) + str->len * sizeof(char);
    else
	return size_long(-1);
}

int string_cmp(String *str1, String *str2)
{
    return strcmp(str1->s + str1->start, str2->s + str2->start);
}

String *string_fread(String *str, int len, FILE *fp)
{
    str = prepare_to_modify(str, str->start, str->len + len);
    fread(str->s + str->start + str->len - len, sizeof(char), len, fp);
    return str;
}

String *string_add(String *str1, String *str2)
{
    str1 = prepare_to_modify(str1, str1->start, str1->len + str2->len);
    MEMCPY(str1->s + str1->start + str1->len - str2->len,
	   str2->s + str2->start, str2->len);
    str1->s[str1->start + str1->len] = 0;
    return str1;
}

String *string_add_chars(String *str, char *s, int len)
{
    str = prepare_to_modify(str, str->start, str->len + len);
    MEMCPY(str->s + str->start + str->len - len, s, len);
    str->s[str->len + str->start + len] = 0;
    return str;
}

String *string_addc(String *str, int c)
{
    str = prepare_to_modify(str, str->start, str->len + 1);
    str->s[str->start + str->len - 1] = c;
    str->s[str->start + str->len] = 0;
    return str;
}

String *string_add_padding(String *str, char *filler, int len, int padding)
{
    str = prepare_to_modify(str, str->start, str->len + padding);

    if (len == 1) {
	/* Optimize this case using memset(). */
	memset(str->s + str->start + str->len - padding, *filler, padding);
	return str;
    }

    while (padding > len) {
	MEMCPY(str->s + str->start + str->len - padding, filler, len);
	padding -= len;
    }
    MEMCPY(str->s + str->start + str->len - padding, filler, padding);
    return str;
}

String *string_truncate(String *str, int len)
{
    str = prepare_to_modify(str, str->start, len);
    str->s[str->start + len] = 0;
    return str;
}

String *string_substring(String *str, int start, int len)
{
    str = prepare_to_modify(str, str->start + start, len);
    str->s[str->start + str->len] = 0;
    return str;
}

String *string_uppercase(String *str)
{
    char *s, *start, *end;

    str = prepare_to_modify(str, str->start, str->len);
    start = str->s + str->start;
    end = start + str->len;
    for (s = start; s < end; s++)
	*s = UCASE(*s);
    return str;
}

String *string_lowercase(String *str)
{
    char *s, *start, *end;

    str = prepare_to_modify(str, str->start, str->len);
    start = str->s + str->start;
    end = start + str->len;
    for (s = start; s < end; s++)
	*s = LCASE(*s);
    return str;
}

/* Compile str's regexp, if it's not already compiled.  If there is an error,
 * it will be placed in regexp_error, and the returned regexp will be NULL. */
regexp *string_regexp(String *str)
{
    if (!str->reg)
	str->reg = regcomp(str->s + str->start);
    return str->reg;
}

void string_discard(String *str)
{
    if (!--str->refs) {
	if (str->reg)
	    free(str->reg);
	free(str);
    }
}

String *string_parse(char **sptr)
{
    String *str;
    char *s = *sptr, *p;

    str = string_new(0);
    s++;
    while (1) {
	for (p = s; *p && *p != '"' && *p != '\\'; p++);
	str = string_add_chars(str, s, p - s);
	s = p + 1;
	if (!*p || *p == '"')
	    break;
	if (*s)
	    str = string_addc(str, *s++);
    }
    *sptr = s;
    return str;
}

String *string_add_unparsed(String *str, char *s, int len)
{
    int i;

    str = string_addc(str, '"');

    /* Add characters to string, escaping quotes and backslashes. */
    while (1) {
	for (i = 0; i < len && s[i] != '"' && s[i] != '\\'; i++);
	str = string_add_chars(str, s, i);
	if (i < len) {
	    str = string_addc(str, '\\');
	    str = string_addc(str, s[i]);
	    s += i + 1;
	    len -= i + 1;
	} else {
	    break;
	}
    }

    return string_addc(str, '"');
}

char *regerror(char *msg)
{
    static char *regexp_error;

    if (msg)
	regexp_error = msg;
    return regexp_error;
}

/* Input to this routine should be a string you want to modify, a start, and a
 * length.  The start gives the offset from str->s at which you start being
 * interested in characters; the length is the amount of characters there will
 * be in the string past that point after you finish modifying it.
 *
 * The return value of this routine is a string whose contents can be freely
 * modified, containing at least the information you claimed was interesting.
 * str->start will be set to the beginning of the interesting characters;
 * str->len will be set to len, even though this will make some characters
 * invalid if len > str->len upon input.  Also, the returned string may not be
 * null-terminated.
 *
 * In general, modifying start and len is the responsibility of this routine;
 * modifying the contents is the responsibility of the calling routine. */
static String *prepare_to_modify(String *str, int start, int len)
{
    String *new;
    int need_to_move, need_to_resize, size;

    /* Figure out if we need to resize the string or move its contents.  Moving
     * contents takes precedence. */
    need_to_resize = (len - start) * 4 < str->size;
    need_to_resize = need_to_resize && str->size > STARTING_SIZE;
    need_to_resize = need_to_resize || (str->size < len);
    need_to_move = (str->refs > 1) || (need_to_resize && start > 0);

    if (need_to_move) {
	/* Move the string's contents into a new list. */
	new = string_new(len);
	MEMCPY(new->s, str->s + start, (len > str->len) ? str->len : len);
	new->len = len;
	string_discard(str);
	return new;
    } else if (need_to_resize) {
	/* Resize the list.  We can assume that list->start == start == 0. */
	str->len = len;
	size = STARTING_SIZE;
	while (size < len)
	    size = size * 2 + MALLOC_DELTA;
	str = erealloc(str, sizeof(String) + (size * sizeof(char)));
	str->size = size;
	return str;
    } else {
	if (str->reg) {
	    free(str->reg);
	    str->reg = NULL;
	}
	str->start = start;
	str->len = len;
	return str;
    }
}

