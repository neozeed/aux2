#ifndef lint	/* .../sys/SVFS/sys/5_alloc.c */
#define _AC_NAME Z_5_alloc_c
#define _AC_NO_MAIN "@(#) Copyright (c) 1984-85 AT&T-IS, All Rights Reserved.  {Apple version 2.1 89/10/14 15:19:45}"
#include <apple_notice.h>

#ifdef _AC_HISTORY
  static char *sccsid = "@(#)Copyright Apple Computer 1989\tVersion 2.1 of 5_alloc.c on 89/10/14 15:19:45";
#endif		/* _AC_HISTORY */
#endif		/* lint */

#define _AC_MODS
/*      @(#)ufs_alloc.c 1.1 86/02/03 SMI; from UCB 6.3 84/02/06 */
/*      @(#)ufs_alloc.c 2.1 86/04/14 NFSSRC */

#include "sys/types.h"
#include "sys/param.h"
#include "sys/time.h"
#include "sys/mmu.h"
#include "sys/page.h"
#include "sys/systm.h"
#include "sys/signal.h"
#include "sys/user.h"
#include "sys/errno.h"
#include "sys/conf.h"
#include "sys/buf.h"
#include "sys/vfs.h"
#include "sys/vnode.h"
#include "sys/vio.h"
#include "sys/sysmacros.h"
#include "svfs/inode.h"
#ifdef QUOTA
#include "sys/quota.h"
#endif QUOTA
#include "svfs/filsys.h"
#include "svfs/fblk.h"
#include "sys/mount.h"

/*
 * Allocate a block in the file system.
 * 
 * The size of the requested block is given, which must be fs_size
 * bpref is not used.
 *
 * It will obtain the next available free disk block from the free list
 * on the specified device.
 */
struct buf *
alloc(ip, bpref, size)
	register struct inode *ip;
	daddr_t bpref;
	int size;
{
	register daddr_t bno;
	register struct filsys *fs;
	register struct buf *bp;
	
#ifdef	lint
	bpref = bpref;
#endif
	fs = ip->i_fs;
	if ((unsigned)size != FsBSIZE(fs)) {
		pre_panic();
		printf("dev = 0x%x, bsize = %d, size = %d\n",
		    ip->i_dev, FsBSIZE(fs), size);
		panic("alloc: bad size");
	}
	while (fs->s_flock)
		(void) sleep((caddr_t)&fs->s_flock, PINOD);
#ifdef QUOTA
	u.u_error = chkdq(ip, (long)btod(size), 0);
	if (u.u_error)
		return (NULL);
#endif
	do {
		if (fs->s_nfree <= 0)
			goto nospace;
		if ((bno = fs->s_free[--fs->s_nfree]) == 0)
			goto nospace;
	} while (badblock(fs, bno, ip->i_dev));

	if (fs->s_nfree <= 0) {
		fs->s_flock++;
		bp = bread(ip->i_devvp, (daddr_t) FsLTOP(fs, bno),
			FsBSIZE(fs));
		if (u.u_error == 0) {
			fs->s_nfree = (bp->b_un.b_fblk)->df_nfree;
			bcopy((caddr_t)(bp->b_un.b_fblk)->df_free,
			    (caddr_t)fs->s_free, sizeof(fs->s_free));
		}
		brelse(bp);
		/*
		 * Prevent "dups in free"
		 */
		bp = getblk(ip->i_devvp, (daddr_t) SBLOCK, SBSIZE);
		bcopy((caddr_t)fs, bp->b_un.b_addr, sizeof(struct filsys));
		fs->s_fmod = 0;
		fs->s_time = time.tv_sec;
		bwrite(bp);
		fs->s_flock = 0;
		wakeup((caddr_t)&fs->s_flock);
	}
	if (fs->s_nfree <= 0 || fs->s_nfree > NICFREE) {
		prdev("Bad free count", ip->i_dev);
		goto nospace;
	}
	bp = getblk(ip->i_devvp, (daddr_t) FsLTOP(fs, bno), size);
	clrbuf(bp);

	if (fs->s_tfree)
		fs->s_tfree--;
	fs->s_fmod = 1;
	return (bp);
nospace:
	fserr(fs, "file system full");
	prdev("no space", ip->i_dev);
	u.u_error = ENOSPC;
	return (NULL);
}

/*
 * Free a block
 *
 * The specified block is placed back in the
 * free map.
 */
free(ip, bno, size)
	register struct inode *ip;
	register daddr_t bno;
	off_t size;
{
	register struct filsys *fs;
	register struct buf *bp;

	fs = ip->i_fs;

	if ((unsigned)size != FsBSIZE(fs)) {
		pre_panic();
		printf("dev = 0x%x, bsize = %d, size = %d\n",
		    ip->i_dev, FsBSIZE(fs), size);
		panic("free: bad size");
	}
	if ((ip->i_mode&IFMT) == IFREG) {
	        register int i;
	        register daddr_t dbn;

	        dbn = FsLTOP(fs, bno);
	        i = 0;
	        do {
		        pfremove(dbn, ip->i_dev);
			dbn += NBPP / DEV_BSIZE;
			i += NBPP;
		} while (i < size);
	}
	fs->s_fmod = 1;
	while (fs->s_flock)
		(void) sleep((caddr_t)&fs->s_flock, PINOD);
	if (badblock(fs, bno, ip->i_dev)) {
		printf("bad block %d, ino %d\n", bno, ip->i_number);
		return;
	}
	if (fs->s_nfree <= 0) {
		fs->s_nfree = 1;
		fs->s_free[0] = 0;
	}
	if (fs->s_nfree >= NICFREE) {
		fs->s_flock++;
		bp = getblk(ip->i_devvp, (daddr_t) FsLTOP(fs, bno),
			FsBSIZE(fs));
		(bp->b_un.b_fblk)->df_nfree = fs->s_nfree;
		bcopy((caddr_t)fs->s_free,
			(caddr_t)(bp->b_un.b_fblk)->df_free,
			sizeof(fs->s_free));
		fs->s_nfree = 0;
		bwrite(bp);
		fs->s_flock = 0;
		wakeup((caddr_t)&fs->s_flock);
	}
	fs->s_free[fs->s_nfree++] = bno;
	fs->s_tfree++;
	fs->s_fmod = 1;
}

/*
 * Check that a block number is in the range between the I list
 * and the size of the device.
 * This is used mainly to check that a
 * garbage file system has not been mounted.
 *
 * bad block on dev x/y -- not in range
 */
badblock(fp, bn, dev)
register struct filsys *fp;
daddr_t bn;
dev_t dev;
{

	if (bn < fp->s_isize || bn >= fp->s_fsize) {
		prdev("bad block", dev);
		return(1);
	}
	return(0);
}

/*
 * Allocate an inode in the file system.
 * 
 * Used with file creation.
 * The algorithm keeps up to NICINOD spare I nodes in the
 * super block. When this runs out, a linear search through the
 * I list is instituted to pick up NICINOD more.
 */
struct inode *
ialloc(pip, mode, nlink)
	register struct inode *pip;
	int mode;
	int nlink;
{
	register struct filsys *fs;
	register struct inode  *ip;
	register struct dinode *dp;
	register int i;
	register daddr_t adr;
	register ino_tl ino;
	struct buf *bp;
	
	fs = pip->i_fs;
#ifdef QUOTA
	u.u_error =
	    chkiq(VFSTOM(pip->i_vnode.v_vfsp),
		(struct inode *)NULL, u.u_uid, 0);
	if (u.u_error)
		return (NULL);
#endif
loop:
	while (fs->s_ilock)
		(void) sleep((caddr_t)&fs->s_ilock, PINOD);
	if (fs->s_ninode > 0
	    && (ino = fs->s_inode[--fs->s_ninode])) {
		if ((ip = iget(pip->i_dev, fs, ino)) == NULL)
			return(NULL);
		if (ip->i_mode == 0) {
			/* found inode: update now to avoid races */
			ip->i_mode = mode;
			ip->i_nlink = nlink;
			ip->i_flag |= IACC|IUPD|ICHG/* |ISYN */;
			ip->i_uid = u.u_uid;
			ip->i_gid = u.u_gid;
			ip->i_size = 0;
			for (i = 0; i < NADDR; i++)
				ip->i_addr[i] = 0;
			if (fs->s_tinode)
			        fs->s_tinode--;
			fs->s_fmod = 1;
			imark(ip, IACC|IUPD|ICHG);
			iupdat(ip, 0);
			return(ip);
		}
		/*
		 * Inode was allocated after all.
		 * Look some more.
		 */
		imark(ip, IACC|IUPD|ICHG);
		iupdat(ip, 0);
		iput(ip);
		goto loop;
	}
	fs->s_ilock++;
	fs->s_ninode = NICINOD;
	ino = FsINOS(fs, fs->s_inode[0]);

	for (adr = FsITOD(fs, ino); fs->s_ninode > 0 && adr < fs->s_isize; adr++) {
		if (vio_okay(pip->i_devvp, pip->i_dev)) {
			/* set up readahead array */
			daddr_t rabv[MAXVIO+1];
			extern int maxba;
			extern struct buf *vbreada();

			for (i = 0; i < maxba; i++) {
				if ((rabv[i] = adr + 1 + i) >= fs->s_isize)
				        break;
			}
			rabv[i] = 0;
			bp = vbreada(pip->i_devvp, (daddr_t) FsLTOP(fs, adr),
				FsBSIZE(fs), FsLTOP(fs, rabv[0]), rabv, FsLTOP(fs,1));
		}  else  {
			/* do 1-block read */
			bp = bread(pip->i_devvp, (daddr_t)FsLTOP(fs,adr), FsBSIZE(fs));
		}
		if (u.u_error) {
			brelse(bp);
			ino += FsINOPB(fs);
			continue;
		}
		dp = bp->b_un.b_dino;

		for (i = 0; i < FsINOPB(fs); i++, ino++, dp++) {
			if (fs->s_ninode <= 0)
				break;
			if (dp->di_mode == 0)
				fs->s_inode[--fs->s_ninode] = ino;
		}
		brelse(bp);
	}
	fs->s_ilock = 0;
	wakeup((caddr_t)&fs->s_ilock);

	if (fs->s_ninode > 0) {
		fs->s_inode[fs->s_ninode-1] = 0;
		fs->s_inode[0] = 0;
	}
	if (fs->s_ninode != NICINOD) {
		fs->s_ninode = NICINOD;
		goto loop;
	}
	fs->s_ninode = 0;
	fs->s_tinode = 0;
	prdev("Out of inodes", pip->i_dev);
	u.u_error = ENOSPC;
	return(NULL);
}

/*
 * Free an inode.
 *
 * The specified inode is placed back in the free map.
 */
/*ARGSUSED2*/
ifree(ip, ino, mode)
	struct inode *ip;
	ino_tl ino;
	int mode;
{
	register struct filsys *fs;

	fs = ip->i_fs;
	fs->s_tinode++;
	if (fs->s_ilock)
		return;
	fs->s_fmod = 1;
	if (fs->s_ninode >= NICINOD) {
		if (ino < fs->s_inode[0])
			fs->s_inode[0] = ino;
		return;
	}
	fs->s_inode[fs->s_ninode++] = ino;
}

/*
 * Fserr prints the name of a file system with an error diagnostic.
 * 
 * The form of the error message is:
 *	fs: error message
 *
 * This only works on 4.2, not v.2 (lyssa)
 */
fserr(fs, cp)
	struct filsys *fs;
	char *cp;
{

#ifdef	lint
	fs = fs;
#endif
	printf("fserr: %s\n", cp);
}

