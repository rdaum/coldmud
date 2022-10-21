/* loc.c: Interface to dbm index of object locations. */

#include <stdio.h>
#include <sys/types.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <ndbm.h>
#include <fcntl.h>
#include "log.h"

#ifdef S_IRUSR
#define READ_WRITE		(S_IRUSR | S_IWUSR)
#define READ_WRITE_EXECUTE	(S_IRUSR | S_IWUSR | S_IXUSR)
#else
#define READ_WRITE 0600
#define READ_WRITE_EXECUTE 0700
#endif

static DBM *dbp;

struct hbuf {
    off_t offset;
    int size;
};

void loc_open(char *name, int new)
{
    if (new)
	dbp = dbm_open(name, O_TRUNC | O_RDWR | O_CREAT, READ_WRITE);
    else
	dbp = dbm_open(name, O_RDWR, READ_WRITE);
    if (!dbp)
	fail_to_start("Cannot open dbm database file.");
}

void loc_close(void)
{
    dbm_close(dbp);
}

void loc_sync(void)
{
    /* Only way to do this with ndbm is close and re-open. */
    dbm_close(dbp);
    dbp = dbm_open("binary/index", O_RDWR | O_CREAT, READ_WRITE);
    if (!dbp)
	panic("Cannot reopen dbm database file.");
}

int loc_retrieve(char *name, off_t *offset, int *size)
{
    datum key, dat;
    struct hbuf hbuf;

    /* Get the key from the database. */
    key.dptr = name;
    key.dsize = strlen(name) + 1;
    dat = dbm_fetch(dbp, key);
    if (!dat.dptr)
	return 0;

    /* Translate the result into offset and size. */
    memcpy(&hbuf, dat.dptr, sizeof(hbuf));
    *offset = hbuf.offset;
    *size = hbuf.size;
    return 1;
}

int loc_store(char *name, off_t offset, int size)
{
    datum key, dat;
    struct hbuf hbuf;

    /* Set up key and data structures. */
    key.dptr = name;
    key.dsize = strlen(name) + 1;
    hbuf.offset = offset;
    hbuf.size = size;
    dat.dptr = (char *) &hbuf;
    dat.dsize = sizeof(hbuf);

    if (dbm_store(dbp, key, dat, DBM_REPLACE)) {
	write_log("ERROR: Failed to store key %s.", name);
	return 0;
    }

    return 1;
}

int loc_remove(char *name)
{
    datum key;

    /* Remove the key from the database. */
    key.dptr = name;
    key.dsize = strlen(name) + 1;
    if (dbm_delete(dbp, key)) {
	write_log("ERROR: Failed to delete key %s.", name);
	return 0;
    }
    return 1;
}

char *loc_first(void)
{
    datum key;

    key = dbm_firstkey(dbp);
    return key.dptr;
}

char *loc_next(void)
{
    datum key;

    key = dbm_nextkey(dbp);
    return key.dptr;
}


