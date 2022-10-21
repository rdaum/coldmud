/* dbpack.h: Declarations for packing objects in the database. */

#ifndef DBPACK_H
#define DBPACK_H
#include <stdio.h>
#include "object.h"

void pack_object(Object *obj, FILE *fp);
void unpack_object(Object *obj, FILE *fp);
int size_object(Object *obj);

#endif

