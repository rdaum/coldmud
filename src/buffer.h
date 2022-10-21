/* buffer.h: Declarations for C-- buffers. */

#ifndef BUFFER_H
#define BUFFER_H

typedef struct buffer Buffer;

#include "list.h"

struct buffer {
    int len;
    int refs;
    unsigned char s[1];
};

Buffer *buffer_new(int len);
Buffer *buffer_dup(Buffer *buf);
void buffer_discard(Buffer *buf);
Buffer *buffer_append(Buffer *buf1, Buffer *buf2);
int buffer_retrieve(Buffer *buf, int pos);
Buffer *buffer_replace(Buffer *buf, int pos, unsigned int c);
Buffer *buffer_add(Buffer *buf, unsigned int c);
int buffer_len(Buffer *buf);
Buffer *buffer_truncate(Buffer *buf, int len);
List *buffer_to_strings(Buffer *buf, Buffer *sep);
Buffer *buffer_from_strings(List *string_list, Buffer *sep);

#endif

