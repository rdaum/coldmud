/* cache.h: Declarations for the object cache. */

#ifndef CACHE_H
#define CACHE_H
#include "object.h"

void init_cache(void);
Object *cache_get_holder(long dbref);
Object *cache_retrieve(long dbref);
Object *cache_grab(Object *object);
void cache_discard(Object *obj);
int cache_check(long dbref);
void cache_sync(void);
Object *cache_first(void);
Object *cache_next(void);
void cache_sanity_check(void);

#endif

