/* dbmchunk.h: Declarations for dbm chunking routines. */

#ifndef DBMCHUNK_H
#define DBMCHUNK_H
#include "object.h"

int init_db(void);
int db_get(Object *object, char *name);
int db_put(Object *object, char *name);
int db_check(char *name);
int db_del(char *name);
char *db_traverse_first(void);
char *db_traverse_next(void);
int db_backup(char *out);
void db_close(void);
void db_flush(void);

#endif

