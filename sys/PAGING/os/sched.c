#ifndef lint	/* .../sys/PAGING/os/sched.c */
#define _AC_NAME sched_c
#define _AC_NO_MAIN "@(#) Copyright (c) 1984-85 AT&T-IS, All Rights Reserved.  {Apple version 2.2 89/11/29 15:15:46}"
#include <apple_notice.h>

#ifdef _AC_HISTORY
  static char *sccsid = "@(#)Copyright Apple Computer 1987, 1988, 1989\tVersion 2.2 of sched.c on 89/11/29 15:15:46";
#endif		/* _AC_HISTORY */
#endif		/* lint */

#define _AC_MODS
/*	@(#)sched.c	UniPlus VVV.2.1.2	*/

#ifdef lint
#include "sys/sysinclude.h"
#else lint
#include "sys/param.h"
#include "sys/types.h"
#include "sys/mmu.h"
#include "sys/sysmacros.h"
#include "sys/signal.h"
#include "sys/page.h"
#include "sys/region.h"
#include "sys/time.h"
#include "sys/proc.h"
#include "sys/systm.h"
#include "sys/sysinfo.h"
#include "sys/var.h"
#include "sys/tuneable.h"
#include "sys/getpages.h"
#include "sys/debug.h"
#endif lint

char	runin, runout;		

extern int freemem;
extern int shutdownflags;

/*
 * Memory scheduler
 */
sched()
{
	register struct proc *rp, *p;
	register outage, inage;
	int maxbad;
	int tmp;
	int runable;
	proc_t	*justloaded;

	/*
	 * find user to swap in;
	 * of users ready, select one out longest
	 */

loop:
	if (shutdownflags)
	        real_shutdown(shutdownflags);

	/* init runout to 0 every time thru---hence a wakeup coming in during
	 * or after the loop will result in the later check returning
	 * immediately to search again, that way, not missing a kick to the
	 * scheduler
	 */
	runout = 0;
	/*
	 * Otherwise, find user to reactivate
	 * Of users ready, select one out longest
	 */
	SPLCLK();
	outage = -20000;
	runable = 0;
	for (rp = &proc[0]; rp < (struct proc *)v.ve_proc; rp++){
		if ((rp->p_stat==SXBRK ||
		    (rp->p_stat==SRUN && (rp->p_flag&SLOAD) == 0)) &&
		    rp->p_time > outage) {
			p = rp;
			outage = rp->p_time;
		}
		if (rp->p_stat == SRUN	&&
		   (rp->p_flag & (SLOAD | SSYS)) == SLOAD)
			runable = 1;
	}

	/*
	 * If there is no one there, wait.
	 */
	if (outage == -20000) {
		runout++;
		(void)sleep(&runout, PSWP);
		goto loop;
	}
	SPL0();

	/*
	 * See if there is memory for that process;
	 * if so, let it go and then delay in order to
	 * let things settle
	 */

	if (!runable  ||  freemem > tune.t_gpgslo) {
		if (p->p_stat == SXBRK) {
			ASSERT(p->p_wchan == 0);
			p->p_stat = SRUN;
			setrq(p);
		}
		p->p_flag |= SLOAD;
		p->p_time = 0;
		if (freemem > tune.t_gpgslo)
			goto delay;
		justloaded = p;
	} else {
		justloaded = NULL;
	}

	/*
	 * none found.
	 * look around for memory.
	 * Select the largest of those sleeping
	 * at bad priority; if none, select the oldest.
	 */


	p = NULL;
	maxbad = 0;
	inage = 0;
	SPLCLK();
	for (rp = &proc[0]; rp < (struct proc *)v.ve_proc; rp++) {
		if (rp->p_stat==SZOMB)
			continue;
		if ((rp->p_flag&(SSYS|SLOAD|SLOCK))!=SLOAD)
			continue;
		if (rp == justloaded)
			continue;
		if (rp->p_stat==SSLEEP || rp->p_stat==SSTOP) {
			tmp = rp->p_pri - PZERO + rp->p_time;
			if (maxbad < tmp) {
				p = rp;
				maxbad = tmp;
			}
		} else
		if (maxbad<=0 && (rp->p_stat==SRUN || rp->p_stat==SXBRK)) {
			tmp = rp->p_time + rp->p_nice - NZERO;
			if (tmp > inage) {
				p = rp;
				inage = tmp;
			}
		}
	}
	SPL0();

	/*
	 * Swap out and deactivate process if
	 * sleeping at bad priority, or if it has spent at least
	 * 2 seconds in memory and the other process has spent
	 * at least 2 seconds out.
	 * Otherwise wait a bit and try again.
	 */
	if (maxbad > 0 || (outage>=2 && inage>=2)) {
		if (maxbad > 0) {
			if (!(p->p_stat == SSLEEP || p->p_stat == SSTOP))
				goto loop;
		} else if (p->p_stat != SRUN  &&  p->p_stat != SXBRK)
			goto loop;
		if(p->p_flag & SLOCK)
			goto loop;

		p->p_flag &= ~SLOAD;

		if(!swapout(p)) {
			p->p_flag |= SLOAD;
			goto delay;
		}
		else {
			p->p_time = 0;
			goto loop;
		}
			
	}

	/*
	 * Delay for 1 second and look again later
	 */
delay:
	SPLCLK();
	runin++;
	(void)sleep(&runin, PSWP);
	goto loop;
}


/*	Swap out process p
 */
swapout(p)
register struct proc *p;
{
	register preg_t *prp;
	register reg_t *rp;
	register int flg;
	register int rtn; 


	/*	Walk through process regions
	 *	Private regions or shared regions that are being used
	 *	exclusively by this process get paged out en masse.
	 *	Other shared regions are just pruned.
	 *
	 *	return 1 - every region gets paged out 
	 *	return 0 - find region(s) locked
	 */

	rtn = 1;
	pglstlk();
	for(prp = p->p_region; rp = prp->p_reg; prp++) {
		if (rp->r_lock) {
			rtn = 0;
			continue;
		}
		rp->r_lock = (int)p;
		flg = (rp->r_type == RT_PRIVATE || rp->r_refcnt == 1);

		/*	if (flg == 0) only take unreferenced pages */
		/*	if (flg == 1) take all valid pages */
		getpages(rp, flg);
		if(gprglst[gprgndx].gpr_count == 0  ||
			gprglst[gprgndx].gpr_rgptr != rp) 
			regrele(rp);
	}
	/*	If we still do not have enough free
	**	memory and there are pages on the
	**	lists to steal, then steal them now 
	**	before we go to sleep.	Otherwise, 
	**	unlock all of the locked regions.
	*/

	if(fpglndx) 
		freechunk((reg_t *)0); 

	if(spglndx)
		swapchunk((reg_t *)0, 1);

	ASSERT(spglndx == 0);
	ASSERT(fpglndx == 0);
	ASSERT(gprglst[gprgndx].gpr_count == 0);
	pglstunlk();
	return(rtn);

}
