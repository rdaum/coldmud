/* methodop.c: Current method operations. */

#include "x.tab.h"
#include "operator.h"
#include "execute.h"
#include "ident.h"

void op_this(void)
{
    /* Accept no arguments, and push the dbref of the current object. */
    if (!func_init_0())
	return;
    push_dbref(cur_frame->object->dbref);
}

void op_definer(void)
{
    /* Accept no arguments, and push the dbref of the method definer. */
    if (!func_init_0())
	return;
    push_dbref(cur_frame->method->object->dbref);
}

void op_sender(void)
{
    /* Accept no arguments, and push the dbref of the sending object. */
    if (!func_init_0())
	return;
    if (cur_frame->sender == NOT_AN_IDENT)
	push_int(0);
    else
	push_dbref(cur_frame->sender);
}

void op_caller(void)
{
    /* Accept no arguments, and push the dbref of the calling method's
     * definer. */
    if (!func_init_0())
	return;
    if (cur_frame->caller == NOT_AN_IDENT)
	push_int(0);
    else
	push_dbref(cur_frame->caller);
}

