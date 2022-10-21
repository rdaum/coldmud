/* ident.h: Declarations for the global identifier table. */

#ifndef IDENT_H
#define IDENT_H

#define NOT_AN_IDENT -1

typedef long Ident;

extern Ident perm_id, type_id, div_id, integer_id, string_id, dbref_id;
extern Ident list_id, symbol_id, error_id, frob_id, unrecognized_id;
extern Ident methodnf_id, methoderr_id, parent_id, maxdepth_id, objnf_id;
extern Ident numargs_id, range_id, paramnf_id, file_id, ticks_id, connect_id;
extern Ident disconnect_id, parse_id, startup_id, socket_id, bind_id;
extern Ident servnf_id, paramexists_id, dictionary_id, keynf_id, address_id;
extern Ident refused_id, net_id, timeout_id, other_id, failed_id;
extern Ident heartbeat_id, regexp_id, buffer_id, namenf_id, salt_id;
extern Ident function_id, opcode_id, method_id, interpreter_id;

void init_ident(void);
Ident ident_get(char *s);
void ident_discard(Ident id);
Ident ident_dup(Ident id);
char *ident_name(Ident id);

#endif

