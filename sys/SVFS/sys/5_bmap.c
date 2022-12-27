#ifndef lint	/* .../sys/SVFS/sys/5_bmap.c */
#define _AC_NAME Z_5_bmap_c
#define _AC_NO_MAIN "@(#) Copyright (c) 1984-85 AT&T-IS, All Rights Reserved.  {Apple version 2.1 89/10/14 15:22:47}"
#include <apple_notice.h>

#ifdef _AC_HISTORY
  static char *sccsid = "@(#)Copyright Apple Computer 1987\tVersion 2.1 of 5_bmap.c on 89/10/14 15:22:47";
#endif		/* _AC_HISTORY */
#endif		/* lint */

#define _AC_MODS
/*      @(#)ufs_bmap.c 1.1 86/02/03 SMI; from UCB 5.4 83/03/15  */
/*      @(#)ufs_bmap.c  2.1 86/04/14 NFSSRC */

#include "sys/types.h"
#include "sys/param.h"
#include "sys/time.h"
#include "sys/mmu.h"
#include "sys/page.h"
#include "sys/region.h"
#include "sys/systm.h"
#include "sys/conf.h"
#include "sys/signal.h"
#include "sys/user.h"
#include "sys/errno.h"
#include "sys/vnode.h"
#include "sys/buf.h"
#include "sys/proc.h"
#include "svfs/inode.h"
#include "svfs/filsys.h"

/*
 * Bmap defines the structure of file system storage
 * by returning the physical block number on a device given the
 * inode and the logical block number in a file.
 * When convenient, it also leaves the physical
 * block number of the next block of the file in rablock
 * for use in read-ahead.
 */
/*VARARGS3*/
daddr_t
bmap(ip, bn, rwflg, size, sync)
	register struct inode *ip;
	daddr_t bn;
	int rwflg;
	int size;	/* not used for svfs */
	int sync;	/* supplied only when rwflg == B_WRITE */
{
	/* map old call to new without rabv return */
	return (vbmap(ip, bn, rwflg, size, sync, 0, 0));
}
/*VARARGS3*/
daddr_t
vbmap(ip, bn, rwflg, size, sync,  rabc, rabv)
	register struct inode *ip;
	daddr_t bn;
	int rwflg;
	int size;	/* not used for svfs */
	int sync;	/* supplied only when rwflg == B_WRITE */
	int rabc;	/* rabv length */
	daddr_t rabv[]; /* readahead logical blkno vector */
{
	register int i;
	struct buf *bp, *nbp;
	int j, sh;
	daddr_t nb;
	int nbap;	/* number bufferahead */
	daddr_t *bap;	/* bufferahead array pointer */

#ifdef	lint
	size = size;
#endif
	if (bn < 0) {
		u.u_error = EFBIG;
		return ((daddr_t)-1);
	}
	u.u_rablock = 0;
	nbap = 0;

	if (rwflg == B_WRITE && bn >= FsPTOL(ip->i_fs, u.u_limit)) {
		u.u_error = EFBIG;
		return((daddr_t)-1);
	}

	/*
	 * The first NDADDR blocks are direct blocks
	 */
	if (bn < NDADDR) {
		i = bn;
		nb = ip->i_addr[i];
		if (nb == 0) {
			if (rwflg == B_READ)
				return ((daddr_t)-1);
			bp = alloc(ip, (daddr_t) 0, FsBSIZE(ip->i_fs));
			if (bp == NULL)
				return ((daddr_t)-1);
			nb = FsPTOL(ip->i_fs, bp->b_blkno);
			ip->i_addr[i] = nb;
			imark(ip, IUPD|ICHG);
			if ((ip->i_mode&IFMT) == IFDIR)
				/*
				 * Write directory blocks synchronously
				 * so they never appear with garbage in
				 * them on the disk.
				 */
				bwrite(bp);
			else
				bdwrite(bp);
		}
		if (bn < NDADDR )
			u.u_rablock = FsLTOP(ip->i_fs, ip->i_addr[bn + 1]);

		/* Here, there can be up to 11 contiguous (logical) block
		 * addresses in the first part of the inode (10 direct,
		 * and 1st indirect).  Combine into into a single read
		 * acceleration vector if they are in fact consecutive.
		 */
		if ( rabc )  {
			bap = &ip->i_addr[bn+1];
			nbap = min( NDADDR-bn, rabc);
			
			/* copy only consecutive addresses */
			for ( i=0; i<nbap; i++)  {
				if ( (rabv[i] = bap[i]) != bap[0] + i)
					break;	/* not consecutive */
			}
			rabv[i] = 0;	/* zero terminates list to maxba+1 */
		}
		return (nb);
	}

	/*
	 * Determine how many levels of indirection.
	 */
	sh = 0;
	nb = 1;
	bn -= NDADDR;
	for (j = NIADDR; j>0; j--) {
		sh += FsNSHIFT(ip->i_fs);
		nb <<= FsNSHIFT(ip->i_fs);
		if (bn < nb)
			break;
		bn -= nb;
	}
	if (j == 0) {
		u.u_error = EFBIG;
		return ((daddr_t)-1);
	}

	/*
	 * fetch the first indirect block
	 */
	nb = ip->i_addr[NADDR - j];
	if (nb == 0) {
		if (rwflg == B_READ)
			return ((daddr_t)-1);
	        bp = alloc(ip, (daddr_t) 0, FsBSIZE(ip->i_fs));
		if (bp == NULL)
			return ((daddr_t)-1);
		nb = FsPTOL(ip->i_fs, bp->b_blkno);
		/*
		 * Write synchronously so that indirect blocks
		 * never point at garbage.
		 */
		bwrite(bp);
		ip->i_addr[NADDR - j] = nb;
		imark(ip, IUPD|ICHG);
	}

	/*
	 * fetch through the indirect blocks
	 */
	for (; j <= NIADDR; j++) {
		bp = bread(ip->i_devvp, (daddr_t)FsLTOP(ip->i_fs, nb),
		    (int)FsBSIZE(ip->i_fs));
		if (bp->b_flags & B_ERROR) {
			brelse(bp);
			return ((daddr_t)-1);
		}
		bap = bp->b_un.b_daddr;
		sh -= FsNSHIFT(ip->i_fs);
		i = (bn >> sh) & FsNMASK(ip->i_fs);
		nb = bap[i];
		if (nb == 0) {
			if (rwflg==B_READ) {
				brelse(bp);
				return ((daddr_t)-1);
			}
		        nbp = alloc(ip, (daddr_t) 0, FsBSIZE(ip->i_fs));
			if (nbp == NULL) {
				brelse(bp);
				return ((daddr_t)-1);
			}
			nb = FsPTOL(ip->i_fs, nbp->b_blkno);
			if (j < NIADDR || (ip->i_mode&IFMT) == IFDIR || sync) {
				/*
				 * Write synchronously so indirect blocks
				 * never point at garbage and blocks
				 * in directories never contain garbage.
				 */
				bwrite(nbp);
			} else {
				bdwrite(nbp);
			}
			bap[i] = nb;
			if (sync) {
				bwrite(bp);
			} else {
				bdwrite(bp);
			}
		} else {
			brelse(bp);
		}
	}

	/*
	 * calculate read-ahead.
	 */
	++i;
	/*
	 * This fixes the case where bn is the last block in an indirect
	 * block and read-ahead goes over the edge. Caused sony problems.
	 */
	if(i < FsNINDIR(ip->i_fs)) {
		u.u_rablock = FsLTOP(ip->i_fs, bap[i]);
	} else {
		u.u_rablock = 0;
	}

	/* Here there can be an entire indirect block's worth of 
	 * consecutive addresses.  Copy up to *maxba* consecutive
	 * addresses for readahead acceleration.
	 */
	if ( rabc ) {
		nbap = FsNINDIR(ip->i_fs) - i;	/* rem in ptr blk */
		bap = &bap[i];			/* re-base array fwa */
		nbap = min( nbap, rabc);

		/* copy only consecutive readahead addresses */
		for ( i=0; i<nbap; i++)  {
			if ( (rabv[i] = bap[i]) != bap[0] + i )
				break;	/* not consecutive */
		}
		rabv[i] = 0;	/* zero terminated to maxba+1 */
	}
	return (nb);
}
