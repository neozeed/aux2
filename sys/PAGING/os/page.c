#ifndef lint	/* .../sys/PAGING/os/page.c */
#define _AC_NAME page_c
#define _AC_NO_MAIN "@(#) Copyright (c) 1985-87 UniSoft Corporation, All Rights Reserved.  {Apple version 2.1 89/10/13 13:56:14}"
#include <apple_notice.h>

#ifdef _AC_HISTORY
  static char *sccsid = "@(#)Copyright Apple Computer 1989\tVersion 2.1 of page.c on 89/10/13 13:56:14";
#endif		/* _AC_HISTORY */
#endif		/* lint */

#define _AC_MODS

/*	@(#)page.c	UniPlus VVV.2.1.17	*/

#ifdef HOWFAR
extern int	T_page;
extern int T_swapalloc;
extern int T_availmem;
#endif HOWFAR
#ifdef lint
#include "sys/sysinclude.h"
#else lint
#include "sys/types.h"
#include "sys/mmu.h"
#include "sys/param.h"
#include "sys/sysmacros.h"
#include "sys/page.h"
#include "sys/systm.h"
#include "sys/dir.h"
#include "sys/signal.h"
#include "sys/time.h"
#include "sys/user.h"
#include "sys/vnode.h"
#include "sys/var.h"
#include "sys/vfs.h"
#include "sys/mount.h"
#include "sys/buf.h"
#include "sys/map.h"
#include "sys/pfdat.h"
#include "sys/region.h"
#include "sys/proc.h"
#include "sys/swap.h"
#include "sys/tuneable.h"
#include "sys/debug.h"
#include "netinet/in.h"
#include "setjmp.h"
#endif lint

int	freemem;
int	availrmem;
int	availsmem;
int	PG_alloc_count = 0;
extern int	invsatb();
extern int	clratb();
char *pagealloc();

/*
 * Device number
 *	vp	-> vnode pointer
 * returns:
 *	dev	-> device number
 */
u_int
effdev(vp)
register struct vnode *vp;
{
	if (vp->v_vfsp->vfs_flag & VFS_REMOTE)
		return((u_int)vp);	 /* remote filesystems dont have real devs */
	return(vp->v_mappedvp->v_rdev);
}

/*
 * Find page by looking on hash chain
 *	dbd	-> Ptr to disk block descriptor being sought.
 * returns:
 *	0	-> can't find it
 *	pfd	-> ptr to pfdat entry
 */

struct pfdat *
pagefind(rp, dbd)
reg_t	       *rp;
register dbd_t *dbd;
{
	register u_int	 dev;
	register daddr_t blkno;
	register pfd_t	*pfd;

	/*	Hash on block and look for match.
	 */

	if (dbd->dbd_type == DBD_SWAP) {
		dev = swaptab[dbd->dbd_swpi].st_dev;
		blkno = dbd->dbd_blkno;
	} else {
		register struct vnode	*vp;

		vp = rp->r_vptr;
		ASSERT(vp != NULL);
		ASSERT(vp->v_map != NULL);
		dev = effdev(vp);
		blkno = vp->v_map[dbd->dbd_blkno];
	}
	for (pfd = phash[(blkno>>DPPSHFT)&phashmask]; pfd != NULL; pfd = pfd->pf_hchain) {
		if (pfd->pf_blkno == blkno && pfd->pf_dev == dev)
			return(pfd);
	}
	return(0);
}

/*
 * Insert page on hash chain
 *	dbd	-> ptr to disk block descriptor.
 *	pfd	-> ptr to pfdat entry.
 * returns:
 *	none
 */

pinsert(rp, dbd, pfd)
reg_t		*rp;
register dbd_t	*dbd;
register pfd_t	*pfd;
{	register u_int dev;
	register int   blkno;

	/* Check page range, see if already on chain
	 */

	if (dbd->dbd_type == DBD_SWAP) {
		dev = swaptab[dbd->dbd_swpi].st_dev;
		blkno = dbd->dbd_blkno;
	} else {
		register struct vnode	*vp;

		vp = rp->r_vptr;
		ASSERT(vp != NULL);
		ASSERT(vp->v_map != NULL);
		if ((blkno = vp->v_map[dbd->dbd_blkno]) == -1)
			return;
		dev = effdev(vp);
	}
	ASSERT(pfd->pf_hchain == NULL);
	ASSERT(pfd->pf_use > 0);

	/*
	 * insert newcomers at tail of bucket
	 */
	{
		register pfd_t *pfd1, **p;

		for (p = &phash[(blkno>>DPPSHFT)&phashmask]; pfd1 = *p;
		     p = &pfd1->pf_hchain) {
			if (pfd1->pf_blkno == blkno && pfd1->pf_dev == dev)
				panic("pinsert dup");
		}
		*p = pfd;
		pfd->pf_hchain = pfd1;
	}
	if (dbd->dbd_type != DBD_SWAP)
		pfd->pf_flags &= ~P_SWAP;
	else {
		pfd->pf_flags |= P_SWAP;
		pfd->pf_swpi = dbd->dbd_swpi;
	}
	pfd->pf_dev = dev;
	pfd->pf_blkno = blkno;
	pfd->pf_flags |= P_HASH;
}


/*
 * remove page from hash chain
 *	pfd	-> page frame pointer
 * returns:
 *	0	Entry not found.
 *	1	Entry found and removed.
 */
premove(pfd)
register pfd_t *pfd;
{	register pfd_t	*pfd1;
	register pfd_t **p;

	for (p = &phash[(pfd->pf_blkno>>DPPSHFT)&phashmask]; pfd1 = *p;
	     p = &pfd1->pf_hchain) {
		if (pfd1 == pfd) {
			*p = pfd->pf_hchain;
			/*
			 * Disassociate page from disk and
			 * remove from hash table
			 */
			pfd->pf_blkno = BLKNULL;
			pfd->pf_hchain = NULL;
			pfd->pf_flags &= ~P_HASH;
			pfd->pf_dev = 0;
			return(1);
		}
	}
	return(0);
}


/*
 * flush all pages associated with a device
 * returns:
 *	none
 */
punmount(dev)
register dev_t dev;
{
	register struct pfdat *pfd;

	for (pfd = phead.pf_next; pfd != &phead; pfd = pfd->pf_next) {
		if (dev == pfd->pf_dev) {
			if (pfd->pf_flags & P_HASH)
				premove(pfd);
		}
	}
}


/*
 * Find page by looking on hash chain
 *	dbd	Ptr to disk block descriptor for block to remove.
 * returns:
 *	0	-> can't find it
 *	i	-> page frame # (index into pfdat)
 */

pbremove(rp, dbd)
reg_t	*rp;
dbd_t	*dbd;
{	register pfd_t	*pfd;
	register pfd_t	*pfd_next;
	register pfd_t **p;
	register int	 blkno;
	register u_int	 dev;

	/*
	 * Hash on block and look for match
	 */

	if (dbd->dbd_type == DBD_SWAP) {
		dev = swaptab[dbd->dbd_swpi].st_dev;
		blkno = dbd->dbd_blkno;
	} else {
		register struct vnode	*vp;

		vp = rp->r_vptr;
		ASSERT(vp != NULL);
		ASSERT(vp->v_map != NULL);
		dev = effdev(vp);
		blkno = vp->v_map[dbd->dbd_blkno];
	}
	for (p = &phash[(blkno>>DPPSHFT)&phashmask]; pfd = *p; p = &pfd->pf_hchain) {
		if (pfd->pf_blkno == blkno && pfd->pf_dev == dev) {
			*p = pfd->pf_hchain;
			pfd->pf_blkno = BLKNULL;
			pfd->pf_hchain = NULL;
			pfd->pf_flags &= ~P_HASH;
			pfd->pf_dev = 0;

			if (pfd->pf_flags & P_QUEUE) {
			      pfd_next = pfd->pf_next;
			      pfd->pf_prev->pf_next = pfd_next;
			      pfd_next->pf_prev = pfd->pf_prev;
			
			      pfd->pf_next = phead.pf_next;
			      pfd->pf_prev = &phead;
			      phead.pf_next = pfd;
			      pfd->pf_next->pf_prev = pfd;
			}
			return(1);
		}
	}
	return(0);

}


pfremove(blkno, dev)
register int   blkno;
register u_int dev;
{	register pfd_t	*pfd;
	register pfd_t	*pfd_next;
	register pfd_t **p;

	for (p = &phash[(blkno>>DPPSHFT)&phashmask]; pfd = *p; p = &pfd->pf_hchain) {
		if (pfd->pf_blkno == blkno && pfd->pf_dev == dev) {
			*p = pfd->pf_hchain;
			pfd->pf_blkno = BLKNULL;
			pfd->pf_hchain = NULL;
			pfd->pf_flags &= ~P_HASH;
			pfd->pf_dev = 0;

			if (pfd->pf_flags & P_QUEUE) {
			      pfd_next = pfd->pf_next;
			      pfd->pf_prev->pf_next = pfd_next;
			      pfd_next->pf_prev = pfd->pf_prev;
			
			      pfd->pf_next = phead.pf_next;
			      pfd->pf_prev = &phead;
			      phead.pf_next = pfd;
			      pfd->pf_next->pf_prev = pfd;
			}
			return(1);
		}
	}

	return(0);
}


/*
 * Reserve size memory pages.  Returns with freemem
 * decremented by size.	 Return values:
 *	0 - Memory available immediately
 *	1 - Had to sleep to get memory
 */
memreserve(rp, size)
register reg_t *rp;
{	register int slept;

	ASSERT(rp->r_lock);

	slept = 0;
	while (freemem < size) {
		slept = 1;
		regrele(rp);
		u.u_procp->p_stat = SXBRK;
		(void)wakeup(&runout);
		swtch();
		reglock(rp);
	}
	freemem -= size;
	return(slept);
}

pfd_t *
pfdall(rp)
reg_t		*rp;
{
	register struct pfdat	*pfd;
	register pfd_t	*pfd_next;

	pfd = phead.pf_next;	/* Take page from head of queue */

	ASSERT (pfd != &phead);
	ASSERT(pfd->pf_flags&P_QUEUE);
	ASSERT(pfd->pf_use == 0);

	/*
	 * Delink a page from free queue and set up pfd
	 */

	pfd_next = pfd->pf_next;
	pfd->pf_prev->pf_next = pfd_next;
	pfd_next->pf_prev = pfd->pf_prev;
	pfd->pf_next = NULL;
	pfd->pf_prev = NULL;
	if (pfd->pf_flags&P_HASH)
		(void) premove(pfd);
	pfd->pf_blkno = BLKNULL;
	pfd->pf_use = 1;
	pfd->pf_flags = 0;
	pfd->pf_rawcnt = 0;
	rp->r_nvalid++;
	return pfd;
}

pfdfree(rp, pfd, dbd)
reg_t		*rp;
register struct pfdat *pfd;
register dbd_t	*dbd;
{

	/* Free pages that aren't being used
	 * by anyone else
	 */
	if (--pfd->pf_use == 0) {

		/* Pages that are associated with disk go to end of
		 * queue in hopes that they will be reused.  All
		 * others go to head of queue.
		 */

		if (dbd == NULL || dbd->dbd_type == DBD_NONE) {
			/*
			 * put at head 
			 */
			pfd->pf_next = phead.pf_next;
			pfd->pf_prev = &phead;
			phead.pf_next = pfd;
			pfd->pf_next->pf_prev = pfd;
		} else {
			/*
			 * put at tail 
			 */
			pfd->pf_prev = phead.pf_prev;
			pfd->pf_next = &phead;
			phead.pf_prev = pfd;
			pfd->pf_prev->pf_next = pfd;
		}
		pfd->pf_flags |= P_QUEUE;
		freemem++;
	}
	rp->r_nvalid--;
}

/*
 *	Make a given pte point to a given pfd. 
 *	Validate -> mark page valid
 */

pfdmap(base, pfd, validate)
pte_t *base;
pfd_t *pfd;
int validate;
{
	/*
	 * Insert in page table
	 */

	base->pgm.pg_pfn = pfdtopf(pfd);
	pg_clrmod(base);
	pg_clrprot(base);

	if (base->pgm.pg_cw)
		pg_setprot(base, PG_RO);
	else pg_setprot(base, PG_RW);

	if (validate)
		pg_setvalid(base);
}

/*
 * Shred page table and update accounting for swapped
 * and resident pages.
 *	rp	-> ptr to the region structure.
 *	pt	-> ptr to the first pte to free.
 *	dbd	-> ptr to disk block descriptor.
 */

pfree(rp, pt, dbd)
reg_t		*rp;
register pte_t	*pt;
register dbd_t	*dbd;
{
	/* 
	 * Zap page table entries
	 */

	if (pt->pgm.pg_v)
		pfdfree(rp, pftopfd(pt->pgm.pg_pfn), dbd);

	if (dbd) {
		if (dbd->dbd_type == DBD_SWAP && swfree1(dbd) == 0)
			(void) pbremove(rp, dbd);
		/* Sets type to DBD_NONE, clears ndref, lock bits */
		*(int *)dbd = 0;
	}
	pt->pgi.pg_pte = 0;	/* pg_zero() clears the dbd, which is bad */
}

pfdtopf(pfd)
register struct pfdat *pfd;
{
	register struct pfdat_desc *pfdd;

	for (pfdd = pfdat_desc; pfdd; pfdd = pfdd->pfd_next) {
		if (pfd >= pfdd->pfd_head && pfd < pfdd->pfd_head + pfdd->pfd_pfcnt)
			return(pfdd->pfd_pfnum + (pfd - pfdd->pfd_head));
	}
	return(0);
}

struct pfdat *
pftopfd(pf)
register u_int pf;
{
	register struct pfdat_desc *pfdd;

	for (pfdd = pfdat_desc; pfdd; pfdd = pfdd->pfd_next) {
		if (pf >= pfdd->pfd_pfnum && pf < pfdd->pfd_pfnum + pfdd->pfd_pfcnt)
			return(pfdd->pfd_head + (pf - pfdd->pfd_pfnum));
	}
	return((struct pfdat *)NULL);
}


/*
 * copypage - Copy one logical page given the page frame number for
 * the source and destination.
 */
copypage(from, to)
int from, to;
{
	/* convert page frame number to pfdat index */
	blt512(ptob(pptop(to)), ptob(pptop(from)), ptob(1)>>9);
}

/*
 * clearpage - Clear one physical page given its page frame number
 */
clearpage(pfn)
int pfn;
{
	/* convert page frame number to pfdat index */
	clear(ptob(pptop(pfn)), ptob(1));
}

char *pagealloc()
{
	register struct pfdat *pfd;
	register pfd_t	*pfd_next;


	while (freemem == 0) {
		u.u_procp->p_stat = SXBRK;
		(void)wakeup(&runout);
		swtch();
	}
	freemem--;
	PG_alloc_count++;

	/*
	 * Delink a page from free queue and set up pfd
	 */
	pfd = phead.pf_next;	/* Take page from head of queue */
	pfd_next = pfd->pf_next;
	pfd->pf_prev->pf_next = pfd_next;
	pfd_next->pf_prev = pfd->pf_prev;
	pfd->pf_next = NULL;
	pfd->pf_prev = NULL;
	if (pfd->pf_flags&P_HASH)
		(void)premove(pfd);
	pfd->pf_blkno = BLKNULL;
	pfd->pf_use = 1;
	pfd->pf_flags = 0;
	pfd->pf_rawcnt = 0;

	return((char *)(pfdtopf(pfd) << PAGESHIFT));
}


pagefree(pageaddr)
{	register struct pfdat *pfd;

	/* 
	 * put at head
	 */
	pfd = pftopfd(pageaddr >> PAGESHIFT);
	pfd->pf_next = phead.pf_next;
	pfd->pf_prev = &phead;
	phead.pf_next = pfd;
	pfd->pf_next->pf_prev = pfd;
	pfd->pf_flags |= P_QUEUE;
	freemem++;
	PG_alloc_count--;
}


/* <@(#)page.c	1.5> */
