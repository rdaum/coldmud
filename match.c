/* match.c: Routine for matching against a template. */

#define _POSIX_SOURCE

#include <stdio.h>
#include <string.h>
#include "x.tab.h"
#include "match.h"
#include "memory.h"
#include "data.h"
#include "cmstring.h"
#include "util.h"

/* We use MALLOC_DELTA to keep the memory allocated in fields thirty-two bytes
 * less than a power of two, assuming fields are twelve bytes. */
#define MALLOC_DELTA		3
#define FIELD_STARTING_SIZE	8

typedef struct {
    char *start;
    char *end;
    int strip;			/* Strip backslashes? */
} Field;

static char *match_coupled_wildcard(char *template, char *s);
static char *match_wildcard(char *template, char *s);
static char *match_word_pattern(char *template, char *s);
static void add_field(char *start, char *end, int strip);

static Field *fields;
static int field_pos, field_size;
    
void init_match(void)
{
    fields = EMALLOC(Field, FIELD_STARTING_SIZE);
    field_size = FIELD_STARTING_SIZE;
}

List *match_template(char *template, char *s)
{
    char *p;
    int i, j, coupled;
    List *l;
    String *str;

    field_pos = 0;

    /* Strip leading spaces in template. */
    while (*template == ' ')
	template++;

    while (*template) {

	/* Skip over spaces in s. */
	while (*s == ' ')
	    s++;

	/* Is the next template entry a wildcard? */
	if (*template == '*') {
	    /* Check for coupled wildcard ("*=*"). */
	    if (template[1] == '=' && template[2] == '*') {
		template += 2;
		coupled = 1;
	    } else {
		coupled = 0;
	    }

	    /* Template is invalid if wildcard is not alone. */
	    if (template[1] && template[1] != ' ')
		return NULL;

	    /* Skip over spaces to find next token. */
	    while (*++template == ' ');

	    /* Two wildcards in a row is illegal. */
	    if (*template == '*')
		return NULL;

	    /* Match the wildcard.  This also matches the next token, if there
	     * is one, and adds the appropriate fields. */
	    if (coupled)
		s = match_coupled_wildcard(template, s);
	    else
		s = match_wildcard(template, s);
	    if (!s)
		return NULL;
	} else {
	    /* Match the word pattern.  This does not add any fields, so we
	     * do it ourselves.*/
	    p = match_word_pattern(template, s);
	    if (!p)
		return NULL;
	    add_field(s, p, 0);
	    s = p;
	}

	/* Get to next token in template, if there is one. */
	while (*template && *template != ' ')
	    template++;
	while (*template == ' ')
	    template++;
    }

    /* Ignore any trailing spaces in s. */
    while (*s == ' ')
	s++;

    /* If there's anything left over in s, the match failed. */
    if (*s)
	return NULL;

    /* The match succeeded.  Construct a list of the fields. */
    l = list_new(field_pos);
    for (i = 0; i < field_pos; i++) {
	s = fields[i].start;
	p = fields[i].end;
	if (fields[i].strip) {
	    str = string_new(p - s);
	    j = 0;
	    while (s < p) {
		if (*s == '\\' && s + 1 < p)
		    s++;
		str->s[j++] = *s;
		s++;
	    }
	    str->s[j] = 0;
	    str->len = j;
	} else {
	    str = string_from_chars(s, p - s);
	}
	l->el[i].type = STRING;
	substr_set_to_full_string(&l->el[i].u.substr, str);
    }

    return l;
}

/* Match a coupled wildcard as well as the next token, if there is one.  This
 * adds the fields that it matches. */
static char *match_coupled_wildcard(char *template, char *s)
{
    char *p, *q;

    /* Check for quoted text. */
    if (*s == '"') {
	/* Find the end of the quoted text. */
	p = s + 1;
	while (*p && *p != '"') {
	    if (*p == '\\' && p[1])
		p++;
	    p++;
	}

	/* Skip whitespace after quoted text. */
	for (q = p + 1; *q && *q == ' '; q++);

	/* Move on if next character is an equals sign. */
	if (*q == '=') {
	    for (q++; *q && *q == ' '; q++);
	    add_field(s + 1, p, 1);
	    return match_wildcard(template, q);
	} else {
	    return NULL;
	}
    }

    /* Find first occurrance of an equal sign. */
    p = strchr(s, '=');
    if (!p)
	return NULL;

    /* Add field up to first nonspace character before equalsign, and move on
     * starting from the first nonspace character after it. */
    for (q = p - 1; *q == ' '; q--);
    for (p++; *p == ' '; p++);
    add_field(s, q + 1, 0);
    return match_wildcard(template, p);
}

/* Match a wildcard.  Also match the next token, if there is one.  This adds
 * the fields that it matches. */
static char *match_wildcard(char *template, char *s)
{
    char *p, *q, *r;

    /* If no token follows the wildcard, then the match succeeds. */
    if (!*template) {
	p = s + strlen(s);
	add_field(s, p, 0);
	return p;
    }

    /* There's a word pattern to match.  Check if wildcard match is quoted. */
    if (*s == '"') {
	/* Find the end of the quoted text. */
	p = s + 1;
	while (*p && *p != '"') {
	    if (*p == '\\' && p[1])
		p++;
	    p++;
	}

	/* Skip whitespace after quoted wildcard match. */
	for (q = p + 1; *q == ' '; q++);

	/* Next token must match here. */
	r = match_word_pattern(template, q);
	if (r) {
	    add_field(s + 1, p, 1);
	    add_field(q, r, 0);
	    return r;
	} else {
	    return NULL;
	}
    } else if (*s == '\\') {
	/* Skip an initial backslash.  This is so a wildcard match can start
	 * with a double quote without being a quoted match. */
	s++;
    }

    /* There is an unquoted wildcard match.  Start by looking here. */
    p = match_word_pattern(template, s);
    if (p) {
	add_field(s, s, 0);
	add_field(s, p, 0);
	return p;
    }

    /* Look for more words to match. */
    p = s;
    while (*p) {
	if (*p == ' ') {
	    /* Skip to end of word separator and try next word. */
	    q = p + 1;
	    while (*q == ' ')
		q++;
	    r = match_word_pattern(template, q);
	    if (r) {
		/* It matches; add wildcard field and word field. */
		add_field(s, p, 0);
		add_field(q, r, 0);
		return r;
	    }
	    /* No match; continue looking at q. */
	    p = q;
	} else {
	    p++;
	}
    }

    /* No words matched, so the match fails. */
    return NULL;
}

/* Match a word pattern.  Do not add any fields. */
static char *match_word_pattern(char *template, char *s)
{
    char *p = s;
    int abbrev = 0;

    while (*template && *template != ' ' && *template != '|') {

	if (*template == '"' || *template == '*') {
	    /* Invalid characters in a word pattern; match fails. */
	    return NULL;

	} else if (*template == '?') {
	    /* A question mark tells us that the matching string can be
	     * abbreviated down to this point. */
	    abbrev = 1;

	} else if (LCASE(*p) != LCASE(*template)) {
	    /* The match succeeds if we're at the end of the word in p and
	     * abbrev is set. */
	    if (abbrev && (!*p || *p == ' '))
		return p;

	    /* Otherwise the match against this word fails.  Try the next word
	     * if there is one, or fail if there isn't.  Also catch illegal
	     * characters (* and ") in the word and return NULL if we find
	     * them. */
	    while (*template && !strchr(" |*\"", *template))
		template++;
	    if (*template == '|')
		return match_word_pattern(template + 1, s);
	    else
		return NULL;

	} else {
	    p++;
	}

	template++;
    }

    /* We came to the end of the word in the template.  If we're also at the
     * end of the word in p, then the match succeeds.  Otherwise, try the next
     * word if there is one, and fail if there isn't. */
    if (!*p || *p == ' ')
	return p;
    else if (*template == '|')
	return match_word_pattern(template + 1, s);
    else
	return NULL;
}

/* Add a field.  strip should be true if this is a field for a wildcard not at
 * the end of the template. */
static void add_field(char *start, char *end, int strip)
{
    if (field_pos >= field_size) {
	field_size = field_size * 2 + MALLOC_DELTA;
	fields = EREALLOC(fields, Field, field_size);
    }
    fields[field_pos].start = start;
    fields[field_pos].end = end;
    fields[field_pos].strip = strip;
    field_pos++;
}

/* Returns a backwards list of fields if <s> matches the pattern <pattern>, or
 * NULL if it doesn't. */
List *match_pattern(char *pattern, char *s)
{
    char *p, *q;
    List *list;
    String *str;
    Data d;

    /* Locate wildcard in pattern, if any.  If there isn't any, return an empty
     * list if pattern and s are equivalent, or fail if they aren't. */
    p = strchr(pattern, '*');
    if (!p)
	return (strccmp(pattern, s) == 0) ? list_new(0) : NULL;

    /* Fail if s and pattern don't match up to wildcard. */
    if (strnccmp(pattern, s, p - pattern) != 0)
	return NULL;

    /* Consider s starting after the part that matches the pattern up to the
     * wildcard. */
    s += (p - pattern);

    /* Match always succeeds if wildcard is at end of line. */
    if (!p[1]) {
	list = list_new(1);
	list->el[0].type = STRING;
	str = string_from_chars(s, strlen(s));
	substr_set_to_full_string(&list->el[0].u.substr, str);
	return list;
    }

    /* Find first potential match of rest of pattern. */
    for (q = s; *q && *q != p[1]; q++);

    /* As long as we have a potential match... */
    while (*q) {
	/* Check to see if this is actually a match. */
	list = match_pattern(p + 1, q);
	if (list) {
	    /* It matched.  Append a field and return the list. */
	    d.type = STRING;
	    str = string_from_chars(s, q - s);
	    substr_set_to_full_string(&d.u.substr, str);
	    list = list_add(list, &d);
	    string_discard(str);
	    return list;
	}

	/* Find next potential match. */
	while (*++q && *q != p[1]);
    }

    /* Return failure. */
    return NULL;
}

