#ifndef lint	/* .../sys/PAGING/os/swapalloc.c */
#define _AC_NAME swapalloc_c
#define _AC_NO_MAIN "@(#) Copyright (c) 1984-85 AT&T-IS, All Rights Reserved.  {Apple version 2.1 89/10/13 10:44:36}"
#include <apple_notice.h>

#ifdef _AC_HISTORY
  static char *sccsid = "@(#)Copyright Apple Computer 1989\tVersion 2.1 of swapalloc.c on 89/10/13 10:44:36";
#endif		/* _AC_HISTORY */
#endif		/* lint */

#define _AC_MODS
/*	@(#)swapalloc.c	UniPlus VVV.2.1.6	*/

#ifdef HOWFAR
extern int	T_swapalloc;
extern int	T_availmem;
#endif HOWFAR
#include "sys/types.h"
#include "sys/mmu.h"
#include "sys/param.h"
#include "sys/sysmacros.h"
#include "sys/sysinfo.h"
#include "sys/dir.h"
#include "sys/signal.h"
#include "sys/time.h"
#include "sys/user.h"
#include "sys/errno.h"
#include "sys/conf.h"
#include "sys/var.h"
#include "sys/vnode.h"
#include "sys/buf.h"
#include "sys/pfdat.h"
#include "sys/page.h"
#include "sys/region.h"
#include "sys/proc.h"
#include "sys/swap.h"
#include "sys/systm.h"
#include "sys/getpages.h"
#include "sys/tuneable.h"
#include "sys/debug.h"
#include "sys/ioctl.h"
#include "sys/ssioctl.h"
#include "sys/file.h"

#ifdef	PASS_MAJOR
#define	passmajor(n)	(n)
#else	PASSMAJOR
#define	passmajor(n)	minor(n)
#endif	PASS_MAJOR

int	swapwant;	/* minimum amount of swap swace wanted */

/*	Allocate swap file space.
 */

swalloc(pglist, size, waitflag)
register pglst_t	*pglist;/* Ptr to a list of pointers to page  */
				/* table entries for which swap is to */
				/* be allocated.		      */
register int	size;		/* Number of pages of swap needed.    */
register int	waitflag;	/* If required space is not available */
				/* then wait for it if this flag is   */
				/* set and return an error otherwise. */
{
	register int	smi;
	register use_t	*cntptr;
	register int	i;
	register int	swappg;
	register dbd_t	*dbd;

	swappg = 0;

	/*	Search all of the swap files, starting with the one
	 *	following the one which we allocated on last, looking
	 *	for a file with enough space to satisfy the current
	 *	request.
	 */
	for(;;) {
		smi = nextswap;

		/*	There can be holes in the swap file table
		 *	(swaptab) due to deletions.
		 */

		do{
			/*	If the current swaptab entry is not
			 *	in use or is in the process of being
			 *	deleted, go on to the next one.
			 */

			if((swaptab[smi].st_ucnt == NULL)  ||
			   (swaptab[smi].st_flags & ST_INDEL))
				continue;
			swappg = swapfind(&swaptab[smi], size);
			if(swappg >= 0)
				break;
		} while((smi = (smi + 1) % MSFILES) != nextswap);

		/*	If we got the swap space, then go set up the
		 *	disk block descriptors.
		 */

		if(swappg >= 0)
			break;

		/*	Try to free up some swap space by removing
		 *	unused sticky text regions.  If this
		 *	suceeds, try to allocate again.	 Otherwise,
		 *	either return an error or go to sleep
		 *	waiting for swap space depending on the
		 *	setting of the "waitflag" argument.
		 */

		if(swapclup()){
			printf("\nWARNING: Swap space running out.\n");
			printf("  Needed %d pages\n", size);
			continue;
		}

		if(waitflag == 0){
			printf("\nWARNING: Swap space running out.\n");
			printf("  Needed %d pages\n", size);
			return(-1);
		}

		printf("\nDANGER: Out of swap space.\n");
		printf("  Needed %d pages\n", size);

		if (swapwant == 0 || size < swapwant)
			swapwant = size;
		sleep(&swapwant, PMEM);
	}

	/*	Set up for main processing loop.
	*/

	cntptr = &swaptab[smi].st_ucnt[swappg];
	swappg = swaptab[smi].st_swplo + (swappg << DPPSHFT);
	swaptab[smi].st_nfpgs -= size;
	nextswap = (smi + 1) % MSFILES;

	/*	Initialize the swap use counts for each page
	 *	and set up the disk block descriptors (dbd's).
	 */

	for(i = 0  ;  i < size	;  i++, cntptr++, pglist++){
		*cntptr = 1;
		dbd = dbdget(pglist->gp_ptptr);
		dbd->dbd_type = DBD_SWAP;
		dbd->dbd_swpi = smi;
		dbd->dbd_blkno = swappg + (i << DPPSHFT);
	}

	return(swappg);
}


/*	Free one page of swap and return the resulting use count.
 */

swfree1(dbd)
register dbd_t	*dbd;	/* Ptr to disk block descriptor for	*/
			/* block to be removed.			*/
{
	register use_t	*cntptr;
	register int	pgnbr;
	register swpt_t	*st;
	register int	retval;

	st = &swaptab[dbd->dbd_swpi];
	pgnbr = (dbd->dbd_blkno - st->st_swplo) >> DPPSHFT;
	cntptr = &st->st_ucnt[pgnbr];

	ASSERT(*cntptr != 0);

	/*	Decrement the use count for this page.	If it goes
	 *	to zero, then free the page.  If anyone is waiting
	 *	for swap space, wake them up.
	 */

	retval = (*cntptr -= 1);

	if(retval == 0) {
		st->st_nfpgs += 1;

		/*	Wake up the first process waiting for swap
		 *	if we have freed up enough space.  Since we
		 *	are only freeing one page, we cannot
		 *	satisfy more than one process's request.
		 */

		if(swapwant  && ! (st->st_flags & ST_INDEL) &&
				swapwant <= st->st_nfpgs) {
			wakeup(&swapwant);
			swapwant = 0;
		}
	}
	if(st->st_flags & ST_INDEL) {
		if (st->st_nfpgs == st->st_npgs)
			swaprem(st);
	}

	return(retval);
}

#ifdef HOWFAR 
/*	Find the use count for a block of swap.
 */

swpuse(dbd)
register dbd_t	*dbd;
{
	register swpt_t	*st;
	register int	pg;

	st = &swaptab[dbd->dbd_swpi];
	pg = (dbd->dbd_blkno - st->st_swplo) >> DPPSHFT;

	return(st->st_ucnt[pg]);
}
#endif HOWFAR

/*	Increment the use count for a block of swap.
 */

swpinc(dbd, nm)
register dbd_t	*dbd;
char		*nm;
{
	register swpt_t	*st;
	register int	pg;

	st = &swaptab[dbd->dbd_swpi];
	pg = (dbd->dbd_blkno - st->st_swplo) >> DPPSHFT;

	if(st->st_ucnt[pg] >= MAXSUSE){
		printf("%s - swpuse count overflow\n", nm);
		return(0);
	}
	st->st_ucnt[pg]++;
	return(1);
}


/*	Add a new swap file.
 */

swapadd(dev, lowblk, nblks)
dev_t		dev;		/* The device code.		*/
int		lowblk;		/* First block on device to use.*/
int		nblks;		/* Nbr of blocks to use.	*/
{
	register int	smi;
	register swpt_t	*st;
	register int	i;
	int	err;
	int	partsize;	/* size of swap partition */

	/*	Open the swap file.
	 */

	if (err = (*bdevsw[major(dev)].d_open)(passmajor(dev), FREAD | FWRITE))
		return(err);

	/*	Get the size of this partition as recorded on disk.
	 *	As a special case, if nblks == 0, use the partition size.
	 *	Check to see if nblks exceeds the size of the partition.
	 *
	 *	Note: invoking the ioctl has the desirable side effect of
	 *	associating the partition with the device, i.e., it's pname'd.
	 */

	if (err = (*cdevsw[major(dev)].d_ioctl)(passmajor(dev), GD_PARTSIZE,
		FREAD | FWRITE))
		return(err);
	partsize = u.u_rval1;
	if (nblks == 0) {
		if ((nblks = (partsize - lowblk)) <= 0)
			return(ERANGE);
	}
	else if ((nblks + lowblk) > partsize)
		return(ERANGE);

	/*	Find a free entry in the swap file table.
	 *	Check to see if the new entry duplicates an
	 *	existing entry.	 If so, this is an error unless
	 *	the existing entry is being deleted.  In this
	 *	case, just clear the INDEL flag and the swap
	 *	file will be used again.
	 */

	smi = -1;
	for(i = 0  ;  i < MSFILES  ;  i++){
		st = &swaptab[i];
		if(st->st_ucnt == NULL){
			if(smi == -1)
				smi = i;
		} else if(st->st_dev == dev  && st->st_swplo == lowblk){
			if((st->st_flags & ST_INDEL)  &&
			   (st->st_npgs == (nblks >> DPPSHFT))){
				st->st_flags &= ~ST_INDEL;
				availsmem += st->st_npgs;
				return(0);
			}
			return(EEXIST);
		}
	}

	/*	If no free entry is available, give an error
	 *	return.
	 */

	if(smi < 0){
		return(ENFILE);
	}
	st = &swaptab[smi];

	/*	Initialize the new entry.
	 */

	st->st_dev = dev;
	st->st_swplo = lowblk;
	st->st_npgs = nblks >> DPPSHFT;
	availsmem += st->st_npgs;
	st->st_nfpgs = st->st_npgs;


	/*	Allocate space for the use count array.	 One counter
	 *	for each page of swap.
	 */

	i = st->st_npgs * sizeof(use_t);  /* Nbr of bytes for use   */
					  /* count.		    */
	i = btop(i);			  /* Nbr of pages to be used*/
					  /*  for count array.	    */
#ifndef lint
	st->st_ucnt = (use_t *)kvalloc(i, PG_ALL, -1);
#else
	st->st_ucnt = (use_t *)0;
#endif
	if (st->st_ucnt == 0 || (int) st->st_ucnt == -1)
		return(ENOMEM);
	st->st_next = st->st_ucnt;

	/*	Clearing the flags allows swalloc to find it
	 */
	st->st_flags = 0;
	if (swapwant) {
		wakeup(&swapwant);
		swapwant = 0;
	}
	return(0);
}


/*	Delete a swap file.
 */

swapdel(dev, lowblk)
register dev_t	dev;	/* Device to delete.			*/
register int	lowblk;	/* Low block number of area to delete.	*/
{
	register swpt_t	*st;
	register int	smi;
	register int	i;
	register int	ok;

	/*	Find the swap file table entry for the file to
	 *	be deleted.  Also, make sure that we don't
	 *	delete the last swap file.
	 */

loop:
	ok = 0;
	smi = -1;
	for(i = 0  ;  i < MSFILES  ;  i++){
		st = &swaptab[i];
		if(st->st_ucnt == NULL)
			continue;
		if(st->st_dev == dev  && st->st_swplo == lowblk)
			smi = i;
		else if((st->st_flags & ST_INDEL) == 0)
			ok++;
	};
	
	/*	If the file was not found, then give an error
	 *	return.
	 */

	if(smi < 0)
		return(EINVAL);

	/*	If we are trying to delete the last swap file,
	 *	then give an error return.
	 */
	
	if(!ok)
		return(ENOMEM);
	
	/*	Lock swaptab and check dev, and swplo again to make
	 *	sure they haven't changed.
	 */

	st = &swaptab[smi];

	if(st->st_dev != dev || st->st_swplo != lowblk)
		goto loop;

	if (availsmem - st->st_npgs < tune.t_minasmem)
		return(EAGAIN);

	/*	Set the delete flag.  Clean up its pages.
	 *	The file will be removed by swfree1 when
	 *	all of the pages are freed.
	 */

	st->st_flags |= ST_INDEL;
	if(st->st_nfpgs < st->st_npgs)
		getswap(smi);

	availsmem -= st->st_npgs;

	if(st->st_nfpgs == st->st_npgs)
		swaprem(st);

	return(0);
}


/*	Remove a swap file from swaptab.
 */

swaprem(st)
swpt_t	*st;
{
	register int	i;
#ifdef HOWFAR
	extern int	apgen;
#endif HOWFAR

	ASSERT(apgen == 0);

	/*	Release the space used by the use count array.
	 */

	i = st->st_npgs * sizeof(use_t);  /* Nbr of bytes for use   */
					  /* count.		    */
	i = btop(i);			  /* Nbr of pages	    */

	kvfree((int)st->st_ucnt, i);

	/*	Mark the swaptab entry as unused.
	 */

	st->st_ucnt = NULL;
}

/*	Try to free up swap space on the swap device being deleted.
 *	Look at every region for pages which are swapped to the
 *	device we want to delete.  Read in these pages and delete
 *	the swap.
 */

getswap(smi)
int	smi;
{
	register reg_t	*rp;
	reg_t		*nrp;
	register pte_t	*pt;
	register dbd_t	*dbd;
	register pte_t	*pglim;
	register int	i;
	register int	seglim;

	rlstlock();

	for(rp = ractive.r_forw; rp != &ractive; rp = nrp) {

		/*	If we can't lock the region, then
		 *	skip it for now.
		 */

		if (rp->r_lock) {
			nrp = rp->r_forw;
			continue;
		}
		rp->r_lock = (int)u.u_procp;

		/*	Loop through all the segments of the region.
		*/

		seglim = ptoL2(rp->r_pgsz);

		for(i = 0  ;  i < seglim  ;  i++){

			/*	Look at all of the pages of the segment.
			 */

			pt = rp->r_plist[i];
			dbd = dbdget(pt);
			if(rp->r_pgsz - L2topg(i) < NPGPT)
				pglim = pt + (rp->r_pgsz - L2topg(i));
			else
				pglim = pt + NPGPT;
			
			for(  ;	 pt < pglim  ;	pt++, dbd++){
				if(dbd->dbd_type == DBD_SWAP  &&
				   dbd->dbd_swpi ==  smi){
					rlstunlock();
					unswap(rp, pt, dbd);
					rlstlock();
				}
			}
		}
		nrp = rp->r_forw;
		regrele(rp);
	}

	rlstunlock();
}

/*	Free up the swap block being used by the indicated page.
 *	The region is locked when we are called.
 */

unswap(rp, pt, dbd)
register reg_t	*rp;
register pte_t	*pt;
register dbd_t	*dbd;
{
	register pfd_t	*pfd;
	pglst_t		pglist;

	ASSERT(rp->r_lock);

	/*	If a copy of the page is in core, then just
	 *	release the copy on swap.
	 */

	if (pt->pgm.pg_v) {
		pfd = pftopfd(pt->pgm.pg_pfn);

		if(pfd->pf_flags & P_HASH)
			(void)premove(pfd);

		(void)swfree1(dbd);
		dbd->dbd_type = DBD_NONE;
		return;
	}

	/*	Allocate a page of physical memory for the page.
	 */

	if (ptmemall(rp, pt, 0))
		return;

	/*	Read in the page from swap and then free up the swap.
	 */

	pglist.gp_ptptr = pt;
	swap(&pglist, 1, B_READ);
	(void)swfree1(dbd);
	dbd->dbd_type = DBD_NONE;
	pfd->pf_flags |= P_DONE;
	pg_setvalid(pt);
	pg_setref(pt);
	pg_setndref(pt);
	pg_clrmod(pt);
}

/*	Search swap use counters looking for size contiguous free pages.
 *	Returns the page number found + 1 on sucess, 0 on failure.
 */

swapfind(st, size)
register swpt_t	*st;
register int size;
{
	register use_t *p, *e;
	register int i;
	use_t *b;

	e = &st->st_ucnt[st->st_npgs - size];
	for(p = st->st_next; p <= e; p++) {
		if(*p == 0) {
			b = p;
			p++;
			for(i = 1; i < size; i++, p++)
				if(*p != 0) goto Cont;
			st->st_next = p;
			return(b - st->st_ucnt);
		}
	  Cont:;
	}
	e = st->st_next - size;
	for(p = st->st_ucnt; p <= e; p++) {
		if(*p == 0) {
			b = p;
			p++;
			for(i = 1; i < size; i++, p++)
				if(*p != 0) goto Cont2;
			st->st_next = p;
			return(b - st->st_ucnt);
		}
	  Cont2:;
	}

	st->st_next = st->st_ucnt;
	return(-1);
}

/* <@(#)swapalloc.c	1.4> */
