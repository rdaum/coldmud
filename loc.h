/* loc.h: Location database routines. */

#ifndef LOC_H
#define LOC_H

void loc_open(char *name, int new);
void loc_close(void);
void loc_sync(void);
int loc_retrieve(char *name, off_t *offset, int *size);
int loc_store(char *name, off_t offset, int size);
int loc_remove(char *name);
char *loc_first(void);
char *loc_next(void);

#endif

