/* ident.h: Declarations for the global identifier table. */

#define NOT_AN_IDENT -1

extern long perm_id, type_id, div_id, integer_id, string_id, dbref_id;
extern long list_id, symbol_id, error_id, frob_id, unrecognized_id;
extern long methodnf_id, methoderr_id, parent_id, maxdepth_id, objnf_id;
extern long numargs_id, range_id, paramnf_id, file_id, ticks_id, connect_id;
extern long disconnect_id, parse_id, startup_id, sys_id, root_id, socket_id;
extern long bind_id, servnf_id, paramexists_id, dictionary_id, keynf_id;
extern long address_id, refused_id, net_id, timeout_id, other_id, failed_id;
extern long heartbeat_id;

void init_ident(void);
long ident_get(char *s);
void ident_discard(long id);
long ident_dup(long id);
char *ident_name(long id);

