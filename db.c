/* db.c: Object storage routines.
 * The block allocation algorithm in this code is due to Marcus J. Ranum. */

#define _POSIX_SOURCE

#include <stdio.h>
#include <sys/param.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "db.h"
#include "lookup.h"
#include "object.h"
#include "cache.h"
#include "log.h"
#include "util.h"
#include "dbpack.h"
#include "memory.h"
#include "config.h"
#include "ident.h"

#define NEEDED(n, b)		(((n) % (b)) ? (n) / (b) + 1 : (n) / (b))
#define ROUND_UP(a, m)		(((a) - 1) + (m) - (((a) - 1) % (m)))

#define	BLOCK_SIZE		256		/* Default block size */
#define	DB_BITBLOCK		512		/* Bitmap growth in blocks */
#define	LOGICAL_BLOCK(off)	((off) / BLOCK_SIZE)
#define	BLOCK_OFFSET(block)	((block) * BLOCK_SIZE)

#ifdef S_IRUSR
#define READ_WRITE		(S_IRUSR | S_IWUSR)
#define READ_WRITE_EXECUTE	(S_IRUSR | S_IWUSR | S_IXUSR)
#else
#define READ_WRITE 0600
#define READ_WRITE_EXECUTE 0700
#endif

static void db_mark(off_t start, int size);
static void db_unmark(off_t start, int size);
static void grow_bitmap(int new_blocks);
static int db_alloc(int size);
static void db_is_clean(void);
static void db_is_dirty(void);

static int last_free = 0;	/* Last known or suspected free block */

static FILE *database_file = NULL;

static char *bitmap = NULL;
static int bitmap_blocks = 0;

static int db_clean;

extern long cur_search, db_top;

int init_db(void)
{
    struct stat statbuf;
    FILE *fp;
    char buf[80];
    off_t offset;
    int new = 1, size;
    long dbref;

    /* Make sure "binary" exists and is a directory. */
    if (stat("binary", &statbuf) == -1) {
	/* Directory binary does not exist; create it. */
	if (mkdir("binary", READ_WRITE_EXECUTE) == -1)
	    fail_to_start("Cannot create binary directory.");
    } else if (!S_ISDIR(statbuf.st_mode)) {
	/* 'binary' is a file; nuke it and create directory. */
	if (unlink("binary") == -1)
	    fail_to_start("Cannot delete file 'binary'.");
	if (mkdir("binary", READ_WRITE_EXECUTE) == -1)
	    fail_to_start("Cannot create binary directory.");
    }

    /* Check if binary/clean exists and contains the right version number. */
    fp = fopen("binary/clean", "r");
    if (fp) {
	if (fgets(buf, 80, fp) && atoi(buf) == VERSION_MAJOR) {
	    if (fgets(buf, 80, fp) && atoi(buf) == VERSION_MINOR) {
		if (fgets(buf, 80, fp) && atoi(buf) == VERSION_BUGFIX) {
		    new = 0;
		    fgets(buf, 80, fp);
		    cur_search = atoi(buf);
		}
	    }
	}
	fclose(fp);
    }

    database_file = fopen("binary/objects", (new) ? "w+" : "r+");
    if (!database_file)
	fail_to_start("Cannot open object database file.");

    /* Open hash table. */
    lookup_open("binary/index", new);

    /* Determine size of chunk file for allocation bitmap. */
    if (stat("binary/objects", &statbuf) < 0)
	fail_to_start("Cannot stat database file.");

    /* Allocate bitmap. */
    bitmap_blocks = ROUND_UP(LOGICAL_BLOCK(statbuf.st_size) + DB_BITBLOCK, 8);
    bitmap = EMALLOC(char, bitmap_blocks / 8);
    memset(bitmap, 0, bitmap_blocks / 8);

    dbref = lookup_first_dbref();
    while (dbref != NOT_AN_IDENT) {
	if (!lookup_retrieve_dbref(dbref, &offset, &size))
	    fail_to_start("Database index is inconsistent.");

	if (dbref >= db_top)
	    db_top = dbref + 1;

	/* Mark blocks as busy in the bitmap. */
	db_mark(LOGICAL_BLOCK(offset), size);

	dbref = lookup_next_dbref();
    }

    /* If database is new, mark it as clean; otherwise, it was clean
     * already. */
    if (new)
	db_is_clean();
    else
	db_clean = 1;

    return new;
}

/* Grow the bitmap to given size. */
static void grow_bitmap(int new_blocks)
{
    new_blocks = ROUND_UP(new_blocks, 8);
    bitmap = EREALLOC(bitmap, char, new_blocks / 8);
    memset(&bitmap[bitmap_blocks / 8], 0,
	   (new_blocks / 8) - (bitmap_blocks / 8));
    bitmap_blocks = new_blocks;
}

static void db_mark(off_t start, int size)
{
    int i, blocks;

    blocks = NEEDED(size, BLOCK_SIZE);

    while (start + blocks > bitmap_blocks)
	grow_bitmap(bitmap_blocks + DB_BITBLOCK);

    for (i = start; i < start + blocks; i++)
	bitmap[i >> 3] |= (1 << (i & 7));
}

static void db_unmark(off_t start, int size)
{
    int i, blocks;

    blocks = NEEDED(size, BLOCK_SIZE);

    /* Remember a free block was here. */
    last_free = start;

    for (i = start; i < start + blocks; i++) 
	bitmap[i >> 3] &= ~(1 << (i & 7));
}

static int db_alloc(int size)
{
    int blocks_needed, b, count, starting_block, over_the_top;

    b = last_free;
    blocks_needed = NEEDED(size, BLOCK_SIZE);
    over_the_top = 0;

    while (1) {
	if (b >= bitmap_blocks) {
	    /* Only wrap around once. */
	    if (!over_the_top) {
		b = 0;
		over_the_top = 1;
	    } else {
		grow_bitmap(b + DB_BITBLOCK);
	    }
	}

	starting_block = b;

	for (count = 0; count < blocks_needed; count++) {
	    if (bitmap[b >> 3] & (1 << (b & 7)))
		break;
	    b++;
	    if (b >= bitmap_blocks)
		grow_bitmap(b + DB_BITBLOCK);
	}

	if (count == blocks_needed) {
	    /* Mark these blocks taken and return the starting block. */
	    for (b = starting_block; b < starting_block + count; b++)
		bitmap[b >> 3] |= (1 << (b & 7));
	    last_free = b;
	    return starting_block;
	}

	b++;
    }
}

int db_get(Object *object, long dbref)
{
    off_t offset;
    int size;

    /* Get the object location for the dbref. */
    if (!lookup_retrieve_dbref(dbref, &offset, &size))
	return 0;

    /* seek to location */
    if (fseek(database_file, offset, SEEK_SET))
	return 0;

    unpack_object(object, database_file);
    return 1;
}

int db_put(Object *obj, long dbref)
{
    off_t old_offset, new_offset;
    int old_size, new_size = size_object(obj);

    db_is_dirty();

    if (lookup_retrieve_dbref(dbref, &old_offset, &old_size)) {
	if (NEEDED(new_size, BLOCK_SIZE) > NEEDED(old_size, BLOCK_SIZE)) {
	    db_unmark(LOGICAL_BLOCK(old_offset), old_size);
	    new_offset = BLOCK_OFFSET(db_alloc(new_size));
	} else {
	    new_offset = old_offset;
	}
    } else {
	new_offset = BLOCK_OFFSET(db_alloc(new_size));
    }

    if (!lookup_store_dbref(dbref, new_offset, new_size))
	return 0;

    if (fseek(database_file, new_offset, SEEK_SET)) {
	write_log("ERROR: Seek failed for %l.", dbref);
	return 0;
    }

    pack_object(obj, database_file);
    fflush(database_file);

    return 1;
}

int db_check(long dbref)
{
    off_t offset;
    int size;

    return lookup_retrieve_dbref(dbref, &offset, &size);
}

int db_del(long dbref)
{
    off_t offset;
    int size;

    /* Get offset and size of key. */
    if (!lookup_retrieve_dbref(dbref, &offset, &size))
	return 0;

    /* Remove key from location db. */
    if (!lookup_remove_dbref(dbref))
	return 0;

    db_is_dirty();

    /* Mark free space in bitmap */
    db_unmark(LOGICAL_BLOCK(offset), size);

    /* Mark object dead in file */
    if (fseek(database_file, offset, SEEK_SET)) {
	write_log("ERROR: Failed to seek to object %l.", dbref);
	return 0;
    }

    fputs("delobj", database_file);
    fflush(database_file);

    return 1;
}

void db_close(void)
{
    lookup_close();
    fclose(database_file);
    free(bitmap);
    db_is_clean();
}

void db_flush(void)
{
    lookup_sync();
    db_is_clean();
}

static void db_is_clean(void)
{
    FILE *fp;

    if (db_clean)
	return;

    /* Create 'clean' file. */
    fp = open_scratch_file("binary/clean", "w");
    if (!fp)
	panic("Cannot create file 'clean'.");

    fformat(fp, "%d\n%d\n%d\n", VERSION_MAJOR, VERSION_MINOR, VERSION_BUGFIX);
    fformat(fp, "%l\n", cur_search);
    close_scratch_file(fp);
    db_clean = 1;
}

static void db_is_dirty(void)
{
    if (db_clean) {
	/* Remove 'clean' file. */
	if (unlink("binary/clean") == -1)
	    panic("Cannot remove file 'clean'.");
	db_clean = 0;
    }
}

