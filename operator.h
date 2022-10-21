/* operator.h: Operator declarations. */

#ifndef OPERATOR_H
#define OPERATOR_H

/* Operators for opcodes generated by language syntax (syntaxop.c). */
void op_comment(void);
void op_pop(void);
void op_set_local(void);
void op_set_obj_var(void);
void op_if(void);
void op_else(void);
void op_for_range(void);
void op_for_list(void);
void op_while(void);
void op_switch(void);
void op_case_value(void);
void op_case_range(void);
void op_last_case_value(void);
void op_last_case_range(void);
void op_end_case(void);
void op_default(void);
void op_end(void);
void op_break(void);
void op_continue(void);
void op_return(void);
void op_return_expr(void);
void op_catch(void);
void op_catch_end(void);
void op_handler_end(void);

void op_zero(void);
void op_one(void);
void op_integer(void);
void op_string(void);
void op_dbref(void);
void op_symbol(void);
void op_error(void);
void op_name(void);
void op_get_local(void);
void op_get_obj_var(void);
void op_start_args(void);
void op_pass(void);
void op_message(void);
void op_expr_message(void);
void op_list(void);
void op_dict(void);
void op_buffer(void);
void op_frob(void);
void op_index(void);
void op_and(void);
void op_or(void);
void op_boolean(void);
void op_splice(void);
void op_critical(void);
void op_critical_end(void);
void op_propagate(void);
void op_propagate_end(void);

/* Arithmetic and relational operators (arithop.c). */
void op_not(void);
void op_negate(void);
void op_div(void);
void op_multiply(void);
void op_divide(void);
void op_modulo(void);
void op_add(void);
void op_splice_add(void);
void op_subtract(void);
void op_equal(void);
void op_not_equal(void);
void op_greater(void);
void op_greater_or_equal(void);
void op_less(void);
void op_less_or_equal(void);
void op_in(void);

/* Generic data manipulation (dataop.c). */
void op_type(void);
void op_class(void);
void op_toint(void);
void op_tostr(void);
void op_toliteral(void);
void op_todbref(void);
void op_tosym(void);
void op_toerr(void);
void op_valid(void);

/* Operations on strings (stringop.c). */
void op_strlen(void);
void op_substr(void);
void op_explode(void);
void op_strsub(void);
void op_pad(void);
void op_match_begin(void);
void op_match_template(void);
void op_match_pattern(void);
void op_match_regexp(void);
void op_crypt(void);
void op_uppercase(void);
void op_lowercase(void);
void op_strcmp(void);

/* List manipulation (listop.c). */
void op_listlen(void);
void op_sublist(void);
void op_insert(void);
void op_replace(void);
void op_delete(void);
void op_setadd(void);
void op_setremove(void);
void op_union(void);

/* Dictionary manipulation (dictop.c). */
void op_dict_keys(void);
void op_dict_add(void);
void op_dict_del(void);
void op_dict_add_elem(void);
void op_dict_del_elem(void);
void op_dict_contains(void);

/* Buffer manipulation (bufferop.c). */
void op_buffer_len(void);
void op_buffer_retrieve(void);
void op_buffer_append(void);
void op_buffer_replace(void);
void op_buffer_add(void);
void op_buffer_truncate(void);
void op_buffer_to_strings(void);
void op_buffer_from_strings(void);

/* Miscellaneous operations (miscop.c). */
void op_version(void);
void op_random(void);
void op_time(void);
void op_ctime(void);
void op_min(void);
void op_max(void);
void op_abs(void);
void op_get_name(void);

/* Current method information operations (methodop.c). */
void op_this();
void op_definer();
void op_sender();
void op_caller();
void op_task_id();

/* Error handling operators (errorop.c). */
void op_error_func(void);
void op_traceback(void);
void op_error_str(void);
void op_error_arg(void);
void op_throw(void);
void op_rethrow(void);

/* Input and output (ioop.c). */
void op_echo(void);
void op_echo_file(void);
void op_disconnect(void);

/* Operations on the current object (objectop.c). */
void op_add_parameter(void);
void op_parameters(void);
void op_del_parameter(void);
void op_set_var(void);
void op_get_var(void);
void op_compile(void);
void op_methods(void);
void op_find_method(void);
void op_find_next_method(void);
void op_list_method(void);
void op_del_method(void);
void op_parents(void);
void op_children(void);
void op_ancestors(void);
void op_has_ancestor(void);
void op_size(void);

/* Administrative operations (adminop.c). */
void op_create(void);
void op_chparents(void);
void op_destroy(void);
void op_log(void);
void op_conn_assign(void);
void op_binary_dump(void);
void op_text_dump(void);
void op_run_script(void);
void op_shutdown(void);
void op_bind(void);
void op_unbind(void);
void op_connect(void);
void op_set_heartbeat_freq(void);
void op_data(void);
void op_set_name(void);
void op_del_name(void);
void op_db_top(void);

#endif

