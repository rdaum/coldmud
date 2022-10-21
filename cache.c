/* cache.c: Object cache routines.
 * This code is based on code written by Marcus J. Ranum.  That code, and
 * therefore this derivative work, are Copyright (C) 1991, Marcus J. Ranum,
 * all rights reserved. */

#define _POSIX_SOURCE

#include <stdio.h>
#include "cache.h"
#include "object.h"
#include "memory.h"
#include "db.h"
#include "loc.h"
#include "log.h"
#include "util.h"
#include "config.h"
#include "ident.h"

/* Store dummy objects for chain heads and tails.  This is a little storage-
 * intensive, but it simplifies and speeds up the list operations. */
Object *active;
Object *inactive;

/* Requires: Shouldn't be called twice.
 * Modifies: active, inactive.
 * Effects: Builds an array of object chains in inactive, and an array of
 *	    empty object chains in active. */
void init_cache(void)
{
    Object *obj;
    int	i, j;

    active = EMALLOC(Object, CACHE_WIDTH);
    inactive = EMALLOC(Object, CACHE_WIDTH);

    for (i = 0; i < CACHE_WIDTH; i++) {
	/* Active list starts out empty. */
	active[i].next = active[i].prev = &active[i];

	/* Inactive list begins as a chain of empty objects. */
	inactive[i].next = inactive[i].prev = &inactive[i];
	for (j = 0; j < CACHE_DEPTH; j++) {
	    obj = EMALLOC(Object, 1);
	    obj->dbref = -1;
	    obj->prev = &inactive[i];
	    obj->next = inactive[i].next;
	    obj->prev->next = obj->next->prev = obj;
	}
    }
}

/* Requires: Initialized cache.
 * Modifies: Contents of active, inactive, database files
 * Effects: Returns an object holder linked to the head of the appropriate
 *	    active chain.  Gets the object holder from the tail of the inactive
 *	    chain, swapping out the object there if necessary.  If the inactive
 *	    inactive chain is empty, then we create a new holder. */
Object *cache_get_holder(long dbref)
{
    int ind = dbref % CACHE_WIDTH;
    Object *obj;

    if (inactive[ind].next != &inactive[ind]) {
	/* Use the object at the tail of the inactive list. */
	obj = inactive[ind].prev;

	/* Check if we need to swap anything out. */
	if (obj->dbref != -1) {
	    if (obj->dirty) {
		if (!db_put(obj, ident_name(obj->dbref)))
		    panic("Could not store an object.");
	    }
	    object_free(obj);
	    ident_discard(obj->dbref);
	}

	/* Unlink it from the inactive list. */
	obj->prev->next = obj->next;
	obj->next->prev = obj->prev;
    } else {
	/* Allocate a new object. */
	obj = EMALLOC(Object, 1);
    }

    /* Link the object a the head of the active chain. */
    obj->prev = &active[ind];
    obj->next = active[ind].next;
    obj->prev->next = obj->next->prev = obj;

    obj->dirty = 0;
    obj->dead = 0;
    obj->refs = 1;
    obj->dbref = ident_dup(dbref);
    return obj;
}

/* Requires: Initialized cache.
 * Modifies: Contents of active, inactive, database files
 * Effects: Returns the object associated with dbref, getting it from the cache
 *	    or from disk.  If the object is in the inactive chain or is on
 *	    disk, it will be linked into the active chain.  Returns NULL if no
 *	    object exists with the given dbref. */
Object *cache_retrieve(long dbref)
{
    int ind = dbref % CACHE_WIDTH;
    Object *obj;

    if (dbref < 0)
	return NULL;

    /* Search active chain for object. */
    for (obj = active[ind].next; obj != &active[ind]; obj = obj->next) {
	if (obj->dbref == dbref) {
	    obj->refs++;
	    return obj;
	}
    }

    /* Search inactive chain for object. */
    for (obj = inactive[ind].next; obj != &inactive[ind]; obj = obj->next) {
	if (obj->dbref == dbref) {
	    /* Remove object from inactive chain. */
	    obj->next->prev = obj->prev;
	    obj->prev->next = obj->next;

	    /* Install object at head of active chain. */
	    obj->prev = &active[ind];
	    obj->next = active[ind].next;
	    obj->prev->next = obj->next->prev = obj;

	    obj->refs = 1;
	    return obj;
	}
    }

    /* Cache miss.  Find an object to load in from disk. */
    obj = cache_get_holder(dbref);

    /* Read the object into the place-holder, if it's on disk. */
    if (db_get(obj, ident_name(dbref))) {
	return obj;
    } else {
	/* Oops.  Install holder at tail of inactive chain and return NULL. */
	obj->dbref = -1;
	obj->prev->next = obj->next;
	obj->next->prev = obj->prev;
	obj->prev = inactive[ind].prev;
	obj->next = &inactive[ind];
	obj->prev->next = obj->next->prev = obj;
	return NULL;
    }
}

Object *cache_grab(Object *obj)
{
    obj->refs++;
    return obj;
}

/* Requires: Initialized cache.  obj should point to an active object.
 * Modifies: obj, contents of active and inactive, database files.
 * Effects: Decreases the refcount on obj, unlinking it from the active chain
 *	    if the refcount hits zero.  If the object is marked dead, then it
 *	    is destroyed when it is unlinked from the active chain. */
void cache_discard(Object *obj)
{
    int ind;

    /* Decrease reference count. */
    obj->refs--;
    if (obj->refs)
	return;

    ind = obj->dbref % CACHE_WIDTH;

    /* Reference count hit 0; remove from active chain. */
    obj->prev->next = obj->next;
    obj->next->prev = obj->prev;

    if (obj->dead) {
	/* The object is dead; remove it from the database, and install it at
	 * the tail of the inactive chain.  Be careful about this, since
	 * object_destroy() can fiddle with the cache.  We're safe as long as
	 * obj isn't in any chains at the time of db_del(). */
	db_del(ident_name(obj->dbref));
	object_destroy(obj);
	obj->dbref = -1;
	obj->prev = inactive[ind].prev;
	obj->next = &inactive[ind];
	obj->prev->next = obj->next->prev = obj;
    } else {
	/* Install at head of inactive chain. */
	obj->prev = &inactive[ind];
	obj->next = inactive[ind].next;
	obj->prev->next = obj->next->prev = obj;
    }
}

/* Requires: Initialized cache.
 * Effects: Returns nonzero if an object exists with the given dbref. */
int cache_check(long dbref)
{
    int ind = dbref % CACHE_WIDTH;
    Object *obj;

    if (dbref < 0)
	return 0;

    /* Search active chain. */
    for (obj = active[ind].next; obj != &active[ind]; obj = obj->next) {
	if (obj->dbref == dbref)
	    return 1;
    }

    /* Search inactive chain. */
    for (obj = inactive[ind].next; obj != &inactive[ind]; obj = obj->next) {
	if (obj->dbref == dbref)
	    return 1;
    }

    /* Check database on disk. */
    return db_check(ident_name(dbref));
}

/* Requires: Initialized cache.
 * Modifies: Database files.
 * Effects: Writes out all objects in the cache which are marked dirty. */
void cache_sync(void)
{
    int i;
    Object *obj;

    /* Traverse all the active and inactive chains. */
    for (i = 0; i < CACHE_WIDTH; i++) {
	/* Check active chain. */
	for (obj = active[i].next; obj != &active[i]; obj = obj->next) {
	    if (obj->dirty) {
		if (!db_put(obj, ident_name(obj->dbref)))
		    panic("Could not store an object.");
		obj->dirty = 0;
	    }
	}

	/* Check inactive chain. */
	for (obj = inactive[i].next; obj != &inactive[i]; obj = obj->next) {
	    if (obj->dbref != -1 && obj->dirty) {
		if (!db_put(obj, ident_name(obj->dbref)))
		    panic("Could not store an object.");
		obj->dirty = 0;
	    }
	}
    }

    db_flush();
}

Object *cache_first(void)
{
    char *name;
    long id;
    Object *obj;

    cache_sync();
    name = loc_first();
    id = ident_get(name);
    obj = cache_retrieve(id);
    ident_discard(id);
    return obj;
}

Object *cache_next(void)
{
    char *name;
    long id;
    Object *obj;

    name = loc_next();
    if (!name)
	return NULL;
    id = ident_get(name);
    obj = cache_retrieve(id);
    ident_discard(id);
    return obj;
}

