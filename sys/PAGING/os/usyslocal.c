#ifndef lint	/* .../sys/PAGING/os/usyslocal.c */
#define _AC_NAME usyslocal_c
#define _AC_NO_MAIN "@(#) Copyright (c) 1984-85 AT&T-IS, All Rights Reserved.  {Apple version 2.1 89/10/13 10:18:32}"
#include <apple_notice.h>

#ifdef _AC_HISTORY
  static char *sccsid = "@(#)Copyright Apple Computer 1989\tVersion 2.1 of usyslocal.c on 89/10/13 10:18:32";
#endif		/* _AC_HISTORY */
#endif		/* lint */

#define _AC_MODS
/*	@(#)usyslocal.c	UniPlus VVV.2.1.1	*/

#ifdef HOWFAR
extern int T_usyslocal;
#endif HOWFAR
#ifdef lint
#include "sys/sysinclude.h"
#else lint
#include "sys/types.h"
#include "sys/mmu.h"
#include "sys/param.h"
#include "sys/uconfig.h"
#include "sys/page.h"
#include "sys/region.h"
#include "sys/sysmacros.h"
#include "sys/dir.h"
#include "sys/time.h"
#include "sys/proc.h"
#include "sys/signal.h"
#include "sys/errno.h"
#include "sys/user.h"
#include "sys/systm.h"
#include "sys/vnode.h"
#include "sys/file.h"
#include "sys/conf.h"
#include "sys/map.h"
#include "sys/var.h"
#include "sys/uio.h"
#include "sys/pathname.h"
#include "svfs/inode.h"

#include "sys/reg.h"
#include "sys/sysinfo.h"
#include "sys/swap.h"
#include "sys/debug.h"
#include "sys/buserr.h"
#include "sys/tuneable.h"
#include "sys/mount.h"

#endif lint

/*	Sys3b function 3 - manipulate swap files.
 */

swapfunc(si)
register swpi_t	*si;
{
	register int		i;
	struct vnode *vp;
	swpi_t			swpbuf;
	int			error;
	struct pathname		pn;
	extern struct vnodeops spec_vnodeops;

	if (error = copyin(si, &swpbuf, sizeof(swpi_t)))
		return(error);
	si = &swpbuf;

	if (si->si_cmd == SI_LIST) {
		i = sizeof(swpt_t) * MSFILES;
		if (error = copyout((caddr_t)swaptab, si->si_buf, i))
			return(error);
		return(0);
	}

	if(!suser())
		return(EPERM);
	if (error = pn_get(si->si_buf, UIOSEG_USER, &pn))
		return(error);

	error = lookuppn(&pn, FOLLOW_LINK, (struct vnode **)0, &vp);
	pn_free(&pn);
	if (error)
		return(error);
	if (vp->v_op != &spec_vnodeops) {
		VN_RELE(vp);
		return(EINVAL);
	}
	if (vp->v_type != VBLK) {
		VN_RELE(vp);
		return(ENOTBLK);
	}
	if (si->si_cmd == SI_DEL)
		error = swapdel(vp->v_rdev, si->si_swplo);
	else if (si->si_cmd == SI_ADD) {
		struct mount *mp;
		/*
		 * check if dev is mounted
		 */
		for (mp = &mounttab[0]; mp < (struct mount *)v.ve_mount; mp++) {
			if (mp->m_bufp != NULL && vp->v_rdev == mp->m_dev) {
				VN_RELE(vp);
				return(EBUSY);
			}
		}

		error = swapadd(vp->v_rdev, si->si_swplo, si->si_nblks);
	}
	else
		error = EINVAL;

	VN_RELE(vp);
	return(error);
}

/*
 * Profiling
 */

profil()
{
	register struct a {
		short	*bufbase;
		unsigned bufsize;
		unsigned pcoffset;
		unsigned pcscale;
	} *uap;
	register struct user *up;

	up = &u;
	uap = (struct a *)up->u_ap;
	up->u_prof.pr_base = uap->bufbase;
	up->u_prof.pr_size = uap->bufsize;
	up->u_prof.pr_off = uap->pcoffset;
	up->u_prof.pr_scale = uap->pcscale;
}

ulimit()
{
	register struct user *up;
	register struct a {
		int	cmd;
		long	arg;
	} *uap;

	up = &u;
	uap = (struct a *)up->u_ap;

	switch(uap->cmd) {
	case 2:
		if (uap->arg > up->u_limit && !suser())
			return;
		up->u_limit = uap->arg;
	case 1:
		up->u_roff = up->u_limit;
		break;

	case 3:{
		register preg_t	*prp, *dprp, *prp2;
		register reg_t *rp;
		register size = 0;

		/*	Find the data region
		 */

		if ((dprp = findpreg(up->u_procp, PT_DATA)) == NULL)
			up->u_roff = 0;
		else {
			/*	Now find the region with a virtual
			 *	address greater than the data region
			 *	but lower than any other region
			 */
			prp2 = NULL;
			for (prp = up->u_procp->p_region; prp->p_reg; prp++) {
				if (prp->p_regva <= dprp->p_regva)
					continue;
				if (prp2==NULL || prp->p_regva < prp2->p_regva)
					prp2 = prp;
			}
			if (prp2 == NULL)
				up->u_roff = L2tob(NL2TBL);
			else if (prp2->p_reg->r_stack)
				up->u_roff = (off_t) (up->u_procp->p_stack -
					L2tob(ptoL2(prp2->p_reg->r_pgsz)));
			else
				up->u_roff = (off_t)prp2->p_regva;
			prp = up->u_procp->p_region;
			while (rp = prp->p_reg) {
				if (prp == dprp)
					size += btop((caddr_t)up->u_roff - 
							prp->p_regva);
				else
					size += rp->r_pgsz;
				prp++;
			}
			if (size > tune.t_maxumem)
				up->u_roff -= (off_t) ptob(size - tune.t_maxumem);
		}
		break;
	}

	default:
		up->u_error = EINVAL;
	}
}
