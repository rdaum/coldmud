/* buffer.c: Routines for C-- buffer manipulation. */

#define _POSIX_SOURCE

#include <ctype.h>
#include "x.tab.h"
#include "data.h"
#include "memory.h"

#define BUFALLOC(len)		emalloc(sizeof(Buffer) + (len) - 1)
#define BUFREALLOC(buf, len)	erealloc(buf, sizeof(Buffer) + (len) - 1)

static Buffer *prepare_to_modify(Buffer *buf);

Buffer *buffer_new(int len)
{
    Buffer *buf;

    buf = BUFALLOC(len);
    buf->len = len;
    buf->refs = 1;
    return buf;
}

Buffer *buffer_dup(Buffer *buf)
{
    buf->refs++;
    return buf;
}

void buffer_discard(Buffer *buf)
{
    buf->refs--;
    if (!buf->refs)
	free(buf);
}

Buffer *buffer_append(Buffer *buf1, Buffer *buf2)
{
    if (!buf2->len)
	return buf1;
    buf1 = prepare_to_modify(buf1);
    buf1 = BUFREALLOC(buf1, buf1->len + buf2->len);
    MEMCPY(buf1->s + buf1->len, buf2->s, buf2->len);
    buf1->len += buf2->len;
    return buf1;
}

int buffer_retrieve(Buffer *buf, int pos)
{
    return buf->s[pos];
}

Buffer *buffer_replace(Buffer *buf, int pos, unsigned int c)
{
    if (buf->s[pos] == c)
	return buf;
    buf = prepare_to_modify(buf);
    buf->s[pos] = OCTET_VALUE(c);
    return buf;
}

Buffer *buffer_add(Buffer *buf, unsigned int c)
{
    buf = prepare_to_modify(buf);
    buf = BUFREALLOC(buf, buf->len + 1);
    buf->s[buf->len] = OCTET_VALUE(c);
    buf->len++;
    return buf;
}

int buffer_len(Buffer *buf)
{
    return buf->len;
}

Buffer *buffer_truncate(Buffer *buf, int len)
{
    if (len == buf->len)
	return buf;
    buf = prepare_to_modify(buf);
    buf = BUFREALLOC(buf, len);
    buf->len = len;
    return buf;
}

/* If sep (separator buffer) is NULL, separate by newlines. */
List *buffer_to_strings(Buffer *buf, Buffer *sep)
{
    Data d;
    String *str;
    List *result;
    unsigned char sepchar, *string_start, *p, *q;
    char *s;
    int seplen;
    Buffer *end;

    sepchar = (sep) ? *sep->s : '\n';
    seplen = (sep) ? sep->len : 1;
    result = list_new(0);
    string_start = p = buf->s;
    while (p + seplen <= buf->s + buf->len) {
	/* Look for sepchar staring from p. */
	p = memchr(p, sepchar, (buf->s + buf->len) - (p + seplen - 1));
	if (!p)
	    break;

	/* Keep going if we don't match all of the separator. */
	if (sep && MEMCMP(p + 1, sep->s + 1, seplen - 1) != 0) {
	    p++;
	    continue;
	}

	/* We found a separator.  Copy the printable characters in the
	 * intervening text into a string. */
	str = string_new(p - string_start);
	s = str->s;
	for (q = string_start; q < p; q++) {
	    if (isprint(*q))
		*s++ = *q;
	}
	*s = 0;
	str->len = s - str->s;

	d.type = STRING;
	substr_set_to_full_string(&d.u.substr, str);
	result = list_add(result, &d);
	string_discard(str);

	string_start = p = p + seplen;
    }

    /* Add the remainder characters to the list as a buffer. */
    end = buffer_new(buf->s + buf->len - string_start);
    MEMCPY(end->s, string_start, buf->s + buf->len - string_start);
    d.type = BUFFER;
    d.u.buffer = end;
    result = list_add(result, &d);
    buffer_discard(end);

    return result;
}

Buffer *buffer_from_strings(Data *d, int n, Buffer *sep)
{
    Buffer *buf;
    int i, len, pos;
    unsigned char *s;

    /* Find length of finished buffer. */
    len = 0;
    for (i = 0; i < n; i++)
	len += d[i].u.substr.span + ((sep) ? sep->len : 2);

    /* Make a buffer and copy the strings into it. */
    buf = buffer_new(len);
    pos = 0;
    for (i = 0; i < n; i++) {
	s = (unsigned char *) data_sptr(&d[i]);
	MEMCPY(buf->s + pos, s, d->u.substr.span);
	pos += d->u.substr.span;
	if (sep) {
	    MEMCPY(buf->s + pos, sep->s, sep->len);
	    pos += sep->len;
	} else {
	    buf->s[pos++] = '\r';
	    buf->s[pos++] = '\n';
	}
    }

    return buf;
}

static Buffer *prepare_to_modify(Buffer *buf)
{
    Buffer *new;

    if (buf->refs == 1)
	return buf;

    /* Make a new buffer with the same contents as the old one. */
    buf->refs--;
    new = buffer_new(buf->len);
    MEMCPY(new->s, buf->s, buf->len);
    return new;
}

