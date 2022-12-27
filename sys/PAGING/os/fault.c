#ifndef lint	/* .../sys/PAGING/os/fault.c */
#define _AC_NAME fault_c
#define _AC_NO_MAIN "@(#) Copyright (c) 1984-85 AT&T-IS, 1985-87 UniSoft Corporation, All Rights Reserved.  {Apple version 2.5 90/03/21 22:49:29}"
#include <apple_notice.h>

#ifdef _AC_HISTORY
  static char *sccsid = "@(#)Copyright Apple Computer 1989\tVersion 2.5 of fault.c on 90/03/21 22:49:29";
#endif		/* _AC_HISTORY */
#endif		/* lint */

#define _AC_MODS
/*	@(#)fault.c	UniPlus VVV.2.1.6	*/

#ifdef HOWFAR
extern int T_fault;
extern int T_pfault;
#endif HOWFAR
#ifdef lint
#include "sys/sysinclude.h"
#else lint
#include "sys/types.h"
#include "sys/param.h"
#include "sys/reg.h"
#include "sys/psl.h"
#include "sys/mmu.h"
#include "sys/sysmacros.h"
#include "sys/page.h"
#include "sys/systm.h"
#include "sys/dir.h"
#include "sys/signal.h"
#include "sys/time.h"
#include "sys/user.h"
#include "sys/errno.h"
#include "sys/vnode.h"
#include "sys/var.h"
#include "sys/buf.h"
#include "sys/utsname.h"
#include "sys/sysinfo.h"
#include "sys/pfdat.h"
#include "sys/region.h"
#include "sys/proc.h"
#include "sys/map.h"
#include "sys/swap.h"
#include "sys/getpages.h"
#include "sys/vfs.h"
#include "sys/debug.h"
#endif lint

extern int	freemem;


/*	Protection fault handler
 */

pfault(vaddr, pt, prp)
caddr_t vaddr;
register pte_t	*pt;	/* Physical address of faulting pte.	*/
preg_t *prp;
{
	register pfd_t	*pfd;
	register struct proc *p;
	register dbd_t	*dbd;
	register reg_t	*rp;
	pte_t	 *tp;
	pte_t	 temppte;


	/*	Get a pointer to the region which the faulting
	 *	virtual address is in.
	 */
	p = u.u_procp;
	if (prp)
	{	rp = prp->p_reg;
		reglock(rp);
	} else
		rp = findreg(p, vaddr);
	/*	Check to see that the pte hasn't been modified
	 *	while waiting for the lock
	 */
	if (!pt->pgm.pg_v){
		regrele(rp);
		return(1);
	}

	/*	Now check for a real protection error as opposed
	 *	to a copy on write.
	 */
	minfo.pfault++;

	if (!pt->pgm.pg_cw){
		regrele(rp);
		return(0);
	}
	ASSERT(rp->r_type == RT_PRIVATE);

	dbd = dbdget(pt);
	pfd = pftopfd(pt->pgm.pg_pfn);

	/*	Copy on write
	 *	If use is 1, and page is not from a file,
	 *	steal it, otherwise copy it
	 */

	if (pfd->pf_use > 1 || dbd->dbd_type == DBD_FILE ||
			       dbd->dbd_type == DBD_LSTFILE) {
		minfo.cw++;

		/*	We are locking the page we faulted on
		**	before calling ptmemall because
		**	ptmemall may unlock the region.	 If
		**	he does, then the page could be stolen
		**	and we would be copying incorrect
		**	data into our new page.
		*/
		tp = &temppte;
		tp->pgi.pg_pte = 0;	/* This pte has no dbd */

		pg_setlock(pt);
		pfd->pf_rawcnt++;
		ptmemall(rp, tp, 1);

		/*	Its O.K. to unlock the page now since
		**	ptmemall has locked the region again.
		*/
		ASSERT(rp->r_lock);
		ASSERT(pg_locked(pt));
		ASSERT(pfd->pf_rawcnt > 0);
	 
		if (--pfd->pf_rawcnt == 0)
			pg_clrlock(pt);
		copypage((int)pt->pgm.pg_pfn, (int)tp->pgm.pg_pfn);
		pfree(rp, pt, dbd);
		*pt = *tp;
	} else {
		minfo.steal++;

		if (pfd->pf_flags & P_HASH)
			(void)premove(pfd);
	}
	/*	We are modifiying the page so break
	 *	the disk association to swap if it exists.
	 */
	if (dbd->dbd_type == DBD_SWAP)
		(void)swfree1(dbd);
	dbd->dbd_type = DBD_NONE;

	/*	Set the modify bit here before the region is unlocked
	 *	so that getpages will write the page to swap if necessary.
	 */
	pg_setmod(pt);
	pg_clrcw(pt);
	pg_clrprot(pt);
	pg_setprot(pt, PG_RW);
	regrele(rp);

	/*	Recalculate priority for return to user mode.
	 */
	curpri = p->p_pri = calcppri(p);
	return(1);
}


/*	Translation fault handler
 */
vfault(vaddr, pt, prp)
caddr_t vaddr;
register pte_t	*pt;	/* Physical address of faulting pte.	*/
preg_t *prp;
{
	register struct proc *p;
	register dbd_t	*dbd;
	register pfd_t	*pfd;
	register reg_t	*rp;
	pte_t	 temppte;

	ASSERT(u.u_procp->p_flag & SLOAD);

	dbd = dbdget(pt);

	/*	Lock the region containing the page that faulted.
	 */
	p = u.u_procp;
	if (prp)
	{	rp = prp->p_reg;
		reglock(rp);
	} 	else
		rp = findreg(p, vaddr);

	/*	Check for an unassigned page.  This is a real
	 *	error.
	 */
	if (dbd->dbd_type == DBD_NONE) {
		regrele(rp);
		return(0);
	}

	/*	Check that the page has not been read in by
	 *	another process while we were waiting for
	 *	it on the reglock above.
	 */
	if (pt->pgm.pg_v) {
		regrele(rp);
		return(1);
	}
	minfo.vfault++;

	/*	Allocate a page in case we need it.  We must
	 *	do it now because it is not convenient to
	 *	wait later if no memory is available.  If
	 *	ptmemall does a wait and some other process
	 *	allocates the page first, then we have
	 *	nothing to do.
	 */
	if (ptmemall(rp, pt, 0)) {
		regrele(rp);
		return(1);
	}

	/*	See what state the page is in.
	 */
	switch(dbd->dbd_type) {
	case DBD_DFILL:
	case DBD_DZERO:{

		/*	Demand zero or demand fill page.
		 */
		minfo.demand++;
		if (dbd->dbd_type == DBD_DZERO)
			clearpage((int)pt->pgm.pg_pfn);
		dbd->dbd_type = DBD_NONE;
		break;
	}
	case DBD_SWAP:
	case DBD_FILE:
	case DBD_LSTFILE: {

		/*	Page is on swap or in a file.  See if a
		 *	copy is in the hash table.
		 */
		if (pfd = pagefind(rp, dbd)) {

			/*	Page is in cache.
			 *	If it is also on the free list,
			 *	remove it.
			 */
			minfo.cache++;

			if (pfd->pf_flags&P_QUEUE) {
				ASSERT(pfd->pf_use == 0);
				ASSERT(freemem > 0);
				freemem--;
				pfd->pf_flags &= ~P_QUEUE;
				pfd->pf_prev->pf_next = pfd->pf_next;
				pfd->pf_next->pf_prev = pfd->pf_prev;
				pfd->pf_next = NULL;
				pfd->pf_prev = NULL;
			}

			/*	Free the page we allocated above
			 *	since we don't need it.
			 */
			temppte = *pt;
			pg_setvalid(&temppte);
			pfree(rp, &temppte, (dbd_t *)NULL);

			rp->r_nvalid++;
			pfd->pf_use++;
			pt->pgm.pg_pfn = pfdtopf(pfd);

			/*	If the page has not yet been read
			 *	in from swap or file, then wait for
			 *	the I/O to complete.
			 */
			while ( !(pfd->pf_flags & P_DONE)) {
				pfd->pf_waitcnt++;
				(void)sleep(pfd, PZERO);
			}
			if (pfd->pf_flags & P_BAD) {
				pfd->pf_flags &= ~(P_DONE|P_BAD);
				premove(pfd);		    /* remove from hash chain */
				goto bad_page;
			}
			if (dbd->dbd_type == DBD_LSTFILE && rp->r_type == RT_PRIVATE) {
				register int i;
				register int pgaddr;

				if (i = poff(rp->r_filesz)) {
					pgaddr = ptob(pptop((u_int)pt->pgm.pg_pfn));
					bzeroba(pgaddr + i, NBPP - i);
				}
			}
		} else {

			/*	Must read from swap or a file.
			 *	Get the pfdat for the newly allocated
			 *	page and insert it in the hash table.
			 *	Note that it cannot already be there
			 *	because the pfind above failed.
			 */
			
			pfd = pftopfd(pt->pgm.pg_pfn);
			ASSERT((pfd->pf_flags & P_HASH) == 0);

			/*	Don't insert in hash table if this
			 *	block is from a swap file we are
			 *	trying to delete.
			 */
bad_page:
			if (dbd->dbd_type == DBD_SWAP){
				register int	swapdel;
				pglst_t		pglist;

				swapdel = swaptab[dbd->dbd_swpi].st_flags&ST_INDEL;
				if (swapdel == 0)
					pinsert(rp, dbd, pfd);

				/*	Read from swap.
				 */
				minfo.swap++;
				pglist.gp_ptptr = pt;
				swap(&pglist, 1, B_READ);

				if (swapdel) {
					(void)swfree1(dbd);
					dbd->dbd_type = DBD_NONE;
				}
			} else {
				/*	Read from file
				 */
				minfo.file++;
				pinsert(rp, dbd, pfd);

				if (readpg(rp, pt, dbd) < 0) {
					if (pfd->pf_use == 1)
					      premove(pfd); /* remove from hash chain */
					else {
					      pfd->pf_flags |= P_BAD|P_DONE;
					      if (pfd->pf_waitcnt) {
						    pfd->pf_waitcnt = 0;
						    wakeup(pfd);
					      }
					}
					pfdfree(rp, pfd, NULL);	/* free page */
					regrele(rp);	/* leave the dbd alone */
					return(0);
				}
			}

			/*	Mark the I/O done in the pfdat and
			 *	awaken anyone who is waiting for it.
			 */
			pfd->pf_flags |= P_DONE;
			if (pfd->pf_waitcnt) {
				pfd->pf_waitcnt = 0;
				wakeup(pfd);
			}
		}
		break;
	}
	default:
		panic("vfault - bad dbd_type");
	}
	pg_setvalid(pt);
	pg_clrmod(pt);
	regrele(rp);

	/*	Recalculate priority for return to user mode
	 */
	curpri = u.u_procp->p_pri = calcppri(u.u_procp);
	return(1);
}


/*	Read page from a file
 *
 *	return 0  - when no error occurs
 *	return -1 - when read error occurs
 */
readpg(rp, pt, dbd)
reg_t	*rp;
pte_t	*pt;
dbd_t	*dbd;
{	register struct buf   *bp;
	register struct vnode *vp;
	register int	      *bnptr;
	register struct buf   *tbp;
	register int	     i;
	register int	     nblks;
	register int	     lbsize;
	register int	     xcount;
	register int	     pgaddr;
	register int	     n;
	int		     pgbase;
	int		     maxcnt;
	struct	 buf	    *bpbase;
	extern struct buf *bincore();


	/*	Get the number of blocks to read and
	 *	a pointer to the block number list.
	 */
	vp = rp->r_vptr;

	if (vp->v_map == NULL)
	        return(-1);
	bnptr = &vp->v_map[dbd->dbd_blkno];
	pgbase = pgaddr = ptob(pptop((u_int)pt->pgm.pg_pfn));
	lbsize = vp->v_vfsp->vfs_bsize;

	if (lbsize > NBPP)
		n = NBPP;
	else
		n = lbsize;
	if ((maxcnt = vp->v_map[-1] - (dbd->dbd_blkno * n)) > NBPP)
		maxcnt = NBPP;

	if (lbsize < NBPP) {
		vp = vp->v_mappedvp;
		nblks = NBPP / lbsize;
		n = lbsize / DEV_BSIZE;	 /* num of phys blks per logical blk */
		bpbase = bp = (struct buf *)getmem(nblks * sizeof(struct buf));

		for (i = 0; i < nblks; i++, bnptr++, pgaddr += xcount, maxcnt -= xcount) {
			if ((bp->b_blkno = *bnptr) == -1)
				break;
			else if (tbp = bincore(vp, *bnptr)) {
				xcount = tbp->b_bcount;
				tbp = bread(vp, *bnptr, xcount);
				bcopy(tbp->b_un.b_addr, pgaddr, xcount);
				brelse(tbp);
			} else {
				xcount = lbsize;

				while (i < (nblks - 1)) {
					if (incore(vp, *(bnptr + 1)))
						break;
					if (*(bnptr + 1) != ((*bnptr) + n))
						break;
					++i;
					++bnptr;
					xcount += lbsize;
				}
				if (maxcnt < xcount)
					xcount = maxcnt;
				bp->b_forw = NULL;
				bp->b_back = NULL;
				bp->b_iodone = NULL;
				bp->b_bufsize = 0;
				bp->b_error = 0;
				bp->b_resid = 0;
				bp->b_vp = vp;
				bp->b_proc = u.u_procp;
				bp->b_dev = vp->v_rdev;
				bp->b_flags = B_BUSY | B_PHYS | B_READ;
				bp->b_un.b_addr = (caddr_t)pgaddr;
				bp->b_bcount = xcount;

				VOP_STRATEGY(bp);
				bp++;
			}
		}
	} else {
		nblks = 1;		    /* setup for the 'releasemem' just below */
		bpbase = bp = (struct buf *)getmem(sizeof(struct buf));
		i = dbd->dbd_blkno & ((lbsize / NBPP) - 1);
		n = vp->v_map[dbd->dbd_blkno - i];
		vp = vp->v_mappedvp;

		if (n == -1)
			bzeroba(pgbase, NBPP);
		else if (tbp = bincore(vp, n)) {
			if ((xcount = tbp->b_bcount) < lbsize) {
			        if (i)
				        xcount = lbsize;
			        else {
				        if (xcount < NBPP)
					        xcount = NBPP;
			        }
			}
			tbp = bread(vp, n, xcount);
			n = i * NBPP;
			if ((xcount -= n) > NBPP)
				xcount = NBPP;
			bcopy(tbp->b_un.b_addr + n, pgaddr, xcount);
			brelse(tbp);
			pgaddr += xcount;
		} else {
			bp->b_blkno = *bnptr;
			bp->b_forw = NULL;
			bp->b_back = NULL;
			bp->b_iodone = NULL;
			bp->b_bufsize = 0;
			bp->b_error = 0;
			bp->b_resid = 0;
			bp->b_vp = vp;
			bp->b_proc = u.u_procp;
			bp->b_dev = vp->v_rdev;
			bp->b_flags = B_BUSY | B_PHYS | B_READ;
			bp->b_un.b_addr = (caddr_t)pgaddr;
			bp->b_bcount = maxcnt;

			VOP_STRATEGY(bp);
			bp++;
			pgaddr += maxcnt;
		}
	}
	n = 0;

	while (--bp >= bpbase) {
		biowait(bp);
		if (bp->b_flags & B_ERROR) {
			prdev("page read error", vp->v_rdev);
			n = -1;
		}
	}
	if (dbd->dbd_type == DBD_LSTFILE) {
		if (rp->r_type == RT_STEXT)
			i = poff(pgaddr);
		else
			i = poff(rp->r_filesz);
		if (i)
			bzeroba(pgbase + i, NBPP - i);
	}
	releasemem(bpbase, nblks * sizeof(struct buf));

	return(n);
}

/* <@(#)fault.c	1.4> */
