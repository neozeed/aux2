#ifndef lint	/* .../sys/PAGING/os/swtch.c */
#define _AC_NAME swtch_c
#define _AC_NO_MAIN "@(#) Copyright (c) 1984-85 AT&T-IS, All Rights Reserved.  {Apple version 2.3 90/01/13 13:33:48}"
#include <apple_notice.h>

#ifdef _AC_HISTORY
  static char *sccsid = "@(#)Copyright Apple Computer 1989\tVersion 2.3 of swtch.c on 90/01/13 13:33:48";
#endif		/* _AC_HISTORY */
#endif		/* lint */

#define _AC_MODS
/*	@(#)swtch.c	UniPlus VVV.2.1.6	*/

#include "sys/types.h"
#include "sys/mmu.h"
#include "sys/debug.h"
#include "sys/param.h"
#include "sys/sysmacros.h"
#include "sys/dir.h"
#include "sys/signal.h"
#include "sys/time.h"
#include "sys/user.h"
#include "sys/page.h"
#include "sys/systm.h"
#include "sys/sysinfo.h"
#include "sys/var.h"
#include "sys/errno.h"
#include "sys/region.h"
#include "sys/proc.h"
#include "sys/pfdat.h"

extern int lticks;
int tb_ticks = HZ;


/*
 * put the current process on
 * the Q of running processes and
 * call the scheduler.
 */
qswtch()
{
	setrq(u.u_procp);
	swtch();
}

/*
 * This routine is called to reschedule the CPU.
 * if the calling process is not in RUN state,
 * arrangements for it to restart must have
 * been made elsewhere, usually by calling via sleep.
 * There is a race here. A process may become
 * ready after it has been examined.
 * In this case, idle() will be called and
 * will return in at most 1HZ time.
 * i.e. its not worth putting an spl() in.
 */
swtch()
{	extern void (*ui_callout)();

	if (save(u.u_rsav)) {
		if (ui_callout && u.u_user[1]) {
		        if (lticks < tb_ticks)      /* if we're a toolbox proc */
			        lticks = tb_ticks;  /* give us at least 1 second */
			(*ui_callout)();            /* + update low core variables */
		}
		return;
	}
	resched();
}

/*
 * This routine is called by the breakpoint trap
 * handler to reschedule the CPU.
 */

int switching;		/* don't bill running process */


resched()
{
	register struct proc *pp;
	register n;

	switching = 1;
	sysinfo.pswitch++;
loop:
	SPLHI();

	/*
	 * Search for highest-priority runnable process
	 */
	{	register struct proc *p, *q, *pq;
		runrun = 0;
		pp = NULL;
		q = NULL;
		n = 128;

		for (p = runq; p; q = p, p = p->p_link) {
			if ((p->p_flag & SLOAD) == 0)
				continue;
			if (p->p_pri > n)
				continue;
			pp = p;
			pq = q;
			n = p->p_pri;
		}
		/*
		 * If no process is runnable, idle.
		 */
		if (pp == NULL) {
			curpri = PIDLE;
			idle();
			goto loop;
		}
		if (pq == NULL)
			runq = pp->p_link;
		else
			pq->p_link = pp->p_link;

		pp->p_stat = SONPROC;	/* process pp will be running	*/
		lticks = v.v_slice;	/* allocate process a time slice  */
		curpri = n;
	}

	if (u.u_procp->p_stat != SZOMB)		/* Allow interrupts if we  */
		SPL0();				/* have a stack */
	else
	{
		invsatb(USRATB, 0, -1);		/* clear out old vals */
		shred_32bit(u.u_procp, 1);
		/* Free the U-block (no dbd) */
		pagefree(u.u_procp->p_addr);
		availrmem += USIZE;
		availsmem += USIZE;
	}
	switching = 0;
	resume(u.u_rsav, pp->p_uptbl);
}


/* <@(#)swtch.c	1.2> */
