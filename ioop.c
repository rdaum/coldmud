/* ioop.c: Function operators for input and output. */

#define _POSIX_SOURCE

#include <sys/types.h>
#include <sys/stat.h>
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
    if (!func_init_1(&args, BUFFER))
	return;

    /* Write the string to any connection associated with this object. */
    tell(cur_frame->object->dbref, args[0].u.buffer);

    pop(1);
    push_int(1);
}

void op_echo_file(void)
{
    size_t size, i, r;
    Data *args;
    FILE *fp;
    char *fname;
    Buffer *buf;
    struct stat statbuf;

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

    /* Stat the file to get its size. */
    if (stat(fname, &statbuf) < 0) {
	tfree_chars(fname);
	throw(perm_id, "Cannot find file %D.", &args[0]);
	return;
    }
    size = statbuf.st_size;

    /* Open the file for reading. */
    fp = open_scratch_file(fname, "r");
    tfree_chars(fname);
    pop(1);
    if (!fp) {
	throw(file_id, "Cannot open file %D for reading.", &args[0]);
	return;
    }

    /* Allocate a buffer to hold the file contents. */
    buf = buffer_new(size);

    /* Read in the file. */
    i = 0;
    while (i < size) {
	r = fread(buf->s + i, sizeof(unsigned char), size, fp);
	if (r <= 0) {
	    buffer_discard(buf);
	    close_scratch_file(fp);
	    throw(file_id, "Trouble reading file %D.", &args[0]);
	    return;
	}
	i += r;
    }

    /* Write the file. */
    tell(cur_frame->object->dbref, buf);

    /* Discard the buffer and close the file. */
    buffer_discard(buf);
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

