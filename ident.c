/* ident.c: The global identifier table. */

#include <stdio.h>
#include <string.h>
#include "ident.h"
#include "memory.h"
#include "util.h"
#ifdef IDENT_DEBUG
#include "log.h"
#endif

#define FWRITE(v, fp) fwrite(&v, sizeof(v), 1, fp)
#define FREAD(v, fp) fread(&v, sizeof(v), 1, fp)

/* We use MALLOC_DELTA to keep the table sizes at least 32 bytes below a power
 * of two, assuming an int is four bytes. */
#define MALLOC_DELTA 8
#define INIT_TAB_SIZE (512 - MALLOC_DELTA)

typedef struct ident Ident;

struct ident {
    char *s;
    int refs;
    long next;
};

static Ident *tab;
static long *hashtab;
static long tab_size, blanks;

long perm_id, type_id, div_id, integer_id, string_id, dbref_id, list_id;
long symbol_id, error_id, frob_id, methodnf_id, methoderr_id, parent_id;
long maxdepth_id, objnf_id, numargs_id, range_id, paramnf_id, file_id;
long ticks_id, connect_id, disconnect_id, startup_id, parse_id, socket_id;
long bind_id, servnf_id, paramexists_id, dictionary_id, keynf_id, address_id;
long refused_id, net_id, timeout_id, other_id, failed_id, heartbeat_id;
long regexp_id, buffer_id, namenf_id;

void init_ident(void)
{
    long i;

    tab_size = INIT_TAB_SIZE;

    tab = EMALLOC(Ident, tab_size);
    hashtab = EMALLOC(long, tab_size);

    for (i = 0; i < tab_size; i++) {
	tab[i].s = NULL;
	tab[i].next = i + 1;
	hashtab[i] = -1;
    }
    tab[tab_size - 1].next = -1;

    blanks = 0;

    perm_id = ident_get("perm");
    type_id = ident_get("type");
    div_id = ident_get("div");
    integer_id = ident_get("integer");
    string_id = ident_get("string");
    dbref_id = ident_get("dbref");
    list_id = ident_get("list");
    symbol_id = ident_get("symbol");
    error_id = ident_get("error");
    frob_id = ident_get("frob");
    methodnf_id = ident_get("methodnf");
    methoderr_id = ident_get("methoderr");
    parent_id = ident_get("parent");
    maxdepth_id = ident_get("maxdepth");
    objnf_id = ident_get("objnf");
    numargs_id = ident_get("numargs");
    range_id = ident_get("range");
    paramnf_id = ident_get("paramnf");
    file_id = ident_get("file");
    ticks_id = ident_get("ticks");
    connect_id = ident_get("connect");
    disconnect_id = ident_get("disconnect");
    parse_id = ident_get("parse");
    startup_id = ident_get("startup");
    socket_id = ident_get("socket");
    bind_id = ident_get("bind");
    servnf_id = ident_get("servnf");
    paramexists_id = ident_get("paramexists");
    dictionary_id = ident_get("dictionary");
    keynf_id = ident_get("keynf");
    address_id = ident_get("address");
    refused_id = ident_get("refused");
    net_id = ident_get("net");
    timeout_id = ident_get("timeout");
    other_id = ident_get("other");
    failed_id = ident_get("failed");
    heartbeat_id = ident_get("heartbeat");
    regexp_id = ident_get("regexp");
    buffer_id = ident_get("buffer");
    namenf_id = ident_get("namenf");
}

long ident_get(char *s)
{
    unsigned long hval = hash(s);
    long ind, new_size, i;

    /* Look for an existing identifier. */
    ind = hashtab[hval % tab_size];
    while (ind != -1) {
	if (strcmp(tab[ind].s, s) == 0) {
	    tab[ind].refs++;
#ifdef IDENT_DEBUG
	    write_log("get %s: %d refs %d", s, ind, tab[ind].refs);
#endif
	    return ind;
	}
	ind = tab[ind].next;
    }

    /* Check if we have to resize the table. */
    if (blanks == -1) {

	/* Allocate new space for table. */
	new_size = tab_size * 2 + MALLOC_DELTA;
	tab = EREALLOC(tab, Ident, new_size);
	hashtab = EREALLOC(hashtab, long, new_size);

	/* Make new string of blanks. */
	for (i = tab_size; i < new_size - 1; i++)
	    tab[i].next = i + 1;
	tab[i].next = -1;
	blanks = tab_size;

	/* Reset hash table. */
	for (i = 0; i < new_size; i++)
	    hashtab[i] = -1;

	/* Install old symbols in hash table. */
	for (i = 0; i < tab_size; i++) {
	    ind = hash(tab[i].s) % new_size;
	    tab[i].next = hashtab[ind];
	    hashtab[ind] = i;
	}

	tab_size = new_size;
    }

    /* Install symbol at first blank. */
    ind = blanks;
    blanks = tab[ind].next;
    tab[ind].s = tstrdup(s);
    tab[ind].refs = 1;
    tab[ind].next = hashtab[hval % tab_size];
    hashtab[hval % tab_size] = ind;

#ifdef IDENT_DEBUG
    write_log("get %s: %d refs %d", s, ind, tab[ind].refs);
#endif
    return ind;
}

void ident_discard(long id)
{
    long ind, *p;

    tab[id].refs--;
#ifdef IDENT_DEBUG
    write_log("discard %s: %d refs %d", tab[id].s, id, tab[id].refs);
#endif
    if (!tab[id].refs) {
	/* Get the hash table thread for this entry. */
	ind = hash(tab[id].s) % tab_size;

	/* Free the string. */
	tfree_chars(tab[id].s);
	tab[id].s = NULL;

	/* Find the pointer to this entry. */
	for (p = &hashtab[ind]; *p != id; p = &tab[*p].next);

	/* Remove this entry and add it to blanks. */
	*p = tab[id].next;
	tab[id].next = blanks;
	blanks = id;
    }
}

long ident_dup(long id)
{
    tab[id].refs++;
#ifdef IDENT_DEBUG
    write_log("dup %s: %d refs %d", tab[id].s, id, tab[id].refs);
#endif
    return id;
}

char *ident_name(long id)
{
    return tab[id].s;
}

