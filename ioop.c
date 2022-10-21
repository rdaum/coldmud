/* ioop.c: Function operators for input and output. */

#define _POSIX_SOURCE

#include <stdio.h>
#include "x.tab.h"
#include "operator.h"
#include "execute.h"
#include "data.h"
#include "memory.h"
#include "io.h"
#include "cmstring.h"
#include "config.h"
#include "ident.h"
#include "util.h"

#define FILE_BUF_SIZE 100

void op_echo(void)
{
    Data *args;

    /* Accept a string to echo. */
    if (!func_init_1(&args, STRING))
	return;

    /* Write the string to any connection associated with this object. */
    tell(cur_frame->object->dbref, data_sptr(&args[0]), args[0].u.substr.span);

    pop(1);
    push_int(0);
}

void op_echo_file(void)
{
    int len;
    Data *args;
    FILE *fp;
    char *fname, line[FILE_BUF_SIZE];
    String *buf;

    /* Accept the name of a file to echo. */
    if (!func_init_1(&args, STRING))
	return;

    fname = TMALLOC(char, args[0].u.substr.span + 6);
    memcpy(fname, "text/", 5);
    memcpy(fname + 5, data_sptr(&args[0]), args[0].u.substr.span);
    fname[args[0].u.substr.span + 5] = 0;

    /* Don't allow walking back up the directory tree. */
    if (strstr(fname, "../")) {
	tfree_chars(fname);
	throw(perm_id, "Filename %D is not legal.", &args[0]);
	return;
    }

    fp = open_scratch_file(fname, "r");
    tfree_chars(fname);
    pop(1);
    if (!fp) {
	throw(file_id, "Cannot find file %D.", &args[0]);
	return;
    }

    /* Read in the file and spew it to the socket, line by line. */
    buf = string_empty(FILE_BUF_SIZE);
    while (fgets(line, FILE_BUF_SIZE, fp)) {
	len = strlen(line);
	if (line[len - 1] == '\n') {
	    buf = string_add(buf, line, len - 1);
	    tell(cur_frame->object->dbref, buf->s, buf->len);
	    *buf->s = 0;
	    buf->len = 0;
	} else {
	    buf = string_add(buf, line, len);
	}
    }
    if (buf->len)
	tell(cur_frame->object->dbref, buf->s, buf->len);
    string_discard(buf);
    close_scratch_file(fp);

    push_int(1);
}

void op_disconnect(void)
{
    /* Accept no arguments. */
    if (!func_init_0())
	return;

    /* Kick off anyone assigned to the current object. */
    push_int(boot(cur_frame->object->dbref));
}

