#ifndef lint	/* .../sys/PAGING/os/probe.c */
#define _AC_NAME probe_c
#define _AC_NO_MAIN "@(#) Copyright (c) 1985-87 UniSoft Corporation, All Rights Reserved.  {Apple version 2.2 90/03/02 16:00:35}"
#include <apple_notice.h>

#ifdef _AC_HISTORY
  static char *sccsid = "@(#)Copyright Apple Computer 1987, 1988, 1989\tVersion 2.2 of probe.c on 90/03/02 16:00:35";
#endif		/* _AC_HISTORY */
#endif		/* lint */

#define _AC_MODS
/*	@(#)probe.c	UniPlus VVV.2.1.6	*/

#ifdef HOWFAR
extern int T_availmem;
#endif HOWFAR

#ifdef lint
#include "sys/sysinclude.h"
#else lint
#include "sys/types.h"
#include "sys/mmu.h"
#include "sys/param.h"
#include "sys/sysmacros.h"
#include "sys/dir.h"
#include "sys/signal.h"
#include "sys/time.h"
#include "sys/user.h"
#include "sys/page.h"
#include "sys/systm.h"
#include "sys/sysinfo.h"
#include "sys/file.h"
#include "sys/vnode.h"
#include "sys/buf.h"
#include "sys/var.h"
#include "sys/errno.h"
#include "sys/region.h"
#include "sys/pfdat.h"
#include "sys/proc.h"
#include "sys/tuneable.h"
#include "sys/debug.h"
#endif lint

/*
 * Calculate number of pages transfer touchs
 */
#define len(base, count)	\
	btop(base + count) - btotp(base)

/*
 * Check user read/write access
 * If rw has B_PHYS set, the user PTEs will be faulted
 * in and locked down.	Returns 0 on failure, 1 on success
 */
useracc(base, count, rw)
register base, count;
{
	register	i;
	register	npgs;
	register int	x;
	register pte_t	*pt;
	register reg_t	*rp;
	pfd_t		*pfd;

	/* Strip address if we're in 24-bit mode */
	if ((u.u_procp->p_flag&(SMAC24|SROOT24)) == (SMAC24|SROOT24))
		base = base & 0x00ffffff;

	/*
	 * If physio, two checks are made:
	 *	1) Base must be word aligned
	 *	2) Transfer must be contained in one segment
	 */

	if (rw & B_PHYS && ((base & 1)
	 || btoL1(base) != btoL1(base + count - 1)))
		return(0);

	/*
	 * Check page permissions and existence
	 */

	npgs = len(base, count);

	rp = findreg(u.u_procp, (caddr_t)base);
	if (rp == NULL)
		return(0);
	regrele(rp);

	for(i = npgs; --i >= 0; base += ptob(1)) {
		x = fubyte((caddr_t)base);
		if(x == -1)
			goto out;

		/*	In the following test we check for a copy-
		**	on-write page and if we find one, we break
		**	the copy-on-write even if we are not going
		**	to write to the page.  This is necessary
		**	in order to make the lock accounting
		**	work correctly.	 The problem is that the
		**	lock bit is in the pde but the count of
		**	the number of lockers is in the pfdat.
		**	Therefore, if more than one pde refers
		**	to the same pfdat, the accounting gets
		**	wrong and we could fail to unlock a
		**	page when we should.  Note that this is
		**	not very likely to happen since it means
		**	we did a fork, no exec, and then started
		**	doing raw I/O.	Still, it could happen.
		*/

		if(rw & B_READ)
			if(subyte((caddr_t)base, x) == -1)
				goto out;
		if ((rw & B_PHYS) && ! rp->r_noswapcnt && (rp->r_type&RT_PHYSCALL) == 0)
		{
#ifndef lint
			pt = Pt_ent(u.u_procp->p_root, base);
#else
			pt = (pte_t *)0;
#endif
			if (pt->pgm.pg_cw  &&  !(rw & B_READ))
				if (subyte(base, x) == -1)
					goto out;
			pg_setlock(pt);
			pfd = pftopfd(pt->pgm.pg_pfn);
			if (pfd->pf_rawcnt++ == 0){
				availrmem--;
				if (availrmem < tune.t_minarmem) {
					printf("useracc - couldn't lock page\n");
					pg_clrlock(pt);
					--pfd->pf_rawcnt;
					u.u_error = EAGAIN;
					availrmem++;
					goto out;
				}
			}
		}
	}
	return(1);
out:
	if ((rw & B_PHYS)  &&  ! rp->r_noswapcnt) {
		for (i++, base -= ptob(1) ; i < npgs ; i++, base -= ptob(1)) {
#ifndef lint
			pt = Pt_ent(u.u_procp->p_root, base);
#else
			pt = (pte_t *)0;
#endif
			pfd = pftopfd(pt->pgm.pg_pfn);
			if (--pfd->pf_rawcnt == 0){
				pg_clrlock(pt);
				availrmem += 1;
			}
		}
	}
	return(0);
}


/*
 * Terminate user physio
 */
undma(base, count, rw)
register int base, rw;
{
	register pte_t *pt;
	register npgs;
	register reg_t *rp;

	/* Strip address if we're in 24-bit mode */
	if ((u.u_procp->p_flag&(SMAC24|SROOT24)) == (SMAC24|SROOT24))
		base = base & 0x00ffffff;

	/*
	 * Unlock PTEs, set the reference bit.
	 * Set the modify bit if B_READ
	 */
	rp = findreg(u.u_procp, (caddr_t)base);
	ASSERT(rp != (reg_t *)NULL);
	if ((rp->r_type&RT_PHYSCALL) == 0)
#ifndef lint
	for (pt = Pt_ent(u.u_procp->p_root, base), npgs = len(base, count); --npgs >= 0; pt++) {
#else lint
	for (pt = (pte_t *)0, npgs = len(base, count); --npgs >= 0; pt++) {
#endif lint
		{
		struct pfdat *pfd = pftopfd(pt->pgm.pg_pfn);

		ASSERT(rp->r_noswapcnt	||  pg_locked(pt));
		ASSERT(rp->r_noswapcnt	||  pfd->pf_rawcnt > 0);
		if (! rp->r_noswapcnt  &&  --pfd->pf_rawcnt == 0) {
			pg_clrlock(pt);
			availrmem++;
		}
		}
		pg_setref(pt);
		if (rw == B_READ) 
			pg_setmod(pt);
	}
	regrele(rp);
}

/* <@(#)probe.c	1.2> */
