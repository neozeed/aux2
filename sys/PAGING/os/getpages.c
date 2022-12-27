#ifndef lint	/* .../sys/PAGING/os/getpages.c */
#define _AC_NAME getpages_c
#define _AC_NO_MAIN "@(#) Copyright (c) 1984-85 AT&T-IS, 1985-87 UniSoft Corporation, All Rights Reserved.  {Apple version 2.2 90/03/03 17:13:56}"
#include <apple_notice.h>

#ifdef _AC_HISTORY
  static char *sccsid = "@(#)Copyright Apple Computer 1987, 1988, 1989\tVersion 2.2 of getpages.c on 90/03/03 17:13:56";
#endif		/* _AC_HISTORY */
#endif		/* lint */

#define _AC_MODS
/*	@(#)getpages.c	UniPlus VVV.2.1.7	*/

#ifdef HOWFAR
extern int T_getpages;
#endif HOWFAR

#ifdef lint
#include "sys/sysinclude.h"
#else lint
#include "sys/types.h"
#include "sys/mmu.h"
#include "sys/tuneable.h"
#include "sys/param.h"
#include "sys/sysmacros.h"
#include "sys/page.h"
#include "sys/systm.h"
#include "sys/dir.h"
#include "sys/signal.h"
#include "sys/time.h"
#include "sys/user.h"
#include "sys/vnode.h"
#include "sys/buf.h"
#include "sys/var.h"
#include "sys/sysinfo.h"
#include "sys/pfdat.h"
#include "sys/region.h"
#include "sys/proc.h"
#include "sys/map.h"
#include "sys/swap.h"
#include "sys/getpages.h"
#include "sys/debug.h"
#endif lint


char		vhandwakeup;
char		pglstlock;
extern int	freemem;
extern int	initial_freemem;
extern int	invsatb();
extern int	clratb();

int		getpgslim;	/* Current limit for getpages.	It is	*/
				/* either tune.t_gpgslo or		*/
				/* tune.t_gpgshi.			*/
int		sclimit;	/* Swap chunk size limit.  Set to	*/
				/* tune.t_maxsc every cycle thru vhand.	*/
int		fclimit;	/* Free chunk size limit.  Set to	*/
				/* tune.t_maxfc every cycle thru vhand.	*/

/*	The following tables are described in getpages.h
 */

pglst_t	spglst[MAXSPGLST];
pglst_t	fpglst[MAXFPGLST];
gprgl_t	gprglst[MAXSPGLST + MAXFPGLST];
int	spglndx;
int	fpglndx;
int	gprgndx;


pglstlk()
{
	register int s;

	s = splhi();
	while (pglstlock&1) {
		pglstlock |= 2;
		sleep(&pglstlock, PZERO);
	}
	pglstlock = 1;
	splx(s);
}


pglstunlk()
{
	ASSERT(pglstlock);
	if (pglstlock&2)
		wakeup(&pglstlock);
	pglstlock = 0;
}


/*	This process is awakened periodically by clock to update the
 *	system's idea of the working sets of all processes and to
 *	steal pages from processes if freemem is too low.
 */

vhand()
{
	register reg_t	*rp;
	register reg_t	*nrp;
	register struct tune *tp;
	register int	trigger;

	tp = &tune;

	if ((trigger = initial_freemem/5) < 30)
		trigger = 20;
	if (trigger < tp->t_gpgshi) {
		printf("Changing free page high water mark from %d to %d\n",
			tp->t_gpgshi, trigger);
		tp->t_gpgshi = trigger;
	}
	if (tp->t_gpgshi-20 < tp->t_gpgslo) {
		if ((trigger = tp->t_gpgshi-20) <= 10)
			trigger = tp->t_gpgshi/2;
		printf("Changing free page low water mark from %d to %d\n",
			tp->t_gpgslo, trigger);
		tp->t_gpgslo = trigger;
	}
	ASSERT(tp->t_gpgslo >= 2);
	getpgslim = tp->t_gpgslo;
	tp->t_vhandl = MAX(maxmem/v.v_vhndfrac, tp->t_gpgshi+20);


	for (;;) {

		/*	If tp->t_maxsc has been changed during the
		**	last pass through the regions or while we
		**	were sleeping, then use the new limit now.
		**	Be sure that an illegal value has not been
		**	specified.
		*/

		if (sclimit != tp->t_maxsc) {
			pglstlk();
			if(tp->t_maxsc > MAXSPGLST){
				printf("tp->t_maxsc reduced to %d.\n",
					MAXSPGLST);
				tp->t_maxsc = MAXSPGLST;
			}
			if (spglndx >= tp->t_maxsc)
				if (!swapchunk((reg_t *)0, 1))
					goto vhand_slp;
			sclimit = tp->t_maxsc;
			pglstunlk();
		}

		/*	If tp->t_maxfc has been changed during the
		**	last pass through the regions or while we
		**	were sleeping, then use the new limit now.
		**	Be sure that an illegal value has not been
		**	specified.
		*/

		if (fclimit != tp->t_maxfc) {
			pglstlk();
			if (tp->t_maxfc > MAXFPGLST) {
				printf("tp->t_maxfc reduced to %d.\n",
					MAXFPGLST);
				tp->t_maxfc = MAXFPGLST;
			}
			if (fpglndx >= tp->t_maxfc)
				freechunk((reg_t *)0);
			fclimit = tp->t_maxfc;
			pglstunlk();
		}

		/*	Scan all regions
		 */
		pglstlk();


		for (rp = ractive.r_forw; rp != &ractive; rp = nrp) {

			/*	if we're dealing with a PHYS'd region,
		        **      or it's currently locked,
			**	skip it.
			*/
			if ((rp->r_type&RT_PHYSCALL) || rp->r_lock) {
				nrp = rp->r_forw;
				continue;
			}
			rp->r_lock = (int)u.u_procp;
			ageregion(rp);

			/*	If memory is not tight, then don't steal any
			 *	pages.	Once we start to steal pages, then
			 *	steal enough to get up to the high water mark.
			 *	This is to avoid getting into repeated tight
			 *	memory situations.
			 */
			if (freemem > getpgslim) {
				regrele(rp);
				getpgslim = tp->t_gpgslo;
			} else {
				getpgslim = tp->t_gpgshi;
				getpages(rp, 0);
				if (gprglst[gprgndx].gpr_count == 0  ||
					gprglst[gprgndx].gpr_rgptr != rp) {
					regrele(rp);
				}

			}
			nrp = rp->r_forw;
		}

		/*	If we still do not have enough free
		**	memory and there are pages on the
		**	lists to steal, then steal them now 
		**	before we go to sleep.	Otherwise, 
		**	unlock all of the locked regions.
		*/

		if (fpglndx) { 
			if (freemem <= getpgslim)
				freechunk((reg_t *)0); 
			else {
				freelist(fpglst, fpglndx, (reg_t *)0);
				fpglndx = 0;
			}
		}
		if (spglndx) {
			if (freemem <= getpgslim)
				swapchunk((reg_t *)0, 0);
			else {
				freelist(spglst, spglndx, (reg_t *)0);
				spglndx = 0;
			}
		}
		ASSERT(spglndx == 0);
		ASSERT(fpglndx == 0);
		ASSERT(gprglst[gprgndx].gpr_count == 0);
		pglstunlk();

		/*	Bump the scheduler if memory is available
		 *	and someone was just made runnable.
		 */
		if (runin  &&  (freemem >= tp->t_gpgslo)) {
			runin = 0;
			setrun(&proc[0]);
		}

		/*	Go to sleep until clock wakes us up again.
		*/
vhand_slp:
		vhandwakeup++;
		sleep(&vhandwakeup, PSWP);
	}
}


/* Do page use bit aging for the pte's in this region
 */
ageregion(rp)
register reg_t	*rp;
{
	register pte_t	*pt;
	register int	i;
	register int	j;
	register int	seglim;
	register int	pglim;
	register int	ptincr;


	/*	Look at all of the segments of the region.
	 */
	seglim = ptoL2(rp->r_pgsz);

	for (i = 0; i < seglim; i++){
		/*	Look at all of the pages of the segment.
		 */
		if (rp->r_stack) {		/* STACK */
			pt = rp->r_plist[(rp->r_plistsz*NPTPBLK) - 1 - i];
			pt += (NPGPT - 1);
			ptincr = -1;
		} else {
			pt = rp->r_plist[i];
			ptincr = 1;
		}
		if ((pglim = rp->r_pgsz - L2topg(i)) > NPGPT)
			pglim = NPGPT;
		ASSERT(pglim >= 0  &&  pglim <= NPGPT);

		for (j = 0; j < pglim; j++, pt += ptincr) {
			/*	Check to see if this page is part of
			 *	the working set.  If not, it does
			 *	not have to be aged.
			 */
			if (!pt->pgm.pg_v)
				continue;

			/*	We have an active page.	 Age it
			 *	unless it as already as old as
			 *	it can get.
			 */

			if (pg_chkref(pt) || pg_chkndref(pt)) {
				if (pg_chkref(pt)) {
					pg_clrref(pt);
					pg_setndref(pt);
				} else
					pg_clrndref(pt);
			}
		}
	}
}


/*	Swap out pages from region rp which is locked by
 *	our caller.  If hard is set, take all valid pages,
 *	othersize take only unreferenced pages
 */

getpages(rp, hard)
register reg_t	*rp;
{
	register pte_t	*pt;
	register int	i;
	register int	j;
	register int	seglim;
	register int	pglim;
	register dbd_t	*dbd;
	register int	ptincr;
	register struct tune *tp;

	tp = &tune;
	ASSERT(rp->r_lock);
	ASSERT(pglstlock);

	
	/*	If the region is marked "don't swap", then don't
	 *	steal any pages from it.
	 */
	ASSERT(rp->r_noswapcnt >= 0);

	if (rp->r_noswapcnt)
		return;

	/*	Look through all of the segments of the region.
	 */
	seglim = ptoL2(rp->r_pgsz);

	for (i = 0; i < seglim; i++) {
		/* Look through segment's page table for valid
		 * pages to dump.
		 */
		if (rp->r_stack) {			/* STACK */
			pt = rp->r_plist[(rp->r_plistsz*NPTPBLK) - 1 - i];
			pt += (NPGPT - 1);
			ptincr = -1;
		} else {
			pt = rp->r_plist[i];
			ptincr = 1;
		}
		if ((pglim = rp->r_pgsz - L2topg(i)) > NPGPT)
			pglim = NPGPT;
		ASSERT(pglim >= 0  &&  pglim <= NPGPT);

		for (j = 0; j < pglim; j++, pt += ptincr) {
			/* If we have gotten enough pages, then don't
			 * steal any more.
			 */
			if ( !hard && (freemem >= getpgslim)) {
				getpgslim = tp->t_gpgslo;
				break;
			}

			/* Check to see if there is a page assigned
			 * and if it is eligible to be stolen.
			 */
			if (!pt->pgm.pg_v || pg_locked(pt))
				continue;

			/* We have a valid page assigned.
			 * Don't steal it, if the page has
			 * been referenced recently
			 */
			if ( !hard &&
			    ((pg_chkref(pt) && (tp->t_gpgsmsk&PG_REF)) ||
			     (pg_chkndref(pt) && (tp->t_gpgsmsk&PG_NDREF))))
				continue;

			/* See if this page must be written to swap.
			 */
			dbd = dbdget(pt);

			switch(dbd->dbd_type) {

			case DBD_NONE: {
				register pfd_t	*pfd;
				
				/* Check to see if the page is already
				 * associated with swap.  If so, just
				 * use the same swap block unless the 
				 * swap use count overflows.
				 */
				pfd = pftopfd(pt->pgm.pg_pfn);
				if ( !(pfd->pf_flags & P_HASH)) {
					addspg(rp, pt, hard);
				} else {
				ASSERT((pt->pgi[0].pg_pte&PG_PROT) == PG_RO);
				ASSERT(pt->pgm.pg_cw);
					dbd->dbd_type = DBD_SWAP;
					dbd->dbd_swpi = pfd->pf_swpi;
					dbd->dbd_blkno = pfd->pf_blkno;
					if (swpinc(dbd, "getpages")) {
						addfpg(rp, pt, hard);
						break;
					}
					dbd->dbd_type = DBD_NONE;
					addspg(rp, pt, hard);
				}
				break;
			}

			case DBD_SWAP:
				/* See if this page has been modified
				 * since it was read in from swap.
				 * If not, then just use the copy
				 * on the swap file unless we are trying
				 * to delete the swap file.  If we are,
				 * then release the current swap copy
				 * and write the page out to another
				 * swap file.
				 */

				if (pg_chkmod(pt) == 0 &&
				   (swaptab[dbd->dbd_swpi].st_flags &
				   ST_INDEL) == 0) {
					minfo.unmodsw++;
					addfpg(rp, pt, hard);
					break;
				}

				/*	The page has been modified.
				 *	Release the current swap
				 *	block and add it to the list
				 *	of pages to be swapped out
				 *	later.
				 */
				if (swfree1(dbd) == 0) {
					if (!pbremove(rp, dbd))
						panic("getpages - pbremove");
				}
				dbd->dbd_type = DBD_NONE;
				addspg(rp, pt, hard);
				break;

			case DBD_FILE:
			case DBD_LSTFILE:
				/* This page cannot have been modified
				 * since if it had been, then it would
				 * be marked DBD_NONE, not DBD_FILE.
				 * Either the page is text and so the
				 * segment table entry is RO or it is
				 * data in which case it it copy-on-
				 * write and also RO.
				 */
				ASSERT(pg_chkmod(pt) == 0);
				addfpg(rp, pt, hard);
				minfo.unmodfl++;
				continue;
			}
		}
		/*	If we have gotten enough pages, then 
		 *	don't steal any more.
		 */
		if ( !hard && (freemem >= getpgslim)) {
			getpgslim = tp->t_gpgslo;
			break;
		}
	}
	ASSERT(rp->r_lock);
}


/*	Add an entry to the spglst table.  If the table is full,
**	then first call freechunk to get pages we can get without
**	doing I/O and then, if we still don't have enough call
**	swapchunk to empty the spglst table.
*/

addspg(rp, pt, hard)
register reg_t	*rp;
register pte_t	*pt;
{
	/*	If the swap table is full, then process it.
	*/
	ASSERT(spglndx <= sclimit);
	ASSERT(rp->r_lock);
	ASSERT(pglstlock);

	if (spglndx == sclimit) {
		/*	If there are any pages on the other list
		**	which can be freed without doing any
		**	swapping, then free them first.	 If that
		**	gives us enough space, then forget
		**	about adding this page to the swap list.
		*/
		if ( !hard && (fpglndx > 0)) {
			freechunk(rp);
			if (freemem > getpgslim)
				return;
		}

		/*	Swap out the pages on the swap list.
		**	If that gives us enough memory, then
		**	forget about adding the new page to
		**	the list.
		*/

		if ( !swapchunk(rp, hard))
			return;
		if ( !hard && (freemem > getpgslim))
			return;
	}
	/*	Increment the count of pages from this region
	**	which are on one of the lists.
	*/
	bumprcnt(rp);

	/*	Add the page to the swap list.
	*/
	ASSERT(spglndx >= 0  &&	 spglndx < sclimit);
	spglst[spglndx].gp_ptptr = pt;
	spglst[spglndx++].gp_rlptr = &gprglst[gprgndx];
}


/*	Add an entry to the fpglst table.  If the table is full,
**	then first call freechunk to process it.
*/

addfpg(rp, pt, hard)
register reg_t	*rp;
register pte_t	*pt;
{
	/*	If the free table is full, then process it.
	**	If this gives us enough free memory, then
	**	forget about adding the new page to the list.
	*/
	ASSERT(pglstlock);
	ASSERT(fpglndx <= fclimit);
	ASSERT(rp->r_lock);
#ifndef lint
	ASSERT((dbdget(pt))->dbd_type == DBD_SWAP  ||
	       (dbdget(pt))->dbd_type == DBD_FILE  ||
	       (dbdget(pt))->dbd_type == DBD_LSTFILE );
#endif lint

	if (fpglndx == fclimit) {
		freechunk(rp);
		if (!hard)
			if(freemem > getpgslim)
				return;
	}

	/*	Increment the count of pages from this region
	**	which are on one of the lists.
	*/
	bumprcnt(rp);

	/*	Add the page to the free list.
	*/
	ASSERT(fpglndx >= 0  &&	 fpglndx < fclimit);
	fpglst[fpglndx].gp_ptptr = pt;
	fpglst[fpglndx++].gp_rlptr = &gprglst[gprgndx];
}



/*	Swap out a chunk of user pages.
 */

swapchunk(rp, hard)
register reg_t	*rp;
{
	register int	i;
	register int	retval;

	ASSERT(pglstlock);
	ASSERT(spglndx > 0  &&	spglndx <= sclimit);
	ASSERT(hard || fpglndx == 0);

	/*	If we are going to free more than is needed
	**	to get the required freemem, then just forget
	**	about the extra.
	*/
	if ( !hard) {
		i = getpgslim - freemem + 1;
		if (spglndx > i) {
			freelist(&spglst[i], spglndx - i, rp);
			spglndx = i;
		}
	}
	retval = 1;

	if (swalloc(spglst, spglndx, 0) < 0) {
		/*	We could not get a contiguous chunk of
		**	swap space of the required size so do
		**	the swaps one page at a time.  Hope
		**	this doesn't happen very often.	 Note
		**	that we get a "low on swap" printout
		**	on the console if this happens.
		*/

		for (i = 0; i < spglndx; i++) {
			/*	Allocate one page of swap and quit
			**	if none is available. Don't wait.
			*/
			if (spglndx == 1 || swalloc(&spglst[i], 1, 0) < 0) {
				freelist(&spglst[i], spglndx - i, rp);
				spglndx = i;
				retval = 0;
				break;
			}

			/*	Clear the valid bit on the page and
			**	flush both atb's.  Boy is this expensive.
			**	Note that if this happens, a message
			**	"WARNING: Swap space running out" is
			**	printed on the console.
			*/
			pg_clrvalid(spglst[i].gp_ptptr);
			clratb(USRATB);

			/*	Swap out one page.
			*/
			swap(&spglst[i], 1, B_WRITE);

		}
	} else {
		/*	Invalidate all of the pages and clear the
		**	atb's once.
		*/
		for (i = 0; i < spglndx; i++)
			pg_clrvalid(spglst[i].gp_ptptr);
		clratb(USRATB);

		/*	Write out all of the pages at once.
		*/
		swap(spglst, spglndx, B_WRITE);
	}

	/*	Free up the memory we just swapped out and
	**	reset the page list index.  Note that we
	**	never process the swap list if there is
	**	anything in the free list so after this
	**	memfree, both lists should be empty and
	**	therefore, the region count list should
	**	be empty also.
	*/
	if (spglndx)
		memfree(spglst, spglndx, rp, 0);
	spglndx = 0;

	return(retval);
}


/*	This routine is called to process the fpglist.	That is,
**	the list of pages which can be freed without doing any
**	swap I/O to create disk copies.
*/

freechunk(rp)
register reg_t	*rp;
{
	register int	pi;

	/*	Loop through all of the page tables entries
	**	turning off the valid bits and then flush
	**	the ATBs on both processors.  Since the
	**	regions are locked, this means that no more
	**	modifications to the page table entries
	**	can occur after the flush.
	*/

	ASSERT(fpglndx > 0  &&	fpglndx <= fclimit);

	for (pi = 0; pi < fpglndx; pi++)
		pg_clrvalid(fpglst[pi].gp_ptptr);
	clratb(USRATB);

	/*	Now free up the actual pages.
	*/
	memfree(fpglst, fpglndx, rp, 1);

	/*	Free pages list is empty now.
	*/
	fpglndx = 0;
}


/*	Free memory.
 */

memfree(pglptr, size, lockedreg, skipmod)
register pglst_t	*pglptr;
int			size;
reg_t			*lockedreg;
int			skipmod;
{
	register pfd_t		*pfd;
	register dbd_t		*dbd;
	register int		j;
	register gprgl_t	*rlptr;
	pte_t			*ptptr;
	reg_t			*rgptr;

	ASSERT(size > 0	 &&  size <= MAX(sclimit, fclimit));

	for (j = 0; j < size; j++, pglptr++) {
		/*	If we are supposed to skip modifed
		**	pages and this page has been modified,
		**	then skip it.  Don't forget to decrement
		**	the use count in the region list.
		*/
		ptptr = pglptr->gp_ptptr;
		rlptr = pglptr->gp_rlptr;
		rgptr = rlptr->gpr_rgptr;

		if (skipmod && pg_chkmod(ptptr)) {
			pg_setvalid(ptptr);
			if (--rlptr->gpr_count == 0 && rgptr != lockedreg) 
				regrele(rgptr);
			continue;
		}

		/*	Disassociate pte from physical page.
		*/
		dbd = dbdget(ptptr);
#ifndef lint
		ASSERT(dbd->dbd_type == DBD_SWAP  ||
		       dbd->dbd_type == DBD_FILE  ||
		       dbd->dbd_type == DBD_LSTFILE );
#endif lint
		pfd = pftopfd(ptptr->pgm.pg_pfn);
		ASSERT(rgptr->r_lock);

		/*	See if the page is in the hash list and
		 *	if not insert it there now.
		 */
		if (!(pfd->pf_flags & P_HASH)) {
			pfd->pf_flags |= P_DONE;
			pinsert(rgptr, dbd, pfd);
		}

		/*	free unused pages.
		*/
		if (--pfd->pf_use == 0) {
			/*	Put pages at end of queue since they
			**	represent disk blocks and we hope they
			**	will be used again soon.
			*/
			pfd->pf_prev = phead.pf_prev;
			pfd->pf_next = &phead;
			phead.pf_prev = pfd;
			pfd->pf_prev->pf_next = pfd;
			pfd->pf_flags |= P_QUEUE;
			freemem++;
			minfo.freedpgs++;
		}
		rgptr->r_nvalid--;

		if (--rlptr->gpr_count == 0 && rgptr != lockedreg) 
			regrele(rgptr);
	}
}


/*	Increment the count of pages for a region.  Either
**	it is the current region or the region is not yet
**	in the list because we process each region only
**	once during each pass of vhand.
*/

bumprcnt(rp)
register reg_t	*rp;
{
	register int	i;

	/*	If this region is not in the region list,
	**	then add it now.  Otherwise, just increment
	**	the count of pages being stolen from this
	**	region.
	*/

	ASSERT(pglstlock);

	if (gprglst[gprgndx].gpr_count == 0 ||
	    gprglst[gprgndx].gpr_rgptr != rp) {

		/*	The region count list can get fragmented
		**	because we can process the free page list
		**	many time before we process the swap page
		**	list.  Therefore, we must search for a
		**	free slot in the region count list.  Note
		**	that the list is cleaned up and starts
		**	fresh at the start of a new pass in vhand.
		**	In addition, there should always be a
		**	free slot because we make the region count
		**	list big enough to hold a separate region
		**	for each entry on both page lists.
		*/
		if (gprglst[gprgndx].gpr_count != 0) {
			i = gprgndx + 1;

			while (i != gprgndx) {
				if (i >= MAXSPGLST + MAXFPGLST)
					i = 0;
				if(gprglst[i].gpr_count == 0)
					break;
				i++;
			}
			if(i == gprgndx)
				panic("region count list overflow.");
			gprgndx = i;
		}
		gprglst[gprgndx].gpr_rgptr = rp;
		gprglst[gprgndx].gpr_count = 1;
	} else
		gprglst[gprgndx].gpr_count++;
}


/*	This routine is called to unlock the regions of a list
**	when the list does not really need to be processed.
*/

freelist(lp, count, lockedreg)
register pglst_t	*lp;
register int		count;
register reg_t		*lockedreg;
{
	register gprgl_t	*rl;
	register reg_t		*rp;
	register int		i;

	ASSERT(pglstlock);
	ASSERT(count > 0);

	for (i = 0; i < count; i++, lp++) {
		rl = lp->gp_rlptr;
		rp = rl->gpr_rgptr;
		if (--rl->gpr_count == 0 && rp != lockedreg)
			regrele(rp);
	}
}
/* <@(#)getpages.c	1.7> */
