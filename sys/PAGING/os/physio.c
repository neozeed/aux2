#ifndef lint	/* .../sys/PAGING/os/physio.c */
#define _AC_NAME physio_c
#define _AC_NO_MAIN "@(#) Copyright (c) 1987, 1988, 1989 Apple Computer, Inc., 1985-87 UniSoft Corporation, All Rights Reserved.  {Apple version 2.2 89/11/29 15:27:01}"
#include <apple_notice.h>

#ifdef _AC_HISTORY
  static char *sccsid = "@(#)Copyright Apple Computer 1987, 1988, 1989\tVersion 2.2 of physio.c on 89/11/29 15:27:01";
#endif		/* _AC_HISTORY */
#endif		/* lint */

#define _AC_MODS

/*	@(#)physio.c	UniPlus VVV.2.1.2	*/

#ifdef HOWFAR
extern int T_swap;
extern int T_dophys;
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
#include "sys/sysinfo.h"
#include "sys/dir.h"
#include "sys/map.h"
#include "sys/signal.h"
#include "sys/time.h"
#include "sys/user.h"
#include "sys/errno.h"
#include "sys/buf.h"
#include "sys/conf.h"
#include "sys/var.h"
#include "sys/vnode.h"
#include "sys/region.h"
#include "sys/proc.h"
#include "sys/swap.h"
#include "sys/getpages.h"
#include "sys/uio.h"
#include "sys/debug.h"
#endif lint


/* Max. number of pages to swap per I/O */

extern caddr_t physiobuf;
extern short physiosize;

#define NPAGE		((physiosize > 0)? physiosize : 1)
int  usingphysiobuf;	/* used to lock the dedicated physio buffer */

/*
 * swap I/O
 */

swap(pglptr, npage, rw)
register pglst_t	*pglptr;
{
	register struct buf	*bp;
	register int	blkno;
	register dev_t	dev;
	register int	i;
	register dbd_t	*dbd;

	syswait.swap++;

	dbd = dbdget(pglptr->gp_ptptr);
	dev = swaptab[dbd->dbd_swpi].st_dev;
	blkno = dbd->dbd_blkno;

	while ((bp = pfreelist.av_forw) == NULL) {
		pfreelist.b_flags |= B_WANTED;
		(void) sleep((caddr_t)&pfreelist, PRIBIO+1);
	}
	pfreelist.av_forw = bp->av_forw;

	bp->b_proc = u.u_procp;
	bp->b_flags = B_BUSY | B_PHYS | rw;
	bp->b_dev = dev;
	bp->b_blkno = blkno;

	for (i = 0; i < npage; i++) {
		bp->b_un.b_addr = (caddr_t)
			(ptob(pptop(pglptr++->gp_ptptr->pgm.pg_pfn)));
		swapseg(dev, bp, 1, rw);
		bp->b_blkno += ptod(1);
	}
	bp->b_flags = 0;
	bp->av_forw = pfreelist.av_forw;
	pfreelist.av_forw = bp;

	if (pfreelist.b_flags & B_WANTED) {
		pfreelist.b_flags &= ~B_WANTED;
		wakeup((caddr_t)&pfreelist);
	}
	syswait.swap--;
}

swapseg(dev, bp, pg, rw)
dev_t		dev;
register struct buf *bp;
register int	pg;
{
	if (rw) {
		sysinfo.swapin++;
		sysinfo.bswapin += pg;
	} else {
		sysinfo.swapout++;
		sysinfo.bswapout += pg;
	}
	u.u_iosw++;

	bp->b_bcount = ptob(pg);

	(*bdevsw[(short)major(dev)].d_strategy)(bp);

	SPLHI();
	while ((bp->b_flags & B_DONE) == 0)
		(void) sleep((caddr_t)bp, PSWP);
	SPL0();

	if ((bp->b_flags & B_ERROR) || bp->b_resid)
		panic("i/o error in swap");
	bp->b_flags &= ~B_DONE;
}


/*
 * Raw I/O. The arguments are
 * The strategy routine for the device
 * A buffer, which is usually NULL, or special buffer
 *   header owned exclusively by the device for this purpose
 * The device number
 * Read/write flag
 */
physio(strat, bp, dev, rw, uio)
register struct buf *bp;
int (*strat)();
dev_t dev;
register struct uio *uio;
{
	register struct iovec *iov;
	register unsigned base, count;
	register int	  hpf, error;

nextiov:
	if (uio->uio_iovcnt == 0)
		return (0);

	iov = uio->uio_iov;
	base = (unsigned)iov->iov_base;
	if (rw)
		sysinfo.phread++;
	else
		sysinfo.phwrite++;
	syswait.physio++;

	hpf = (bp == NULL);

	if (hpf) {
		while ((bp = pfreelist.av_forw) == NULL) {
			pfreelist.b_flags |= B_WANTED;
			(void) sleep((caddr_t)&pfreelist, PRIBIO+1);
		}
		pfreelist.av_forw = bp->av_forw;
	} else while (bp->b_flags & B_BUSY) {
		bp->b_flags |= B_WANTED;
		(void) sleep((caddr_t)bp, PRIBIO+1);
	}
	bp->b_error = 0;
	bp->b_proc = u.u_procp;
	bp->b_un.b_addr = physiobuf;
	bp->b_flags = B_BUSY | B_PHYS | rw;
	bp->b_dev = dev;

	while (usingphysiobuf) {
		usingphysiobuf |= B_WANTED;
		(void) sleep(&usingphysiobuf, PRIBIO);
	}
	usingphysiobuf |= B_BUSY;

	while (count = iov->iov_len) {
		if (count > ptob(NPAGE))
			count = ptob(NPAGE);
		if (!(rw&B_READ)) {
			if (copyin((caddr_t)base, (caddr_t)physiobuf, count)) {
			        bp->b_flags |= B_ERROR;
				bp->b_error = EFAULT;
				break;
			}
		}
		bp->b_blkno = uio->uio_offset >> DEV_BSHIFT;
		bp->b_bcount = count;
		(*strat)(bp);
		SPLHI();
		while ((bp->b_flags & B_DONE) == 0)
			(void) sleep((caddr_t)bp, PRIBIO);
		SPL0();
		bp->b_flags &= ~B_DONE;
		if (bp->b_flags & B_ERROR)
			break;
		if ((count -= bp->b_resid) == 0)
		        break;
		iov->iov_len -= count;
		uio->uio_resid -= count;
		uio->uio_offset += count;
		if ((rw&B_READ)) {
			if (copyout(physiobuf, (caddr_t)base, count)) {
			        bp->b_flags |= B_ERROR;
				bp->b_error = EFAULT;
				break;
			}
		}
		if (bp->b_resid)
			break;
		base += count;
	}
	if (usingphysiobuf & B_WANTED)
		wakeup(&usingphysiobuf);
	usingphysiobuf = 0;

	bp->b_flags &= ~(B_BUSY|B_PHYS);
	error = geterror(bp);
	count = bp->b_resid;

	if (hpf) {
		bp->av_forw = pfreelist.av_forw;
		pfreelist.av_forw = bp;
		bp = (struct buf *)NULL;

		if (pfreelist.b_flags & B_WANTED) {
			pfreelist.b_flags &= ~B_WANTED;
			wakeup((caddr_t)&pfreelist);
		}
	} else if (bp->b_flags & B_WANTED)
		wakeup((caddr_t)bp);

	syswait.physio--;
	if (count || error)
		return(error);
	uio->uio_iov++;
	uio->uio_iovcnt--;
	goto nextiov;
}
/* <@(#)physio.c	1.2> */
