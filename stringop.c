/* stringop.c: Function operators acting on strings. */
#define _POSIX_SOURCE

#include <string.h>
#include <stdlib.h>
#include "x.tab.h"
#include "operator.h"
#include "execute.h"
#include "cmstring.h"
#include "data.h"
#include "util.h"
#include "match.h"
#include "ident.h"

static int do_match(char *s, int slen, char *m, int mlen, char *t, int tlen);

static char *regexp_error;

void op_strlen(void)
{
    Data *args;
    int len;

    /* Accept a string to take the length of. */
    if (!func_init_1(&args, STRING))
	return;

    /* Replace the argument with its length. */
    len = args[0].u.substr.span;
    pop(1);
    push_int(len);
}

void op_substr(void)
{
    int num_args, start, span;
    Data *args;

    /* Accept a string for the initial string, an integer specifying the start
     * of the substring, and an optional integer specifying the length of the
     * substring. */
    if (!func_init_2_or_3(&args, &num_args, STRING, INTEGER, INTEGER))
	return;

    start = args[1].u.val - 1;
    span = (num_args == 3) ? args[2].u.val : args[0].u.substr.span - start;
    if (start < 0) {
	throw(range_id, "Start (%d) is less than one.", start + 1);
    } else if (span < 0) {
	throw(range_id, "Span (%d) is less than zero.", span);
    } else if (start + span > args[0].u.substr.span) {
	throw(range_id,
	      "The substring extends to %d, past the end of the string (%d).",
	      start + span, args[0].u.substr.span);
    } else {
	/* Replace first argument with substring, and pop other arguments. */
	args[0].u.substr.start += start;
	args[0].u.substr.span = span;
	pop(num_args - 1);
    }
}

void op_explode(void)
{
    int num_args, tlen, len, i, start;
    Data *args, d;
    List *exploded;
    char *token, *s;
    String *word;

    /* Accept a string to explode and an optional string for the word
     * separator. */
    if (!func_init_1_or_2(&args, &num_args, STRING, STRING))
	return;

    if (num_args == 2) {
	token = data_sptr(&args[1]);
	tlen = args[1].u.substr.span;
    } else {
	token = " ";
	tlen = 1;
    }
    s = data_sptr(&args[0]);
    len = args[0].u.substr.span;

    exploded = list_new(0);
    start = 0;
    while (1) {
	/* Look for first character of token in s. */
	for (i = start; i + tlen <= len && s[i] != *token; i++);

	/* Stop if we hit the end of the string. */
	if (i + tlen > len)
	    break;

	if (strnccmp(&s[i], token, tlen) == 0) {
	    /* We found a word separator. */
	    if (i > start) {
		/* Only add word if it has nonzero length. */
		word = string_from_chars(&s[start], i - start);
		d.type = STRING;
		substr_set_to_full_string(&d.u.substr, word);
		exploded = list_add(exploded, &d);
		string_discard(word);
	    }
	    start = i + tlen;
	} else {
	    start++;
	}
    }

    /* Add the last word unless we're right at the end. */
    if (start < len) {
	word = string_from_chars(&s[start], len - start);
	d.type = STRING;
	substr_set_to_full_string(&d.u.substr, word);
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
    int slen, rlen, len, i, start;
    Data *args;
    char *sstr, *rstr, *s;
    String *subbed;

    /* Accept a base string, a search string, and a replacement string. */
    if (!func_init_3(&args, STRING, STRING, STRING))
	return;

    s = data_sptr(&args[0]);
    len = args[0].u.substr.span;
    sstr = data_sptr(&args[1]);
    slen = args[1].u.substr.span;
    rstr = data_sptr(&args[2]);
    rlen = args[2].u.substr.span;

    subbed = string_empty(slen);
    start = 0;
    while (1) {
	/* Look for first character of sstr in s. */
	for (i = start; i + slen <= len && s[i] != *sstr; i++);

	/* Stop if we hit the end of the string. */
	if (i + slen > len)
	    break;

	if (strnccmp(&s[i], sstr, slen) == 0) {
	    /* We found the search string. */
	    subbed = string_add(subbed, &s[start], i - start);
	    subbed = string_add(subbed, rstr, rlen);
	    start = i + slen;
	} else {
	    subbed = string_add(subbed, &s[start], i + 1 - start);
	    start = i + 1;
	}
    }

    subbed = string_add(subbed, &s[start], len - start);

    /* Pop the arguments and push the new string onto the stack. */
    pop(3);
    push_string(subbed);
    string_discard(subbed);
}

/* Pad a string on the left (positive length) or on the right (negative
 * length).  The optional third argument gives the fill character. */
void op_pad(void)
{
    int num_args, len, padding;
    Data *args;
    char fill;
    Substring *substr;
    String *padded;

    if (!func_init_2_or_3(&args, &num_args, STRING, INTEGER, STRING))
	return;
    if (num_args == 3 && args[2].u.substr.span != 1) {
	throw(type_id, "The third argument (%D) is not one character.",
	      &args[2]);
	return;
    }

    /* Construct the padded string. */
    anticipate_assignment();
    substr = &args[0].u.substr;
    len = (args[1].u.val > 0) ? args[1].u.val : -args[1].u.val;
    padding = len - args[0].u.substr.span;
    fill = (num_args == 3) ? *data_sptr(&args[2]) : ' ';
    if (padding <= 0) {
	substr->span = len;
    } else if (args[1].u.val > 0) {
	substring_truncate(substr);
	substr->str = string_extend(substr->str, substr->start + len);
	memset(&substr->str->s[substr->start + substr->span], fill, padding);
	substr->span = len;
	substr->str->len = substr->start + len;
	substr->str->s[substr->start + len] = 0;
    } else {
	padded = string_of_char(fill, padding);
	padded = string_add(padded, &substr->str->s[substr->start],
			    substr->span);
	string_discard(substr->str);
	substr_set_to_full_string(substr, padded);
    }

    /* Discard all but the first argument. */
    pop(num_args - 1);
}

static int do_match(char *s, int slen, char *m, int mlen, char *t, int tlen)
{
    int pos;

    /* Obviously, no match if slen is less than mlen. */
    if (slen < mlen)
	return 0;

    /* Check for a match at the beginning of the string. */
    if (strnccmp(s, m, mlen) == 0)
	return 1;

    /* Start checking after one token's length. */
    pos = tlen;
    while (1) {
	/* Look for the first character in m. */
	while (pos + mlen <= slen && s[pos] != *m)
	    pos++;

	/* No match if we couldn't find *m for pos <= slen - mlen. */
	if (pos + mlen > slen)
	    return 0;

	/* Only check against m if we're just after a word-separator. */
	if (strnccmp(&s[pos - slen], t, tlen) == 0) {
	    /* Match against m. */
	    if (strnccmp(&s[pos], m, mlen) == 0) {
		/* We have a match.  Return 1. */
		return 1;
	    }
	}

	/* It wasn't a match.  Continue at pos + 1. */
	pos++;
    }

    /* We never found a match; return 0. */
    return 0;
}

void op_match_begin(void)
{
    int num_args, tlen, result;
    Data *args;
    char *token;

    /* Accept a string to search in, a string to search for, and an optional
     * string giving the word separator. */
    if (!func_init_2_or_3(&args, &num_args, STRING, STRING, STRING))
	return;

    token = (num_args == 3) ? data_sptr(&args[2]) : " ";
    tlen = (num_args == 3) ? args[2].u.substr.span : 1;

    result = do_match(data_sptr(&args[0]), args[0].u.substr.span,
		      data_sptr(&args[1]), args[1].u.substr.span,
		      token, tlen);

    pop(num_args);
    push_int(result);
}

/* Match against a command template. */
void op_match_template(void)
{
    Data *args;
    List *fields;

    /* Accept a string for the template and a string to match against. */
    if (!func_init_2(&args, STRING, STRING))
	return;

    /* Make sure strings we pass to match_template() are null-terminated. */
    substring_truncate(&args[0].u.substr);
    substring_truncate(&args[1].u.substr);
    fields = match_template(data_sptr(&args[0]), data_sptr(&args[1]));

    pop(2);
    if (fields) {
	stack[stack_pos].type = LIST;
	sublist_set_to_full_list(&stack[stack_pos].u.sublist, fields);
	stack_pos++;
    } else {
	push_int(0);
    }
}

/* Match against a command template. */
void op_match_pattern(void)
{
    Data *args;
    List *fields;
    Substring tmp;
    int i;

    /* Accept a string for the pattern and a string to match against. */
    if (!func_init_2(&args, STRING, STRING))
	return;

    /* Make sure strings we pass to match_pattern() are null-terminated. */
    substring_truncate(&args[0].u.substr);
    substring_truncate(&args[1].u.substr);
    fields = match_pattern(data_sptr(&args[0]), data_sptr(&args[1]));

    pop(2);
    if (fields) {
	/* fields is backwards.  Reverse it. */
	for (i = 0; i * 2 < fields->len; i++) {
	    tmp = fields->el[i].u.substr;
	    fields->el[i].u.substr = fields->el[fields->len - 1 - i].u.substr;
	    fields->el[fields->len - 1 - i].u.substr = tmp;
	}
	stack[stack_pos].type = LIST;
	sublist_set_to_full_list(&stack[stack_pos].u.sublist, fields);
	stack_pos++;
    } else {
	push_int(0);
    }
}

void op_match_regexp(void)
{
    Data *args;
    regexp *reg;
    List *fields = NULL, *elemlist;
    int i;
    char *s;

    if (!func_init_2(&args, STRING, STRING))
	return;

    /* Get the cached regexp, if there is one, or compile it. */
    substring_truncate(&args[0].u.substr);
    substring_truncate(&args[1].u.substr);
    if (args[0].u.substr.start == 0 && args[0].u.substr.str->reg)
	reg = args[0].u.substr.str->reg;
    else
	reg = regcomp(data_sptr(&args[0]));

    if (!reg) {
	throw(regexp_id, "%s", regexp_error);
	return;
    }

    /* Execute the regexp. */
    s = data_sptr(&args[1]);
    if (regexec(reg, s)) {
	/* Build the list of fields. */
	fields = list_new(NSUBEXP);
	for (i = 0; i < NSUBEXP; i++) {
	    elemlist = list_new(2);
	    elemlist->el[0].type = elemlist->el[1].type = INTEGER;
	    if (reg->startp[i]) {
		elemlist->el[0].u.val = reg->startp[i] - s + 1;
		elemlist->el[1].u.val = reg->endp[i] - reg->startp[i];
	    } else {
		elemlist->el[0].u.val = elemlist->el[1].u.val = 0;
	    }
	    fields->el[i].type = LIST;
	    sublist_set_to_full_list(&fields->el[i].u.sublist, elemlist);
	}
    }

    /* Store the regexp if possible. */
    if (args[0].u.substr.start == 0)
	args[0].u.substr.str->reg = reg;
    else
	free(reg);

    pop(2);
    if (fields) {
	push_list(fields);
	list_discard(fields);
    } else {
	push_int(0);
    }
}

void regerror(char *msg)
{
    regexp_error = msg;
}

/* Encrypt a string. */
void op_crypt(void)
{
    int num_args, len;
    Data *args;
    char *s, save, *encrypted, salt[3];
    String *str;

    /* Accept a string to encrypt and an optional salt. */
    if (!func_init_1_or_2(&args, &num_args, STRING, STRING))
	return;

    /* Temporarily convert args[0] to a null-terminated string. */
    s = data_sptr(&args[0]);
    len = args[0].u.substr.span;
    save = s[len];
    s[len] = 0;

    if (num_args == 2) {
	salt[0] = *data_sptr(&args[1]);
	salt[1] = *(data_sptr(&args[1]) + 1);
	salt[2] = 0;
	encrypted = crypt_string(s, salt);
    } else {
	encrypted = crypt_string(s, NULL);
    }

    /* Restore the character we clobbered. */
    s[len] = save;

    pop(num_args);
    stack[stack_pos].type = STRING;
    str = string_from_chars(encrypted, strlen(encrypted));
    substr_set_to_full_string(&stack[stack_pos].u.substr, str);
    stack_pos++;
}

void op_uppercase(void)
{
    Data *args;
    Substring *substr;
    char *s;

    /* Accept a string to uppercase. */
    if (!func_init_1(&args, STRING))
	return;

    /* Uppercase all the characters in the argument. */
    substr = &args[0].u.substr;
    substring_truncate(substr);
    for (s = data_sptr(&args[0]); *s; s++)
	*s = UCASE(*s);
}

void op_lowercase(void)
{
    Data *args;
    Substring *substr;
    char *s;

    /* Accept a string to uppercase. */
    if (!func_init_1(&args, STRING))
	return;

    /* Uppercase all the characters in the argument. */
    substr = &args[0].u.substr;
    substring_truncate(substr);
    for (s = data_sptr(&args[0]); *s; s++)
	*s = LCASE(*s);
}

void op_strcmp(void)
{
    Data *args;
    int l1, l2, l, val;

    /* Accept two strings to compare. */
    if (!func_init_2(&args, STRING, STRING))
	return;

    /* Compare the strings case-sensitively. */
    l1 = args[0].u.substr.span;
    l2 = args[1].u.substr.span;
    l = (l1 < l2) ? l1 : l2;
    val = strncmp(data_sptr(&args[0]), data_sptr(&args[1]), l);
    if (!val && l1 > l2)
	val = data_sptr(&args[0])[l2];
    else if (!val && l1 < l2)
	val = data_sptr(&args[1])[l1];
    pop(2);
    push_int(val);
}

