/* string.c: String-handling routines.
 * This code is not ANSI-conformant, because it allocates memory at the end
 * of String structure and references it with a one-element array. */

#define _POSIX_SOURCE

#include <stdio.h>
#include <string.h>
#include "String.h"
#include "memory.h"
#include "util.h"

/* Note that we number string elements [0..(len - 1)] internally, while the
 * user sees string elements as numbered [1..len]. */

/* Delta and minimum bit size for strings. */
const int DELTA = (sizeof(String) + 32);
const int MIN_POWER = 7;

/* Representation of an empty string. */
String_rep empty_rep = { 0, 0, 1, 1, NULL, { 0 } };

static String_rep* rep_new(int size);
static String_rep* rep_replace(String_rep* rep, int size, int start, int len);

String::String(int size)
{
    rep = rep_new(size);
}

String::String(char* s)
{
    int len = strlen(s);

    rep = rep_new(len);
    MEMCPY(rep->s, s, char, len);
}

String::String(char* s, int len)
{
    rep = rep_new(len);
    MEMCPY(rep->s, s, char, len);
}

regexp* String::regexp()
{
    if (!rep->reg)
	rep->reg = regcomp(chars());
    return rep->reg;
}

void String::append(char *s, int len)
{
    if (len == 0)
	return;
    else if (rep->refs > 1 || rep->size < rep->len + len)
	rep = rep_replace(rep, rep->len + len, 0, rep->len);
    MEMCPY(rep->s + rep->start + rep->len, s, char, len);
    rep->len += len;
    rep->s[rep->start + rep->len] = 0;
}

void String::append(char c)
{
    if (rep->refs > 1 || rep->size < rep->len + 1)
	rep = rep_replace(rep, rep->len + 1, 0, rep->len);
    rep->s[rep->start + rep->len] = c;
    rep->len++;
    rep->s[rep->start + rep->len] = 0;
}

void String::append(char c, int len)
{
    if (rep->refs > 1 || rep->size < rep->len + len)
	rep = rep_replace(rep, rep->len + len, 0, rep->len);
    memset(rep->s + rep->start + rep->len, c, len);
    rep->len += len;
    rep->s[rep->start + rep->len] = 0;
}

void String::append(String str)
{
    if (rep->len == 0) {
	if (--rep->refs == 0)
	    rep_free(rep);
	rep = str->rep;
	rep->refs++;
    } else {
	append(str->chars(), str->length());
    }
}

void String::substring(int start, int len)
{
    if (len == -1)
	len = rep->len - start;
    if (len == rep->len)
	return;
    if (rep->refs > 1 || rep->size > len * 4) {
	rep = rep_replace(rep, len, start, len);
    } else {
	rep->start += start;
	rep->len = len;
    }
}

void String::uppercase()
{
    char *s, *end;

    if (rep->refs > 1)
	rep = rep_replace(rep, rep->len, start, rep->len);
    end = rep->s + rep->start + rep->len;
    for (s = rep->s + rep->start; s < end; s++)
	*s = UCASE(*s);
}

void String::lowercase()
{
    char *s, *end;

    if (rep->refs > 1)
	rep = rep_replace(rep, rep->len, rep->len, start);
    end = rep->s + rep->start + rep->len;
    for (s = rep->s + rep->start; s < end; s++)
	*s = LCASE(*s);
}

void rep_free(String_rep* rep)
{
    if (rep->reg)
	free(rep->reg);
    free(rep);
}

/* Return a rep which has a ref count of one, is large enough to hold
 * <size> characters plus a null terminator, and is empty. */
static String_rep* rep_new(int size)
{
    String_rep* rep;

    size = adjust_size(size + sizeof(String));
    rep = EMALLOC(rep, char, size);
    rep->size = size - sizeof(String);
    rep->start = 0;
    rep->len = 0;
    rep->s[0] = 0;
    rep->reg = NULL;
    rep->refs = 1;
    return rep;
}

/* Return a rep which has a ref count of one, is large enough to hold <size>
 * characters plus a null terminator, and contains the first <len> characters
 * of the old rep's string, starting at an offset of <start> from the
 * beginning of the old rep's string.  The new rep is a valid rep; that is,
 * it is null-terminated with a length of <len>. */
static String_rep* rep_replace(String_rep* rep, int size, int start, int len)
{
    String_rep* new;

    if (rep->refs > 1) {
	/* Replace the old representation with a new one. */
	new = rep_new(size);
	MEMCPY(new->s + new->start, rep->s + rep->start + start, len);
	new->len = len;
	new->s[new->start + len] = 0;
	rep->refs--;
	return new;
    } else {
	/* Resize the old representation. */
	size = adjust_size(size + sizeof(String), MIN_POWER, DELTA);
	if (rep->start + start != 0) {
	    MEMMOVE(rep->s, rep->s + rep->start + start, len);
	    rep->start = 0;
	}
	rep = EREALLOC(rep, char, size);
	rep->size = size - sizeof(String);
	rep->len = len;
	rep->s[rep->len] = 0;
	return rep;
    }
}

char *regerror(char *msg)
{
    static char *regexp_error;

    if (msg)
	regexp_error = msg;
    return regexp_error;
}

