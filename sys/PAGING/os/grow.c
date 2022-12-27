#ifndef lint	/* .../sys/PAGING/os/grow.c */
#define _AC_NAME grow_c
#define _AC_NO_MAIN "@(#) Copyright (c) 1984-85 AT&T-IS, 1985-87 UniSoft Corporation, All Rights Reserved.  {Apple version 2.1 89/10/13 12:39:13}"
#include <apple_notice.h>

#ifdef _AC_HISTORY
  static char *sccsid = "@(#)Copyright Apple Computer 1987, 1988, 1989\tVersion 2.1 of grow.c on 89/10/13 12:39:13";
#endif		/* _AC_HISTORY */
#endif		/* lint */

#define _AC_MODS
/*	@(#)grow.c	UniPlus VVV.2.1.7	*/

#ifdef HOWFAR
extern int T_grow;
extern int T_availmem;
#endif HOWFAR

#ifdef lint
#include "sys/sysinclude.h"
#else lint
#include "sys/types.h"
#include "sys/mmu.h"
#include "sys/bitmasks.h"
#include "sys/param.h"
#include "sys/sysmacros.h"
#include "sys/page.h"
#include "sys/systm.h"
#include "sys/dir.h"
#include "sys/signal.h"
#include "sys/time.h"
#include "sys/user.h"
#include "sys/errno.h"
#include "sys/var.h"
#include "sys/pfdat.h"
#include "sys/region.h"
#include "sys/proc.h"
#include "sys/tuneable.h"
#include "sys/buserr.h"
#include "sys/debug.h"
#endif lint


/* brk and sbrk system calls
 */

sbreak()
{
	struct a {
		int nva;
	};
	register preg_t	*prp;
	register reg_t	*rp;
	register struct user *up;
	register struct proc *p;
	register int	nva;
	register int	change;

	up = &u;
	p = up->u_procp;
	/*	Find the processes data region.
	 */
	if ((prp = findpreg(p, PT_DATA)) == NULL) {
		up->u_error = ENOMEM;
		return;
	}
	rp = prp->p_reg;
	reglock(rp);

	nva = ((struct a *)up->u_ap)->nva;
	if (nva < up->u_exdata.ux_datorg)
		nva = up->u_exdata.ux_datorg;
	change = btop((int)nva) - btotp(prp->p_regva) - rp->r_pgsz;

	if (change > 0 && chkpgrowth(change) < 0) {
		regrele(rp);
		up->u_error = ENOMEM;
		return;
	}
	if (growreg(prp, change, DBD_DZERO) < 0) {
		regrele(rp);
		/* let errno from growreg be passed on through */
		return;
	}
	regrele(rp);

	if (change <= 0) {
		register int	n;

		/* clear released part of last page */
		if ((n = ptob(1) - poff(nva)) < ptob(1))
			uclear(nva, n);
		if (change < 0)
			clratb(USRATB);
	}
}


/* grow the stack to include the SP
 * true return if successful.
 */

grow(sp)
register unsigned sp;
{
	register preg_t	*prp;
	register reg_t	*rp;
	register	si;
	register struct proc *p;

	/*	Find the processes stack region.
	 */
	p = u.u_procp;
	if ((prp = findpreg(p, PT_STACK)) == NULL)
		return(0);
	rp = prp->p_reg;
	reglock(rp);

	if ((sp > (p->p_stack - ptob(rp->r_pgsz))) && (sp < p->p_stack)) {
		regrele(rp);
		return(0);
	}
	if ((si = btop(p->p_stack - sp) - rp->r_pgsz + SINCR) <= 0) {
		regrele(rp);
		return(0);
	}
	if (chkpgrowth(si) < 0) {
		regrele(rp);
		return(0);
	}
	if (growreg(prp, si, DBD_DZERO) < 0) {
		regrele(rp);
		return(0);
	}
	regrele(rp);
	return(1);
}


/*	Check that a process is not trying to expand
**	beyond the maximum allowed virtual address
**	space size.
*/

chkpgrowth(size)
register int	size;	/* Increment being added (in pages).	*/
{
	register preg_t	*prp;
	register reg_t	*rp;

	prp = u.u_procp->p_region;

	while (rp = prp->p_reg) {
		size += rp->r_pgsz;
		prp++;
	}
	if (size > tune.t_maxumem)
		return(-1);
	return(0);
}

/* <@(#)grow.c	1.2> */
