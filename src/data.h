/* data.h: Declarations for C-- data. */

#ifndef DATA_H
#define DATA_H

typedef struct frob Frob;
typedef struct data Data;

#include "cmstring.h"
#include "list.h"
#include "dict.h"
#include "buffer.h"
#include "ident.h"
#include "object.h"

/* Buffer contents must be between 0 and 255 inclusive, even if an unsigned
 * char can hold other values. */
#define OCTET_VALUE(n) (((unsigned long) (n)) & ((1 << 8) - 1))

struct data {
    int type;
    union {
	long val;
	Dbref dbref;
	Ident symbol;
        Ident error;
	String *str;
	List *list;
	Frob *frob;
	Dict *dict;
	Buffer *buffer;
    } u;
};

struct frob {
    long class;
    Data rep;
};

int data_cmp(Data *d1, Data *d2);
int data_true(Data *data);
unsigned long data_hash(Data *d);
void data_dup(Data *dest, Data *src);
void data_discard(Data *data);
String *data_tostr(Data *data);
String *data_to_literal(Data *data);
String *data_add_literal_to_str(String *str, Data *data);
char *data_from_literal(Data *d, char *s);
long data_type_id(int type);

#define DATA_H_DONE
#include "list.h"
#include "object.h"

#endif

