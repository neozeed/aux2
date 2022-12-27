/*
 * Copyright 1989 Apple Computer, Inc.
 * All Rights Reserved.
 *
 * THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF APPLE COMPUTER, INC.
 * The copyright notice above does not evidence any actual or
 * intended publication of such source code.
 */

/* (@(#)ptbl.c	2.3 89/10/20) */
#ifndef lint
#define _AC_NAME ptbl_c
#define _AC_NO_MAIN "@(#) Copyright (c) 1989 Apple Computer, Inc., All Rights Reserved.  {Apple version 2.5 89/10/21}"
#include <apple_notice.h>

#ifdef _AC_HISTORY
  static char *sccsid = "@(#)ptbl.c	2.5 89/10/21";
#endif		/* _AC_HISTORY */
#endif		/* lint */

#define _AC_MODS
/*
 * Code to allocate/deallocate page tables and kernel/user virtual space
 */

#ifdef lint
#include "sys/sysinclude.h"
#else lint
#include "sys/bitmasks.h"
#include "sys/types.h"
#include "sys/mmu.h"
#include "sys/param.h"
#include "sys/sysmacros.h"
#include "sys/time.h"
#include "sys/page.h"
#include "sys/pfdat.h"
#include "sys/region.h"
#include "sys/map.h"
#include "sys/proc.h"
#include "sys/tuneable.h"
#include "sys/systm.h"
#include "sys/debug.h"
#endif lint

extern pfd_t *pfdall();
extern char k1to1tbl[];
extern int freemem;
int PFD_Limbo = 0;

/* This file contains much of the code devoted to page table manipulation,
 *  both for kernel virtual and for user spaces.
 * There are two kinds of routines here:
 *   pt* - manipulate page (and other mmu) tables
 *   kv* - manipulate kernel virtual (=spt) space
 */

/*
 * ptblalloc - return a page table address
 *  Parameterized by the size of the required table (Units are 256 bytes):
 *  uses are: page tables for non-swappable pages, L1/L2 tables, or region
 *  page table lists.
 * We use two free lists, one for 512-byte chunks (040 boundary concerns) and
 *  one for the rest.
 */
pte_t *
ptblalloc(num)
register int num;
{	register int pt;
	register pfd_t *pf;
	register struct pfdat *pfr_dat;
	register int i, j;
	register int mask;

	if (num > NPTPPG)
		return((pte_t *)NULL);

	/* Set mask for contiguous chunks
	 */
	mask = setmask(num);
	pfr_dat = (num == 2) ? &pt2free : &ptfree;
	for (;;)
	{	if (pfr_dat->pf_freecount >= num)
		for (pf = pfr_dat->pf_next; pf != pfr_dat; pf = pf->pf_next)
		{	/* try all positions of the mask to find (1 or 2)
			 * contiguous chunks in a page.
			 */
			for (j = 0, i = mask; j <= NPTPPG - num; j++, i = i<<1)
			{	if (((unsigned short)pf->pf_use & i) == i)
				{	/* we have found the page tables.
					 * turn off their bits.  if no page
					 * tables are left in the page, then
					 * remove the page from the page-
					 * table list.
					 */
					pf->pf_use &= ~i;
					if (pf->pf_use == 0)
					{	pf->pf_prev->pf_next = pf->pf_next;
						pf->pf_next->pf_prev = pf->pf_prev;
						pf->pf_next = 0;
						pf->pf_prev = 0;
						PFD_Limbo++;
					}
					
					/* get address of page tables we
					 * have allocated.  update the
					 * free page table count and
					 * clear the page tables.
					 */
					pt = ptob(pfdtopf(pf));
					pt += j << PTSZSHFT;
					pfr_dat->pf_freecount -= num;
					bzero((caddr_t)pt, num<<PTSZSHFT);
					return((pte_t *)pt);
				}
			}
		}
		/*
		 * no space - allocate a new page, put it at the head,
		 *  and the next time around, we'll pick this one.
		 */
		reglock(&sysreg);
		availrmem--;
		availsmem--;
		memreserve(&sysreg, 1);
		pf = pfdall(&sysreg);
		regrele(&sysreg);
		if (availsmem < tune.t_minasmem || availrmem < tune.t_minarmem)
		{	availsmem++;
			availrmem++;
			pfdfree(&sysreg, pf, (dbd_t *)NULL);
			return(NULL);
		}
		pfr_dat->pf_freecount += NPTPPG;
		pf->pf_next = pfr_dat->pf_next;
		pf->pf_prev = pfr_dat;
		pfr_dat->pf_next = pf;
		pf->pf_next->pf_prev = pf;
		pf->pf_use = -1;	/* initial mask - unused chunks */
	}
}

/*
 * free previously allocated page tables.
 * assume they were allocated via ptblalloc(), hence are contiguous,
 * and fit in one page.
 */
ptblfree(p, npgtbls)
register caddr_t p;
register int npgtbls;
{
	register int	nfree;
	register int	ndx;
	register pfd_t	*pf;
	register struct pfdat *pfr_dat;

	if (npgtbls == 0 || npgtbls > NPTPPG)
		return;

	pfr_dat = (npgtbls == 2) ? &pt2free : &ptfree;

	/* Get a pointer to the pfdat for the page in which
	 * we are freeing page tables.
	 */
	pf = btopfd(p);
	/* Get the index into the page of the first page table
	 *  being freed.
	 */
	ndx = (int)(p - ptob(btotp(p))) >> PTSZSHFT;

	ASSERT((pf->pf_flags & (P_QUEUE | P_HASH)) == 0);

	/* If the page has no free page tables and we are not going
	 *  to free the entire page, then put the pfdat on the free
	 *  page table list (indicating free page tables available).
	 */
	if (pf->pf_use == 0 && npgtbls < NPTPPG)
	{	pf->pf_next = pfr_dat->pf_next;
		pf->pf_prev = pfr_dat;
		pfr_dat->pf_next->pf_prev = pf;
		pfr_dat->pf_next = pf;
		PFD_Limbo--;
	}

	/* Set the appropriate bits in the pfdat.  If
	 * we free the entire page, then return it to
	 * the system free space list and update the
	 * physical to system virtual address map to
	 * show that the page is no longer being used
	 * for page tables.
	 */
	ASSERT((pf->pf_use & (setmask(npgtbls) << ndx)) == 0);
	pf->pf_use |= setmask(npgtbls) << ndx;

	if ((unsigned short)pf->pf_use == setmask(NPTPPG))
	{	/* Remove pfdat from page table allocation list and reset
		 * use count.  Note that the pfdat is on the allocation
		 * list only if there are existing tables available.
		 */
		if (npgtbls != NPTPPG)
		{	pf->pf_prev->pf_next = pf->pf_next;
			pf->pf_next->pf_prev = pf->pf_prev;
		}
		pf->pf_use = 1;			/* As we got it */

		/* Free the pages and the system virtual
		 * address space.  Update the count of allocated
		 * and free pages.
		 */
		pfdfree(&sysreg, pf, (dbd_t *)NULL);
		availrmem++;
		availsmem++;
		nptalloced -= NPTPPG;
		pfr_dat->pf_freecount -= NPTPPG - npgtbls;	/* Since we are releasing it */
	} else
		pfr_dat->pf_freecount += npgtbls;
}

/*
 * Allocate kernel virtual address space and allocate or link pages.
 *  Returns a virtual address where the requested size has been allocated.
 * Note: this may not be the same as the requested `base', even if the
 *  base is in physical memory.
 * Can be called in three ways:
 *  `base' is physical and mapped 1-1; returns `base'.
 *  `base' is -1; allocates and fills in KV space according to `size'. 
 *  `base' is actually a real address, but beyond the apparent physical memory.
 *	   Fill in KV space using `base' as the page address.
 * What should happen if `base' is physical, but `base'+`size' isn't,
 * is unclear.
 */
caddr_t
kvalloc(size, mode, base)
register int size, mode;
register unsigned int base;
{	register unsigned int svaddr;
	register int bsize;

	bsize = ptob(size);

	/*
	 * If base is specified, return if the region is all mapped 1-1.
	 * Assumes that 1-1 mapping is determined only at L1.
	 */
	if (base != -1)
	{	svaddr = base;
		while ((svaddr < base + bsize) && k1to1tbl[ptoL1(svaddr)])
			svaddr += L1topg(1);
		if (svaddr >= base + bsize)
			return((caddr_t)base);
	}
	/*
	 * Otherwise, if size is one page, allocate a new page, 
	 *  and return the virtual (== physical) address.
	 */
	if (base == -1 && size == 1)
	{	pfd_t *pfd;
		reglock(&sysreg);
		memreserve(&sysreg, 1);
		pfd = pfdall(&sysreg);
		svaddr = ptob(pfdtopf(pfd));
		regrele(&sysreg);
		return((caddr_t)svaddr);
	}

	/*
	 * Requested address or size forces us to fit into kernel virtual
	 *  space.  Try to alloc and fill the page tables now.
	 */
	if (svaddr = kvreserve(size))
		kvfill(svaddr, size, mode, base);
	return((caddr_t)svaddr);
}

/*
 * Reserve a chunk of kernel virtual space.
 *  This is a separate routine since it's needed by the debugger.
 * If there is not sufficient space in the map, try to build more, and
 *  try again.  Note that this only fills in tables.  The pages themselves
 *  are added elsewhere.
 * Returns NULL on failure.
 */
kvreserve(size)
register int size;
{	register unsigned int sp;

	if (!(sp = malloc(sptmap, size)))
		kvgrow(ptob(size));
	if (!sp && !(sp = malloc(sptmap, size)))
	{	printf("kvalloc: No kernel virtual space\n");
		return(NULL);
	}
	return(ptob(sp));
}

/*
 * Grow the KV area to include at least `bytes' bytes.
 * The L1, L2, and Page tables are allocated if needed, and added to the
 * spt map.
 * Note that the spt map is initially empty, and is grown as needed.
 * kv_end is the next address to allocate.
 */
static
kvgrow(bytes)
{	register freeaddr, freecnt;
	register int i1, i2, j1, j2, k1, k2;
	register Dt_t *L1p, *L2p;
	register pte_t *Ptp;
	unsigned int last_address, end_address;

	if (kv_end+bytes > hardlimit)
		panic("kvgrow: hard limit exceded\n");

	if (kv_end+bytes > softlimit)
	{	printf("kvgrow: soft limit exceded\n");
		softlimit = kv_end + bytes;
	}
	freeaddr = btop(kv_end);
	freecnt = 0;

	i1 = L1ix(kv_end);
	i2 = L1ix(kv_end+bytes);
	L1p = &K_L1tbl[i1];		/* Allocated in mmusetup */
	last_address = kv_end+bytes;

	/* Loop through the L1 tables, doing our bit at each turn */
	for (;i1++ <= i2; L1p++ )
	{	L2p = L2_ptr(K_L1tbl, kv_end);
		/* Assume L2p is not a pointer if !Lx_valid() */
		if (!Lx_valid(L2p))
		{	/* Need to alloc L2 */
			L2p = (Dt_t *)ptblalloc(2);
			wtl1e(L1p, L2p, Lx_RW);
		}
		/* Have an L2 table (L2p).  Fill it */
		j1 = L2ix(kv_end);
		/* Index up to end of this L2 table */
		j2 = (int)(L2_round(kv_end+1)-1);
		end_address = min(last_address, (unsigned int)j2);
		j2 = L2ix(end_address);
		/* Now, get the proper page table entry within the L2 table */
		L2p = L2_ent(K_L1tbl, kv_end);
		for (; j1++ <= j2; L2p++)
		{	ASSERT(!Lx_valid(L2p));
			/* Need to alloc PT (no dbd's) */
			Ptp = ptblalloc(1);
			wtl2e(L2p, Ptp, Lx_RW);
			freecnt += NPGPT;
			kv_end += pttob(1);
		}
	}
	mfree(sptmap, freecnt, freeaddr);
	return 0;
}

/*
 * Fill in, or mark as valid, page tables for the indicated range
 * If pbase is -1, fill in the page tables with real memory.  If not, pbase
 * is a (very high) physical address to be mapped as the given virtual address.
 */
kvfill(svaddr, size, mode, pbase)
caddr_t svaddr;
register int size, mode;
int pbase;
{
	register int sp, pb;
	register pte_t *p;

	if (pbase  ==  -1)		/* Allocate and fill in pages */
	{	reglock(&sysreg);
		/* Look for a good way to avoid repetitive Pt_ent()s */
		for (; size; size--, svaddr += ptob(1))
		{	p = Pt_ent(K_L1tbl, svaddr);
			p->pgi.pg_pte = 0;
			if (ptmemall(&sysreg, p, 1))
				panic("kvfill - ptmemall failed");
			p->pgi.pg_pte |= mode;
			invsatb(SYSATB, svaddr, 1);
		}
		regrele(&sysreg);
	} else 
	{	pb = pnum(pbase);
		/* Map addresses */
		for (; size; size--, svaddr += ptob(1), pbase++)
		{	p = Pt_ent(K_L1tbl, svaddr);
			p->pgi.pg_pte = 0;
			wtpte(p, pbase, mode);
			invsatb(SYSATB, svaddr, 1);
		}
	}
/*	return 0;*/
}

/*
 * Free a chunck of KV space.  Similar rules apply as for kvalloc().
 * If physical, just verify the extent.  Otherwise, free up the tables.
 * Use `flag' to determine whether to free pages in either case.
 */
kvfree(vaddr, size, flag)
register int vaddr;
register int size;
{	register pte_t *p;
	register i, sp;

	if (k1to1tbl[btoL1(vaddr)])
	{	i = vaddr;
		sp = vaddr + ptob(size);
		while ((i < sp) && k1to1tbl[btoL1(i)])
			i += L1tob(1);
		if (i >= sp)
		{	if (flag)
			{	sp = vaddr;
				for (;size; size--, sp += ptob(1))
					pfdfree(&sysreg, btopfd(sp),
							(dbd_t *) NULL);
			}
			return;
		}
	}
	sp = vaddr;

	if (flag)
	{	reglock(&sysreg);
		for (i = 0; i < size; i++, sp += ptob(1))
			pfree(&sysreg, Pt_ent(K_L1tbl, sp), (dbd_t *) NULL);
		regrele(&sysreg);
	}
	else for (i = 0; i < size; i++, sp += ptob(1))
	{	p = Pt_ent(K_L1tbl, sp);
		p->pgi.pg_pte = 0;
	}

	mfree(sptmap, size, btop(vaddr));
}

/*
 * Allocate pages and fill in page table
 *	rp		-> region pages are being added to.
 *	base		-> address of page table
 *	validate	-> Mark page valid if set.
 * returns:
 *	0	Memory allocated.
 *	1       no memory allocated (although it is already allocated)
 */
ptmemall(rp, base, validate)
reg_t *rp;
pte_t *base;
{
	register pfd_t *pfd;

	ASSERT(rp->r_lock);

	/* Did we sleep?  If so, if the page is now valid, drop the reserve */
	/* THIS CODE IS WRONG - CLEAN IT UP AFTER WE RUN OUT OF GAS */
	if(memreserve(rp, 1)  &&  base->pgm.pg_v)
	{	freemem++;
		return 1;
	}

	ASSERT(rp->r_lock);

	/* pfdall() can't fail */
	pfd = pfdall(rp);

	pfdmap(base, pfd, validate);
	return 0;
}


/*
 * Simple-minded memory allocator - replaces some uses of kmem_alloc().
 * Rules:
 *  1) The alloc call may sleep (so generally, don't call from interrupts)
 *  2) requests are in byte counts
 *  3) returns are either NULL (failed) or a valid address; the address is
 *     the beginning of a chunk of memory at least as big as requested.
 *  4) The free call takes BOTH the returned address and the requested size.
 *  5) The returned chunk will be physical IFF the requested size is a page
 *     or less.  Otherwise, it will be in "kernel virtual" space.
 *  6) Small requests are rounded to a multiple of a basic chunk (256 bytes);
 *     large chunks are rounded to a multiple of pages.
 */
caddr_t
getmem(bytecount)
register unsigned int bytecount;
{	register unsigned int chunksize;
	register caddr_t p;

	if (bytecount > ptob(1))
		p = kvalloc(btop(bytecount), Lx_RW, -1);
	else
	{	chunksize = (bytecount+(1<<PTSZSHFT)-1)/(1<<PTSZSHFT);
		p = (caddr_t)ptblalloc(chunksize);
	}
	if (p == NULL)
	        panic("getmem returning NULL\n");
	return(p);
}

/*
 * Free the chunk of memory allocated by getmem().  Both the address and the
 *  size are used to determine how to release memory.  If the two are
 *  inconsistant, we panic (otherwise, we are likely to confuse things).
 */
releasemem(address, bytecount)
register caddr_t address;
register unsigned int bytecount;
{	register unsigned int chunksize;

	if (bytecount > (unsigned int)ptob(1)) {
	        if (address >= (caddr_t)KV_SPT)
		        kvfree(address, btop(bytecount), 1);
	        else
		        panic("GETMEM Botch\n");
	} else if (address < (caddr_t)KV_SPT) {
		chunksize = (bytecount+(1<<PTSZSHFT)-1)/(1<<PTSZSHFT);
		ptblfree(address, chunksize);
	} else
		panic("GETMEM Botch\n");
}
