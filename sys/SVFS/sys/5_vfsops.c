#ifndef lint	/* .../sys/SVFS/sys/5_vfsops.c */
#define _AC_NAME Z_5_vfsops_c
#define _AC_NO_MAIN "@(#) Copyright (c) 1983-87 Sun Microsystems Inc., All Rights Reserved.  {Apple version 2.3 90/02/19 14:12:05}"
#include <apple_notice.h>

#ifdef _AC_HISTORY
  static char *sccsid = "@(#)Copyright Apple Computer 1989\tVersion 2.3 of 5_vfsops.c on 90/02/19 14:12:05";
#endif		/* _AC_HISTORY */
#endif		/* lint */

#define _AC_MODS
/*      @(#)ufs_vfsops.c 1.1 86/02/03 SMI; from UCB 4.1 83/05/27        */
/*      @(#)ufs_vfsops.c        2.2 86/05/14 NFSSRC */

#include "sys/types.h"
#include "sys/param.h"
#include "sys/time.h"
#include "sys/mmu.h"
#include "sys/page.h"
#include "sys/region.h"
#include "sys/systm.h"
#include "sys/signal.h"
#include "sys/user.h"
#include "sys/errno.h"
#include "sys/proc.h"
#include "sys/buf.h"
#include "sys/pathname.h"
#include "sys/vfs.h"
#include "sys/vnode.h"
#include "sys/file.h"
#include "sys/uio.h"
#include "sys/conf.h"
#include "svfs/filsys.h"
#include "sys/mount.h"
#include "svfs/inode.h"
#include "sys/var.h"
#include "sys/sysmacros.h"
#include "sys/ssioctl.h"

/*
 * svfs vfs operations.
 */
extern int svfs_mount();
extern int svfs_unmount();
extern int svfs_root();
extern int svfs_statfs();
extern int svfs_sync();
extern int svfs_vget();
extern int svfs_mountroot();
extern int svfs_unmountroot();
extern int svfs_testfs();
extern int svfs_mountable();


struct vfsops svfs_vfsops = {
	svfs_mount,
	svfs_unmount,
	svfs_root,
	svfs_statfs,
	svfs_sync,
	svfs_vget,
	svfs_mountroot,
	svfs_unmountroot,
	svfs_testfs,
	svfs_mountable
};

extern struct vnode *devtovp();

/*
 * Default device to mount on.
 */
extern dev_t rootdev;
extern struct vnode *rootvp;


/*
 * svfs_mount system call
 */
svfs_mount(vfsp, path, data)
	struct vfs *vfsp;
	char *path;
	caddr_t data;
{
	int error;
	dev_t dev;
	struct vnode *vp;
	struct svfs_args args;

	/*
	 * Get arguments
	 */
	error = copyin(data, (caddr_t)&args, sizeof (struct svfs_args));
	if (error) {
		return (error);
	}
	if ((error = getmdev(args.fspec, &dev)) != 0)
		return (error);
	/*
	 * Mount the filesystem.
	 */
	error = svfs_mountfs(dev, path, vfsp);
	return (error);
}

/*
 * Called by vfs_mountroot when svfs is going to be mounted as root
 */
svfs_mountroot(iflag)
int iflag;     /* non zero if initial mount of root, zero if remounting root */
{
	register struct vfs *vfsp;
	register struct filsys *fsp;
	register int error;
        struct mount *mp;
	extern short readonlyroot;

        if (iflag) {
                rootvp = devtovp(rootdev);
                vfsp = (struct vfs *)kmem_alloc(sizeof (struct vfs));
                VFS_INIT(vfsp, &svfs_vfsops, (caddr_t)0);
        } else {
                vfsp = rootvfs;
                mp = (struct mount *)vfsp->vfs_data;
                xumount(vfsp);
                dnlc_purge();
#ifdef QUOTA
                iflush(mp->m_dev, mp->m_qinod);
#else
                iflush(mp->m_dev);
#endif /* QUOTA */
                binval(rootvp);
        }
	if (readonlyroot) {
		vfsp->vfs_flag |= VFS_RDONLY;
	}
        if (error = svfs_mountfs(rootdev, "/", vfsp)) {
		kmem_free((caddr_t)vfsp, sizeof (struct vfs));
		return (error);
        }
        if (error = vfs_add((struct vnode *)0, vfsp, 0)) {
                svfs_unmountfs(vfsp);
                kmem_free((caddr_t)vfsp, sizeof (struct vfs));
                return (error);
        }
        vfs_unlock(vfsp);

	/*
	 * In the event that PRAM wasn't valid (i.e. the battery
	 * was removed or went bad), attempt to initialize unix
	 * time based on the last time recorded on the boot block.
	 * init_time will ignore the call if it isn't necessary.
	 */
        fsp = ((struct mount *)(vfsp->vfs_data))->m_bufp->b_un.b_fs;
	init_time (fsp->s_time);

	return (0);
}

static int
svfs_mountfs(dev, path, vfsp)
register dev_t dev;
char *path;
register struct vfs *vfsp;
{
	register struct filsys *fsp;
	register struct mount *mp = 0;
	register struct buf *bp = 0;
	struct buf *tp = 0;
        struct filsys *tfsp;
	struct vnode *dev_vp;
	register int error;
	int root = 0;

#ifdef	lint
	path = path;
#endif
        /*
	 * If we are mounting the root device we want to treat
	 * the superblock state specially
	 */
	if (dev == rootdev)
        	root = 1;

	/*
	 * Open block device mounted on.
	 * When bio is fixed for vnodes this can all be vnode operations
	 */
	error = (*bdevsw[major(dev)].d_open)(dev,
		(vfsp->vfs_flag & VFS_RDONLY) ? FREAD : FREAD|FWRITE);
	if (error)
		return (error);

	/*
	 * read in superblock
	 */
	dev_vp = devtovp(dev);
	tp = bread(dev_vp, SBLOCK, SBSIZE);
	if (tp->b_flags & B_ERROR) {
		error = EIO;
		goto out;
	}

	/*
	 * check for dev already mounted on
	 */
	for (mp = &mounttab[0]; mp < (struct mount *)v.ve_mount; mp++) {
		if (mp->m_bufp != NULL && dev == mp->m_dev) {
		        mp = 0;
			error = EBUSY;
			goto out;
		}
	}
	/*
	 * find empty mount table entry
	 */
	for (mp = &mounttab[0]; mp < (struct mount *)v.ve_mount; mp++) {
		if (mp->m_bufp == 0)
			break;
	}
	if (mp >= (struct mount *)v.ve_mount) {
	        mp = 0;
	        error = EBUSY;
		goto out;
	}
	mp->m_bufp = tp;	      /* just to reserve this slot */
	mp->m_dev = NODEV;
        mp->m_vfsp = vfsp;
	vfsp->vfs_data = (caddr_t)mp;

	/*
	 * Copy the super block into a buffer in its native size.
	 */
	vfsp->vfs_fsid.val[0] = (long)dev;
	vfsp->vfs_fsid.val[1] = MOUNT_SVFS;

	tfsp = tp->b_un.b_fs;
	if (tfsp->s_magic != FsMAGIC) {
	        tp->b_flags |= B_NOCACHE;
		error = EINVAL;
		goto out;
	}
	mp->m_bufp = bp = geteblk(SBSIZE);
	bcopy((caddr_t)tp->b_un.b_addr, (caddr_t)bp->b_un.b_addr,
	   sizeof(struct filsys));
	fsp = bp->b_un.b_fs;

	if (vfsp->vfs_flag & VFS_RDONLY && !root) {
                fsp->s_ronly = 1;
		brelse(tp);
		tp = 0;
        } else {
                if ((fsp->s_state + (long)fsp->s_time) == FsOKAY) {
		        fsp->s_state = FsACTIVE;
                } else {
		        fsp->s_state = FsBAD;
		}
		if (fsp->s_state == FsACTIVE || root) {
                        fsp->s_ronly = 0;
                        fsp->s_fmod = 0;
			/*
			 * Write out superblock with revised state
			 */
			tfsp->s_state = fsp->s_state;
                        tfsp->s_ronly = fsp->s_ronly;
                        tfsp->s_fmod = fsp->s_fmod;
			bwrite(tp);
			tp = 0;
                } else {
			tp->b_flags |= B_NOCACHE;
			error = ENOSPC;
			goto out;
                }
        }
	fsp->s_flock = 0;
	fsp->s_ilock = 0;
	fsp->s_ninode = 0;
	fsp->s_inode[0] = 0;
	vfsp->vfs_bsize = FsBSIZE(fsp);

	mp->m_dev = dev;
	mp->m_devvp = dev_vp;
	VN_RELE(dev_vp);
        (*cdevsw[major(dev)].d_ioctl)(dev, GD_SBZBTMOUNT, 
					&time.tv_sec, FREAD|FWRITE);
	return (0);
out:
	if (mp)
	        mp->m_bufp = 0;
	if (bp)
		brelse(bp);
	if (tp)
		brelse(tp);
	VN_RELE(dev_vp);
        if (error && root && fsp)
                fsp->s_state = FsBAD;
	return (error);
}

/*
 * vfs operations
 */

svfs_unmount(vfsp)
	struct vfs *vfsp;
{
	return (svfs_unmountfs(vfsp));
}

static int
svfs_unmountfs(vfsp)
	register struct vfs *vfsp;
{
	register struct mount *mp;
	register struct filsys *fs;
	register int stillopen;
	register dev_t dev;
	struct buf *bp;
	int flag;
	int root;

	mp = (struct mount *)vfsp->vfs_data;

	if ((dev = mp->m_dev) == rootdev)
                root = 1;
	else
	        root = 0;
#ifdef QUOTA
	if ((stillopen = iflush(dev, mp->m_qinod)) < 0)
		return(EBUSY);
	closedq(mp);
	/*
	 * Here we have to iflush again to get rid of the quota inode.
	 * A drag, but it would be ugly to cheat, & this doesn't happen often
	 */
	iflush(dev, (struct inode *)NULL);
#else
	if ((stillopen = iflush(dev)) < 0)
		return(EBUSY);
#endif
	punmount(dev);        /* flush all cached pages associated with this device */

	fs = mp->m_bufp->b_un.b_fs;
	flag = !fs->s_ronly;

        if (flag && fs->s_state == FsACTIVE) {
                fs->s_time = time.tv_sec;
                fs->s_state = FsOKAY - (long)fs->s_time;
                bp = getblk(mp->m_devvp, SBLOCK, SBSIZE);
                bcopy((caddr_t)fs, bp->b_un.b_addr, SBSIZE);
                bp->b_flags &= ~B_ASYNC;
                bwrite(bp);
        }
	brelse(mp->m_bufp);
	mp->m_bufp = 0;
	mp->m_dev = 0;
	if (!stillopen) {
		register struct vnode *dev_vp;

		(*cdevsw[major(dev)].d_ioctl)(dev, GD_SBZBTUMOUNT, &time.tv_sec,
			(vfsp->vfs_flag & VFS_RDONLY) ? FREAD : FREAD|FWRITE);
		(*bdevsw[major(dev)].d_close)(dev, flag);
		dev_vp = devtovp(dev);
		binval(dev_vp);
		VN_RELE(dev_vp);
	}
	return(0);
}

/*
 * find root of svfs
 */
int
svfs_root(vfsp, vpp)
	struct vfs *vfsp;
	struct vnode **vpp;
{
	register struct mount *mp;
	register struct inode *ip;

	mp = (struct mount *)vfsp->vfs_data;
	ip = iget(mp->m_dev, mp->m_bufp->b_un.b_fs, (ino_tl)ROOTINO);
	if (ip == (struct inode *)0) {
		return (u.u_error);
	}
	iunlock(ip);
	*vpp = ITOV(ip);
	return (0);
}

/*
 * Get file system statistics.
 */
int
svfs_statfs(vfsp, sbp)
register struct vfs *vfsp;
struct statfs *sbp;
{
	register struct filsys *fsp;

	fsp = ((struct mount *)vfsp->vfs_data)->m_bufp->b_un.b_fs;
	if (fsp->s_magic != FsMAGIC)
		panic("svfs_statfs");
	sbp->f_bsize = FsBSIZE(fsp);
	sbp->f_blocks = fsp->s_fsize;
	sbp->f_bfree = fsp->s_tfree;
	sbp->f_bavail = fsp->s_tfree;
	/*
	 * inodes
	 */
	sbp->f_files =  (long) ((fsp->s_isize - 2) * FsINOPB(fsp));
	sbp->f_ffree = (long) fsp->s_tinode;
	bcopy((caddr_t)&vfsp->vfs_fsid, (caddr_t)&sbp->f_fsid, sizeof (fsid_t));

	/*
	 * os/utssys.c needs these for the ustat() function
	 */
	bcopy(fsp->s_fname, sbp->f_fname, sizeof sbp->f_fname);
	bcopy(fsp->s_fpack, sbp->f_fpack, sizeof sbp->f_fpack);
	return (0);
}

/*
 * Flush any pending I/O.
 */
int
svfs_sync(vfsp, flag)
        struct  vfs     *vfsp;
	int		flag;
{
        update(vfsp, flag);
        return (0);
}


/*
 * Common code for mount and umount.
 * Check that the user's argument is a reasonable
 * thing on which to mount, and return the device number if so.
 */
static int
getmdev(fspec, pdev)
	char *fspec;
	dev_t *pdev;
{
	register int error;
	struct vnode *vp;

	/*
	 * Get the device to be mounted
	 */
	error =
	    lookupname(fspec, UIOSEG_USER, FOLLOW_LINK,
		(struct vnode **)0, &vp);
	if (error)
		return (error);
	if (vp->v_type != VBLK) {
		VN_RELE(vp);
		return (ENOTBLK);
	}
	*pdev = vp->v_rdev;
	VN_RELE(vp);
	if (major(*pdev) >= bdevcnt)
		return (ENXIO);
	return (0);
}

sbupdate(mp)
	struct mount *mp;
{
	register struct filsys *fs = mp->m_bufp->b_un.b_fs;
	register struct buf *bp;
	register struct vnode *dev_vp;

	dev_vp = devtovp(mp->m_dev);
	bp = getblk(dev_vp, SBLOCK, SBSIZE);
	bcopy((caddr_t)fs, bp->b_un.b_addr, SBSIZE);
	bwrite(bp);
	VN_RELE(dev_vp);
}


svfs_vget(vfsp, vpp, fidp)
      struct vfs *vfsp;
      struct vnode **vpp;
      struct fid *fidp;
{
    register struct ufid *ufid;
    register struct inode *ip;
    register struct mount *mp;

    mp = (struct mount *)vfsp->vfs_data;
    ufid = (struct ufid *)fidp;
    ip = iget(mp->m_dev, mp->m_bufp->b_un.b_fs, ufid->ufid_ino);
    if (ip == NULL) {
	*vpp = NULL;
	return (0);
    }
    if (ip->i_gen != ufid->ufid_gen) {
	idrop(ip);
	*vpp = NULL;
	return (0);
    }
    iunlock(ip);
    *vpp = ITOV(ip);
    return (0);
}

svfs_unmountroot()
{
        struct vfs *vfsp;
        register struct mount *mp;
        register struct filsys *fsp;
        struct buf *bp = 0;

        vfsp = rootvfs;
        mp = (struct mount *)vfsp->vfs_data;
        fsp = mp->m_bufp->b_un.b_fs;
        if (fsp->s_state == FsACTIVE) {
                fsp->s_time = time.tv_sec;
                fsp->s_state = FsOKAY - (long)fsp->s_time;
                bp = getblk(rootvp, SBLOCK, SBSIZE);
                bcopy((caddr_t)fsp, bp->b_un.b_addr, SBSIZE);
                bp->b_flags &= ~B_ASYNC;
                bwrite(bp);
        }
        return(0);
}
/*
 * Return MOUNT_SVFS if dev contains a SVFS, return -1 otherwise.
 */
int
svfs_testfs(dev)
dev_t dev;
{
	register struct filsys *fsp;
	struct buf *tp = 0;
	struct vnode *dev_vp;
	int error;
	int retval = -1;

	/*
	 * Open block device mounted on.
	 * When bio is fixed for vnodes this can all be vnode operations
	 */
	error = (*bdevsw[major(dev)].d_open)(dev, FREAD);
	if (error) {
		return (-1);
	}
	/*
	 * read in superblock
	 */
	dev_vp = devtovp(dev);
	tp = bread(dev_vp, SBLOCK, SBSIZE);
	if (tp->b_flags & B_ERROR) {
		retval = -1;
		goto out;
	}
	fsp = tp->b_un.b_fs;
	if ((fsp->s_magic != FsMAGIC) ||
	    (fsp->s_isize == 0) ||
	    (fsp->s_fsize <= 0) ) {
		tp->b_flags |= B_NOCACHE;
		retval = -1;
		goto out;
	} else {
	    retval = MOUNT_SVFS;
	}
out:
	if (tp)
	    brelse(tp);
	return (retval);
}

/*
 * return 0 if name is mountable
 * i.e. must be a block device
 * return errno on failure
 */
svfs_mountable(data)
struct svfs_args *data;
{       int error;
	struct vnode *vp;
        struct svfs_args args;

	if (copyin(data, &args, sizeof(struct svfs_args)))
	        return(EFAULT);

	if (error = lookupname(args.fspec, UIOSEG_USER, FOLLOW_LINK, 0, &vp))
	        return(error);

	if (vp->v_type != VBLK) {
		VN_RELE(vp);
		return(ENOTBLK);
	}
	VN_RELE(vp);

	return(0);
}
