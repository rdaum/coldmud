/* stringop.c: Function operators acting on strings. */
#define _POSIX_SOURCE

#include <string.h>
#include <stdlib.h>
#include "x.tab.h"
#include "operator.h"
#include "execute.h"
#include "cmstring.h"
#include "data.h"
#include "match.h"
#include "ident.h"
#include "util.h"

void op_strlen(void)
{
    Data *args;
    int len;

    /* Accept a string to take the length of. */
    if (!func_init_1(&args, STRING))
	return;

    /* Replace the argument with its length. */
    len = string_length(args[0].u.str);
    pop(1);
    push_int(len);
}

void op_substr(void)
{
    int num_args, start, len, string_len;
    Data *args;

    /* Accept a string for the initial string, an integer specifying the start
     * of the substring, and an optional integer specifying the length of the
     * substring. */
    if (!func_init_2_or_3(&args, &num_args, STRING, INTEGER, INTEGER))
	return;

    string_len = string_length(args[0].u.str);
    start = args[1].u.val - 1;
    len = (num_args == 3) ? args[2].u.val : string_len - start;

    /* Make sure range is in bounds. */
    if (start < 0) {
	throw(range_id, "Start (%d) is less than one.", start + 1);
    } else if (len < 0) {
	throw(range_id, "Length (%d) is less than zero.", len);
    } else if (start + len > string_len) {
	throw(range_id,
	      "The substring extends to %d, past the end of the string (%d).",
	      start + len, string_len);
    } else {
	/* Replace first argument with substring, and pop other arguments. */
	anticipate_assignment();
	args[0].u.str = string_substring(args[0].u.str, start, len);
	pop(num_args - 1);
    }
}

void op_explode(void)
{
    int num_args, sep_len, len, want_blanks;
    Data *args, d;
    List *exploded;
    char *sep, *s, *p, *q;
    String *word;

    /* Accept a string to explode and an optional string for the word
     * separator. */
    if (!func_init_1_to_3(&args, &num_args, STRING, STRING, 0))
	return;

    want_blanks = (num_args == 3) ? data_true(&args[2]) : 0;
    if (num_args >= 2) {
	sep = string_chars(args[1].u.str);
	sep_len = string_length(args[1].u.str);
    } else {
	sep = " ";
	sep_len = 1;
    }

    s = string_chars(args[0].u.str);
    len = string_length(args[0].u.str);

    exploded = list_new(0);
    p = s;
    for (q = strcstr(p, sep); q; q = strcstr(p, sep)) {
	if (want_blanks || q > p) {
	    /* Add the word. */
	    word = string_from_chars(p, q - p);
	    d.type = STRING;
	    d.u.str = word;
	    exploded = list_add(exploded, &d);
	    string_discard(word);
	}
	p = q + sep_len;
    }

    if (*p || want_blanks) {
	/* Add the last word. */
	word = string_from_chars(p, len - (p - s));
	d.type = STRING;
	d.u.str = word;
	exploded = list_add(exploded, &d);
	string_discard(word);
    }

    /* Pop the arguments and push the list onto the stack. */
    pop(num_args);
    push_list(exploded);
    list_discard(exploded);
}

void op_strsub(void)
{
    int len, search_len, replace_len;
    Data *args;
    char *search, *replace, *s, *p, *q;
    String *subbed;

    /* Accept a base string, a search string, and a replacement string. */
    if (!func_init_3(&args, STRING, STRING, STRING))
	return;

    s = string_chars(args[0].u.str);
    len = string_length(args[0].u.str);
    search = string_chars(args[1].u.str);
    search_len = string_length(args[1].u.str);
    replace = string_chars(args[2].u.str);
    replace_len = string_length(args[2].u.str);

    subbed = string_new(search_len);
    p = s;
    for (q = strcstr(p, search); q; q = strcstr(p, search)) {
	subbed = string_add_chars(subbed, p, q - p);
	subbed = string_add_chars(subbed, replace, replace_len);
	p = q + search_len;
    }

    subbed = string_add_chars(subbed, p, len - (p - s));

    /* Pop the arguments and push the new string onto the stack. */
    pop(3);
    push_string(subbed);
    string_discard(subbed);
}

/* Pad a string on the left (positive length) or on the right (negative
 * length).  The optional third argument gives the fill character. */
void op_pad(void)
{
    int num_args, len, padding, filler_len;
    Data *args;
    char *filler;
    String *padded;

    if (!func_init_2_or_3(&args, &num_args, STRING, INTEGER, STRING))
	return;

    if (num_args == 3) {
	filler = string_chars(args[2].u.str);
	filler_len = string_length(args[2].u.str);
    } else {
	filler = " ";
	filler_len = 1;
    }

    len = (args[1].u.val > 0) ? args[1].u.val : -args[1].u.val;
    padding = len - string_length(args[0].u.str);

    /* Construct the padded string. */
    anticipate_assignment();
    padded = args[0].u.str;
    if (padding == 0) {
	/* Do nothing.  Easiest case. */
    } else if (padding < 0) {
	/* We're shortening the string.  Almost as easy. */
	padded = string_truncate(padded, len);
    } else if (args[1].u.val > 0) {
	/* We're lengthening the string on the right. */
	padded = string_add_padding(padded, filler, filler_len, padding);
    } else {
	/* We're lengthening the string on the left. */
	padded = string_new(padding + args[0].u.str->len);
	padded = string_add_padding(padded, filler, filler_len, padding);
	padded = string_add(padded, args[0].u.str);
	string_discard(args[0].u.str);
    }
    args[0].u.str = padded;

    /* Discard all but the first argument. */
    pop(num_args - 1);
}

void op_match_begin(void)
{
    Data *args;
    int sep_len, search_len;
    char *sep, *search, *s, *p;

    /* Accept a base string, a search string, and a replacement string. */
    if (!func_init_3(&args, STRING, STRING, STRING))
	return;

    s = string_chars(args[0].u.str);
    sep = string_chars(args[1].u.str);
    sep_len = string_length(args[1].u.str);
    search = string_chars(args[2].u.str);
    search_len = string_length(args[2].u.str);

    for (p = strcstr(s, sep); p; p = strcstr(p + 1, sep)) {
	/* We found a separator; see if it's followed by search. */
	if (strnccmp(p + sep_len, search, search_len) == 0) {
	    pop(3);
	    push_int(1);
	    return;
	}
    }

    pop(3);
    push_int(0);
}

/* Match against a command template. */
void op_match_template(void)
{
    Data *args;
    List *fields;
    char *template, *str;

    /* Accept a string for the template and a string to match against. */
    if (!func_init_2(&args, STRING, STRING))
	return;

    template = string_chars(args[0].u.str);
    str = string_chars(args[1].u.str);

    fields = match_template(template, str);

    pop(2);
    if (fields) {
	push_list(fields);
	list_discard(fields);
    } else {
	push_int(0);
    }
}

/* Match against a command template. */
void op_match_pattern(void)
{
    Data *args;
    List *fields;
    char *pattern, *str;

    /* Accept a string for the pattern and a string to match against. */
    if (!func_init_2(&args, STRING, STRING))
	return;

    pattern = string_chars(args[0].u.str);
    str = string_chars(args[1].u.str);

    fields = match_pattern(pattern, str);

    pop(2);
    if (!fields) {
	push_int(0);
	return;
    }

    /* fields is backwards.  Reverse it. */
    fields = list_reverse(fields);

    push_list(fields);
    list_discard(fields);
}

void op_match_regexp(void)
{
    Data *args, d;
    regexp *reg;
    List *fields, *elemlist;
    int num_args, case_flag, i;
    char *s;

    if (!func_init_2_or_3(&args, &num_args, STRING, STRING, 0))
	return;

    case_flag = (num_args == 3) ? data_true(&args[2]) : 0;

    reg = string_regexp(args[0].u.str);
    if (!reg) {
	throw(regexp_id, "%s", regerror(NULL));
	return;
    }

    /* Execute the regexp. */
    s = string_chars(args[1].u.str);
    if (regexec(reg, s, case_flag)) {
	/* Build the list of fields. */
	fields = list_new(NSUBEXP);
	for (i = 0; i < NSUBEXP; i++) {
	    elemlist = list_new(2);

	    d.type = INTEGER;
	    if (reg->startp[i]) {
		d.u.val = 0;
		elemlist = list_add(elemlist, &d);
		elemlist = list_add(elemlist, &d);
	    } else {
		d.u.val = reg->startp[i] - s + 1;
		elemlist = list_add(elemlist, &d);
		d.u.val = reg->endp[i] - reg->startp[i];
		elemlist = list_add(elemlist, &d);
	    }

	    d.type = LIST;
	    d.u.list = elemlist;
	    fields = list_add(fields, &d);
	}
    }

    pop(num_args);
    if (fields) {
	push_list(fields);
	list_discard(fields);
    } else {
	push_int(0);
    }
}

/* Encrypt a string. */
void op_crypt(void)
{
    int num_args;
    Data *args;
    char *s, *encrypted;
    String *str;

    /* Accept a string to encrypt and an optional salt. */
    if (!func_init_1_or_2(&args, &num_args, STRING, STRING))
	return;
    if (num_args == 2 && string_length(args[1].u.str) != 2) {
	throw(salt_id, "Salt (%S) is not two characters.", args[1].u.str);
	return;
    }

    s = string_chars(args[0].u.str);

    if (num_args == 2) {
	encrypted = crypt_string(s, string_chars(args[1].u.str));
    } else {
	encrypted = crypt_string(s, NULL);
    }

    pop(num_args);
    str = string_from_chars(encrypted, strlen(encrypted));
    push_string(str);
    string_discard(str);
}

void op_uppercase(void)
{
    Data *args;

    /* Accept a string to uppercase. */
    if (!func_init_1(&args, STRING))
	return;

    args[0].u.str = string_uppercase(args[0].u.str);
}

void op_lowercase(void)
{
    Data *args;

    /* Accept a string to uppercase. */
    if (!func_init_1(&args, STRING))
	return;

    args[0].u.str = string_lowercase(args[0].u.str);
}

void op_strcmp(void)
{
    Data *args;
    int val;

    /* Accept two strings to compare. */
    if (!func_init_2(&args, STRING, STRING))
	return;

    /* Compare the strings case-sensitively. */
    val = strcmp(string_chars(args[0].u.str), string_chars(args[1].u.str));
    pop(2);
    push_int(val);
}

