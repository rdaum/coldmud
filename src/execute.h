/* execute.h: Declarations for executing C-- tasks. */

#ifndef EXECUTE_H
#define EXECUTE_H

typedef struct frame Frame;
typedef struct error_action_specifier Error_action_specifier;
typedef struct handler_info Handler_info;

#include <sys/types.h>
#include <stdarg.h>
#include "data.h"
#include "object.h"
#include "io.h"

/* We use the MALLOC_DELTA defines to keep table sizes thirty-two bytes less
 * than a power of two, which is helpful on buddy systems. */
#define STACK_MALLOC_DELTA 4
#define ARG_STACK_MALLOC_DELTA 8

struct frame {
    Object *object;
    Dbref sender;
    Dbref caller;
    Method *method;
    long *opcodes;
    int pc;
    int last_opcode;
    int ticks;
    int stack_start;
    int var_start;
    Error_action_specifier *specifiers;
    Handler_info *handler_info;
    Frame *caller_frame;
};

struct error_action_specifier {
    int type;
    int stack_pos;
    union {
	struct {
	    int end;
	} critical;
	struct {
	    int end;
	} propagate;
	struct {
	    int error_list;
	    int handler;
	} catch;
    } u;
    Error_action_specifier *next;
};

struct handler_info {
    List *traceback;
    Ident error;
    Handler_info *next;
};

extern Frame *cur_frame, *suspend_frame;
extern Connection *cur_conn;
extern Data *stack;
extern int stack_pos, stack_size;
extern int *arg_starts, arg_pos, arg_size;
extern String *numargs_str;
extern long task_id;

void init_execute(void);
void task(Connection *conn, Dbref dbref, long message, int num_args, ...);
void task_method(Connection *conn, Object *obj, Method *method);
long frame_start(Object *obj, Method *method, Dbref sender, Dbref caller,
		 int stack_start, int arg_start);
void frame_return(void);
void anticipate_assignment(void);
Ident pass_message(int stack_start, int arg_start);
Ident send_message(Dbref dbref, Ident message, int stack_start, int arg_start);
void pop(int n);
void check_stack(int n);
void push_int(long n);
void push_string(String *str);
void push_dbref(Dbref dbref);
void push_list(List *list);
void push_symbol(Ident id);
void push_error(Ident id);
void push_dict(Dict *dict);
void push_buffer(Buffer *buffer);
int func_init_0();
int func_init_1(Data **args, int type1);
int func_init_2(Data **args, int type1, int type2);
int func_init_3(Data **args, int type1, int type2, int type3);
int func_init_0_or_1(Data **args, int *num_args, int type1);
int func_init_1_or_2(Data **args, int *num_args, int type1, int type2);
int func_init_2_or_3(Data **args, int *num_args, int type1, int type2,
		     int type3);
int func_init_1_to_3(Data **args, int *num_args, int type1, int type2,
		     int type3);
void func_num_error(int num_args, char *required);
void func_type_error(char *which, Data *wrong, char *required);
void func_error(Ident id, char *fmt, ...);
void throw(long id, char *fmt, ...);
void unignorable_error(Ident id, String *str);
void interp_error(Ident error, String *str);
void user_error(Ident error, String *str, Data *arg);
void propagate_error(List *traceback, Ident error);
void pop_error_action_specifier(void);
void pop_handler_info(void);

#endif

