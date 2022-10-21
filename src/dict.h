/* dict.h: Declarations for C-- dictionaries. */

#ifndef DICT_H
#define DICT_H

typedef struct dict Dict;

#include "data.h"

struct dict {
    List *keys;
    List *values;
    int *links;
    int *hashtab;
    int hashtab_size;
    int refs;
};

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
int dict_size(Dict *dict);
String *dict_add_literal_to_str(String *str, Dict *dict);

#endif

