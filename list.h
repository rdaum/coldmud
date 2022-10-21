/* list.h: Declarations for C-- lists. */

/* The header file ordering breaks down here; we need to run this file
 * after data.h has completed, not just after the typedefs are done.
 * Thus the ugly conditionals. */

#ifndef DID_LIST_TYPEDEF
typedef struct list List;
#define DID_LIST_TYPEDEF
#endif

#include "data.h"

#ifdef DATA_H_DONE

#ifndef LIST_H
#define LIST_H

struct list {
    int start;
    int len;
    int size;
    int refs;
    Data el[1];
};

List *list_new(int len);
List *list_dup(List *list);
int list_length(List *list);
Data *list_first(List *list);
Data *list_next(List *list, Data *d);
Data *list_last(List *list);
Data *list_prev(List *list, Data *d);
Data *list_elem(List *list, int i);
Data *list_empty_spaces(List *list, int spaces);
int list_search(List *list, Data *data);
int list_cmp(List *l1, List *l2);
List *list_insert(List *list, int pos, Data *elem);
List *list_add(List *list, Data *elem);
List *list_replace(List *list, int pos, Data *elem);
List *list_delete(List *list, int pos);
List *list_delete_element(List *list, Data *elem);
List *list_append(List *list1, List *list2);
List *list_reverse(List *list);
List *list_setadd(List *list, Data *elem);
List *list_setremove(List *list, Data *elem);
List *list_union(List *list1, List *list2);
List *list_sublist(List *list, int start, int len);
void list_discard(List *list);

#endif

#endif

