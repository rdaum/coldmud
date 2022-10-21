/* memory.c: Memory management.
 * This code is not ANSI-conformant, because it plays some games with pointer
 * conversions which ANSI does not allow.  It also assumes that a long has the
 * most restrictive alignment.  I do the pile and tray mallocs this way because
 * they work on most systems and save space. */

#define _POSIX_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include "memory.h"
#include "log.h"

/* This file supports a tray malloc and a pile malloc.  The tray malloc
 * enhances allocation efficiency by keeping trays for small pieces of
 * data.  Note that tfree() and trealloc() require the size of the old
 * block.  trealloc() handles ptr == NULL and newsize == 0 appropriately.
 *
 * The pile malloc enhances efficiency and convenience for applications
 * that allocate a lot of memory in small chunks and then free it all at
 * once.  We call new_pile() to get a handle on a 'pile' that we can
 * allocate memory from.  pile_malloc() accepts a pile in addition to the
 * size argument.  pile_free() frees all the used memory in a pile.  It
 * retains up to MAX_BLOCKS blocks of memory in the pile to avoid repeated
 * mallocs and frees of large blocks. */

#define MAX(a, b)	(((a) >= (b)) ? (a) : (b))

#define TRAY_INC	sizeof(Tlist)
#define NUM_TRAYS	4
#define MAX_USE_TRAY	(NUM_TRAYS * TRAY_INC)
#define TRAY_ELEM	508

#define PILE_BLOCK_SIZE 254
#define MAX_PILE_BLOCKS 8

typedef long Align;
typedef union tlist Tlist;
typedef struct block Block;
typedef struct oversized Oversized;

union tlist {
    Tlist *next;
    Align a;
};

struct pile {
    Block *blocks;
    Oversized *over;
};

struct block {
    Align data[PILE_BLOCK_SIZE];
    int pos;
    Block *next;
};

struct oversized {
    void *data;
    Oversized *next;
};

static Tlist *trays[NUM_TRAYS];

void *emalloc(size_t size)
{
    void *ptr;

    ptr = malloc(size);
    if (!ptr)
        panic("malloc() failed.");
    return ptr;
}

void *erealloc(void *ptr, size_t size)
{
    void *newptr;

    newptr = realloc(ptr, size);
    if (!newptr)
	panic("realloc() failed.");
    return newptr;
}

void *tmalloc(size_t size)
{
    int t, n, i;
    void *p;

    /* If the block isn't fairly small, fall back on malloc(). */
    if (size > MAX_USE_TRAY)
	return emalloc(size);

    /* Find the appropriate tray to use. */
    t = (size - 1) / TRAY_INC;

    /* If we're out of tray elements, make a new tray. */
    if (!trays[t]) {
	trays[t] = EMALLOC(Tlist, TRAY_ELEM);

	/* n is the number of Tlists we need for each tray element. */
	n = t + 1;

	/* Link up tray elements. */
	for (i = 0; i <= TRAY_ELEM - (2 * n); i += n)
	    trays[t][i].next = &trays[t][i + n];
	trays[t][i].next = NULL;
    }

    /* Return the first tray element, and set trays[t] to the next tray
     * element. */
    p = (void *) trays[t];
    trays[t] = trays[t]->next;
    return p;
}

void tfree(void *ptr, size_t size)
{
    int t;

    /* If the block size is greater than MAX_USE_TRAY, then tmalloc() didn't
     * pull it out of a tray, so just free it normally. */
    if (size > MAX_USE_TRAY) {
	free(ptr);
	return;
    }

    /* Add this element to the appropriate tray. */
    t = (size - 1) / TRAY_INC;
    ((Tlist *) ptr)->next = trays[t];
    trays[t] = (Tlist *) ptr;
}

void *trealloc(void *ptr, size_t oldsize, size_t newsize)
{
    void *new;

    /* If neither the old block or the new block is fairly small, then just
     * fall back on realloc(). */
    if (oldsize > MAX_USE_TRAY && newsize > MAX_USE_TRAY)
	return erealloc(ptr, newsize);

    /* If sizes are such that we would be using the same tray for both blocks,
     * just return the old pointer. */
    if ((oldsize - 1) / TRAY_INC == (newsize - 1) / TRAY_INC)
	return ptr;

    /* Allocate a new tray, copy into it, and free the old tray. */
    new = tmalloc(newsize);
    memcpy(new, ptr, MAX(newsize, oldsize));
    tfree(ptr, oldsize);

    return new;
}

/* Duplicate a string, using tray memory. */
char *tstrdup(char *s)
{
    int len = strlen(s);
    char *new;

    new = TMALLOC(char, len + 1);
    memcpy(new, s, len + 1);
    return new;
}

char *tstrndup(char *s, int len)
{
    char *new;

    new = TMALLOC(char, len + 1);
    memcpy(new, s, len);
    new[len] = 0;
    return new;
}

/* Frees a tray-allocated string, assuming we allocated exactly enough memory
 * for it. */
void tfree_chars(char *s)
{
    tfree(s, strlen(s) + 1);
}

Pile *new_pile(void)
{
    Pile *new;

    new = TMALLOC(Pile, 1);

    /* Start with one block and no oversized blocks. */
    new->blocks = EMALLOC(Block, 1);
    new->blocks->pos = 0;
    new->blocks->next = NULL;
    new->over = NULL;
    return new;
}

void *pmalloc(Pile *pile, size_t size)
{
    Block *b;
    int aligns = (size - 1) / sizeof(Align) + 1;

    /* If the size is larger than a block, then make an oversized block and
     * link it in. */
    if (aligns > PILE_BLOCK_SIZE) {
	Oversized *o;

	o = TMALLOC(Oversized, 1);
	o->data = emalloc(size);
	o->next = pile->over;
	pile->over = o;
	return o->data;
    }

    /* Look for a block with enough space. */
    for (b = pile->blocks; b; b = b->next) {
	if (b->pos + aligns <= PILE_BLOCK_SIZE) {
	    b->pos += aligns;
	    return (void *) (&b->data[b->pos - aligns]);
	}
    }

    /* There weren't any.  Make a new block. */
    b = EMALLOC(Block, 1);
    b->pos = aligns;
    b->next = pile->blocks;
    pile->blocks = b;
    return (void *)(&b->data[0]);
}

/* Free all the memory allocated from a pile. */
void pfree(Pile *pile)
{
    Oversized *o, *nexto;
    Block *b, *nextb;
    int count;

    /* Free all the oversized blocks. */
    for (o = pile->over; o; o = nexto) {
	nexto = o->next;
	free(o->data);
	TFREE(o, 1);
    }
    pile->over = NULL;

    /* Reset positions of blocks up to MAX_PILE_BLOCKS - 1. */
    b = pile->blocks;
    count = 0;
    while (b && count < MAX_PILE_BLOCKS - 1) {
	b->pos = 0;
	b = b->next;
	count++;
    }

    /* If we didn't run out of blocks, then we're at the last block.  Reset its
     * position and set its ->next pointer to null.  Then free the rest of the
     * blocks. */
    if (b) {
	b->pos = 0;
	nextb = b->next;
	b->next = NULL;
	while (nextb) {
	    b = nextb->next;
	    free(nextb);
	    nextb = b;
	}
    }
}

