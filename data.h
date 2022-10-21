/* data.h: Declarations for C-- data. */

#ifndef DATA_H
#define DATA_H
#include "cmstring.h"

/* Buffer contents must be between 0 and 255 inclusive, even if an unsigned
 * char can hold other values. */
#define OCTET_VALUE(n) (((unsigned long) (n)) & ((1 << 8) - 1))

typedef struct substring	Substring;
typedef struct sublist		Sublist;
typedef struct frob		Frob;
typedef struct data		Data;
typedef struct list		List;
typedef struct dict		Dict;
typedef struct buffer 		Buffer;

struct substring {
    String *str;
    int start;
    int span;
};

struct sublist {
    List *list;
    int start;
    int span;
};

struct frob {
    long class;
    int rep_type;
    union {
	Dict *dict;
	List *list;
    } rep;
};

struct data {
    int type;
    union {
	long val, dbref, symbol, error;
	Substring substr;
	Sublist sublist;
	Frob frob;
	Dict *dict;
	Buffer *buffer;
    } u;
};

struct list {
    int len;
    int size;
    int refs;
    Data el[1];
};

struct dict {
    List *keys;
    List *values;
    int *links;
    int *hashtab;
    int hashtab_size;
    int refs;
};

struct buffer {
    int len;
    int refs;
    unsigned char s[1];
};

/* data.c */
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
int sublist_search(Sublist *sublist, Data *data);
void sublist_truncate(Sublist *sublist);
void substring_truncate(Substring *substr);
char *data_sptr(Data *data);
Data *data_dptr(Data *data);
void substr_set_to_full_string(Substring *target, String *str);
void sublist_set_to_full_list(Sublist *target, List *list);

/* list.h */
List *list_new(int len);
List *list_dup(List *list);
List *list_from_data(Data *data, int len);
List *list_from_sublist(Sublist *sublist);
int list_search(List *list, Data *data);
int list_cmp(List *l1, List *l2);
List *list_insert(List *list, int pos, Data *elem);
List *list_add(List *list, Data *elem);
List *list_replace(List *list, int pos, Data *elem);
List *list_delete(List *list, int pos);
List *list_delete_element(List *list, Data *elem);
List *list_append(List *list, Data *d, int len);
List *list_setadd(List *list, Data *elem);
List *list_setremove(List *list, Data *elem);
void list_discard(List *list);

/* dict.h */
Dict *dict_new(List *keys, List *values);
Dict *dict_new_empty(void);
Dict *dict_from_slices(List *slices);
Dict *dict_dup(Dict *dict);
void dict_discard(Dict *dict);
int dict_cmp(Dict *dict1, Dict *dict2);
Dict *dict_add(Dict *dict, Data *key, Data *value);
Dict *dict_del(Dict *dict, Data *key);
long dict_find(Dict *dict, Data *key, Data *ret);
int dict_contains(Dict *dict, Data *key);
List *dict_keys(Dict *dict);
List *dict_key_value_pair(Dict *mapping, int i);
String *dict_add_literal_to_str(String *str, Dict *dict);

/* buffer.h */
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
Buffer *buffer_from_strings(Data *d, int n, Buffer *sep);

#endif

