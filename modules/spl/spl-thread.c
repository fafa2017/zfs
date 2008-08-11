/*
 *  This file is part of the SPL: Solaris Porting Layer.
 *
 *  Copyright (c) 2008 Lawrence Livermore National Security, LLC.
 *  Produced at Lawrence Livermore National Laboratory
 *  Written by:
 *          Brian Behlendorf <behlendorf1@llnl.gov>,
 *          Herb Wartens <wartens2@llnl.gov>,
 *          Jim Garlick <garlick@llnl.gov>
 *  UCRL-CODE-235197
 *
 *  This is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA.
 */

#include <sys/thread.h>
#include <sys/kmem.h>

#ifdef DEBUG_SUBSYSTEM
#undef DEBUG_SUBSYSTEM
#endif

#define DEBUG_SUBSYSTEM S_THREAD

/*
 * Thread interfaces
 */
typedef struct thread_priv_s {
	unsigned long tp_magic;		/* Magic */
        int tp_name_size;		/* Name size */
        char *tp_name;			/* Name (without _thread suffix) */
	void (*tp_func)(void *);	/* Registered function */
	void *tp_args;			/* Args to be passed to function */
	size_t tp_len;			/* Len to be passed to function */
	int tp_state;			/* State to start thread at */
	pri_t tp_pri;			/* Priority to start threat at */
} thread_priv_t;

static int
thread_generic_wrapper(void *arg)
{
	thread_priv_t *tp = (thread_priv_t *)arg;
	void (*func)(void *);
	void *args;

	ASSERT(tp->tp_magic == TP_MAGIC);
	func = tp->tp_func;
	args = tp->tp_args;
	set_current_state(tp->tp_state);
	set_user_nice((kthread_t *)get_current(), PRIO_TO_NICE(tp->tp_pri));
	kmem_free(tp->tp_name, tp->tp_name_size);
	kmem_free(tp, sizeof(thread_priv_t));

	if (func)
		func(args);

	return 0;
}

void
__thread_exit(void)
{
	ENTRY;
	EXIT;
	complete_and_exit(NULL, 0);
	/* Unreachable */
}
EXPORT_SYMBOL(__thread_exit);

/* thread_create() may block forever if it cannot create a thread or
 * allocate memory.  This is preferable to returning a NULL which Solaris
 * style callers likely never check for... since it can't fail. */
kthread_t *
__thread_create(caddr_t stk, size_t  stksize, thread_func_t func,
		const char *name, void *args, size_t len, int *pp,
		int state, pri_t pri)
{
	thread_priv_t *tp;
	DEFINE_WAIT(wait);
	struct task_struct *tsk;
	char *p;
	ENTRY;

	/* Option pp is simply ignored */
	/* Variable stack size unsupported */
	ASSERT(stk == NULL);

	tp = kmem_alloc(sizeof(thread_priv_t), KM_SLEEP);
	if (tp == NULL)
		RETURN(NULL);

	tp->tp_magic = TP_MAGIC;
	tp->tp_name_size = strlen(name) + 1;

	tp->tp_name = kmem_alloc(tp->tp_name_size, KM_SLEEP);
        if (tp->tp_name == NULL) {
		kmem_free(tp, sizeof(thread_priv_t));
		RETURN(NULL);
	}

	strncpy(tp->tp_name, name, tp->tp_name_size);

	/* Strip trailing "_thread" from passed name which will be the func
	 * name since the exposed API has no parameter for passing a name.
	 */
	p = strstr(tp->tp_name, "_thread");
	if (p)
		p[0] = '\0';

	tp->tp_func  = func;
	tp->tp_args  = args;
	tp->tp_len   = len;
	tp->tp_state = state;
	tp->tp_pri   = pri;

	tsk = kthread_create(thread_generic_wrapper, (void *)tp, tp->tp_name);
	if (IS_ERR(tsk)) {
		CERROR("Failed to create thread: %ld\n", PTR_ERR(tsk));
		RETURN(NULL);
	}

	wake_up_process(tsk);
	RETURN((kthread_t *)tsk);
}
EXPORT_SYMBOL(__thread_create);
