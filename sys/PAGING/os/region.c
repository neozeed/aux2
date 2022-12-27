#ifndef lint	/* .../sys/PAGING/os/region.c */
#define _AC_NAME region_c
#define _AC_NO_MAIN "@(#) Copyright (c) 1985-87 UniSoft Corporation, All Rights Reserved.  {Apple version 2.11 90/04/19 11:02:47}"
#include <apple_notice.h>

#ifdef _AC_HISTORY
  static char *sccsid = "@(#)Copyright Apple Computer 1989\tVersion 2.11 of region.c on 90/04/19 11:02:47";
#endif		/* _AC_HISTORY */
#endif		/* lint */

#define _AC_MODS

/*	@(#)region.c	UniPlus VVV.2.1.17	*/

#ifdef HOWFAR
extern int T_region;
extern int T_availmem;
#endif HOWFAR

#ifdef lint
#include "sys/sysinclude.h"
#else lint
#include "sys/types.h"
#include "sys/mmu.h"
#include "sys/param.h"
#include "sys/page.h"
#include "sys/systm.h"
#include "sys/sysmacros.h"
#include "sys/pfdat.h"
#include "sys/signal.h"
#include "sys/dir.h"
#include "sys/time.h"
#include "sys/user.h"
#include "sys/errno.h"
#include "sys/vnode.h"
#include "sys/var.h"
#include "sys/buf.h"
#include "sys/debug.h"
#include "sys/region.h"
#include "sys/proc.h"
#include "sys/uio.h"
#include "sys/vfs.h"
#include "sys/tuneable.h"
#endif lint

preg_t	nullpregion;
int	rlist_lock;

void
reginit()
{
	register reg_t *rp;

	rfree.r_forw = &rfree;
	rfree.r_back = &rfree;

	ractive.r_forw = &ractive;
	ractive.r_back = &ractive;

	for (rp = region; rp < &region[v.v_region]; rp++) {
		rp->r_back = rfree.r_back;
		rp->r_forw = &rfree;
		rfree.r_back->r_forw = rp;
		rfree.r_back = rp;
	}
}

reglock(rp)
register reg_t *rp;
{
	register int s;

	s = splhi();
	while (rp->r_lock) {
		rp->r_lock |= 1;
		sleep(&rp->r_lock, PZERO);
	}
	rp->r_lock = ((int)u.u_procp);
	splx(s);
}

regrele(rp)
register reg_t *rp;
{
	ASSERT(rp->r_lock);
	if (rp->r_lock&1)
		wakeup(&rp->r_lock);
	rp->r_lock = 0;
}

rlstlock()
{
	register int s;

	s = splhi();
	while (rlist_lock) {
		rlist_lock |= 1;
		sleep(&rlist_lock, PZERO);
	}
	rlist_lock = ((int) u.u_procp);
	splx(s);
}

rlstunlock()
{
	ASSERT(rlist_lock);
	if (rlist_lock&1)
		wakeup(&rlist_lock);
	rlist_lock = 0;
}

/*
 * Allocate a new region.
 * Returns a locked region pointer or NULL on failure
 * The region is linked into the active list.
 */

reg_t *
allocreg(vp, type)
register struct vnode	*vp;
{
	register reg_t *rp;

	rlstlock();

	if ((rp = rfree.r_forw) == &rfree) {
		rlstunlock();
		printf("Region table overflow\n");
		u.u_error = ENOMEM;
		return(NULL);
	}
	/*
	 * Remove from free list
	 */
	rp->r_back->r_forw = rp->r_forw;
	rp->r_forw->r_back = rp->r_back;

	/* Initialize region fields and bump vnode reference
	 * count. Vnode is locked by the caller
	 */
	rp->r_type = type;
	rp->r_vptr = vp;
	reglock(rp);

	if (vp != NULL)
		VN_HOLD(vp);

	/*
	 * Link onto active list
	 */
	rp->r_forw = ractive.r_forw;
	rp->r_back = &ractive;
	ractive.r_forw->r_back = rp;
	ractive.r_forw = rp;

	rlstunlock();
	return(rp);
}

/*
 * Free an unused region table entry.
 */
void
freereg(rp)
register reg_t *rp;	/* pointer to a locked region */
{	register int		i, lim, size;
	register pte_t		*pt;
	register pte_t		**lp;
	register int		tsize;
	register struct vnode	*vp;

	ASSERT(rp->r_lock);

	/* If the region is still in use, then don't free it.
	 */
	if (rp->r_refcnt != 0)
	{	regrele(rp);
		return;
	}

	/*
	 * Decrement use count on associated vnode
	 * Vnode is locked by the caller.
	 */
	if (vp = rp->r_vptr)
	{	if (rp->r_type == RT_STEXT)
			vp->v_flag &= ~(VTEXT|VTEXTMOD);
		VN_RELE(vp);
	}

	/*	Free the memory pages and associated page tables.
	 *	Note that each page table has an associated disk map
	 *	immediately following it.
	 */
	tsize = rp->r_pgsz;
	lim = ptoL2(tsize);

	if (rp->r_stack)
	{	lp = &rp->r_plist[NPTPBLK*rp->r_plistsz - 1];
		for (i = 0; i < lim; i++)
		{	pt = *lp;
			if ((rp->r_type & RT_PHYSCALL) == 0)
			{	/* Compute remaining size, and therefore,
				 * number in current block
				 */
				size = tsize - L2topg(i);
				if (size > NPGPT)
					size = NPGPT;
				pt += (NPGPT - size);

				while (size--)
				{	pfree(rp, pt, dbdget(pt));
					pt++;
				}
			}
			ptblfree(*lp--, 2);
		}
	} else
	{	lp = &rp->r_plist[0];
		for (i = 0; i < lim; i++)
		{	pt = *lp;
			if ((rp->r_type & RT_PHYSCALL) == 0)
			{	size = tsize - L2topg(i);
				if (size > NPGPT)
					size = NPGPT;

				while (size--)
				{	pfree(rp, pt, dbdget(pt));
					pt++;
				}
			}
			ptblfree(*lp++, 2);
		}
	}
	if ((rp->r_type & RT_PHYSCALL) == 0)
	{	availsmem += tsize;
		ASSERT(rp->r_noswapcnt >= 0  &&	 rp->r_noswapcnt <= 1);
		if (rp->r_noswapcnt)
			availrmem += tsize;
	}

	/*
	 * Free the list.
	 */
	ptblfree(rp->r_plist, rp->r_plistsz);

	/*
	 * Remove from active list
	 * and clean up region fields.
	 */
	rlstlock();

	rp->r_back->r_forw = rp->r_forw;
	rp->r_forw->r_back = rp->r_back;

	rp->r_flags = 0;
	rp->r_plistsz = 0;
	rp->r_pgsz = 0;
	rp->r_ptcount = 0;
	rp->r_stack = 0;
	rp->r_nvalid = 0;
	rp->r_type = 0;
	rp->r_filesz = 0;
	rp->r_noswapcnt = 0;
	rp->r_plist = NULL;

	regrele(rp);

	/*
	 * Link into free list
	 */
	rp->r_forw = rfree.r_forw;
	rp->r_back = &rfree;
	rfree.r_forw->r_back = rp;
	rfree.r_forw = rp;

	rlstunlock();
}

/*
 * Attach a region to a process' address space
 */
preg_t *
attachreg(rp, p, vaddr, type, prot)
register reg_t	*rp;	/* pointer to region to be attached */
register proc_t	*p;	/* pointer to proc entry */
register caddr_t vaddr;	/* virtual address to attach at */
register int type;	/* Type to make the pregion. */
register int prot;	/* permissions for segment table entries. */
{
	register preg_t *prp;


	ASSERT(rp->r_lock);

	/* Check attach address. A stack segment's virtual address
	 *  won't be aligned.  All others must be page-table (L2 entry)
	 *  aligned.
	 */
	if (type != PT_STACK && L2off((int)vaddr))
	{	u.u_error = EINVAL;
		return(NULL);
	}

	/*
	 * If we're a 24-bit process, in 24-bit mode, verify that the attachment
	 *  point is a valid 24-bit address (alas, this doesn't work during
	 *  exec(), since we haven't set SROOT24 yet).
	 */
	if ((p->p_flag&(SMAC24|SROOT24)) == (SMAC24|SROOT24) &&
	    (unsigned int)vaddr >= 0x1000000)
	{	u.u_error = EINVAL;
		return(NULL);
	}

	/*	Allocate a pregion.  We should always find a
	 *	free pregion because of the way the system
	 *	is configured.
	 */
	if ((prp = findpreg(p, PT_UNUSED)) == NULL)
	{	u.u_error = EMFILE;
		return(NULL);
	}

	/*	init pregion
	 */
	prp->p_reg = rp;
	prp->p_regva = vaddr;
	prp->p_type = type;

	if (prot == Lx_RO)
		prp->p_flags |= PF_RDONLY;
	if (type == PT_STACK)
		rp->r_stack = 1;
	else
		rp->r_stack = 0;

	/*	Check that region does not go beyond end of virtual
	 *	address space.
	 */
	if (chkgrowth(p, prp, 0, rp->r_pgsz))
	{	/* Region beyond end of address space.
		 * Undo what has been done so far
		 */
		*prp = nullpregion;
		u.u_error = EINVAL;
		return(NULL);
	}

	/*
	 * Load the page tables if the region is not null
	 */
	if (rp->r_pgsz)
		loadptbls(p, prp, 0);

	rp->r_refcnt++;
	p->p_size += rp->r_pgsz;

	/*
	 * If we are a 24-bit process, record the current world view in
	 * the pregion flags.
	 * Note that we set 32-bit mode if not 24-bit so that for text,
	 *  data+bss, stack regions, we can easily record them as both
	 *  (since these pregions are implicitly shared rather than being
	 *  attached twice).  See getxfile() (the root is not set until after
	 *  these segments are attatched).
	 */
	if (p->p_flag&SMAC24)
	{	if (p->p_flag&SROOT24)
			prp->p_flags |= PF_MAC24;
		else
			prp->p_flags |= PF_MAC32;
	}

	return(prp);
}

/*
 * Detach a region from the current process' address space
 */
void
detachreg(prp, p)
register preg_t *prp;
register proc_t *p;
{	register reg_t	*rp;
	register int	i, j;
	register Dt_t	*L2p;
	register unsigned int sp;

	rp = prp->p_reg;

	ASSERT(rp);
	ASSERT(rp->r_lock);

	/*
	 * Invalidate L2 page table entries pointing at the region:
	 *  since attach at pt boundary, can just stomp on L2 entries.
	 */
	sp = (unsigned int)prp->p_regva;
	j = ptoL2(rp->r_pgsz);
	for (i = 0; i < j; i++, sp += L2tob(1))
	{	L2p = L2_ent(p->p_root, sp);
		Dt_invalidate(L2p);
	}

	/*	Decrement process size by size of region.
	 */
	p->p_size -= rp->r_pgsz;

	/*
	 * Decrement use count and free region if zero
	 * and RG_NOFREE is not set, otherwise unlock.
	 */
	if (--rp->r_refcnt == 0 && !(rp->r_flags & RG_NOFREE))
		freereg(rp);
	else
		regrele(rp);

	/* Clear out the pregion of the region we just detached.
	 */
	for (i = prp - p->p_region; i < pregpp-1; i++, prp++)
		*prp = *(prp+1);
	*prp = nullpregion;
}

/*
 * Duplicate a region
 */
reg_t *
dupreg(rp)
register reg_t *rp;
{
	register unsigned int i, j;
	register unsigned int size;
	register unsigned int endix, count;
	register pte_t	*ppte, *cpte;
	register reg_t	*rp2;
	extern pte_t	*kvalloc();


	ASSERT(rp->r_lock);

	/* If region is shared, there is no work to do.
	 * Just return the passed region.  The region reference
	 * counts are incremented by attachreg().
	 */
	if (rp->r_type != RT_PRIVATE)
		return(rp);

	availsmem -= rp->r_pgsz;	/* Reserve space for the new region */
					/*  in case all pages are split off */
					/*  due to copy-on-write */
	if (availsmem < tune.t_minasmem)
	{	availsmem += rp->r_pgsz;
		u.u_error = EAGAIN;
		return(NULL);
	}

	/*
	 * Need to copy the region.
	 * Allocate a region descriptor
	 */
	if ((rp2 = allocreg(rp->r_vptr, rp->r_type)) == NULL)
	{	availsmem += rp->r_pgsz;
		u.u_error = EAGAIN;
		return(NULL);
	}

	/*	Allocate a list for the new region.
	 */
	rp2->r_plistsz = rp->r_plistsz;
	rp2->r_plist = (pte_t **)ptblalloc(rp2->r_plistsz);

	if (rp2->r_plist == (pte_t **)NULL)
	{	rp2->r_plistsz = 0;
		rp2->r_plist = NULL;
		availsmem += rp->r_pgsz;
		freereg(rp2);
		u.u_error = EAGAIN;
		return(NULL);
	}

	/*
	 * Copy pertinent data to new region
	 */
	rp2->r_pgsz = rp->r_pgsz;
	rp2->r_ptcount = rp->r_ptcount;
	rp2->r_filesz = rp->r_filesz;
	rp2->r_nvalid = rp->r_nvalid;
	rp2->r_stack = rp->r_stack;

	/* Scan the parents page table list and fix up each page table.
	 * Allocate a page table and map table for the child and
	 *  copy it from the parent.
	 * `Count' is computed to allow us to treat stacks and other regions
	 *  the same when determining "size" of each chunk.  For stacks, it
	 *  appears as if r_plist is full.  `j' gives the start.
	 */
	if (rp->r_stack)
	{	endix = rp->r_plistsz*NPTPBLK;
		i = endix - rp->r_ptcount;
		j = NPGPT - (rp->r_pgsz%NPGPT);
		count = endix*NPGPT;
	} else
	{	i = j = 0;
		endix = rp->r_ptcount;
		count = rp->r_pgsz;
	}
	for (; i < endix; i++)
	{	if ((cpte = ptblalloc(2)) == (pte_t *)NULL)
		{	rp2->r_pgsz = L2topg(i);
			freereg(rp2);
			availsmem += rp->r_pgsz;
			u.u_error = EAGAIN;
			return(NULL);
		}
		rp2->r_plist[i] = cpte;
		cpte += j;
		ppte = rp->r_plist[i] + j;

		if ((int)(size = count - L2topg(i)) > NPGPT)
			size = NPGPT;

		for (; j < size; j++, cpte++, ppte++)
		{	dbd_t	map;

			if (!(ppte->pgi.pg_pte&PG_RO))
			{	pg_setprot(ppte, PG_RO);
				pg_setcw(ppte);
			}
			*cpte = *ppte;

			if (ppte->pgm.pg_v)
			{	struct pfdat *pfd;

				pfd = pftopfd(ppte->pgm.pg_pfn);

				ASSERT(pfd->pf_use > 0); 
				pfd->pf_use++;
			}
			map = *(dbdget(ppte));

			if (map.dbd_type == DBD_SWAP)
			{	ASSERT(swpuse((dbd_t *)&map) != 0);
				if (!swpinc(((dbd_t *)&map), "dupreg"))
				{	(dbdget(cpte))->dbd_type = DBD_NONE;
					freereg(rp2);
					availsmem += rp->r_pgsz;
					u.u_error = EAGAIN;
					return(NULL);
				}
			}
			*(dbdget(cpte)) = map;
		}
		j = 0;
	}
	return(rp2);
}

/*
 * Change the size of a region
 *  change == 0	 -> no-op
 *  change  < 0	 -> shrink
 *  change  > 0	 -> expand
 * For expansion, you get (fill) real pages (change-fill) demand zero pages
 * For shrink, the caller must flush the ATB
 * Returns 0 on no-op, -1 on failure, and 1 on success.
 */
growreg(prp, change, type)
register preg_t *prp;
int change, type;
{
	register pte_t	*pt;
	register unsigned int	i;
	register reg_t	*rp;
	register unsigned int size, osize;
	register unsigned int endix;
	register unsigned int nsize;
	register unsigned int offset;

	rp = prp->p_reg;

	ASSERT(rp->r_noswapcnt >=0);

	if (change == 0)
		return(0);
	osize = rp->r_pgsz;
	if ((int)(nsize = ((int) osize + change)) < 0)
		nsize = 0;

	if (change < 0)
	{	/*	The region is being shrunk.  Compute the new
		 *	size and free up the unneeded space.
		 */
		if ( !(rp->r_type & RT_PHYSCALL))
		{	availsmem -= change;
			if (rp->r_noswapcnt)
				availrmem -= change;
		}
#ifdef PRE_RELEASE
		if (rp->r_stack)
			panic("Shrinking stack!");
		else
#endif
		{	i = ptotL2(nsize);
			endix = ptoL2(osize);
		}

		offset = nsize % NPGPT;
		if ((int)(size = osize - L2topg(i)) > NPGPT)
			size = NPGPT;
		size -= offset;
		pt = rp->r_plist[i];
		pt += offset;

		for (;;)
		{	/* Free up the allocated pages for this Page Table */

			while (size-- > 0)
			{	pfree(rp, pt, dbdget(pt));
				pt++;
			}

			if (++i >= endix)
				break;

			pt = rp->r_plist[i];
			if ((int)(size = osize - L2topg(i)) > NPGPT)
				size = NPGPT;
		}

		/*	Free up the page tables which we no
		 *	longer need.
		 */
		(void) ptexpand(rp, change);
	} else
	{	/*	We are expanding the region.  Make sure that
		 *	the new size is legal and then allocate new
		 *	page tables if necessary.
		 */
		if ( !(rp->r_type & RT_PHYSCALL))
		{	availsmem -= change;
			if (availsmem < tune.t_minasmem)
			{	availsmem += change;
				u.u_error = EAGAIN;
				return(-1);
			}

			if (rp->r_noswapcnt)
			{	availrmem -= change;
				if (availrmem < tune.t_minarmem)
				{	availrmem += change;
					availsmem += change;
					u.u_error = EAGAIN;
					return(-1);
				}
			}
		}

		if (chkgrowth(u.u_procp, prp, osize, nsize) ||
		    ptexpand(rp, change))
		{	u.u_error = ENOMEM;

			if ( !(rp->r_type & RT_PHYSCALL))
			{	availsmem += change;
				if (rp->r_noswapcnt)
					availrmem += change;
			}
			return(-1);
		}
		/* update attach point for stack after ptexpand() */
		if (rp->r_stack)
			prp->p_regva = (caddr_t)u.u_procp->p_stack - ptob(rp->r_pgsz + change);

		/*
		 * Initialize new page tables and allocate pages as required.
		 * May be called with current size == change == 0: no op.
		 * For stack, just reverse sense of old, new sizes after
		 *  taking them away from the total size.
		 */
		if (nsize > 0)
		{	if (rp->r_stack)
			{	register unsigned int tsize;

				/* First, determine # page tables in plist */
				tsize = rp->r_plistsz*NPTPBLK*NPGPT;
				i = tsize - nsize;
				nsize = tsize - osize;
				osize = i;
			}
			endix = ptoL2(nsize);
			i = ptotL2(osize);
			offset = osize % NPGPT;
			if ((int)(size = nsize - L2topg(i)) > NPGPT)
				size = NPGPT;
			size -= offset;
			pt = rp->r_plist[i];
			pt += offset;
			for (;;)
			{	/* Init a chunk of pte's */
				while ((int)(--size) >= 0)
				{	/*
					 * pg_zero() isn't needed, since these
					 *  are allocated by ptblalloc() which
					 *  clears them.
					 * Also, R/W protection is zero, and
					 *  pg_setprot() will generate
					 *  extraneous code, so don't do it.
					 * pg_zero(pt);
					 * pg_setprot(pt, PG_RW);
					 */
					(dbdget(pt))->dbd_type = type;
					pt++;
				}
				if (++i >= endix)
					break;
				
				pt = rp->r_plist[i];
				if ((int)(size = nsize - L2topg(i)) > NPGPT)
					size = NPGPT;
			}
		}
	}
	/* Now, make sure proc's page table reflects new growth */
	loadptbls(u.u_procp, prp, change);

	rp->r_pgsz += change;
	u.u_procp->p_size += change;
	return(1);
}

/*
 * Check that grow (increase size) of a pregion is legal:
 *  The growth must remain within the region, and not overlap an existing one.
 *  Note that there is no inherent max size for a region.
 */
chkgrowth(p, prp, osize, nsize)
register struct proc *p;
register preg_t	*prp;
int	osize;	/* Old size in pages. */
int	nsize;	/* New size in pages. */
{	register Dt_t	*dt;
	register caddr_t loaddr;
	register int i1, i2, j1, j2;
	int change;
	register Dt_t *L2p;
	register caddr_t hiaddr;

	dt = p->p_root;
	/* If this is a null region, make sure that we are not trying to
	 *  attach on top of an already attached region (indicated by a
	 *  valid L2 entry).
	 */
	if (nsize == 0)
	{	loaddr = prp->p_regva;
		if (prp->p_reg->r_stack)
			loaddr--;
		if (Lx_valid(L2_ent(dt, loaddr)))
			return(-1);
		else
			return(0);
	}

	change = ptob(nsize - osize);	/* Always >= 0 */
	/*
	 * Check L2 entries first for overlap with another region.
	 * Ignore the L2 entry for the existing piece of address space
	 *  (if any) by rounding up the low addr or truncating down the
	 *  high addr.
	 */
	if (prp->p_reg->r_stack)
	{	hiaddr = prp->p_regva;
		loaddr = hiaddr - change;
		hiaddr = (caddr_t)Pt_trunc(hiaddr);
	} else
	{	loaddr = prp->p_regva + ptob(osize);
		hiaddr = loaddr + change;
		loaddr = (caddr_t)Pt_round(loaddr);
	}

	/* Not expanding into new "segment" */
	if (loaddr >= hiaddr)
		return(0);

	/*
	 * Make sure that no new Page Tables are part of another region.
	 * Since regions attach on PT boundaries, we need only check L2 ptrs.
	 */
	i1 = L1ix(loaddr);
	i2 = L1ix(hiaddr-1);
	for (; i1 <= i2; i1++)
	{	j1 = L2ix(loaddr);
		j2 = min((unsigned int)hiaddr-1, L2_round(loaddr+1));
		j2 = L2ix(j2);
		L2p = L2_ptr(dt, loaddr);
		if ((int) L2p <= 0)
			continue;	/* No L2 table this time */
		L2p = &L2p[j1];		/* Compute addr of 1st Pt */
		for (; j1++ <= j2; L2p++)
		{	L2p = L2_ent(dt, loaddr);
			if (Lx_valid(L2p))
				return(-1);
			loaddr += pttob(1);
		}
	}

	return(0);
}

/*
 * Update L1/L2 tables for (new) region size (Page Tables have already been
 * set up with the region).
 * Not called for shared regions or for shrinking stack (can't happen).
 * May be called with change == 0 to set protections and set up tables.
 * If we can't get a page table at this point, just commit suicide.
 *  God will straighten things out.
 * For 24-bit procs, if the Pregion is schizo, make sure we do the job for
 *  both 24- and 32-bit trees.
 */
loadptbls(p, prp, change)
register struct proc *p;
register preg_t		*prp;
register int	change;
{	register int fold_table;

	/* Assure table folding if in 24-bit mode */
	fold_table = ((p->p_flag&(SMAC24|SROOT24)) == (SMAC24|SROOT24));
	loadtree(p, p->p_root, prp, change, fold_table);
	/* If pregion swings both ways, assure that both trees are updated */
	if ((prp->p_flags&(PF_MAC24|PF_MAC32)) == (PF_MAC24|PF_MAC32))
	{	/* Fold only if we didn't fold before */
		loadtree(p, p->p_flag&SROOT24 ? p->p_root32 : p->p_root24,
			 prp, change, ~fold_table);
	}
}

/*
 * Load specified page table tree
 */
loadtree(p, dt, prp, change, fold_table)
struct proc *p;
Dt_t	*dt;
preg_t		*prp;
int	change, fold_table;
{	register Dt_t *L2p;
	register caddr_t loaddr, hiaddr;
	register int i1, i2, j1, j2;
	register int	prot;
	caddr_t regva;
	reg_t	*rp;

	rp = prp->p_reg;
	prot = prp->p_flags & PF_RDONLY ? Lx_RO : Lx_RW;
	regva = prp->p_regva;
	if (change < 0)
	{	j2 = rp->r_pgsz;
		j1 = j2+change;
		if (ptopt(j1) < ptopt(j2))
		{	hiaddr = regva + ptob(rp->r_pgsz);
			change = -change;
			loaddr = hiaddr - ptob(change);
			i1 = L1ix(loaddr);
			i2 = L1ix(hiaddr);

			/* ASSERT - all pointers are valid */
			for (; i1 < i2; i1++)
			{	L2p = L2_ent(dt, loaddr);
				j2 = ptoL2(change);
				j1 = NL2TBL - L2ix(loaddr);
				if (j2 > j1)
					j2 = j1;
				/* If L2 table is empty, should get rid of it */
				for (j1=0; j1++ < j2; L2p++)
				{	Dt_invalidate(L2p);
					if (fold_table)
						Dt_invalidate(L2p+NL2TBL/2);
				}
				loaddr = (caddr_t)L2_round(loaddr+1);
				change -= j2;
			}
		}
	} else
	{	register pte_t **lp;
		register Dt_t *L1p, *L2q;
		register int ptix;

		/*
		 * `Change' could be == 0; run through the full table, to
		 *   assure protections are set up.
		 * Assume the plist is already expanded.
		 */
		/* Compute plist indices */
		if (rp->r_stack)
		{	hiaddr = (caddr_t)(p->p_stack-1);
			loaddr = regva;
			ptix = NPTPBLK*rp->r_plistsz -
				ptopt(rp->r_pgsz+change);
		} else
		{	loaddr = regva;
			hiaddr = loaddr + ptob(rp->r_pgsz+change) - 1;
			ptix = 0;
		}
		lp = &rp->r_plist[ptix];
		/* Do this once per L2 table */
		i1 = 0;
		i2 = ptoL1(rp->r_pgsz+change);
		while (i1++ < i2)
		{	L1p = L1_ent(dt, loaddr);
			/* allocate an L2 table if needed */
 			if (!Lx_valid(L1p))
			{	if (((int)L1p > 0) && (L2p = DT_addr(L1p)))
					L2p->Dtm.Dt_dt = Lx_VALID;
				else
				{	regrele(rp);
					L2p = (Dt_t *)ptblalloc(2);
					if (!L2p)
					{	psignal(u.u_procp, SIGKILL);
						return;
					}
					reglock(rp);
					wtl1e(L1p, L2p, Lx_RW);
				}
			}
			L2p = L2_ent(dt, loaddr);
			/* Point to the middle of the L2 table (for 24-bit stuff) */
			L2q = L2p + (NL2TBL/2);
			/* Now, fill in pt ptrs from region for this L2 */
			j1 = L2ix(loaddr);
			j2 = min((unsigned int)hiaddr, L2_round(loaddr+1)-1);
			j2 = L2ix(j2);
			while (j1++ <= j2)
			{	wtl2e(L2p++, *lp++, prot);
				if (fold_table)
					*L2q++ = L2p[-1];
				loaddr += L2tob(1);
			}
		}
	}
}

/*
 * Expand user page tables for a region 
 */
ptexpand(rp, change)
reg_t		*rp;
{	register pte_t **lp, **lp1, **lp2;
	register unsigned int	osize;
	register int	nsize;
	register int i1, i2;
	extern pte_t	*ptblalloc();

	/* Calculate the new size in pages.
	 */

	osize = rp->r_pgsz;
	nsize = osize + change;

	/*	If we are shrinking the region, then free page and map
	 *	tables (pages should already be free'd).
	 *	Free the plist if shrinking to zero.
	 */
	if (ptopt(nsize) < ptopt(osize))
	{	i1 = rp->r_ptcount - ptopt(-change);
		i2 = rp->r_ptcount-1;

		lp = &rp->r_plist[i1];
		lp2 = &rp->r_plist[i2];
		for ( ; lp <= lp2; lp++)
		{	ptblfree(*lp, 2);
			*lp = 0;
			rp->r_ptcount--;
		}
		if (nsize <= 0)
		{	ptblfree(rp->r_plist, rp->r_plistsz);
			rp->r_plistsz = 0;
			rp->r_plist = (pte_t **)0;
		}
	}

	/*	If the region shrunk, then we are done.
	 */
	if (change <= 0)
		return(0);
	
	/*
	 * Otherwise, the region grew; make sure r_plist is large enough.
	 *
	 * Release this region each time we call ptblalloc().
	 *  To get this memory, we may have to go to sleep and vhand
	 *  may have to steal pages from this region.  The
	 *  vhand routines use r_pgsz to determine the current
	 *  size of the region, and r_pgsz will not be increased
	 *  from its original value until the end of growreg.
	 *
	 * This fixes a deadlock which occurred when trying to
	 *  load a data region with huge data and bss sections.
	 *  In particular, the system previously hung when
	 *  trying to expand the page tables for a huge bss
	 *  area (say 100 Meg), after having used up all of
	 *  physical memory with initialized data.
	 *
	 * If the requested size exceeds the current r_plist size, 
	 *  reallocate and copy; free the old one.
	 */
	i1 = ptopt(nsize);		/* # PT's needed */
	i2 = (i1+NPTPBLK-1)/NPTPBLK;	/* # Plist chunks needed */
	if (i2 > rp->r_plistsz)
	{	register pte_t **ptp;
		register int j;
		register int oldsize;

		if (i1 <= NPTPR)
		{	regrele(rp);
			ptp = (pte_t **)ptblalloc(i2);
			if (!ptp)
			{	psignal(u.u_procp, SIGKILL);
				return;
			}
			reglock(rp);
		} else
		{	u.u_error = E2BIG;
			return(-1);
		}
		if (oldsize = rp->r_plistsz)
		{	if (rp->r_stack)
			{	bcopy(&rp->r_plist[oldsize*NPTPBLK-rp->r_ptcount],
				      &ptp[i1*NPTPBLK-rp->r_ptcount],
				      rp->r_ptcount*sizeof (pte_t *));
			} else
				bcopy(rp->r_plist, ptp,
				      rp->r_ptcount*sizeof (pte_t *));
			ptblfree(rp->r_plist, oldsize);
		}
		rp->r_plist = ptp;
		rp->r_plistsz = i2;
	}

	/*
	 * Allocate a new set of page tables and disk maps (i1 set above).
	 */
	if (rp->r_stack)
	{	i2 = NPTPBLK*rp->r_plistsz - rp->r_ptcount;
		i1 = NPTPBLK*rp->r_plistsz - i1;
	} else
	{	i2 = i1;
		i1 = rp->r_ptcount;
	}
	lp1 = &rp->r_plist[i1];
	lp2 = &rp->r_plist[i2];

	for (lp = lp1; lp < lp2; lp++)
	{	regrele(rp);
		*lp = (pte_t *)ptblalloc(2);
		reglock(rp);

		if (*lp == (pte_t *)NULL)
		{	/*
			 * Release what we've grabbed so far.  This is
			 * necessary because we haven't changed r_pgsz yet.
			 */
			for ( ; lp1 < lp; lp1++)
			{	ptblfree(*lp1, 2);
				*lp1 = 0;
				rp->r_ptcount--;
			}
			return(-1);
		}
		rp->r_ptcount++;

	}
	return(0);
}

loadreg(prp, vaddr, vp, off, count)
register preg_t		*prp;
caddr_t			vaddr;
register struct vnode	*vp;
{
	register struct user *up;
	register reg_t	*rp;
	register int	gap;

	/*	Grow the region to the proper size to load the file.
	 */

	up = &u;
	rp = prp->p_reg;
	ASSERT(rp->r_lock);
	gap = vaddr - prp->p_regva;

	if (growreg(prp, (int)btotp(gap), DBD_NONE) < 0) 
		return(-1);
	if (growreg(prp, (int)(btop(count+gap) - btotp(gap)), DBD_DFILL) < 0) 
		return(-1);


	/*	We must unlock the region here because we are going
	 *	to fault in the pages as we read them.	No one else
	 *	will try to use the region before we finish because
	 *	the RG_DONE flag is not set yet.
	 */
	regrele(rp);

	/*	Set up to do the I/O.
	 */
	up->u_error = vn_rdwr(UIO_READ, vp, vaddr, count, off, UIOSEG_USER, IO_UNIT, NULL);
	if (up->u_error)
	{	reglock(rp);
		return(-1);
	}


	/*	Clear the last (unused)	 part of the last page.
	 */
	vaddr += count;
	count = ptob(1) - poff(vaddr);

	if (count > 0 && count < ptob(1))
	{	if (uclear(vaddr, count) == -1)
		{	u.u_error = EFAULT;
			reglock(rp);
			return(-1);
		}
	}
	reglock(rp);
	rp->r_flags |= RG_DONE;

	if (rp->r_flags & RG_WAITING)
	{	rp->r_flags &= ~RG_WAITING;
		wakeup(&rp->r_flags);
	}
	return(0);
}

mapreg(prp, vaddr, vp, off, count)
preg_t	       *prp;
caddr_t		vaddr;
struct vnode   *vp;
register int	off;
int		count;
{
	register int	i;
	register int	j;
	register int	blkspp;
	register int	gap;
	register int	lim;
	register reg_t	*rp;
	register dbd_t	*dbd;
	register pte_t	*pt;
	int		L2lim;

	/*	If the block number list is not built,
	 *	then build it now.
	 */
	if (vp->v_map == 0)
	{	if (mapfile(vp) < 0)
			return(-1);
	}

	/*	Get region pointer and effective device number.
	 */
	rp = prp->p_reg;
	ASSERT(rp->r_lock);

	/*	Compute the number of file system blocks in a page.
	 *	This depends on the file system block size.
	 */
	if ((blkspp = NBPP / vp->v_vfsp->vfs_bsize) == 0)
		blkspp = 1;

	/*	Allocate invalid pages for the gap at the start of
	 *	the region and demand-fill pages for the actual
	 *	text.
	 */
	gap = vaddr - prp->p_regva;
	if (growreg(prp, (int)btotp(gap), DBD_NONE) < 0)
		return(-1);
	if (growreg(prp, (int)(btop(count+gap) - btotp(gap)), DBD_DFILL) < 0)
		return(-1);
	
	rp->r_filesz = count + off;
	
	/*	Build block list pointing to map table.
	 */
	gap = btotp(gap);	    /* Gap in pages. */
	off = btotp(off) * blkspp;  /* File offset in blocks. */
	i = ptotL2(gap);

	for (L2lim = ptoL2(rp->r_pgsz); i < L2lim; i++)
	{	if (gap > L2topg(i))
			j = gap - L2topg(i);
		else
			j = 0;

		if ((lim = rp->r_pgsz - L2topg(i)) > NPGPT)
			lim = NPGPT;

		pt = (pte_t *)rp->r_plist[i] + j;
		dbd = dbdget(pt);

		for ( ; j < lim; j++, pt++, dbd++)
		{	/* If these are private pages, then make
			 * them copy-on-write since they will
			 * be put in the hash table.
			 */
			if (rp->r_type == RT_PRIVATE){
				pg_clrprot(pt);
				pg_setprot(pt, PG_RO);
				pg_setcw(pt);
			}
			dbd->dbd_type  = DBD_FILE;
			dbd->dbd_blkno = off;
			off += blkspp;
		}
	}
	/*
	 * Mark the last page for special handling
	 */
	dbd[-1].dbd_type = DBD_LSTFILE;

	rp->r_flags |= RG_DONE;

	if (rp->r_flags & RG_WAITING)
	{	rp->r_flags &= ~RG_WAITING;
		wakeup(&rp->r_flags);
	}
	return(0);
}

/*	Create the block number list for an vnode.
 */
mapfile(vp)
register struct vnode	*vp;
{
	register int	i;
	register int	blkspp;
	register int	nblks;
	register int	lbsize;
	register int	*bnptr;
	struct vattr	vattr;

	ASSERT(vp->v_map == 0);

	/*
	 * Get number of blocks to be mapped.
	 */
	VOP_GETATTR(vp, &vattr, u.u_cred);
	lbsize = vp->v_vfsp->vfs_bsize;

	if (blkspp = NBPP / lbsize)
		i = lbsize;
	else
	{	blkspp = 1;
		i = NBPP;
	}
	nblks = (vattr.va_size + i - 1) / i;

	/*	Round up the file size in block to an
	 *	integral number of pages.  Allocate
	 *	space for the block number list.
	 */
	i = (((nblks + blkspp - 1) / blkspp) * blkspp) + 1;

	if ((bnptr = (int *)kvalloc(btop(i*sizeof(int *)), PG_ALL, -1)) == (int *)NULL)
	{	u.u_error = ENOMEM;
		return(-1);
	}
	/* reserve slot for file size (rounded up to nearest device block size */
	*bnptr++ = (vattr.va_size + DEV_BSIZE - 1) & ~(DEV_BSIZE - 1);
	vp->v_map = bnptr;
	vp->v_mapsize = i;

	/*	Build the actual list of block numbers
	 *	for the file.
	 */
	bldblklst(bnptr, vp, (vattr.va_size + lbsize - 1) / lbsize);

	/*	If the size is not an integral number of
	 *	pages long, then the last few block
	 *	numbers up to the next page boundary are
	 *	made -1 so that no one will try to
	 *	read them in.  See code in fault.c/vfault.
	 */
	if (blkspp > 1)
	{	bnptr += nblks;

		while (nblks % blkspp)
		{	*bnptr++ = -1;
			nblks++;
		}
	}
	return(0);
}

/*	Find the highest region starting below the given virtual address.
 *	We don't care if it is within the region.
 *
 *	ASSUMPTION: Address has been stripped!
 */
reg_t	*
findreg(p, vaddr)
register struct proc	*p;
register caddr_t	vaddr;
{
	register preg_t	*prp;
	register preg_t	*oprp;

	oprp = 0;
	for (prp = &p->p_region[0] ; prp->p_reg ; prp++) {
		if (vaddr >= prp->p_regva) {
			if (oprp) {
				if (prp->p_regva > oprp->p_regva)
					oprp = prp;
			} else
				oprp = prp;
		}
	}
	if (oprp && vaddr >= oprp->p_regva) {
		reglock(oprp->p_reg);
		return(oprp->p_reg);
	}
	panic("findreg - no match");
	/*NOTREACHED*/
}


/*	Find the pregion of a particular type.  Don't need to worry about
 *	24/32 bittedness, since this is only called to find text/data/stack
 *	pregions which aren't differentiated by 24/32.
 */
preg_t *
findpreg(pp, type)
register proc_t	*pp;
register int	type;
{
	register preg_t	*prp;

	for (prp = pp->p_region ; prp->p_reg ; prp++) {
		if (prp->p_type == type)
			return(prp);
	}

	/*	We stopped on an unused region.	 If this is what
	 *	was called for, then return it unless it is the
	 *	last region for the process.  We leave the last
	 *	region unused as a marker.
	 */

	if ((type == PT_UNUSED) && (prp < &pp->p_region[pregpp - 1]))
		return(prp);
	return(NULL);
}

/*
 * Change protection of ptes for a region
 */
void
chgprot(prp, prot)
preg_t	*prp;
{
	if (prot == Lx_RO)
		prp->p_flags |= PF_RDONLY;
	else
		prp->p_flags &= ~PF_RDONLY;
	loadptbls(u.u_procp, prp, 0);

	clratb(USRATB);
}

/*
 * Locate process region for a given virtual address.  Need to worry about
 * 24/32 bittedness to return the proper pregion - for 24-bit procs, the
 * addr/pregion mapping is ambiguous without looking at the current root.
 *
 * ASSUMPTION: Address has been stripped!
 */
preg_t *
vtopreg(p, vaddr)
register struct proc *p;
register caddr_t vaddr;
{	register int flags;
	register preg_t *prp;
	register reg_t *rp;

	if (p->p_flag&SMAC24)
		flags = p->p_flag&SROOT24 ? PF_MAC24 : PF_MAC32;
	else
		flags = 0;

	for (prp = p->p_region; rp = prp->p_reg; prp++) {
		if (vaddr >= prp->p_regva
		  && vaddr <= (prp->p_regva + ptob(rp->r_pgsz) - 1)
		  && (prp->p_flags&flags) == flags)
			return(prp);
	}
	return(NULL);
}

/* <@(#)region.c	2.11> */
