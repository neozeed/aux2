#ifndef lint	/* .../sys/SVFS/sys/5_inode.c */
#define _AC_NAME Z_5_inode_c
#define _AC_NO_MAIN "@(#) Copyright (c) 1983-87 Sun Microsystems Inc., 1985-87 UniSoft Corporation, All Rights Reserved.  {Apple version 2.1 89/10/14 15:25:31}"
#include <apple_notice.h>

#ifdef _AC_HISTORY
  static char *sccsid = "@(#)Copyright Apple Computer 1989\tVersion 2.1 of 5_inode.c on 89/10/14 15:25:31";
#endif		/* _AC_HISTORY */
#endif		/* lint */

#define _AC_MODS
/*      @(#)ufs_inode.c 1.1 86/02/03 SMI; from UCB 4.36 83/06/11        */
/*      @(#)ufs_inode.c 2.1 86/04/14 NFSSRC */

#include "sys/types.h"
#include "sys/param.h"
#include "sys/time.h"
#include "sys/mmu.h"
#include "sys/page.h"
#include "sys/systm.h"
#include "sys/signal.h"
#include "sys/user.h"
#include "sys/errno.h"
#include "sys/vfs.h"
#include "sys/vnode.h"
#include "sys/conf.h"
#include "sys/buf.h"
#include "sys/mount.h"
#include "svfs/inode.h"
#include "svfs/filsys.h"
#include "sys/var.h"
#include "sys/sysmacros.h"
#include "sys/sysinfo.h"
#ifdef QUOTA
#include "sys/quota.h"
#endif
#include "vax/vaxque.h"

#ifdef	HOWFAR
extern int	T_iaccess;
#endif

#define	INOHSZ	128
#define	INOHASH(dev,ino)	(((unsigned)((dev)+(ino)))%INOHSZ)

union ihead {				/* inode LRU cache, Chris Maltby */
	union  ihead *ih_head[2];
	struct inode *ih_chain[2];
} ihead[INOHSZ];

struct inode *ifreeh, **ifreet;

/*
 * Initialize hash links for inodes
 * and build inode free list.
 */
svfs_inoinit()
{
	register int i;
	register struct inode *ip = inode;
	register union  ihead *ih = ihead;

	for (i = INOHSZ; --i >= 0; ih++) {
		ih->ih_head[0] = ih;
		ih->ih_head[1] = ih;
	}
	ifreeh = ip;
	ifreet = &ip->i_freef;
	ip->i_freeb = &ifreeh;
	ip->i_forw = ip;
	ip->i_back = ip;
	ip->i_vnode.v_data = (caddr_t)ip;
	ip->i_vnode.v_op = &svfs_vnodeops;
	for (i = v.v_inode; --i > 0; ) {
		++ip;
		ip->i_forw = ip;
		ip->i_back = ip;
		*ifreet = ip;
		ip->i_freeb = ifreet;
		ifreet = &ip->i_freef;
		ip->i_vnode.v_data = (caddr_t)ip;
		ip->i_vnode.v_op = &svfs_vnodeops;
	}
	ip->i_freef = NULL;
}

#ifdef notdef
/*
 * Find an inode if it is incore.
 * This is the equivalent, for inodes,
 * of ``incore'' in bio.c or ``pfind'' in subr.c.
 */
struct inode *
ifind(dev, ino)
	register dev_t dev;
	register ino_tl ino;
{
	register struct inode *ip;
	register union  ihead *ih;

	ih = &ihead[INOHASH(dev, ino)];
	for (ip = ih->ih_chain[0]; ip != (struct inode *)ih; ip = ip->i_forw)
		if (ino == ip->i_number && dev == ip->i_dev)
			return (ip);
	return ((struct inode *)0);
}
#endif notdef

/*
 * Look up an inode by device,inumber.
 * If it is in core (in the inode structure),
 * honor the locking protocol.
 * If it is not in core, read it in from the
 * specified device.
 * If the inode is mounted on, perform
 * the indicated indirection.
 * In all cases, a pointer to a locked
 * inode structure is returned.
 *
 * panic: no imt -- if the mounted file
 *	system is not in the mount table.
 *	"cannot happen"
 */
struct inode *
iget(dev, fs, ino)
	register dev_t dev;
	register struct filsys *fs;
	register ino_tl ino;
{
	register struct inode *ip;
	register struct buf *bp;
	register struct inode *iq;
	struct vnode *vp;
	union  ihead *ih;
	struct dinode *dp;
	struct mount *mp;

	/*
	 * lookup inode in cache
	 */
	sysinfo.iget++;
loop:
	if ((mp = getmp(dev)) == NULL)
		panic("iget: bad dev\n");
	if (mp->m_bufp->b_un.b_fs != fs)
		panic("iget: bad fs");

	ih = &ihead[INOHASH(dev, ino)];

	for (ip = ih->ih_chain[0]; ip != (struct inode *)ih; ip = ip->i_forw)
		if (ino == ip->i_number && dev == ip->i_dev) {
			/*
			 * found it. check for locks
			 */
			if ((ip->i_flag & ILOCKED) != 0) {
				ip->i_flag |= IWANT;
				(void) sleep((caddr_t)ip, PINOD);
				goto loop;
			}
			/*
			 * If inode is on free list, remove it.
			 */
			if ((ip->i_flag & IREF) == 0) {
				if (iq = ip->i_freef)
					iq->i_freeb = ip->i_freeb;
				else
					ifreet = ip->i_freeb;
				*ip->i_freeb = iq;
				ip->i_freef = NULL;
				ip->i_freeb = NULL;
			}
			/*
			 * mark inode locked and referenced and return it.
			 */
			ip->i_flag |= ILOCKED | IREF;
			ip->i_lockedfile = __FILE__;
			ip->i_lockedline = __LINE__;
#ifdef ITRACE
			itrace(ip, caller(), 1);
			timeout(ilpanic, ip, 30 * v.v_hz);
#endif
			VN_HOLD(ITOV(ip));
			return(ip);
		}

	/*
	 * Inode was not in cache. Get free inode slot for new inode.
	 */
	if (ifreeh == NULL) {
		dnlc_purge();
	}
	if ((ip = ifreeh) == NULL) {
		tablefull("inode");
		syserr.inodeovf++;
		u.u_error = ENFILE;
		return(NULL);
	}
	if (iq = ip->i_freef)
		iq->i_freeb = &ifreeh;
	ifreeh = iq;
	ip->i_freef = NULL;
	ip->i_freeb = NULL;
	/*
	 * Now to take inode off the hash chain it was on
	 * (initially, or after an iflush, it is on a "hash chain"
	 * consisting entirely of itself, and pointed to by no-one,
	 * but that doesn't matter), and put it on the chain for
	 * its new (ino, dev) pair
	 */
	remque(ip);
	insque(ip, ih);
#ifdef QUOTA
	dqrele(ip->i_dquot);
#endif
	ip->i_flag = ILOCKED | IREF;
	ip->i_lockedfile = __FILE__;
	ip->i_lockedline = __LINE__;
#ifdef ITRACE
	itrace(ip, caller(), 1);
	timeout(ilpanic, ip, 30 * v.v_hz);
#endif
	ip->i_dev = dev;
	ip->i_devvp = mp->m_devvp;
	ip->i_number = ino;
	ip->i_diroff = 0;
	ip->i_fs = fs;
	ip->i_lastr = 0;
	bp = bread(ip->i_devvp, (daddr_t)FsLTOP(fs, FsITOD(fs, ino)), (int)FsBSIZE(fs));
	/*
	 * Check I/O errors
	 */
	if ((bp->b_flags & B_ERROR) != 0) {
		brelse(bp);
		/*
		 * the inode doesn't contain anything useful, so it would
		 * be misleading to leave it on its hash chain.
		 * 'iput' will take care of putting it back on the free list.
		 */
		remque(ip);
		ip->i_forw = ip;
		ip->i_back = ip;
		/*
		 * we also loose its inumber, just in case (as iput
		 * doesn't do that any more) - but as it isn't on its
		 * hash chain, I doubt if this is really necessary .. kre
		 * (probably the two methods are interchangable)
		 */
		ip->i_number = 0;
		iunlock(ip);
		iinactive(ip);
		return(NULL);
	}
	dp = bp->b_un.b_dino;
	iunpack(ip, dp + FsITOO(fs, ino));

	vp = ITOV(ip);
	VN_INIT(vp, mp->m_vfsp, IFTOVT(ip->i_mode), ip->i_rdev);
	if (ino == (ino_t)ROOTINO)
		vp->v_flag |= VROOT;
	brelse(bp);
#ifdef QUOTA
	if (ip->i_mode != 0)
		ip->i_dquot = getinoquota(ip);
#endif
	return (ip);
}

/*
 * Unlock inode and vrele associated vnode
 */
iput(ip)
	register struct inode *ip;
{

	if ((ip->i_flag & ILOCKED) == 0)
		panic("iput");
	iunlock(ip);
	VN_RELE(ITOV(ip));
}

/*
 * Check that inode is not locked and release associated vnode.
 */
irele(ip)
	register struct inode *ip;
{
	if (ip->i_flag & ILOCKED)
		panic("irele");
	VN_RELE(ITOV(ip));
}

/*
 * Drop inode without going through the normal chain of unlocking
 * and releasing.
 */
idrop(ip)
	register struct inode *ip;
{
	register struct vnode *vp = &ip->i_vnode;

	if ((ip->i_flag & ILOCKED) == 0)
		panic("idrop");
	IUNLOCK(ip);
	if (--vp->v_count == 0) {
		ip->i_flag = 0;
		/*
		 * Put the inode back on the end of the free list.
		 */
		if (ifreeh) {
			*ifreet = ip;
			ip->i_freeb = ifreet;
		} else {
			ifreeh = ip;
			ip->i_freeb = &ifreeh;
		}
		ip->i_freef = NULL;
		ifreet = &ip->i_freef;
	}
}

/*
 * Vnode is no loger referenced, write the inode out and if necessary,
 * truncate and deallocate the file.
 */
iinactive(ip)
	register struct inode *ip;
{
	int mode;

	if (ip->i_flag & ILOCKED)
		panic("svfs_inactive");
	if (ip->i_fs->s_ronly == 0) {
		ip->i_flag |= ILOCKED;
		ip->i_lockedfile = __FILE__;
		ip->i_lockedline = __LINE__;
#ifdef ITRACE
		itrace(ip, caller(), 1);
		timeout(ilpanic, ip, 30 * v.v_hz);
#endif
		if (ip->i_nlink <= 0) {
			ip->i_gen++;
			itrunc(ip, (u_long)0);
			mode = ip->i_mode;
			ip->i_mode = 0;
			ip->i_rdev = 0;
			imark(ip, IUPD|ICHG);
			ifree(ip, ip->i_number, mode);
#ifdef QUOTA
			(void)chkiq(VFSTOM(ip->i_vnode.v_vfsp),
			    ip, ip->i_uid, 0);
			dqrele(ip->i_dquot);
			ip->i_dquot = NULL;
#endif
		}
		if (ip->i_flag & (IUPD|IACC|ICHG))
			iupdat(ip, 0);
		iunlock(ip);
	}
	ip->i_flag = 0;
	/*
	 * Put the inode on the end of the free list.
	 * Possibly in some cases it would be better to
	 * put the inode at the head of the free list,
	 * (eg: where i_mode == 0 || i_number == 0)
	 * but I will think about that later .. kre
	 * (i_number is rarely 0 - only after an i/o error in iget,
	 * where i_mode == 0, the inode will probably be wanted
	 * again soon for an ialloc, so possibly we should keep it)
	 */
	if (ifreeh) {
		*ifreet = ip;
		ip->i_freeb = ifreet;
	} else {
		ifreeh = ip;
		ip->i_freeb = &ifreeh;
	}
	ip->i_freef = NULL;
	ifreet = &ip->i_freef;
}

/*
 * Check accessed and update flags on
 * an inode structure.
 * If any is on, update the inode
 * If waitfor is given, then must insure
 * i/o order so wait for write to complete.
 */
iupdat(ip, waitfor)
	register struct inode *ip;
	int waitfor;
{
	register struct buf *bp;
	register struct filsys *fp;
	struct dinode *dp;

	fp = ip->i_fs;
	if ((ip->i_flag & (IUPD|IACC|ICHG)) != 0) {
		if (fp->s_ronly)
			return;
		bp = bread(ip->i_devvp,
		          (daddr_t)FsLTOP(fp, FsITOD(fp, ip->i_number)), FsBSIZE(fp));
		if (bp->b_flags & B_ERROR) {
			brelse(bp);
			return;
		}
		dp = bp->b_un.b_dino + FsITOO(fp, ip->i_number);
		ipack(dp, ip);
		ip->i_flag &= ~(IUPD|IACC|ICHG);
		if (waitfor)
			bwrite(bp);
		else
			bdwrite(bp);
	}
}

/*
 * Mark the accessed, updated, or changed times in an inode
 * with the current (unique) time
 */
imark(ip, flag)
	register struct inode *ip;
	register int flag;
{
	struct timeval ut;

	uniqtime(&ut);
	ip->i_flag |= flag;
	if (flag & IACC)
		ip->i_atime = ut.tv_sec;
	if (flag & IUPD)
		ip->i_mtime = ut.tv_sec;
	if (flag & ICHG) {
		ip->i_diroff = 0;
		ip->i_ctime = ut.tv_sec;
	}
}

#define	SINGLE	0	/* index of single indirect block */
#define	DOUBLE	1	/* index of double indirect block */
#define	TRIPLE	2	/* index of triple indirect block */
/*
 * Truncate the inode ip to at most
 * length size.  Free affected disk
 * blocks -- the blocks of the file
 * are removed in reverse order.
 *
 * NB: triple indirect blocks are untested.
 */
itrunc(oip, length)
	register struct inode *oip;
	u_long length;
{
	register struct inode *ip;
	register i;
	register daddr_t lastblock;
	register daddr_t bn;
	register int level;
	register long blocksreleased = 0;
	register long nblocks;
	daddr_t  lastiblock[NIADDR];
	struct inode tip;
	long indirtrunc();

	if (oip->i_size <= length) {
		imark(oip, IUPD|ICHG);
		iupdat(oip, 1);
		return;
	}
	/*
	 * Calculate index into inode's block list of
	 * last direct and indirect blocks (if any)
	 * which we want to keep.  Lastblock is -1 when
	 * the file is truncated to 0.
	 */
	lastblock = FsBNO(oip->i_fs, length + FsBSIZE(oip->i_fs) - 1) - 1;
	lastiblock[SINGLE] = lastblock - NDADDR;
	lastiblock[DOUBLE] = lastiblock[SINGLE] - FsNINDIR(oip->i_fs);
	lastiblock[TRIPLE] = lastiblock[DOUBLE] - FsNINDIR(oip->i_fs) * FsNINDIR(oip->i_fs);
	nblocks = btod(FsBSIZE(oip->i_fs));
	/*
	 * Update size of file and block pointers
	 * on disk before we start freeing blocks.
	 * If we crash before free'ing blocks below,
	 * the blocks will be returned to the free list.
	 * lastiblock values are also normalized to -1
	 * for calls to indirtrunc below.
	 * (? fsck doesn't check validity of pointers in indirect blocks)
	 */
	tip = *oip;
	for (level = TRIPLE; level >= SINGLE; level--)
		if (lastiblock[level] < 0) {
			oip->i_addr[NDADDR+level] = 0;
			lastiblock[level] = -1;
		}
	for (i = NDADDR - 1; i > lastblock; i--)
		oip->i_addr[i] = 0;
	oip->i_size = length;
	imark(oip, IUPD|ICHG);
	iupdat(oip, 1);
	ip = &tip;

	/*
	 * Indirect blocks first.
	 */
	for (level = TRIPLE; level >= SINGLE; level--) {
		bn = ip->i_addr[NDADDR+level];
		if (bn != 0) {
			blocksreleased +=
			    indirtrunc(ip, bn, lastiblock[level], level);
			if (lastiblock[level] < 0) {
				ip->i_addr[NDADDR+level] = 0;
				free(ip, bn, (off_t)FsBSIZE(ip->i_fs));
				blocksreleased += nblocks;
			}
		}
		if (lastiblock[level] >= 0)
			goto done;
	}

	/*
	 * All whole direct blocks
	 */
	for (i = NDADDR - 1; i > lastblock; i--) {
		register int size;

		bn = ip->i_addr[i];
		if (bn == 0)
			continue;
		ip->i_addr[i] = 0;
		size = (off_t)FsBSIZE(ip->i_fs);
		free(ip, bn, (off_t) size);
		blocksreleased += btod(size);
	}
	if (lastblock < 0)
		goto done;

done:
/* BEGIN PARANOIA */
	for (level = SINGLE; level <= TRIPLE; level++)
		if (ip->i_addr[NDADDR+level] != oip->i_addr[NDADDR+level])
			panic("itrunc1");
	for (i = 0; i < NDADDR; i++)
		if (ip->i_addr[i] != oip->i_addr[i])
			panic("itrunc2");
/* END PARANOIA */
	imark(oip, ICHG);
#ifdef QUOTA
	(void) chkdq(oip, -blocksreleased, 0);
#endif
}

/*
 * Release blocks associated with the inode ip and
 * stored in the indirect block bn.  Blocks are free'd
 * in LIFO order up to (but not including) lastbn.  If
 * level is greater than SINGLE, the block is an indirect
 * block and recursive calls to indirtrunc must be used to
 * cleanse other indirect blocks.
 *
 * NB: triple indirect blocks are untested.
 */
long
indirtrunc(ip, bn, lastbn, level)
	register struct inode *ip;
	daddr_t bn;
        register daddr_t lastbn;
	register int level;
{
	register daddr_t *bap;
	register struct buf *bp, *copy;
	register daddr_t nb, last;
	register int i;
	register int blocksreleased = 0;
	long factor;
	int  nblocks;

	/*
	 * Calculate index in current block of last
	 * block to be kept.  -1 indicates the entire
	 * block so we need not calculate the index.
	 */
	factor = 1;
	for (i = SINGLE; i < level; i++)
		factor *= FsNINDIR(ip->i_fs);
	last = lastbn;
	if (lastbn > 0)
		last /= factor;
	nblocks = btod(FsBSIZE(ip->i_fs));
	/*
	 * Get buffer of block pointers, zero those 
	 * entries corresponding to blocks to be free'd,
	 * and update on disk copy first.
	 */
	copy = geteblk(FsBSIZE(ip->i_fs));
	bp = bread(ip->i_devvp, (daddr_t) FsLTOP(ip->i_fs, bn), FsBSIZE(ip->i_fs));
	if (bp->b_flags&B_ERROR) {
		brelse(copy);
		brelse(bp);
		return (0);
	}
	bap = bp->b_un.b_daddr;
	bcopy((caddr_t)bap, (caddr_t)copy->b_un.b_daddr, (unsigned) FsBSIZE(ip->i_fs));
	bzero((caddr_t)&bap[last + 1],
	  (u_int)(FsNINDIR(ip->i_fs) - (last + 1)) * sizeof (daddr_t));
	bwrite(bp);
	bp = copy, bap = bp->b_un.b_daddr;

	/*
	 * Recursively free totally unused blocks.
	 */
	for (i = FsNINDIR(ip->i_fs) - 1; i > last; i--) {
		nb = bap[i];
		if (nb == 0)
			continue;
		if (level > SINGLE)
			blocksreleased +=
			    indirtrunc(ip, nb, (daddr_t)-1, level - 1);
		free(ip, nb, (off_t) FsBSIZE(ip->i_fs));
		blocksreleased += nblocks;
	}

	/*
	 * Recursively free last partial block.
	 */
	if (level > SINGLE && lastbn >= 0) {
		last = lastbn % factor;
		nb = bap[i];
		if (nb != 0)
			blocksreleased += indirtrunc(ip, nb, last, level - 1);
	}
	brelse(bp);
	return (blocksreleased);
}

/*
 * remove any inodes in the inode cache belonging to dev
 *
 * There should not be any active ones, return error if any are found
 * (nb: this is a user error, not a system err)
 *
 * Also, count the references to dev by block devices - this really
 * has nothing to do with the object of the procedure, but as we have
 * to scan the inode table here anyway, we might as well get the
 * extra benefit.
 *
 * this is called from sumount()/sys3.c when dev is being unmounted
 */
#ifdef QUOTA
iflush(dev, iq)
	register dev_t dev;
	register struct inode *iq;
#else
iflush(dev)
	register dev_t dev;
#endif
{
	register struct inode *ip;
	register open = 0;

	for (ip = inode; ip < (struct inode *) v.ve_inode; ip++) {
#ifdef QUOTA
		if (ip != iq && ip->i_dev == dev)
#else
		if (ip->i_dev == dev)
#endif
			if (ip->i_flag & IREF)
				return(-1);
			else {
				remque(ip);
				ip->i_forw = ip;
				ip->i_back = ip;
				/*
				 * as i_count == 0, the inode was on the free
				 * list already, just leave it there, it will
				 * fall off the bottom eventually. We could
				 * perhaps move it to the head of the free
				 * list, but as umounts are done so
				 * infrequently, we would gain very little,
				 * while making the code bigger.
				 */
#ifdef QUOTA
				dqrele(ip->i_dquot);
				ip->i_dquot = NULL;
#endif
			}
		else if ((ip->i_flag & IREF) && (ip->i_mode&IFMT)==IFBLK &&
		    ip->i_rdev == dev)
			open++;
	}
	return (open);
}

/*
 * Lock an inode. If its already locked, set the WANT bit and sleep.
 */
ilock(ip, filename, lineno)
	register struct inode *ip;
	char *filename;
	int lineno;
{

	ILOCK(ip, filename, lineno);
}

/*
 * Unlock an inode.  If WANT bit is on, wakeup.
 */
iunlock(ip)
	register struct inode *ip;
{

	if (!(ip->i_flag & ILOCKED)) {
		panic("iunlock");
	}
	IUNLOCK(ip);
}

/*
 * Check mode permission on inode.
 * Mode is READ, WRITE or EXEC.
 * In the case of WRITE, the
 * read-only status of the file
 * system is checked.
 * Also in WRITE, prototype text
 * segments cannot be written.
 * The mode is shifted to select
 * the owner/group/other fields.
 * The super user is granted all
 * permissions.
 */
iaccess(ip, m)
	register struct inode *ip;
	register int m;
{
	register int *gp;

#ifdef	HOWFAR
	if (T_iaccess)
		printf("iaccess(inode[%d], 0%o):  ", ip - &inode[0], m);
#endif
	if (m & IWRITE) {
		register struct vnode *vp;

		vp = ITOV(ip);
		/*
		 * Disallow write attempts on read-only
		 * file systems; unless the file is a block
		 * or character device resident on the
		 * file system.
		 */
		if (ip->i_fs->s_ronly != 0) {
			if ((ip->i_mode & IFMT) != IFCHR &&
			    (ip->i_mode & IFMT) != IFBLK) {
#ifdef	HOWFAR
				if (T_iaccess)
					printf("i_mode:  0%o:  EROFS\n", ip->i_mode);
#endif
				u.u_error = EROFS;
				return (EROFS);
			}
		}
		/*
		 * If there's shared text associated with
		 * the inode, try to free it up once.  If
		 * we fail, we can't allow writing.
		 */
		if (vp->v_flag & VTEXT)
			xrele(vp);
		if (vp->v_flag & VTEXT) {
			u.u_error = ETXTBSY;
#ifdef	HOWFAR
			if (T_iaccess)
				printf("vp:  0x%x:  ETXTBSY\n", vp);
#endif
			return (ETXTBSY);
		}
	}
	/*
	 * If you're the super-user,
	 * you always get access.
	 */
#ifdef	HOWFAR
	if (u.u_uid == 0) {
		if (T_iaccess)
			printf("u_uid:  0:  error 0\n");
		return (0);
	}
	else
		if (T_iaccess)
			printf("u_uid:  %d\t", u.u_uid);
#else
	if (u.u_uid == 0)
		return (0);
#endif
	/*
	 * Access check is based on only
	 * one of owner, group, public.
	 * If not owner, then check group.
	 * If not a member of the group, then
	 * check public access.
	 */
	if (u.u_uid != ip->i_uid) {
#ifdef	HOWFAR
		if (T_iaccess)
			printf("i_uid:  %d\ti_gid:  %d\tu_gid:  %d\t", ip->i_uid, ip->i_gid, u.u_gid);
#endif
		m >>= 3;
		if (u.u_gid == ip->i_gid)
			goto found;
		gp = u.u_groups;
		for (; gp < &u.u_groups[NGROUPS] && *gp != NOGROUP; gp++)
			if (ip->i_gid == *gp)
				goto found;
		m >>= 3;
	}
found:
#ifdef	HOWFAR
	if ((ip->i_mode & m) == m) {
		if (T_iaccess)
			printf("error 0\n");
		return (0);
	}
	else
		if (T_iaccess)
			printf("i_mode:  0%o m:  0%o EACCES\n", ip->i_mode, m);
#else
	if ((ip->i_mode & m) == m)
		return (0);
#endif
	u.u_error = EACCES;
	return (EACCES);
}

iunpack(ip, dp)
	register struct inode *ip;
	register struct dinode *dp;
{
	register char *p1, *p2;
	register int i;

	ip->i_mode = dp->di_mode;
	ip->i_nlink = dp->di_nlink;
	ip->i_uid = dp->di_uid;
	ip->i_gid = dp->di_gid;
	ip->i_size = dp->di_size;
	p1 = (char *)ip->i_addr;
	p2 = (char *)dp->di_addr;
	i = NADDR - 1;
	do {
		*p1++ = 0;
		*p1++ = *p2++;
		*p1++ = *p2++;
		*p1++ = *p2++;
	} while (--i != -1);
	ip->i_gen = (long) dp->di_gen;
	ip->i_atime = dp->di_atime;
	ip->i_mtime = dp->di_mtime;
	ip->i_ctime = dp->di_ctime;
}

ipack(dp, ip)
	register struct dinode *dp;
	register struct inode *ip;
{
	register char *p1, *p2;
	register int i;

	dp->di_mode = ip->i_mode;
	dp->di_nlink = ip->i_nlink;
	dp->di_uid = ip->i_uid;
	dp->di_gid = ip->i_gid;
	dp->di_size = ip->i_size;
	p1 = (char *)dp->di_addr;
	p2 = (char *)ip->i_addr;
	if ((ip->i_mode & IFMT) == IFIFO) {
		i = NFADDR - 1;
		do {
			if (*p2++ != 0)
				printf("iaddress > 2^24\n");
			*p1++ = *p2++;
			*p1++ = *p2++;
			*p1++ = *p2++;
		} while (--i != -1);
		i = NADDR - NFADDR - 1;
		if (i >= 0) {
			do {
				*p1++ = 0;
				*p1++ = 0;
				*p1++ = 0;
			} while (--i != -1);
		}
	} else {
		i = NADDR - 1;
		do {
			if(*p2++ != 0)
				printf("iaddress > 2^24\n");
			*p1++ = *p2++;
			*p1++ = *p2++;
			*p1++ = *p2++;
		} while (--i != -1);
	}
	dp->di_gen = (char) ip->i_gen;
	dp->di_atime = ip->i_atime;
	dp->di_mtime = ip->i_mtime;
	dp->di_ctime = ip->i_ctime;
}

#ifdef ITRACE
struct itr {
	int (*it_cfn)();
	struct inode *it_ip;
	int it_lock;
} itracebuf[NITRACE];
int itracex;

itrace(ip, cfn, lock)
	struct inode *ip;
	int (*cfn)();
	int lock;
{
	struct itr *itp;

	itp = &itracebuf[itracex++];
	itp->it_cfn = cfn;
	itp->it_ip = ip;
	itp->it_lock = lock;
	if (itracex >= NITRACE)
		itracex = 0;
}

ilpanic(ip)
	struct inode *ip;
{
	printf("ilpanic: hung locked inode");
	printf("\n\tip = 0x%x, dev = 0x%x, ino = %d, flags = 0x%x\n",
	    ip, ip->i_dev, ip->i_number, ip->i_flag);
}
#endif
