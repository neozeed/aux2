#ifndef lint	/* .../sys/PAGING/os/slp.c */
#define _AC_NAME slp_c
#define _AC_NO_MAIN "@(#) Copyright (c) 1984-85 AT&T-IS, All Rights Reserved.  {Apple version 2.2 89/10/21 20:13:30}"
#include <apple_notice.h>

#ifdef _AC_HISTORY
  static char *sccsid = "@(#)Copyright Apple Computer 1987\tVersion 2.2 of slp.c on 89/10/21 20:13:30";
#endif		/* _AC_HISTORY */
#endif		/* lint */

#define _AC_MODS
/*	@(#)slp.c	UniPlus VVV.2.1.8	*/

#ifdef HOWFAR
extern int T_slp;
#endif HOWFAR
#include "sys/types.h"
#include "sys/mmu.h"
#include "sys/sysmacros.h"
#include "sys/param.h"
#include "sys/dir.h"
#include "sys/signal.h"
#include "sys/time.h"
#include "sys/user.h"
#include "sys/page.h"
#include "sys/region.h"
#include "sys/proc.h"
#include "sys/systm.h"
#include "sys/sysinfo.h"
#include "sys/map.h"
#include "sys/file.h"
#include "sys/vnode.h"
#include "sys/buf.h"
#include "sys/var.h"
#include "sys/psl.h"
#include "sys/errno.h"
#include "sys/conf.h"
#include "sys/debug.h"


#define	NHSQUE	64	/* must be power of 2 */
#define	sqhash(X)	(&hsque[((int)(X) >> 3) & (NHSQUE-1)])
struct proc *hsque[NHSQUE];

/*
 */

setrq(p)
register struct proc *p;
{
	register struct proc *q;
	register s;

	s = splhi();
	for (q = runq; q != NULL; q = q->p_link) {
		if (q == p) {
			printf("proc on q\n");
			goto out;
		}
	}
	p->p_link = runq;
	runq = p;
	p->p_stat = SRUN;	/* p is now runnable, but not running	*/

	if ((p->p_flag & SLOAD) == 0)
		wakeup(&runout);
out:
	splx(s);
}


remrq(p)
register struct proc *p;
{
	register struct proc **q;
	register int s;

	s = splhi();
	for (q = &runq; *q != NULL; q = &((*q)->p_link)) {
		if (*q == p)
			break;
	}
	if (*q == NULL)
		panic("remrq");
	*q = (*q)->p_link;
	splx(s);
}


#define	TZERO	10


sleep(chan, disp)
caddr_t chan;
{
	register struct proc *rp = u.u_procp;
	register struct proc **q = sqhash(chan);
	register s;

	s = splhi();
	if (panicstr) {
		SPL0();
		splx(s);
		return(0);
	}
	rp->p_stat = SSLEEP;
	rp->p_wchan = chan;
	rp->p_link = *q;
	*q = rp;
	if (rp->p_time > TZERO)
		rp->p_time = TZERO;

	if ((rp->p_pri = (disp&PMASK)) > PZERO || (disp&PINTR)) {
	        rp->p_flag |= SINTR;

		if ((rp->p_cursig || rp->p_sig) && issig()) {
			unsleep(rp);
			rp->p_stat = SRUN;
			SPL0();
			goto psig;
		}
		if(rp->p_wchan == 0) {	/* issig took away reason for sleep */
			splx(s);
			return(0);
		}
		if (runin != 0) {
			runin = 0;
			wakeup((caddr_t)&runin);
		}
		SPL0();
		swtch();
		if ((rp->p_cursig || rp->p_sig) && issig())
			goto psig;
	} else {
	        rp->p_flag &= ~SINTR;
		SPL0();
		swtch();
	}
	splx(s);
	return(0);

	/*
	 * If priority was low (>PZERO) and there has been a signal,
	 * if PCATCH is set, return 1, else
	 * execute non-local goto to the qsav location.
	 */
psig:
	splx(s);
	if (disp&PCATCH)
		return(1);
	resume(u.u_qsav, rp->p_uptbl);
	/* NOTREACHED */
}

/*
 * Remove a process from its wait queue.
 */
unsleep(p)
register struct proc *p;
{
	register struct proc **q;
	register int s;

	s = splhi();
	if (p->p_wchan) {
		for (q = sqhash(p->p_wchan); *q != p; q = &(*q)->p_link) ;
		*q = p->p_link;
		p->p_wchan = 0;
	}
	splx(s);
}

wakeup(chan)
register caddr_t chan;
{
	register struct proc *p;
	register struct proc **q;
	register s;

	s = splhi();
	for (q = sqhash(chan); p = *q; ) {
		if (p->p_stat != SSLEEP && p->p_stat != SSTOP) {
			pre_panic();
			printf("Debug: p_pid = %d\n", p->p_pid); /* XXX */
			panic("wakeup p_stat");
		}
		if (p->p_wchan == chan) {
			p->p_wchan = 0;
			*q = p->p_link;
			if (p->p_stat == SSLEEP) {
				/* take off sleep queue, put on run queue */
				p->p_stat = SRUN;
				p->p_link = runq;
				runq = p;

				if (!(p->p_flag&SLOAD)) {
					p->p_time = 0;
				/* defer setrun to avoid breaking link chain */
					if (runout > 0)
						runout = -runout;
				} else if (p->p_pri < curpri)
					runrun++;
			}
		} else
			q = &p->p_link;
	}
	if (runout < 0) {
		runout = 0;
		setrun(&proc[0]);
	}
	splx(s);
}

/*
 * Wake up the first process sleeping on chan.
 *
 * Be very sure that the first process is really
 * the right one to wakeup.
 */
wakeup_one(chan)
register caddr_t chan;
{
	register struct proc *p;
	register struct proc **q;
	register s;

	s = splhi();
	for (q = sqhash(chan); p = *q; ) {
		if (p->p_stat != SSLEEP && p->p_stat != SSTOP)
			panic("wakeup_one");
		if (p->p_wchan == chan) {
			p->p_wchan = 0;
			*q = p->p_link;
			if (p->p_stat == SSLEEP) {
				/* take off sleep queue, put on run queue */
				p->p_stat = SRUN;
				p->p_link = runq;
				runq = p;
				if (!(p->p_flag&SLOAD)) {
					p->p_time = 0;
				/* defer setrun to avoid breaking link chain */
					if (runout > 0)
						runout = -runout;
				} else if (p->p_pri < curpri)
					runrun++;
				break;		/* all done */
			}
		} else
			q = &p->p_link;
	}
	if (runout < 0) {
		runout = 0;
		setrun(&proc[0]);
	}
	splx(s);
}


/* setrun used for wakeups
 */
 
setrun(p)
register struct proc *p;
{
	register s;

	s = splhi();
	if (p->p_stat == SSLEEP || p->p_stat == SSTOP) {
		/* take off sleep queue */
		unsleep(p);
	} else if (p->p_stat == SRUN || p->p_stat == SONPROC) {
		/* already on run queue (or currently running) - just return */
		splx(s);
		return;
	}
	/* put on run queue */
	p->p_stat = SRUN;
	p->p_link = runq;
	runq = p;

	if (!(p->p_flag&SLOAD)) {
		p->p_time = 0;
		if (runout > 0) {
			runout = 0;
			setrun(&proc[0]);
		}
	} else if (p->p_pri < curpri)
		runrun++;
	splx(s);
}

/* <@(#)slp.c	6.3> */
