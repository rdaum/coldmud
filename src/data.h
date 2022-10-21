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
  private:
    int type_tag;
    union {
	long integer;
	Dbref dbref;
	Ident symbol;
        Ident error;
	String str;
	List list;
	Frob frob;
	Dict dict;
	Buffer buffer;
    } u;

  public:

    /* Constructors and destructor. */
    Data() { type_tag = 0; }
    Data(long integer) { type_tag = INTEGER; u.integer = integer; }
    Data(Dbref dbref) { type_tag = DBREF; u.dbref = dbref; }
    Data(Ident symbol) { type_tag = SYMBOL; u.symbol = symbol; }
    Data(Ident error) { type_tag = ERROR; u.error = error; }
    Data(String string) { type_tag = STRING; u.string = string; }
    Data(List list) { type_tag = LIST; u.list = list; }
    Data(Frob frob) { type_tag = FROB; u.frob = frob; }
    Data(Dict dict) { type_tag = DICT; u.dict = dict; }
    Data(Buffer buffer) { type_tag = BUFFER; u.buffer = buffer; }
    ~Data();

    /* Observers. */
    int type() { return type_tag; }
    int is(int type) { return (type_tag == type); }
    long& integer() { return u.integer; }
    Dbref& dbref() { return u.dbref; }
    Ident& symbol() { return u.symbol; }
    Ident& error() { return u.error; }
    String& string() { return u.str; }
    List& list() { return u.list; }
    Frob& frob() { return u.frob; }
    Dict& dict() { return u.dict; }
    Buffer& buffer() { return u.buffer; }
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

