#ifndef lint	/* .../sys/PAGING/os/bio.c */
#define _AC_NAME bio_c
#define _AC_NO_MAIN "@(#) Copyright (c) 1983-87 Sun Microsystems Inc., 1985-87 UniSoft Corporation, All Rights Reserved.  {Apple version 2.2 90/02/12 13:09:34}"
#include <apple_notice.h>

#ifdef _AC_HISTORY
  static char *sccsid = "@(#)Copyright Apple Computer 1987\tVersion 2.2 of bio.c on 90/02/12 13:09:34";
#endif		/* _AC_HISTORY */
#endif		/* lint */

#define _AC_MODS
/*	@(#)bio.c	UniPlus VVV.2.1.1	*/

#include	<sys/param.h>
#include	<sys/types.h>
#include	<sys/mmu.h>
#include	<sys/sysmacros.h>
#include	<sys/time.h>
#include	<sys/page.h>
#include	<sys/systm.h>
#include	<sys/sysinfo.h>
#include	<sys/signal.h>
#include	<sys/user.h>
#include	<sys/errno.h>
#include	<sys/buf.h>
#include	<sys/iobuf.h>
#include	<sys/conf.h>
#include	<sys/region.h>
#include	<sys/proc.h>
#include	<sys/var.h>
#include	<sys/uio.h>

extern	struct map *bufmap;
/*
 *
 */
allocbuf(tp, size)
register struct buf *tp;
register int size;
{
	register caddr_t	x;

	if (size <= 0 || size > MAXBSIZE) {
		printf("allocbuf: size = %d\n", size);
		panic("allocbuf");
	}
	/*
	 * Buffer size does not change.
	 */
	if (tp->b_bufsize == size) {
	        tp->b_bcount = size;
		return(1);
	}
	/*
	 * buffer is shrinking - return space to the free map.
	 */
	if (tp->b_bufsize > size) {
		mfree(bufmap, tp->b_bufsize-size, tp->b_un.b_addr+size);
		tp->b_bufsize = size;
	        tp->b_bcount = size;
		return(1);
	}
	/*
	 * grow the buffer - if we cannot allocate enough memory
	 * steal memory from buffers on the "most free" list,
	 * put the empty buffers back onto the "empty" list.
	 */
	while ((x = (caddr_t)malloc(bufmap, size)) == 0) {
		register struct buf *bp;

		bp = getnewbuf(BQ_AGE);
		mfree(bufmap, bp->b_bufsize, bp->b_un.b_addr);
		bp->b_bcount = bp->b_bufsize = 0;
		bremhash(bp);
		binshash(bp, &bfreelist[BQ_EMPTY]);
		bp->b_dev = NODEV;
		bp->b_error = 0;
		bp->b_flags |= B_INVAL;
		brelse(bp);
	}
	/*
	 * copy data from old area to new, then free the old space
	 */
	if (tp->b_bcount)
		bcopy(tp->b_un.b_addr, x, tp->b_bcount);
	if (tp->b_bufsize)
		mfree(bufmap, tp->b_bufsize, tp->b_un.b_addr);
	tp->b_un.b_addr = x;
	tp->b_bufsize = size;
	tp->b_bcount = size;
	return(1);
}

/*
 * Release space associated with a buffer.
 */
bfree(bp)
        struct buf *bp;
{
        bp->b_bcount = 0;
}
