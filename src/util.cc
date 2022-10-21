/* util.c: General utilities. */

#define _POSIX_SOURCE

#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdarg.h>
#include <fcntl.h>
#include "x.tab.h"
#include "util.h"
#include "cmstring.h"
#include "data.h"
#include "config.h"
#include "ident.h"
#include "token.h"
#include "log.h"

#define FORMAT_BUF_INITIAL_LENGTH 48
#define MAX_SCRATCH 2

/* crypt() is not POSIX. */
extern char *crypt();

char lowercase[128];
char uppercase[128];
static int reserve_fds[MAX_SCRATCH];
static int fds_used;

static void claim_fd(int i);

void init_util(void)
{
    int i;

    for (i = 0; i < 128; i++) {
	lowercase[i] = (isupper(i) ? tolower(i) : i);
	uppercase[i] = (islower(i) ? toupper(i) : i);
    }
    srand(time(NULL) + getpid());
}

unsigned long hash(char *s)
{
    unsigned long hashval = 0, g;

    /* Algorithm by Peter J. Weinberger. */
    for (; *s; s++) {
	hashval = (hashval << 4) + *s;
	g = hashval & 0xf0000000;
	if (g) {
	    hashval ^= g >> 24;
	    hashval ^= g;
	}
    }
    return hashval;
}

unsigned long hash_case(char *s, int n)
{
    unsigned long hashval = 0, g;
    int i;

    /* Algorithm by Peter J. Weinberger. */
    for (i = 0; i < n; i++) {
	hashval = (hashval << 4) + (s[i] & 0x5f);
	g = hashval & 0xf0000000;
	if (g) {
	    hashval ^= g >> 24;
	    hashval ^= g;
	}
    }
    return hashval;
}

long atoln(char *s, int n)
{
    long val = 0;

    while (n-- && isdigit(*s))
	val = val * 10 + *s++ - '0';
    return val;
}

char *long_to_ascii(long num, Number_buf nbuf)
{
    char *p = &nbuf[NUMBER_BUF_SIZE - 1];
    int sign = 0;

    *p-- = 0;
    if (num < 0) {
	sign = 1;
	num = -num;
    } else if (!num) {
	*p-- = '0';
    }
    while (num) {
	*p-- = num % 10 + '0';
	num /= 10;
    }
    if (sign)
	*p-- = '-';
    return p + 1;
}

/* Compare two strings, ignoring case. */
int strccmp(char *s1, char *s2)
{
    while (*s1 && LCASE(*s1) == LCASE(*s2))
	s1++, s2++;
    return LCASE(*s1) - LCASE(*s2);
}

/* Compare two strings up to n characters, ignoring case. */
int strnccmp(char *s1, char *s2, int n)
{
    while (n-- && *s1 && LCASE(*s1) == LCASE(*s2))
	s1++, s2++;
    return (n >= 0) ? LCASE(*s1) - LCASE(*s2) : 0;
}

/* Look for c in s, ignoring case. */
char *strcchr(char *s, int c)
{
    for (; *s; s++) {
	if (LCASE(*s) == c)
	    return s;
    }
    return (c) ? NULL : s;
}

char *strcstr(char *s, char *search)
{
    char *p;
    int search_len = strlen(search);

    for (p = strcchr(s, *search); p; p = strcchr(p + 1, *search)) {
	if (strnccmp(p, search, search_len) == 0)
	    return p;
    }

    return NULL;
}

/* A random number generator.  A lot of Unix rand() implementations don't
 * produce very random low bits, so we shift by eight bits if we can do that
 * without truncating the range. */
long random_number(long n)
{
    long num = rand();

    if (RAND_MAX >> 8 >= n)
	num >>= 8;
    return num % n;
}

/* Encrypt a string.  The salt can be NULL. */
char *crypt_string(char *key, char *salt)
{
    char rsalt[2];

    if (!salt) {
	rsalt[0] = random_number(95) + 32;
	rsalt[1] = random_number(95) + 32;
	salt = rsalt;
    }

    return crypt(key, salt);
}

/* Result must be copied before it can be re-used.  Non-reentrant. */
String *vformat(char *fmt, va_list arg)
{
    String *buf, *str;
    char *p, *s;
    Number_buf nbuf;

    buf = string_new(0);

    while (1) {

	/* Find % or end of string. */
	p = strchr(fmt, '%');
	if (!p || !p[1]) {
	    /* No more percents; copy rest and stop. */
	    buf = string_add_chars(buf, fmt, strlen(fmt));
	    break;
	}
	buf = string_add_chars(buf, fmt, p - fmt);

	switch (p[1]) {

	  case '%':
	    buf = string_addc(buf, '%');
	    break;

	  case 's':
	    s = va_arg(arg, char *);
	    buf = string_add_chars(buf, s, strlen(s));
	    break;

	  case 'S':
	    str = va_arg(arg, String *);
	    buf = string_add(buf, str);
	    break;

	  case 'd':
	    s = long_to_ascii(va_arg(arg, int), nbuf);
	    buf = string_add_chars(buf, s, strlen(s));
	    break;

	  case 'l':
	    s = long_to_ascii(va_arg(arg, long), nbuf);
	    buf = string_add_chars(buf, s, strlen(s));
	    break;

	  case 'D':
	    str = data_to_literal(va_arg(arg, Data *));
	    if (string_length(str) > MAX_DATA_DISPLAY) {
		str = string_truncate(str, MAX_DATA_DISPLAY - 3);
		str = string_add_chars(str, "...", 3);
	    }
	    buf = string_add_chars(buf, string_chars(str), string_length(str));
	    string_discard(str);
	    break;

	  case 'I':
	    s = ident_name(va_arg(arg, long));
	    if (is_valid_ident(s))
		buf = string_add_chars(buf, s, strlen(s));
	    else
		buf = string_add_unparsed(buf, s, strlen(s));
	    break;
	}

	fmt = p + 2;
    }

    return buf;
}

String *format(char *fmt, ...)
{
    va_list arg;
    String *str;

    va_start(arg, fmt);
    str = vformat(fmt, arg);
    va_end(arg);
    return str;
}

void fformat(FILE *fp, char *fmt, ...)
{
    va_list arg;
    String *str;

    va_start(arg, fmt);

    str = vformat(fmt, arg);
    fputs(string_chars(str), fp);
    string_discard(str);

    va_end(arg);
}

String *fgetstring(FILE *fp)
{
    String *line;
    char buf[1000];
    int len;

    line = string_new(0);
    while (fgets(buf, 1000, fp)) {
	len = strlen(buf);
	if (buf[len - 1] == '\n')
	    return string_add_chars(line, buf, len - 1);
	else
	    line = string_add_chars(line, buf, len);
    }
    if (line->len) {
	return line;
    } else {
	string_discard(line);
	return NULL;
    }
}

char *english_type(int type)
{
    switch (type) {
      case INTEGER:	return "an integer";
      case STRING:	return "a string";
      case DBREF:	return "a dbref";
      case LIST:	return "a list";
      case SYMBOL:	return "a symbol";
      case ERROR:	return "an error";
      case FROB:	return "a frob";
      case DICT:	return "a dictionary";
      case BUFFER:	return "a buffer";
      default:		return "a mistake";
    }
}

char *english_integer(int n, Number_buf nbuf)
{
    static char *first_eleven[] = {
	"no", "one", "two", "three", "four", "five", "six", "seven",
	"eight", "nine", "ten" };

    if (n <= 10)
	return first_eleven[n];
    else
	return long_to_ascii(n, nbuf);
}

long parse_ident(char **sptr)
{
    String *str;
    char *s = *sptr;
    long id;

    if (*s == '"') {
	str = string_parse(&s);
    } else {
	while (isalnum(*s) || *s == '_')
	    s++;
	str = string_from_chars(*sptr, s - *sptr);
    }

    id = ident_get(string_chars(str));
    string_discard(str);
    *sptr = s;
    return id;
}

FILE *open_scratch_file(char *name, char *type)
{
    FILE *fp;

    if (fds_used == MAX_SCRATCH)
	return NULL;

    close(reserve_fds[fds_used++]);
    fp = fopen(name, type);
    if (!fp) {
	claim_fd(--fds_used);
	return NULL;
    }
    return fp;
}

void close_scratch_file(FILE *fp)
{
    fclose(fp);
    claim_fd(--fds_used);
}

void init_scratch_file(void)
{
    int i;

    for (i = 0; i < MAX_SCRATCH; i++)
	claim_fd(i);
}

static void claim_fd(int i)
{
    reserve_fds[i] = open("/dev/null", O_WRONLY);
    if (reserve_fds[i] == -1)
	panic("Couldn't reset reserved fd.");
}

/* Return the smallest number N such that N >= size and N = 2^B - delta for
 * some B >= min_bits. */
int adjust_size(int size, int min_power, int delta)
{
    int b;

    size += delta - 1;
    b = min_power;
    while ((size >> b) != 0)
	b++;
    return (1 << b) - delta;
}

