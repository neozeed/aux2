#ifndef lint	/* .../sys/psn/io/vio.c */
#define _AC_NAME vio_c
#define _AC_NO_MAIN "@(#) Copyright (c) 1989 Apple Computer, Inc., All Rights Reserved.  {Apple version 2.4 90/02/17 13:36:32}"
#include <apple_notice.h>

#ifdef _AC_HISTORY
#endif		/* _AC_HISTORY */
#endif		/* lint */

/*  vio.c ver 1.1, Gene Dronek, Vulcan Lab, Feb 7 1988  */
#include "sys/types.h"
#include "sys/mmu.h"
#include "sys/sysmacros.h"
#include "sys/param.h"
#include "sys/time.h"
#include "sys/page.h"
#include "sys/region.h"
#include "sys/systm.h"
#include "sys/signal.h"
#include "sys/user.h"
#include "sys/errno.h"
#include "sys/var.h"
#include "sys/buf.h"
#include "sys/vio.h"
#include "sys/conf.h"
#include "sys/config.h"
#include "sys/proc.h"
#include "sys/trace.h"
#include "sys/vnode.h"
#include "sys/vfs.h"
#include "sys/mount.h"
#include "sys/sysinfo.h"

#define  MAXTRANSFER  (32 * 1024)
int  maxwb = 7;
int  maxba = 7;			/* max buffer-ahead */
int  vulcan_on = 1;		/* vulcan read-ahead/write-behind is on? */
int  vbreada_tot, vbreada_cnt;	/* buffer-ahead stats */
int  vbwrite_tot, vbwrite_cnt;	/* write-behind stats */
struct buf *vincore();

vio_init()
{
    if (vulcan_on) {
#if 0
	printf("Vulcan Labs I/O Acceleration:");
#else
	printf("I/O Acceleration:");
#endif
	printf("  version g21,  maxba=%d,  maxwb=%d\n", maxba, maxwb);
    }
}


/*
 * Read in the block, along with up to *maxba* read-ahead blocks.
 */
struct buf *
vbreada( vp, blkno, size, rablkno, rabv, fsltop)
	register struct vnode *vp;
	register daddr_t blkno;
	register int size;
	daddr_t rablkno;
	daddr_t rabv[];
	register int fsltop;
{
	register struct buf *bp;
	register struct buf *tp;
	register struct vio *viop;
	register int i, n, bn;
	struct buf *rabp = (struct buf *)NULL;

	/*
	 *   If not scatter-gather vio device, use plain breada
	 */
	if (!vio_okay(vp, vp->v_rdev))
		return(breada(vp, blkno, size, rablkno, size));
	if (maxba > MAXVIO)
	        maxba = MAXVIO;

	/*
	 *   If the block isn't in core, then allocate
	 *   a buffer and initiate i/o
	 */
	bp = NULL;
	if (!incore(vp, blkno))  {
		sysinfo.lread++;
		bp = getblk(vp, blkno, size);
		if ((bp->b_flags&B_DONE) == 0) {
			bp->b_flags |= B_READ;
			if (bp->b_bcount > bp->b_bufsize)
				panic("vbreada");
			VOP_STRATEGY(bp);
			u.u_ior++;
			sysinfo.bread++;
			trace(TR_BREADMISS, vp, blkno);
		} else
			trace(TR_BREADHIT, vp, blkno);
	}

	/*
	 *   Filter read-ahead, use acceleration if more than 2 blocks.
	 */
	if (rablkno && (rabv[0] * fsltop) == rablkno) {
		n = 0;
		if (rabv[1]) {
			/*
			 *  Allocate and fill the vio struct we will
			 *  pass to the driver.  The driver will free
			 *  this struct when I/O is started.
			 */
			viop = (struct vio *)getmem(sizeof *viop);
			viop->vio_tot = 0;
			IOVADD(viop,0,(struct buf *)0,0,0);

			/*  fill vio struct with block pointers */
			for (i = 0; i < maxba && rabv[i] == (rabv[0] + i); i++) {
				bn = rabv[i] * fsltop;
				if (incore(vp, bn))
					break;
				tp = getblk(vp, bn, size);
				if (tp->b_flags & B_DONE) {
					brelse(tp);
					break;
				}
				if (tp->b_bcount > tp->b_bufsize)
					panic("vbreada");
				tp->b_flags |= B_READ|B_ASYNC;
				IOVADD(viop, i, tp, tp->b_un.b_addr, size);
				n++;
			}

			/* check final count of acceleration blocks */
			if ( n == 0 )  {
				/* none after all, release vio mem */
				releasemem(viop, sizeof *viop);	
			}  else  {
				/* set for scatter VIO read-ahead */
				rabp = viop->vio_gdiskbp[0];
				rabp->b_flags |= B_VIO;
				rabp->b_resid = (long)viop;
			}
		}  else if (!incore(vp, rablkno)) {
			/* set for 1-block plain read-ahead */
			n = 1;
			rabp = getblk(vp, rablkno, size);
			if (rabp->b_flags & B_DONE) {
				brelse( rabp);
				rabp = (struct buf *)NULL;
			}
		}

		/*
		 *  Start i/o on readahead block(s)
		 */
		if (rabp != (struct buf *)NULL) {
			if (rabp->b_bcount > rabp->b_bufsize)
				panic("vbreada");
			rabp->b_flags |= B_READ|B_ASYNC;
			VOP_STRATEGY(rabp);
			u.u_ior       += n;	/* update statistics */
			sysinfo.bread += n;
			vbreada_tot   += n;
			vbreada_cnt++;
		}
	}
	
	/*
	 * Finally, if the original block was in core, let bread get it.
	 * If block wasn't in core, then the read was started above,
	 * and just wait for it.
	 */
	if (bp == NULL)
		return(bread(vp, blkno, size));
	biowait(bp);
	return (bp);
}

/*
 * Write the buffer along with any contiguous blocks.
 * Wait for completion.
 * Then release the buffer.
 */
vbwrite(bp)
register struct buf *bp;
{
	register struct buf *tp;
	register int	 nb;
	register int     bn;
	register int     rnd;
	register int     maxbsize;
	register int     i;
	register int     start;
	int              synchronous;
	int              total;
	struct   mount   *mp;
	struct   buf     *bptrs[MAXVIO * 2];
	extern   int     basyncnt;

	sysinfo.lwrite++;

	if (maxwb > MAXVIO)
	        maxwb = MAXVIO;
	if (bp->b_bcount > bp->b_bufsize)
		panic("vbwrite");
	if (bp->b_flags & B_ASYNC) {
	        if (bp->b_flags & B_DELWRI)
		      bp->b_flags |= B_AGE;
		basyncnt++;
		synchronous = 0;
	} else
		synchronous = 1;
	bp->b_flags &= ~(B_READ | B_DONE | B_ERROR | B_DELWRI);
	bptrs[maxwb - 1] = bp;
	nb = 1;                         /* num blks collected */

	if ((mp = getmp(bp->b_dev)) == NULL)
	        goto cant_coalesce;
	maxbsize = mp->m_vfsp->vfs_bsize;
	rnd = maxbsize / DEV_BSIZE;

	/* probe cache for delwrite blocks in front of bp */
	bn = bp->b_blkno;
	start = maxwb - 1;
	total = bp->b_bcount;

	while (nb < maxwb && (tp = vincore(bp->b_vp, (bn -= rnd), maxbsize)))  {
	        if ((total += tp->b_bcount) > MAXTRANSFER)
		        break;
		nb++;
		bptrs[--start] = tp;
	}
	if (total < MAXTRANSFER) {
	        bn = bp->b_blkno;            /* probe for blocks after end */
		i = maxwb;
		tp = bp;

		while (nb < maxwb &&
		      (tp = vincore(bp->b_vp, (bn += (tp->b_bcount / DEV_BSIZE)), 0)))  {
		        if ((total += tp->b_bcount) > MAXTRANSFER)
			        break;
		        nb++;
			bptrs[i++] = tp;
		}
	}
	/* coalesce cache hits into multi-block write */
	if (nb > 1) {
		register struct vio *viop;

		/* build vio starting with front block */
		viop = (struct vio *)getmem(sizeof *viop);
		viop->vio_tot = 0;
		IOVADD(viop,0,0,0,0);

		for (i = 0; i < nb; i++) {
		        tp = bptrs[start + i];
			if (tp != bp) {
				if (tp->b_bcount > tp->b_bufsize)
				        panic("vbwvio");
				notavail(tp);
				tp->b_flags |= B_ASYNC|B_AGE;
				tp->b_flags &= ~(B_READ|B_DONE|B_ERROR|B_DELWRI);
				basyncnt++;
			}
			IOVADD(viop, i, tp, tp->b_un.b_addr, tp->b_bcount);
		}
		/* set bp for gather VIO write */
		bp = bptrs[start];
		bp->b_flags |= B_VIO;
		bp->b_resid = (long)viop;
	}
cant_coalesce:
	trace(TR_BWRITE, bp->b_vp, bp->b_blkno);
	/*
	 *  Start write(-behind) of block(s)
	 */
	VOP_STRATEGY(bp);
	u.u_iow        += nb;
	sysinfo.bwrite += nb;
	vbwrite_tot    += nb;
	vbwrite_cnt++;

	/*
	 * If synchronous, await write-completion of original bp.
	 */
	if (synchronous) {
	        bp = bptrs[maxwb - 1];      /* get original requested bp */
		biowait(bp);
		brelse(bp);
	}
}

/*
 *  test if logical block in cache, return handle or null (if not)
 */
static struct buf *vincore(vp, blkno, maxbsize)
register struct vnode *vp;
register daddr_t blkno;
register int     maxbsize;
{
	register struct buf *bp;
	register struct buf *dp;

	dp = BUFHASH(vp, blkno);

	for (bp = dp->b_forw; bp != dp; bp = bp->b_forw)  {
		if (bp->b_blkno == blkno && bp->b_vp == vp &&
		   (bp->b_flags & (B_INVAL | B_DELWRI | B_BUSY)) == B_DELWRI) {
		        if (maxbsize == 0 || bp->b_bcount == maxbsize)
			        return (bp);
			break;
		}
	}
	return ((struct buf *)0);
}

/*
 *  Iodone on scatter-gather bp.  Copy error flag, remove VIO bit
 */
viodone( bp, vp, niovsent)
	register struct buf *bp;
	register struct vio *vp;
{
	register int i;
	register eflag = B_ERROR & bp->b_flags;

	if ( vp && vp->vio_niov && vp->vio_ngdiskbp )  {
		/* post completion for bp array */
		for ( i = 0; i < vp->vio_ngdiskbp; i++ )  {
			register struct buf *tp = vp->vio_gdiskbp[i];
			/* no error propagation blks successfully done */
			tp->b_flags |= ((i<niovsent) ? 0 : eflag);
			tp->b_flags &= ~B_VIO;
			iodone( tp);
		}
	}  else 
		iodone( bp );
}

/*
 *  Check whether dev can do vio.  Return true if it can.
 */
vio_okay(vp, dev)
	register struct vnode *vp;
	register dev_t	dev;
{
        extern struct vnodeops dev_vnode_ops;

	if (vp == NULL || vp->v_op != &dev_vnode_ops ||
		major(dev) < 24+0 || major(dev) > 24+7) {
		return(0);
	} else {
		return(vulcan_on);
	}
}
