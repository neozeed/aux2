#ifndef lint	/* .../sys/PAGING/os/fork.c */
#define _AC_NAME fork_c
#define _AC_NO_MAIN "@(#) Copyright (c) 1984-85 AT&T-IS, 1985-87 UniSoft Corporation, All Rights Reserved.  {Apple version 2.3 90/01/13 13:03:32}"
#include <apple_notice.h>

#ifdef _AC_HISTORY
  static char *sccsid = "@(#)Copyright Apple Computer 1989\tVersion 2.3 of fork.c on 90/01/13 13:03:32";
#endif		/* _AC_HISTORY */
#endif		/* lint */

#define _AC_MODS
/*	@(#)fork.c	UniPlus VVV.2.1.9	*/

#include "compat.h"
#ifdef HOWFAR
extern int T_fork;
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
#include "sys/pfdat.h"
#include "sys/map.h"
#include "sys/file.h"
#include "sys/vnode.h"
#include "sys/buf.h"
#include "sys/var.h"
#include "sys/errno.h"
#include "sys/region.h"
#include "sys/proc.h"
#include "sys/debug.h"
#include "sys/tuneable.h"
#endif lint


/*
 * Create a new process-- the internal version of
 * sys fork.
 *
 * This changes the new proc structure and
 * alters only u.u_procp of the uarea.
 * The second arg indicates whether the new process should be
 *  a kernel process, and is added to the process flags.
 *
 * It returns 1 in the new process, 0 in the old.
 */
newproc(failok, sysflag)
{
	register struct user *up;
	register struct proc *cp, *pp, *pend;
	register n, a;
	register int (**fptr)();
	register struct var *vp;
#ifdef POSIX
	register int fork_flags = (SCOFF|SPGRP42|SMAC24|SNOCLD);
#else
	register int fork_flags = (SCOFF|SPGRP42|SMAC24);
#endif
	extern int (*forkfunc[])();
	static mpid;

	up = &u;
	vp = &v;
	/*
	 * First, just locate a slot for a process
	 * and copy the useful info from this process into it.
	 * The panic "cannot happen" because fork has already
	 * checked for the existence of a slot.
	 */
retry:
	mpid++;
	if (mpid >= MAXPID) {
		mpid = 0;
		goto retry;
	}
	pp = &proc[0];
	cp = NULL;
	n = (struct proc *)vp->ve_proc - pp;
	a = 0;
	do {
		if (pp->p_stat == NULL) {
			if (cp == NULL)
				cp = pp;
			continue;
		}
#ifdef POSIX
		if (pp->p_pid == mpid || pp->p_pgrp == mpid)
			goto retry;
#else
		if (pp->p_pid==mpid)
			goto retry;
#endif POSIX
		if (pp->p_uid == up->u_ruid)
			a++;
		pend = pp;
	} while(pp++, --n);
	if (cp==NULL) {
		if ((struct proc *)vp->ve_proc >= &proc[vp->v_proc]) {
			if (failok) {
				tablefull("proc");
				syserr.procovf++;
				up->u_error = EAGAIN;
				return(-1);
			} else
				panic("no procs");
		}
		cp = (struct proc *)vp->ve_proc;
	}
	if (cp > pend)
		pend = cp;
	pend++;
#ifdef lint
	vp->ve_proc = (struct proc *)pend;
#else lint
	vp->ve_proc = (char *)pend;
#endif lint
	if (up->u_uid && up->u_ruid) {
		if (cp == &proc[vp->v_proc-1] || a > vp->v_maxup) {
			up->u_error = EAGAIN;
			return(-1);
		}
	}

	/*
	 * make proc entry for new proc
	 */

	pp = up->u_procp;
	cp->p_uid = pp->p_uid;
	cp->p_suid = pp->p_suid;
	cp->p_sgid = pp->p_sgid;
	cp->p_pgrp = pp->p_pgrp;
	cp->p_nice = pp->p_nice;
	cp->p_ttyp = pp->p_ttyp;
	cp->p_stat = SIDL;

	/*
	 * p_clktim should be inheirited (per the manual)
	 * but it drives us nuts
	 */

	cp->p_clktim = 0;
	timerclear(&cp->p_realtimer.it_value);
	cp->p_flag = sysflag | SLOAD | (pp->p_flag & fork_flags);

	cp->p_pid = mpid;
	cp->p_ppid = pp->p_pid;
	cp->p_time = 0;
	cp->p_sigmask = pp->p_sigmask;
	cp->p_sigcatch = pp->p_sigcatch;
	cp->p_sigignore = pp->p_sigignore;
	cp->p_cpu = 0;
	cp->p_pri = PUSER + pp->p_nice - NZERO;
	cp->p_size = USIZE;
	cp->p_compatflags = pp->p_compatflags;
	cp->p_stack = pp->p_stack;	/* Inherit stack base */
	cp->p_sr = pp->p_sr;

	/* link up to parent-child-sibling chain---
	 * no need to lock generally since only a free proc call
	 * (done by same parent as newproc) diddles with child chain.
	 */
	cp->p_sibling = pp->p_child;
	cp->p_parent = pp;
	pp->p_child = cp;

	/*
	 * make duplicate entries
	 * where needed
	 */

	for(n=0; n<NOFILE; n++)
		if (up->u_ofile[n] != NULL)
			up->u_ofile[n]->f_count++;
	VN_HOLD(up->u_cdir);
	if (up->u_rdir)
		VN_HOLD(up->u_rdir);

	crhold(up->u_cred);

	for (fptr = forkfunc; *fptr; fptr++)
		(**fptr)(cp, pp);

	/* Can't rsav because parent may have to sleep while allocating
	 * the udot for the child.  So ssav now, and copy to rsav in setuctxt.
	 */
	if (save(up->u_ssav))
		return(1);		/* child will return here */

	switch (procdup(cp, pp, sysflag)) {
	case 0:
		/* Successful copy */
		break;
	case -1:
		/* reset all incremented counts */

		pexit(cp);

		/* Could not duplicate or attach region.
		 * clean up parent-child-sibling pointers--
		 * No lock necessary since nobody else could
		 * be diddling with them here.
		 */

		pp->p_child = cp->p_sibling;
		cp->p_parent = NULL;
		cp->p_sibling = NULL;
		cp->p_stat = NULL;
		up->u_error = EAGAIN;
		return(-1);
	}
	cp->p_stat = SRUN;

	setrq(cp);
	up->u_rval1 = cp->p_pid;		/* parent returns pid of child */

	/* have parent give up processor after
	 * its priority is recalculated so that
	 * the child runs first (its already on
	 * the run queue at sufficiently good
	 * priority to accomplish this).  This
	 * allows the dominant path of the child
	 * immediately execing to break the multiple
	 * use of copy on write pages with no disk home.
	 * The parent will get to steal them back
	 * rather than uselessly copying them.
	 */

	runrun = 1;
	return(0);
}

/*
 * Create a duplicate copy of a process
 */
procdup(cp, pp, sysflag)
struct proc *cp, *pp;
{
	register user_t *uservad;

	/*	Allocate a u-block for the child process
	 *	in the kernel virtual space.
	 */
	availrmem -= USIZE;
	availsmem -= USIZE;

	if ((availrmem < tune.t_minarmem) || (availsmem < tune.t_minasmem)) {
		printf("procdup - can't allocate for U-block\n");
		availrmem += USIZE;
		availsmem += USIZE;
		return(-1);
	}

	if ((uservad = (struct user *)pagealloc()) == NULL) {
		availrmem += USIZE;
		availsmem += USIZE;
		return(-1);
	}
	
	/*	Setup child u-block
	 */
	setuctxt(cp, uservad);

	/* A page table for the kid */
	if (sysflag)
		cp->p_root = K_L1tbl;
	else
		if ((cp->p_root = (Dt_t *)ptblalloc(2)) == (Dt_t *)NULL)
		{	pagefree(uservad);
			availrmem += USIZE;
			availsmem += USIZE;
			return(-1);
		}

	/* Now, dup the regions - we do this separately for 24-bit and other
	 *  processes, since the former involves a little more craftiness.
	 * Should time permit, this whole scheme should be revisted and cleaned up.
	 */
	if (pp->p_flag&SMAC24)
	{	if (dupproc24(pp, cp) < 0)
		{	pagefree(uservad);
			availrmem += USIZE;
			availsmem += USIZE;
			return(-1);
		}
	} else
	{	if (dupproc(pp, cp) < 0)
		{	pagefree(uservad);
			availrmem += USIZE;
			availsmem += USIZE;
			return(-1);
		}
	} 

	/*	Flush ATB.  Copy-on-write page permissions
	 *	have changed from RW to RO
	 */
	clratb(USRATB);
	return(0);
}

/*
 * Dup a 24-bit toolbox process
 *  The code here is similar to dupproc(), but in order to avoid constant testing
 *  in the normal case (is this a 24-bit proc?), we (almost) duplicate the code.
 */
dupproc24(pp, cp)
register struct proc *pp, *cp;
{	register preg_t	*p_prp;
	register preg_t	*c_prp;
	register reg_t *rp;
	register int proc_flag, preg_flag, prot;

	/*	Duplicate all the regions of the process.
	 */
	p_prp = pp->p_region;
	c_prp = cp->p_region;
	proc_flag = pp->p_flag&(SMAC24|SROOT24|SROOT32);

	/*
	 * First, look for regions that are both 24- and 32-bit.  These will
	 *  be duped into what will become the 24-bit tree, and then "check_shape"d
	 *  into a 32-bit tree.
	 */
	for ( ; p_prp->p_reg; p_prp++)
	{	if ((p_prp->p_flags&(PF_MAC24|PF_MAC32)) == (PF_MAC24|PF_MAC32))
		{	prot = (p_prp->p_flags & PF_RDONLY ? Lx_RO : Lx_RW);
			reglock(p_prp->p_reg);

			if ((rp = dupreg(p_prp->p_reg)) == NULL)
			{	regrele(p_prp->p_reg);
				if (c_prp > cp->p_region)
				{	do
					{	c_prp--;
						reglock(c_prp->p_reg);
						detachreg(c_prp, cp);
					} while(c_prp > cp->p_region);
				}
				return(-1);
			}
			if ((c_prp = attachreg(rp, cp, p_prp->p_regva,
				      p_prp->p_type, prot)) == NULL)
			{	freereg(rp);

				if (rp != p_prp->p_reg) {
					regrele(p_prp->p_reg);

					/* Note that we don't want to
					 ** do a VN_RELE(vp) here since
					 ** rp will have had the same
					 ** vp value and the freereg
					 ** will have unlocked it.
					 */
				}
				if (c_prp > cp->p_region) {
					do {
						c_prp--;
						reglock(c_prp->p_reg);
						detachreg(c_prp, cp);
					} while(c_prp > cp->p_region);
				}
				return(-1);
			}
			regrele(p_prp->p_reg);
			if (rp != p_prp->p_reg)
				regrele(rp);
		}
	}

	/*
	 * If this fails, have already set up part of the folded table, so
	 *  we shred it explicitly.
	 */
	if (u.u_error = check_shape(cp))
	{	shred_24bit(cp, 0);
		if (c_prp > cp->p_region)
		{	do
			{	c_prp--;
				reglock(c_prp->p_reg);
				detachreg(c_prp, cp);
			} while(c_prp > cp->p_region);
		}
		return(-1);
	}
	/*
	 * Continue where we left off, this time looking for, and duping, those
	 *  regions that are either 24- or 32-bit, but not both.  These will be
	 *  duped in the appropriate environment.
	 */
	p_prp = pp->p_region;
	for ( ; p_prp->p_reg; p_prp++)
	{	register int		prot;
		register reg_t		*rp;

		preg_flag = p_prp->p_flags&(PF_MAC24|PF_MAC32);
		if (preg_flag == (PF_MAC24|PF_MAC32))
			continue;
		prot = (p_prp->p_flags & PF_RDONLY ? Lx_RO : Lx_RW);
		reglock(p_prp->p_reg);

		if ((rp = dupreg(p_prp->p_reg)) == NULL)
		{	regrele(p_prp->p_reg);
			shred_24bit(cp, 0);
			if (c_prp > cp->p_region)
			{	do
				{	c_prp--;
					reglock(c_prp->p_reg);
					detachreg(c_prp, cp);
				} while(c_prp > cp->p_region);
			}
			return(-1);
		}

		if (preg_flag == PF_MAC32)
		{	cp->p_flag = (cp->p_flag|SROOT32) & ~SROOT24;
			cp->p_root = cp->p_root32;
		} else		/* Must be PF_MAC24 */
		{	cp->p_flag = (cp->p_flag|SROOT24) & ~SROOT32;
			cp->p_root = cp->p_root24;
		}
		if ((c_prp = attachreg(rp, cp, p_prp->p_regva,
			     p_prp->p_type, prot)) == NULL)
		{	freereg(rp);

			if (rp != p_prp->p_reg)
			{	regrele(p_prp->p_reg);

				/* Note that we don't want to
				** do a VN_RELE(vp) here since
				** rp will have had the same
				** vp value and the freereg
				** will have unlocked it.
				*/
			}
			shred_24bit(cp, 0);
			if (c_prp > cp->p_region)
			{	do
				{	c_prp--;
					reglock(c_prp->p_reg);
					detachreg(c_prp, cp);
				} while(c_prp > cp->p_region);
			}
			return(-1);
		}
		regrele(p_prp->p_reg);
		if (rp != p_prp->p_reg)
			regrele(rp);
	}
}

/*
 * Dup a real A/UX or 32-bit Toolbox process
 */
dupproc(pp, cp)
register struct proc *pp, *cp;
{	register preg_t	*p_prp;
	register preg_t	*c_prp;

	/*	Duplicate all the regions of the process.
	 */
	p_prp = pp->p_region;
	c_prp = cp->p_region;

	/*
	 * Note that c_prp is actually allocated in attachreg; we can use the
	 *  ++ trick here because we know in this case which prp is going to
	 *  be returned.
	 */
	for ( ; p_prp->p_reg; p_prp++, c_prp++) {
		register int		prot;
		register reg_t		*rp;

		prot = (p_prp->p_flags & PF_RDONLY ? Lx_RO : Lx_RW);
		reglock(p_prp->p_reg);

		if ((rp = dupreg(p_prp->p_reg)) == NULL) {
			regrele(p_prp->p_reg);
			if (c_prp > cp->p_region) {
				do {
					c_prp--;
					reglock(c_prp->p_reg);
					detachreg(c_prp, cp);
				} while(c_prp > cp->p_region);
			}
			return(-1);
		}

		if (attachreg(rp, cp, p_prp->p_regva,
			     p_prp->p_type, prot) == NULL) {
			freereg(rp);

			if (rp != p_prp->p_reg) {
				regrele(p_prp->p_reg);

				/* Note that we don't want to
				** do a VN_RELE(vp) here since
				** rp will have had the same
				** vp value and the freereg
				** will have unlocked it.
				*/
			}
			if (c_prp > cp->p_region) {
				do {
					c_prp--;
					reglock(c_prp->p_reg);
					detachreg(c_prp, cp);
				} while(c_prp > cp->p_region);
			}
			return(-1);
		}
		regrele(p_prp->p_reg);
		if (rp != p_prp->p_reg)
			regrele(rp);
	}
}

/*
 * Set up context of child process - note that the caller must set up
 *  a root pointer for the child process before it can be resumed (started).
 */
setuctxt(p, up)
register struct proc *p;	/* child proc pointer */
register struct user *up;	/* child u-block pointer */
{
	register pte_t *pt;
	register int i;

	/* Copy u-block, without actual kernel stack
	 */
	i = (((int)u.u_stack - (int)&u) + 511) / 512;
	blt512(up, &u, i);

	/* Copy valid portion of stack */
	i = ((int)&u + ptob(USIZE)) - up->u_ssav[11];
	bcopy(u.u_ssav[11], (int)up + ptob(USIZE) - i, i);

	/*
	 * Set up for page tables, U block for child.
	 * Put the entry in the proc table for later use (by resume()).
	 */
	wtpte(&p->p_uptbl, pnum(up), Lx_RW);
	up->u_procp = p;
	p->p_addr = (caddr_t)up;

	/* Child was saved in ssav, but will be resumed on rsav.
	 */
	bcopy((caddr_t)up->u_ssav, (caddr_t)up->u_rsav, sizeof(label_t));
}

/* <@(#)fork.c	2.3> */
