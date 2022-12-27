#ifndef lint	/* .../sys/SVFS/sys/5_vnodeops.c */
#define _AC_NAME Z_5_vnodeops_c
#define _AC_NO_MAIN "@(#) Copyright (c) 1983-87 Sun Microsystems Inc., 1985-87 UniSoft Corporation, All Rights Reserved.  {Apple version 2.4 90/04/09 19:38:09}"
#include <apple_notice.h>

#ifdef _AC_HISTORY
  static char *sccsid = "@(#)Copyright Apple Computer 1989\tVersion 2.4 of 5_vnodeops.c on 90/04/09 19:38:09";
#endif		/* _AC_HISTORY */
#endif		/* lint */

#define _AC_MODS
/*      @(#)5_vnodeops.c 	UniPlus 2.1.1.17 */
/*      @(#)ufs_vnodeops.c 1.1 86/02/03 SMI     */
/*      @(#)ufs_vnodeops.c      2.1 86/04/14 NFSSRC */

#ifdef HOWFAR
extern int T_clai;
#endif

#include "compat.h"
#include "sys/types.h"
#include "sys/param.h"
#include "sys/time.h"
#include "sys/mmu.h"
#include "sys/page.h"
#include "sys/region.h"
#include "sys/debug.h"
#include "sys/systm.h"
#include "sys/signal.h"
#include "sys/user.h"
#include "sys/errno.h"
#include "sys/buf.h"
#include "sys/vfs.h"
#include "sys/vnode.h"
#include "sys/proc.h"
#include "sys/file.h"
#include "sys/uio.h"
#include "sys/vio.h"
#include "sys/conf.h"
#include "sys/dir.h"
#undef DIRBLKSIZ
#include "svfs/fsdir.h"
#include "svfs/filsys.h"
#include "svfs/inode.h"
#include "sys/mount.h"
#include "sys/sysinfo.h"
#include "sys/var.h"
#include "sys/sysmacros.h"
#include "sys/stream.h"
#include "krpc/lockmgr.h"

int rasize;
struct vnode *specvp();
#define ISVDEV(t) ((t == VCHR) || (t == VBLK) || (t == VFIFO))
extern int maxba;
struct buf *vbreada();		/* VIO breada */

int svfs_open();
int svfs_close();
int svfs_rdwr();
int svfs_ioctl();
int svfs_select();
int svfs_getattr();
int svfs_setattr();
int svfs_access();
int svfs_lookup();
int svfs_create();
int svfs_remove();
int svfs_link();
int svfs_rename();
int svfs_mkdir();
int svfs_rmdir();
int svfs_readdir();
int svfs_symlink();
int svfs_readlink();
int svfs_fsync();
int svfs_inactive();
int svfs_bmap();
int svfs_badop();
int svfs_bread();
int svfs_brelse();
int svfs_lockctl();
int svfs_fid();
struct vnode *svfs_mkfifo();

struct vnodeops svfs_vnodeops = {
	svfs_open,
	svfs_close,
	svfs_rdwr,
	svfs_ioctl,
	svfs_select,
	svfs_getattr,
	svfs_setattr,
	svfs_access,
	svfs_lookup,
	svfs_create,
	svfs_remove,
	svfs_link,
	svfs_rename,
	svfs_mkdir,
	svfs_rmdir,
	svfs_readdir,
	svfs_symlink,
	svfs_readlink,
	svfs_fsync,
	svfs_inactive,
	svfs_bmap,
	svfs_badop,
	svfs_bread,
	svfs_brelse,
	svfs_lockctl,
	svfs_fid,
	svfs_mkfifo
};
#ifdef	PASS_MAJOR
#define	passmajor(n)	(n)
#else	PASSMAJOR
#define	passmajor(n)	minor(n)
#endif	PASS_MAJOR

/*ARGSUSED2*/
int
svfs_open(vpp, flag, cred)
	struct vnode **vpp;
	int flag;
	struct ucred *cred;
{
	struct inode *ip;

	ip = VTOI(*vpp);

	if ((ip->i_mode & IFMT) == IFSOCK)
		return(EOPNOTSUPP);
	return(0);
}

/*ARGSUSED2*/
int
svfs_close(vp, flag, cred)
	struct vnode *vp;
	int flag;
	struct ucred *cred;
{

	return(0);
}

/*
 * read or write a vnode
 */
/*ARGSUSED4*/
int
svfs_rdwr(vp, uiop, rw, ioflag, cred)
register struct vnode *vp;
register struct uio *uiop;
register enum uio_rw rw;
register int ioflag;
	struct ucred *cred;
{
	register struct inode *ip;
	register ushort mode;
	register int error;

	ip = VTOI(vp);
	mode = ip->i_mode & IFMT;

	if (mode == IFREG || mode == IFIFO) {
		ILOCK(ip, __FILE__, __LINE__);

		if ((ioflag & IO_APPEND) && (rw == UIO_WRITE)) {
			/*
			 * in append mode start at end of file.
			 */
			uiop->uio_offset = ip->i_size;
		}
		if (vp->v_locklist) {
			IUNLOCK(ip);
			if (error = locked(1, ip, uiop->uio_offset,
					  (off_t)(uiop->uio_offset + uiop->uio_resid))) {
				return (error);
			}
			ILOCK(ip, __FILE__, __LINE__);
		}
		error = rwip(ip, uiop, rw, ioflag);

		IUNLOCK(ip);
	} else
		error = rwip(ip, uiop, rw, ioflag);

	return (error);
}

int
rwip(ip, uio, rw, ioflag)
	register struct inode *ip;
	register struct uio *uio;
	enum uio_rw rw;
	int ioflag;
{
	register struct user *up;
	register struct buf *bp;
	register daddr_t lbn, bn;
	register long bsize;
	register int n, on, type;
	struct vnode *devvp;
	dev_t dev;
	int xcnt;
	daddr_t hashbn;

	up = &u;
	dev = (dev_t)ip->i_rdev;
	type = ip->i_mode&IFMT;

	if (rw != UIO_READ && rw != UIO_WRITE)
		panic("rwip");
	if (rw == UIO_READ && uio->uio_resid == 0)
	        return(0);
	if (uio->uio_offset < 0 || (uio->uio_offset + uio->uio_resid) < 0)
		return(EINVAL);
	if (uio->uio_resid == 0)
		return(0);
	if (rw == UIO_READ)
		imark(ip, IACC);
	else {
	        if ((up->u_procp->p_compatflags & COMPAT_BSDSIGNALS) && type == IFREG &&
		    uio->uio_offset+uio->uio_resid > u.u_rlimit[RLIMIT_FSIZE].rlim_cur) {
		        psignal(up->u_procp, SIGXFSZ);
			return(EFBIG);
		}
	}
	up->u_error = 0;
	devvp = ip->i_devvp;
	bsize = FsBSIZE(ip->i_fs);

	do {
		daddr_t rabv[MAXVIO+1];	/* read-ahead computations */

		lbn = uio->uio_offset / bsize;
		on = uio->uio_offset % bsize;
		xcnt = MIN((unsigned)(bsize - on), uio->uio_resid);
		hashbn = -1;

		if (rw == UIO_READ) {
			if (type == IFIFO)
				n = ip->i_size;
			else {
				if ((n = ip->i_size - uio->uio_offset) <= 0)
					return (0);
			}
			if (n < xcnt)
				xcnt = n;
		}
		bn = FsLTOP(ip->i_fs, vbmap(ip, lbn, (rw == UIO_WRITE) ? B_WRITE : B_READ,
					    on + xcnt, ioflag & IO_SYNC, maxba, rabv));
		if (up->u_error || rw == UIO_WRITE && (long)bn < 0)
			return (up->u_error);
		if (rw == UIO_WRITE) {
		        if (uio->uio_offset < ip->i_size && type == IFREG) {
			        if (bsize >= NBPP)
				        hashbn = bn;
				else {
				        hashbn = lbn & ~((NBPP / bsize) - 1);
					if (hashbn == lbn)
					        hashbn = bn;
					else
					        hashbn = FsLTOP(ip->i_fs,
						vbmap(ip,hashbn,B_READ,0,0,0,0));
				}
			}
		        if ((uio->uio_offset + xcnt > ip->i_size) &&
		            (type == IFDIR || type == IFREG || type == IFLNK))
			        ip->i_size = uio->uio_offset + xcnt;
	        }
		rasize = bsize;

		if (rw == UIO_READ) {
			if ((long)bn < 0) {
				bp = geteblk(bsize);
				clrbuf(bp);
			} else if (ip->i_lastr + 1 == lbn || lbn == 0)
				bp = vbreada(devvp, bn, bsize, up->u_rablock,
					rabv, FsLTOP(ip->i_fs,1));
			else
				bp = bread(devvp, bn, bsize);
			ip->i_lastr = lbn;
		} else {
			if (type == IFIFO) {
				if (on == 0 && ip->i_size < (PIPSIZ - bsize))
					bp = getblk(devvp, bn, bsize);
				else
					bp = bread(devvp, bn, bsize);
			} else {
				if (xcnt == bsize) 
					bp = getblk(devvp, bn, bsize);
				else
					bp = bread(devvp, bn, bsize);
			}
		}
		n = MIN(xcnt, (bp->b_bcount - bp->b_resid - on));

		if ((bp->b_flags & B_ERROR) || (n < 0)) {
			n = geterror(bp);
			brelse(bp);
			return(n);
		}
		up->u_error = uiomove(bp->b_un.b_addr+on, n, rw, uio);

		if (rw == UIO_READ) {
			if (type == IFIFO && n == bsize) {
                                bp->b_flags &= ~B_DELWRI;
                                bp->b_flags |= B_AGE;
                        }
			brelse(bp);
			sysinfo.readch += n;
		} else {
			if (hashbn != -1) {
			        int i;

				i = 0;
				do {
				        pfremove(hashbn, ip->i_dev);
					hashbn += NBPP / DEV_BSIZE;
					i += NBPP;
				} while (i < bsize);
			}
			if ((ioflag & IO_SYNC) || type == IFDIR)
				bwrite(bp);
			else
				bdwrite(bp);

			if (type == IFIFO) {
				if (uio->uio_offset == PIPSIZ)
					uio->uio_offset = 0;
				ip->i_size += n;
			}
			imark(ip, IUPD|ICHG);
			sysinfo.writech += n;
		}
	} while (up->u_error == 0 && uio->uio_resid > 0 && n != 0);

	if ((ioflag & IO_SYNC) && (rw == UIO_WRITE) &&
	    (ip->i_flag & (IUPD|ICHG))) {
		iupdat(ip, 1);
	}
	return (up->u_error);
}

/*ARGSUSED2*/
int
svfs_ioctl(vp, com, data, flag, cred)
	struct vnode *vp;
	int com;
	caddr_t data;
	int flag;
	struct ucred *cred;
{
	register struct inode *ip;
	int	err;

	ip = VTOI(vp);
	if ((ip->i_mode & IFMT) != IFCHR)
		panic("svfs_ioctl");
	if (cdevsw[(short)major(ip->i_rdev)].d_str) {
		err = (*stream_ioctl)(vp, com, data, flag, cred);
		if(err == EINTR && 
		  (u.u_procp->p_compatflags & COMPAT_SYSCALLS)) {
#ifdef SIG43
			if (u.u_sigintr & sigmask(u.u_procp->p_cursig))
				return (EINTR);
#endif SIG43
			u.u_eosys = RESTARTSYS;
			return(0);;
		}
		return(err);
	} else
	return ((*cdevsw[major(ip->i_rdev)].d_ioctl)
			(passmajor(ip->i_rdev), com, data, flag));
}

/*ARGSUSED2*/
int
svfs_select(vp, which, cred)
	struct vnode *vp;
	int which;
	struct ucred *cred;
{
	printf("svfs_select: character or fifo select.\n");
	return(EINVAL);
}

/*ARGSUSED2*/
int
svfs_getattr(vp, vap, cred)
	struct vnode *vp;
	register struct vattr *vap;
	struct ucred *cred;
{
	register struct inode *ip;

	ip = VTOI(vp);
	/*
	 * Copy from inode table.
	 */
	vap->va_type = IFTOVT(ip->i_mode);
	vap->va_mode = ip->i_mode;
	vap->va_uid = ip->i_uid;
	vap->va_gid = ip->i_gid;
	vap->va_fsid = ip->i_dev;
	vap->va_nodeid = ip->i_number;
	vap->va_nlink = ip->i_nlink;
	vap->va_size = ip->i_size;
	vap->va_atime.tv_sec = ip->i_atime;
	vap->va_atime.tv_usec = 0;
	vap->va_mtime.tv_sec = ip->i_mtime;
	vap->va_mtime.tv_usec = 0;
	vap->va_ctime.tv_sec = ip->i_ctime;
	vap->va_ctime.tv_usec = 0;
	vap->va_rdev = ip->i_rdev;

	if (ip->i_size == 0)
		vap->va_blocks = 0;
	else {
		/* still doesn't handle "holey" files */
		register long blocks;

		vap->va_blocks = blocks =
			(ip->i_size + FsBSIZE(ip->i_fs) - 1)
			>> FsBSHIFT(ip->i_fs);
		/* add overhead for indirect blocks, if any */
		if (blocks > NDADDR) {
			blocks -= NDADDR;
			vap->va_blocks
				+= ((blocks - 1) >> FsNSHIFT(ip->i_fs)) + 1;
			if (blocks > FsNINDIR(ip->i_fs)) {
				int shift = FsNSHIFT(ip->i_fs) << 1;

				blocks -= FsNINDIR(ip->i_fs);
				vap->va_blocks += ((blocks - 1) >> shift) + 1;
				if (blocks > (1 << shift))
					(vap->va_blocks)++;
			}
		}
		/* correct units are physical (512-byte) blocks */
		vap->va_blocks = FsLTOP(ip->i_fs, vap->va_blocks);
	}

	switch(ip->i_mode & IFMT) {

	case IFBLK:
		vap->va_blocksize = BLKDEV_IOSIZE;
		break;

	case IFCHR:
		vap->va_blocksize = MAXBSIZE;
		break;

	default:
		vap->va_blocksize = FsBSIZE(ip->i_fs);
		break;
	}
	return (0);
}

int
svfs_setattr(vp, vap, cred)
	register struct vnode *vp;
	register struct vattr *vap;
	struct ucred *cred;
{
	register struct inode *ip;
	int chtime = 0;
	int error = 0;

	/*
	 * cannot set these attributes
	 */
	if ((vap->va_nlink != (short) -1) || (vap->va_blocksize != -1) ||
	    (vap->va_rdev != (dev_t) -1) || (vap->va_blocks != -1) ||
	    (vap->va_fsid != -1) || (vap->va_nodeid != -1) ||
	    ((int)vap->va_type != -1)) {
		return (EINVAL);
	}

	ip = VTOI(vp);
	ilock(ip, __FILE__, __LINE__);
	/*
	 * Change file access modes. Must be owner or su.
	 */
	if (vap->va_mode != (u_short)-1) {
		error = OWNER(cred, ip);
		if (error)
			goto out;
		ip->i_mode &= IFMT;
		ip->i_mode |= vap->va_mode & ~IFMT;
		if (cred->cr_uid != 0) {
			ip->i_mode &= ~ISVTX;
			if (!groupmember((int) ip->i_gid))
				ip->i_mode &= ~ISGID;
		}
		imark(ip, ICHG);
		if ((ip->i_flag & ITEXT) && ((ip->i_mode & ISVTX) == 0)) {
			xrele(ITOV(ip));
		}
	}
	/*
	 * Change file ownership. Must be owner or su.
	 */
	if ((vap->va_uid != (short) -1) || (vap->va_gid != (short) -1)) {
		error = OWNER(cred, ip);
		if (error)
			goto out;
		error = chown1(ip, vap->va_uid, vap->va_gid);
		if (error)
			goto out;
	}
	/*
	 * Truncate file. Must have write permission and not be a directory.
	 */
	if (vap->va_size != (u_long)-1) {
		if ((ip->i_mode & IFMT) == IFDIR) {
			error = EISDIR;
			goto out;
		}
		if (iaccess(ip, IWRITE)) {
			error = u.u_error;
			goto out;
		}
		itrunc(ip, vap->va_size);
	}
	/*
	 * Change file access or modified times.
	 */
	if (vap->va_atime.tv_sec != -1) {
		error = OWNER(cred, ip);
		/* nit pick: non-owner can pass utime set to current time */
		if (error && (time.tv_sec - vap->va_atime.tv_sec <= 1)) {
			if (iaccess(ip, IWRITE)) {
				error = u.u_error;
				goto out;
			}
			else
				error = 0;
		}
		else if (error)
			goto out;
		ip->i_atime = vap->va_atime.tv_sec;
		chtime++;
	}
	if (vap->va_mtime.tv_sec != -1) {
		error = OWNER(cred, ip);
		/*
		 * Text mod time is checked for paging of remote files.
		 * To be consistent we prevent svfs modtime being changed.
		 */
		if (vp->v_flag & VTEXT) {
			xrele(vp);
			if (vp->v_flag & VTEXT) {
				error = ETXTBSY;
				goto out;
			}
		}
		/* nit pick: non-owner can pass utime set to current time */
		if (error && (time.tv_sec - vap->va_mtime.tv_sec <= 1)) {
			if (iaccess(ip, IWRITE)) {
				error = u.u_error;
				goto out;
			}
			else
				error = 0;
		}
		else if (error)
			goto out;
		ip->i_mtime = vap->va_mtime.tv_sec;
		chtime++;
	}
	if (chtime) {
		ip->i_flag |= IACC|IUPD|ICHG;
		ip->i_ctime = time.tv_sec;
	}
out:
	iupdat(ip, 1);			/* XXX should be asyn for perf */
	iunlock(ip);
	return (error);
}

/*
 * Perform chown operation on inode ip;
 * inode must be locked prior to call.
 */
chown1(ip, uid, gid)
	register struct inode *ip;
	int uid, gid;
{
	register struct user *up;
#ifdef QUOTA
	register long change;
#endif

	up = &u;
	if (uid == -1)
		uid = ip->i_uid;
	if (gid == -1)
		gid = ip->i_gid;

	/* error if
	 *	1) not super-user
	 *	2) not current owner
	 *	3) if BSD compatible
	 *		3a) trying to change owner
	 *		3b) new group is not a member of process group set
	 */
	if (!suser()) {
	    	if (u.u_uid != ip->i_uid)
			return(EPERM);
		if (u.u_procp->p_compatflags & COMPAT_BSDCHOWN) {
			if ((uid != ip->i_uid) || !groupmember(gid))
				return(EPERM);
		}
	}

#ifdef QUOTA
	if (ip->i_uid == uid)		/* this just speeds things a little */
		change = 0;
	else
		change = ip->i_blocks;
	(void) chkdq(ip, -change, 1);
        (void) chkiq(VFSTOM(ip->i_vnode.v_vfsp), ip, ip->i_uid, 1);
	dqrele(ip->i_dquot);
#endif
	ip->i_uid = uid;
	ip->i_gid = gid;
	imark(ip, ICHG);
	if (up->u_uid != 0)
		ip->i_mode &= ~(ISUID|ISGID);
#ifdef QUOTA
	ip->i_dquot = getinoquota(ip);
	(void) chkdq(ip, change, 1);
        (void) chkiq(VFSTOM(ip->i_vnode.v_vfsp), (struct inode *)NULL, uid, 1);
	return (up->u_error);		/* should == 0 ALWAYS !! */
#else
	return (0);
#endif
}

/*ARGSUSED2*/
int
svfs_access(vp, mode, cred)
	struct vnode *vp;
	int mode;
	struct ucred *cred;
{
	register struct inode *ip;
	int error;

	ip = VTOI(vp);
	ilock(ip, __FILE__, __LINE__);
	error = iaccess(ip, mode);
	iunlock(ip);
	return (error);
}

/*ARGSUSED2*/
int
svfs_readlink(vp, uiop, cred)
	struct vnode *vp;
	struct uio *uiop;
	struct ucred *cred;
{
	register struct inode *ip;
	register int error;

	if (vp->v_type != VLNK)
		return (EINVAL);
	ip = VTOI(vp);
	ilock(ip, __FILE__, __LINE__);
	error = rwip(ip, uiop, UIO_READ, 0);
	iunlock(ip);
	return (error);
}

/*ARGSUSED2*/
int
svfs_fsync(vp, cred)
	struct vnode *vp;
	struct ucred *cred;
{
	register struct inode *ip;

	ip = VTOI(vp);
	ilock(ip, __FILE__, __LINE__);
	syncip(ip);
	iunlock(ip);
	return (0);
}

/*ARGSUSED2*/
int
svfs_inactive(vp, cred)
	struct vnode *vp;
	struct ucred *cred;
{

	iinactive(VTOI(vp));
	return (0);
}

/*
 * Unix file system operations having to do with directory manipulation.
 */

/*ARGSUSED3*/
svfs_lookup(dvp, nm, vpp, cred)
	struct vnode *dvp;
	char *nm;
	struct vnode **vpp;
	struct ucred *cred;
{
	struct inode *ip;
	register int error;

	error = dirlook(VTOI(dvp), nm, &ip);
	if (error == 0) {
		*vpp = ITOV(ip);
		iunlock(ip);
                /*
                 * If vnode is a device return special vnode instead
                 */
                if (ISVDEV((*vpp)->v_type)) {
                        struct vnode *newvp;

                        newvp = specvp(*vpp, (*vpp)->v_rdev);
                        VN_RELE(*vpp);
                        *vpp = newvp;
                }
	}
	return (error);
}

svfs_create(dvp, nm, vap, exclusive, mode, vpp, cred)
	struct vnode *dvp;
	char *nm;
	struct vattr *vap;
	enum vcexcl exclusive;
	int mode;
	struct vnode **vpp;
	struct ucred *cred;
{
	register int error, existed, lockerror = 0;
	struct inode *ip;

	/*
	 * can't create directories. use svfs_mkdir.
	 */
	if (vap->va_type == VDIR)
		return (EISDIR);
	ip = (struct inode *) 0;
	existed = error = direnter(VTOI(dvp), nm, DE_CREATE,
		(struct inode *)0, (struct inode *)0, vap, &ip);
	/*
	 * if file exists and this is a nonexclusive create,
	 * check for not directory and access permissions
	 */
	if (error == EEXIST) {
	        if ((ip->i_mode & IFMT) == IFSOCK) {
		        iput(ip);
			return(EOPNOTSUPP);
		}
		if (ITOV(ip)->v_locklist && ((ip->i_mode & IFMT) == IFREG)) {
			lockerror = locked(2, ip, (long) 0, (long) (1L << 30));
			if (lockerror)
				error = lockerror;
		}
		if ((lockerror == 0) && (exclusive == NONEXCL)) {
			if (((ip->i_mode & IFMT) == IFDIR) && (mode & IWRITE)) {
				error = EISDIR;
			} else if (mode) {
				error = iaccess(ip, mode);
			} else {
				error = 0;
			}
		}
		if (error) {
			iput(ip);
			return(error);
		}
	} 
	if (error) {
		return (error);
	}
	/*
	 * truncate regular files, if required
	 */
	if (((ip->i_mode & IFMT) == IFREG) && (vap->va_size == 0) && existed == EEXIST) {
		itrunc(ip, (u_long) 0);
	}
	*vpp = ITOV(ip);
	if (vap != (struct vattr *)0) {
		(void) svfs_getattr(*vpp, vap, cred);
	}
	iunlock(ip);
        /*
         * If vnode is a device return special vnode instead
         */
        if (ISVDEV((*vpp)->v_type)) {
                struct vnode *newvp;

                newvp = specvp(*vpp, (*vpp)->v_rdev);
                VN_RELE(*vpp);
                *vpp = newvp;
        }

        if (vap != (struct vattr *)0) {
                (void) VOP_GETATTR(*vpp, vap, cred);
        }
	return (error);
}

/*ARGSUSED2*/
svfs_remove(vp, nm, cred)
	struct vnode *vp;
	char *nm;
	struct ucred *cred;
{
	register int error;

	error = dirremove(VTOI(vp), nm, (struct inode *)0, 0);
	return (error);
}

/*
 * link a file or a directory
 * If source is a directory, must be superuser
 */
/*ARGSUSED3*/
svfs_link(vp, tdvp, tnm, cred)
	struct vnode *vp;
	struct vnode *tdvp;
	char *tnm;
	struct ucred *cred;
{
	register struct inode *sip;
	register int error;

	sip = VTOI(vp);
	if (((sip->i_mode & IFMT) == IFDIR) && !suser()) {
		return (EPERM);
	}
	if (sip->i_nlink >= MAXLINK)
		return (EMLINK);
	error =
	    direnter(VTOI(tdvp), tnm, DE_LINK,
		(struct inode *)0, sip, (struct vattr *)0, (struct inode **)0);
	return (error);
}

/*
 * Rename a file or directory
 * We are given the vnode and entry string of the source and the
 * vnode and entry string of the place we want to move the source to
 * (the target). The essential operation is:
 *	unlink(target);
 *	link(source, target);
 *	unlink(source);
 * but "atomically". Can't do full commit without saving state in the inode
 * on disk, which isn't feasible at this time. Best we can do is always
 * guarantee that the TARGET exists.
 */
/*ARGSUSED4*/
svfs_rename(sdvp, snm, tdvp, tnm, cred)
	struct vnode *sdvp;		/* old (source) parent vnode */
	char *snm;			/* old (source) entry name */
	struct vnode *tdvp;		/* new (target) parent vnode */
	char *tnm;			/* new (target) entry name */
	struct ucred *cred;
{
	struct inode *sip;		/* source inode */
	register struct inode *sdp;	/* old (source) parent inode */
	register struct inode *tdp;	/* new (target) parent inode */
	register int error;

	sdp = VTOI(sdvp);
	tdp = VTOI(tdvp);
	/*
	 * make sure we can delete the source entry
	 */
	error = iaccess(sdp, IWRITE);
	if (error) {
		return (error);
	}
	/*
	 * look up inode of file we're supposed to rename.
	 */
	error = dirlook(sdp, snm, &sip);
	if (error) {
		return (error);
	}

	iunlock(sip);			/* unlock inode (it's held) */
	/*
	 * check for renaming '.' or '..' or alias of '.'
	 */
	if ((strcmp(snm, ".") == 0) || (strcmp(snm, "..") == 0) ||
	    (sdp == sip)) {
		error = EINVAL;
		goto out;
	}
	/*
	 * link source to the target
	 */
	error =
	    direnter(tdp, tnm, DE_RENAME,
		sdp, sip, (struct vattr *)0, (struct inode **)0);
	if (error) {
#ifdef POSIX
		if (u.u_procp->p_compatflags & COMPAT_POSIXFUS
			&& error == ENOTDIR && (sip->i_mode & IFMT) != IFDIR)
			error = EISDIR;
#endif /* POSIX */
                if (error == ESAME)     /* renaming linked files */
                        error = 0;      /* not error just nop */
                goto out;
        }

	/*
	 * Unlink the source
	 * Remove the source entry. Dirremove checks that the entry
	 * still reflects sip, and returns an error if it doesn't.
	 * If the entry has changed just forget about it. 
	 * Release the source inode.
	 */
	error = dirremove(sdp, snm, sip, 0);
	if (error == ENOENT) {
		error = 0;
	} else if (error) {
		goto out;
	}

out:
	irele(sip);
	return (error);
}

/*ARGSUSED4*/
svfs_mkdir(dvp, nm, vap, vpp, cred)
	struct vnode *dvp;
	char *nm;
	register struct vattr *vap;
	struct vnode **vpp;
	struct ucred *cred;
{
	struct inode *ip;
	register int error;

	error =
	    direnter(VTOI(dvp), nm, DE_CREATE,
		(struct inode *)0, (struct inode *)0, vap, &ip);
	if (error == 0) {
		*vpp = ITOV(ip);
		iunlock(ip);
	} else if (error == EEXIST) {
		iput(ip);
	}
	return (error);
}

/*ARGSUSED2*/
svfs_rmdir(vp, nm, cred)
	struct vnode *vp;
	char *nm;
	struct ucred *cred;
{
	register int error;

	error = dirremove(VTOI(vp), nm, (struct inode *)0, 1);
	return (error);
}

/*ARGSUSED2*/
svfs_readdir(vp, uiop, cred)
	struct vnode *vp;
	register struct uio *uiop;
	struct ucred *cred;
{
	struct iovec *iov;
	register int i;
	register unsigned TransferCount;
	struct buf *bp = (struct buf *) NULL, *blkatoff();
	register struct svfsdirect *SVFSdp;
	struct direct nfsd;
	char dirbuf[DIRBLKSIZ];
	register struct direct *NFSdp = &nfsd;
	struct direct *PreviousDirPtr = (struct direct *) NULL;
	struct inode *ip;
	register caddr_t TransferBase, TransferEnd;
	caddr_t UserBaseAddr;
	int error = 0;

	iov = uiop->uio_iov;
	TransferCount = iov->iov_len;
	if ((uiop->uio_iovcnt != 1) || (TransferCount < DIRBLKSIZ) ||
	    (uiop->uio_offset & (sizeof(struct svfsdirect) - 1)))
		return (EINVAL);
	TransferCount &= ~(DIRBLKSIZ - 1);
	uiop->uio_resid -= iov->iov_len - TransferCount;
	iov->iov_len = TransferCount;

	/*
	 *	If this is a user request, we canonicalize directories into
	 *	a temporary buffer (dirbuf[]) and copy the data to user space
	 *	only when the buffer is full.
	 */
	if (uiop->uio_seg == UIOSEG_USER) {
		UserBaseAddr = iov->iov_base;
		TransferBase = &dirbuf[0];
	}
	else
		TransferBase = iov->iov_base;
	TransferEnd = TransferBase + DIRBLKSIZ;

	ip = VTOI(vp);
	ilock(ip, __FILE__, __LINE__);
	while ((uiop->uio_resid > 0) && (uiop->uio_offset < ip->i_size)) {
		/*
		 *	If we're at a directory offset (FsBLKOFF == 0) or we
		 *	haven't started yet (bp == 0), release the previous
		 *	directory block's buffer (if necessary), read in the
		 *	next directory block, and initialize SVFSdp.
		 */
		if (FsBLKOFF(ip->i_fs, uiop->uio_offset) == 0 ||
		    bp == (struct buf *) NULL) {
			if (bp)
				brelse(bp);
			bp = blkatoff(ip, (off_t) uiop->uio_offset, (char **)0);
			if (bp == (struct buf *) NULL) {
				error = u.u_error;	/* XXX */
				goto bad;
			}
			SVFSdp = (struct svfsdirect *) (bp->b_un.b_addr + FsBOFF(ip->i_fs, uiop->uio_offset));
		}

		/*
		 *	Canonicalize SVFSdp into NFSdp, adjusting the record
		 *	length so that the canonicalized entry has a length
		 *	that is a multiple of 4.
		 */
		NFSdp->d_ino = (u_long) SVFSdp->d_ino;
		for (i = 0; (i < SVFSDIRSIZ) && (SVFSdp->d_name[i] != '\0'); i++)
		    NFSdp->d_name[i] = SVFSdp->d_name[i];
		NFSdp->d_namlen		= (u_short) i;
		NFSdp->d_name[i]	= '\0';
		NFSdp->d_reclen		= sizeof(NFSdp->d_ino) +
					  sizeof(NFSdp->d_reclen) +
					  sizeof(NFSdp->d_namlen) +
					  NFSdp->d_namlen + 1;
		if (NFSdp->d_reclen & 3)
			NFSdp->d_reclen = (NFSdp->d_reclen + 3) & ~3;

		if (TransferBase + NFSdp->d_reclen > TransferEnd) {
			/*
			 *	If this entry won't fit in the remainder of the
			 *	buffer, adjust the previous entry's record
			 *	length to show that it consumed the rest of the
			 *	space in the buffer and pretend that we've
			 *	transferred (TransferEnd - TransferBase) more
			 *	bytes.
			 *
			 *	Note that SVFSdp is not incremented:  SVFSdp
			 *	will be recanonicalized on the next pass through
			 *	the loop (we know it will fit as the first
			 *	entry in the next block).
			 */
			TransferCount	= TransferEnd - TransferBase;
			TransferBase	= TransferEnd;
			if (uiop->uio_seg != UIOSEG_USER)
				TransferEnd	= TransferBase + DIRBLKSIZ;
			if (PreviousDirPtr == (struct direct *) NULL)
				panic("svfs_readdir");
			PreviousDirPtr->d_reclen += TransferCount;
		} else {
			/*
			 *	Copy the canonicalized directory entry to
			 *	TransferBase and go on to the next entry.  If
			 *	the file offset exceeds the directory size,
			 *	pretend that the previous directory entry
			 *	consumed the remainder of the space in this
			 *	directory block.
			 */
			bcopy((caddr_t) NFSdp, TransferBase, NFSdp->d_reclen);
			SVFSdp++;
			uiop->uio_offset	+= sizeof(struct svfsdirect);
			TransferCount	= NFSdp->d_reclen;
			PreviousDirPtr	= (struct direct *) TransferBase;
			TransferBase	+= TransferCount;
			if (uiop->uio_offset >= ip->i_size) {
				PreviousDirPtr->d_reclen += TransferEnd - TransferBase;
				TransferCount	+= TransferEnd - TransferBase;
				TransferBase	= TransferEnd;
			}
		}
		iov->iov_base	+= TransferCount;
		iov->iov_len	-= TransferCount;
		uiop->uio_resid	-= TransferCount;

		/*
		 *	If this is a transfer to user space and we've filled
		 *	dirbuf[], copy the data out, reset TransferBase to
		 *	the beginning of dirbuf[], and remember the user's
		 *	current base transfer address (necessary because
		 *	we've been incrementing iov->iov_base).
		 */
		if ((uiop->uio_seg == UIOSEG_USER) && TransferBase >= &dirbuf[DIRBLKSIZ]) {
			error = copyout(&dirbuf[0], UserBaseAddr, DIRBLKSIZ);
			UserBaseAddr	= iov->iov_base;
			if (error)
				break;
			TransferBase	= &dirbuf[0];
		}
	}
bad:
	if (bp)
		brelse(bp);
	iunlock(ip);
	return(error);
}

/*ARGSUSED4*/
svfs_symlink(dvp, lnm, vap, tnm, cred)
	struct vnode *dvp;
	char *lnm;
	struct vattr *vap;
	char *tnm;
	struct ucred *cred;
{
	struct inode *ip;
	register int error;

	ip = (struct inode *) 0;
	vap->va_type = VLNK;
	vap->va_rdev = 0;
	error =
	    direnter(VTOI(dvp), lnm, DE_CREATE,
		(struct inode *)0, (struct inode *)0, vap, &ip);
	if (error == 0) {
		error =
		    rdwri(UIO_WRITE, ip,
			tnm, strlen(tnm), 0, UIOSEG_KERNEL, (int *)0);
		iput(ip);
	} else if (error == EEXIST) {
		iput(ip);
	}
	return (error);
}

rdwri(rw, ip, base, len, offset, seg, aresid)
	enum uio_rw rw;
	struct inode *ip;
	caddr_t base;
	int len;
	int offset;
	int seg;
	int *aresid;
{
	struct uio auio;
	struct iovec aiov;
	register int error;

	aiov.iov_base = base;
	aiov.iov_len = len;
	auio.uio_iov = &aiov;
	auio.uio_iovcnt = 1;
	auio.uio_offset = offset;
	auio.uio_seg = seg;
	auio.uio_resid = len;
	error = svfs_rdwr(ITOV(ip), &auio, rw, 0, u.u_cred);
	if (aresid) {
		*aresid = auio.uio_resid;
	} else if (auio.uio_resid) {
		error = EIO;
	}
	return (error);
}

int
svfs_bmap(vp, lbn, vpp, bnp)
	struct vnode *vp;
	daddr_t lbn;
	struct vnode **vpp;
	daddr_t *bnp;
{
	register struct inode *ip;

	ip = VTOI(vp);
	if (vpp)
		*vpp = ip->i_devvp;
	if (bnp)
		*bnp = FsLTOP(ip->i_fs, vbmap(ip, lbn, B_READ,0,0,0,0));
	return (0);
}

/*
 * read a logical block and return it in a buffer
 */
/*ARGSUSED3*/
int
svfs_bread(vp, lbn, bpp, sizep)
	struct vnode *vp;
	daddr_t lbn;
	struct buf **bpp;
	long *sizep;
{
	daddr_t  rabv[MAXVIO+1];	/* save 0-terminated readahead */
	register struct inode *ip;
	register struct buf *bp;
	register daddr_t bn;
	register long size;

	ip = VTOI(vp);
	size = FsBSIZE(ip->i_fs);
	bn = FsLTOP(ip->i_fs, vbmap(ip, lbn, B_READ, size, 0, maxba, rabv));
	if ((long)bn < 0) {
		bp = geteblk((int) size);
		clrbuf(bp);
	} else if (ip->i_lastr + 1 == lbn || lbn == 0) {
		/* use accelerated vector read */
		bp = vbreada(ip->i_devvp, bn, (int) size, u.u_rablock, rabv, FsLTOP(ip->i_fs, 1));
	} else {
		bp = bread(ip->i_devvp, bn, (int) size);
	}
	ip->i_lastr = lbn;
	imark(ip, IACC);
	if (bp->b_flags & B_ERROR) {
		brelse(bp);
		return (EIO);
	} else {
		*bpp = bp;
		return (0);
	}
}

/*
 * release a block returned by svfs_bread
 */
svfs_brelse(vp, bp)
	struct vnode *vp;
	struct buf *bp;
{
#ifdef	lint
	vp = vp;
#endif
	bp->b_flags |= B_AGE;
	bp->b_resid = 0;
	brelse(bp);
}

int
svfs_badop()
{
	panic("svfs_badop");
}

/*
 * Record-locking requests are passed to the local Lock-Manager daemon, if
 * one exists.
 */
int
svfs_lockctl(vp, ld, cmd, cred)
    struct vnode *vp;
    struct flock *ld;
    int cmd;
    struct ucred *cred;
{
    lockhandle_t lh;
    struct fid *fidp;
    extern int (*lockctl)();

    /* Convert vnode into lockhandle-id. This is awfully like makefh(). */
    if (VOP_FID(vp, &fidp) || fidp == NULL) {
	return (EINVAL);
    }
    bzero((caddr_t)&lh.lh_id, sizeof (lh.lh_id));   /* clear extra bytes */
    lh.lh_fsid.val[0] = vp->v_vfsp->vfs_fsid.val[0];
    lh.lh_fsid.val[1] = vp->v_vfsp->vfs_fsid.val[1];
    lh.lh_fid.fid_len = fidp->fid_len;
    bcopy(fidp->fid_data, lh.lh_fid.fid_data, fidp->fid_len);
    freefid(fidp);
    
    /* Add in vnode and server and call to common code */
    lh.lh_vp = vp;
    lh.lh_servername = hostname;

    return ((*lockctl)(&lh, ld, cmd, cred));
}

svfs_fid(vp, fidpp)
    struct vnode *vp;
    struct fid **fidpp;
{
    register struct ufid *ufid;

    ufid = (struct ufid *)kmem_alloc(sizeof(struct ufid));
    bzero((caddr_t)ufid, sizeof(struct ufid));
    ufid->ufid_len = sizeof(struct ufid) - (sizeof(struct fid) - MAXFIDSZ);
    ufid->ufid_ino = VTOI(vp)->i_number;
    ufid->ufid_gen = VTOI(vp)->i_gen;
    *fidpp = (struct fid *)ufid;
    return (0);
}

struct vnode *
svfs_mkfifo(vp)
        struct vnode *vp;
{
        struct inode *ip;

        if ((ip = ialloc(VTOI(vp), IFIFO, 0)) == NULL)
                return NULL;
        iunlock(ip);
        return ITOV(ip);
}

