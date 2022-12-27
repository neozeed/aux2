#ifndef lint	/* .../sys/COMMON/os/vfs_io.c */
#define _AC_NAME vfs_io_c
#define _AC_NO_MAIN "@(#) Copyright (c) 1983-87 Sun Microsystems Inc., 1985-87 UniSoft Corporation, All Rights Reserved.  {Apple version 2.2 90/02/23 18:09:14}"
#include <apple_notice.h>

#ifdef _AC_HISTORY
  static char *sccsid = "@(#)Copyright Apple Computer 1989\tVersion 2.2 of vfs_io.c on 90/02/23 18:09:14";
#endif		/* _AC_HISTORY */
#endif		/* lint */

#define _AC_MODS
/*      @(#)vfs_io.c 1.1 86/02/03 SMI   */
/*      NFSSRC @(#)vfs_io.c     2.1 86/04/15 */
#include "compat.h"
#include "sys/types.h"
#include "sys/sysmacros.h"
#include "sys/param.h"
#include "sys/signal.h"
#include "sys/mmu.h"
#include "sys/page.h"
#include "sys/region.h"
#include "sys/time.h"
#include "sys/user.h"
#include "sys/proc.h"
#include "sys/uio.h"
#include "sys/vfs.h"
#include "sys/vnode.h"
#include "sys/file.h"
#include "sys/ostat.h"
#include "sys/stat.h"
#include "sys/ioctl.h"
#include "sys/errno.h"
#include "sys/var.h"
#include "sys/mount.h"
#include "sys/flock.h"

int vno_rw();
int vno_ioctl();
int vno_select();
int vno_close();
int vno_stat();

struct fileops vnodefops = {
        vno_rw,
        vno_ioctl,
        vno_select,
        vno_close,
	vno_stat
};

int
vno_rw(fp, rw, uiop)
	register struct file *fp;
	register enum uio_rw rw;
	register struct uio *uiop;
{
	register struct vnode *vp;
	register int count;
	register int error;
	register int ioflag;

	vp = (struct vnode *)fp->f_data;
	/*
	 * If write make sure filesystem is writable
	 */
	if ((rw == UIO_WRITE) && (vp->v_vfsp->vfs_flag & VFS_RDONLY))
		return(EROFS);
	count = uiop->uio_resid;
	ioflag = 0;

	switch (vp->v_type) {
		case VREG:
			ioflag |= IO_UNIT;
			break;
		case VFIFO:
			/*
			 * NOTE: Kludge to ensure that FAPPEND stays set.
			 * This ensures that fp->f_offset is always accurate.
			 */
			fp->f_flag |= FAPPEND;
			/* IO_NDELAY currently only used by FIFOs */
			if (fp->f_flag & (FNDELAY | FNONBLOCK))
				ioflag |= IO_NDELAY;
			break;
		default:
			break;
	}
	if (fp->f_flag & FAPPEND)
		ioflag |= IO_APPEND;

	error = VOP_RDWR(vp, uiop, rw, ioflag, fp->f_cred);

	if ((ioflag & IO_NDELAY) && (error == EWOULDBLOCK)) {
		/* massage errno for standards lawyers */
		if (fp->f_flag & FNDELAY)
			error = 0;
#ifdef POSIX
		if (fp->f_flag & FNONBLOCK)
			error = EAGAIN;
#endif /* POSIX */
	}
	if (error)
		return(error);

	if (fp->f_flag & FAPPEND) {
		/*
		 * The actual offset used for append is set by VOP_RDWR
		 * so compute actual starting location
		 */
		fp->f_offset = uiop->uio_offset - (count - uiop->uio_resid);
	}
	return(0);
}

int
vno_ioctl(fp, com, data)
	struct file *fp;
	int com;
	caddr_t data;
{
	struct vattr vattr;
	struct vnode *vp;
	int error = 0;

	vp = (struct vnode *)fp->f_data;
	switch((int) vp->v_type) {

	case VREG:
	case VDIR:
	case VFIFO:
		switch (com) {

		case FIONREAD:
			error = VOP_GETATTR(vp, &vattr, u.u_cred);
			if (error == 0)
				if (vp->v_type==VFIFO)
					*(off_t *) data = vattr.va_size;
				else
					*(off_t *) data = vattr.va_size - fp->f_offset;
			break;

		case FIONBIO:
		case FIOASYNC:
			break;

		default:
			error = ENOTTY;
			break;
		}
		break;

	case VCHR:
		u.u_rval1 = 0;
#ifdef SIG43
		if ((u.u_procp->p_compatflags & COMPAT_SYSCALLS) && save(u.u_qsav)) {
			if (u.u_sigintr & sigmask(u.u_procp->p_cursig))
				return (EINTR);
			u.u_eosys = RESTARTSYS;
			return (0);
		} else {
			error = VOP_IOCTL(vp, com, data, fp->f_flag,fp->f_cred);
		}
#else
		if ((u.u_procp->p_compatflags & COMPAT_SYSCALLS) && save(u.u_qsav)) {
			u.u_eosys = RESTARTSYS;
		} else {
			error = VOP_IOCTL(vp, com, data, fp->f_flag,fp->f_cred);
		}
#endif SIG43
		break;

	default:
		error = ENOTTY;
		break;
	}
	return (error);
}

int
vno_select(fp, flag)
	register struct file *fp;
	int flag;
{
	register struct vnode *vp;

	vp = (struct vnode *)fp->f_data;
	switch((int) vp->v_type) {
	case VCHR:
	case VFIFO:
		return (VOP_SELECT(vp, flag, fp->f_cred));

	default:
		/*
		 * Always selected
		 */
		return (1);
	}
}

int
vno_stat(vp, sb)
	register struct vnode *vp;
	register struct stat *sb;
{
	register int error;
	register struct vattr *va;
	struct vattr vattr;

	va = &vattr;
	if (error = VOP_GETATTR(vp, va, u.u_cred))
		return (error);
	sb->st_mode = va->va_mode;
	sb->st_uid = va->va_uid;
	sb->st_gid = va->va_gid;
	sb->st_dev = va->va_fsid;
	sb->st_ino = va->va_nodeid;
	sb->st_inol = va->va_nodeid;
	sb->st_nlink = va->va_nlink;
	sb->st_size = va->va_size;
        sb->st_blksize = va->va_blocksize;
	sb->st_atime = va->va_atime.tv_sec;
	sb->st_mtime = va->va_mtime.tv_sec;
        sb->st_spare2 = 0;
	sb->st_ctime = va->va_ctime.tv_sec;
        sb->st_spare3 = 0;
	sb->st_rdev = (dev_t)va->va_rdev;
        sb->st_blocks = va->va_blocks;
#ifdef POSIX
	/*
	 * If these spare fields are ever needed for anything important;
	 * nuke this code.  The POSIX library still fudges these fields;
	 * this code is here only for applications which define _POSIX_SOURCE
	 * but don't link with the POSIX library (naughty, naughty).
	 */
	sb->st_spare4[0] = va->va_uid;
	sb->st_spare4[1] = va->va_gid;
#else
	sb->st_spare4[0] = 0;
	sb->st_spare4[1] = 0;
#endif /* POSIX */

	return (0);
}

int
vno_close(fp)
	register struct file *fp;
{
	register struct vnode *vp;
	register struct file *ffp;
	register struct vnode *tvp;
	register struct mount *mp;
	register enum vtype type;
	register dev_t dev;

	vp = (struct vnode *)fp->f_data;
	if (fp->f_flag & (FSHLOCK | FEXLOCK))
		vno_unlock(fp, (FSHLOCK | FEXLOCK));

	type = vp->v_type;
	if ((type == VBLK) || (type == VCHR)) {
		/*
		 * check that another vnode for the same device isn't active.
		 * This is because the same device can be referenced by two
		 * different vnodes.
		 */
		dev = vp->v_rdev;
		for (ffp = file; ffp < (struct file *) v.ve_file; ffp++) {
			if (ffp == fp)
				continue;
			if (ffp->f_type != DTYPE_VNODE)		/* XXX */
				continue;
			if (ffp->f_count &&
			    (tvp = (struct vnode *)ffp->f_data) &&
			    tvp->v_rdev == dev && tvp->v_type == type &&
			    tvp->v_sptr == vp->v_sptr ) {
				VN_RELE(vp);
				return (0);
			}
		}
		if(type == VBLK) {
			for (mp = &mounttab[0]; 
			    mp < (struct mount *)v.ve_mount; mp++) {
				if (mp->m_bufp != NULL && dev == mp->m_dev) {
					VN_RELE(vp);
					return (0);
				}
			}
		}
	}
	u.u_error = vn_close(vp, fp->f_flag);
	VN_RELE(vp);
	return (u.u_error);
}

/*
 * lockctl() and lockrel() are initally set to no-op stubs.  They get 
 * patched to point at local or network lock manager routines during 
 * initialization.  whoever patches them becomes the manager that gets
 * used.  There is no provision for formally tunring off a previous lock
 * manger. 
 */
int no_lockctl();
int (*lockctl)() = no_lockctl;

int
no_lockctl() {
    return(ENOLCK);
}

int no_lockrel();
int (*lockrel)() = no_lockrel;

no_lockrel()
{
	return(0);
}

/*
 * This routine is called for every file close (exluding kernel processes
 * that call closef() directly) in order to implement the brain-damaged
 * SVID 'feature' that the FIRST close of a descriptor that refers to
 * a locked object causes all the locks to be released for that object.
 * It is called, for example, by close(), exit(), exec(), & dup2().
 *
 * NOTE: If the SVID ever changes to hold locks until the LAST close,
 *       then this routine might be moved to closef() [note that the
 *       window system calls closef() directly for file descriptors
 *       that is has dup'ed internally....such descriptors may or may
 *       not count towards holding a lock]
 *
 * TODO: The record-lock flag should be in the u-area.
 */
int
vno_lockrelease(fp)
	struct file *fp;
{
        register struct user *up;

	up = &u;
	/*
	 * Only do extra work if the process has done record-locking.
	 */
	if (up->u_procp->p_flag & SLKDONE) {
		register struct vnode *vp;
		register struct file *ufp;
		register int i;
		register int locked;
		struct flock ld;

		locked = 0;     /* innocent until proven guilty */
		up->u_procp->p_flag &= ~SLKDONE;  /* reset process flag */
		vp = (struct vnode *)fp->f_data;
		/*
		 * Check all open files to see if there's a lock
		 * possibly held for this vnode.
		 */
		for (i = NOFILE; i-- > 0; ) {
			if ((ufp = up->u_ofile[i]) && (up->u_pofile[i] & UF_FDLOCK)) {

				/* the current file has an active lock */
				if ((struct vnode *)ufp->f_data == vp) {

					/* release this lock */
					locked = 1; /* (later) */
					up->u_pofile[i] &= ~UF_FDLOCK;
				} else {

					/* another file is locked */
					up->u_procp->p_flag |= SLKDONE;
				}
			}
		}
		if (up->u_gofile) {
		        for (i = GNOFILE; i-- > NOFILE; ) {
			        if ((ufp = up->u_gofile[i]) &&
				    (up->u_gpofile[i] & UF_FDLOCK)) {

				        /* the current file has an active lock */
				        if ((struct vnode *)ufp->f_data == vp) {

					        /* release this lock */
					        locked = 1; /* (later) */
						up->u_gpofile[i] &= ~UF_FDLOCK;
					} else {

					        /* another file is locked */
					        up->u_procp->p_flag |= SLKDONE;
					}
				}
			}
		}
		/*
		 * If 'locked' is set, release any locks that this process
		 * is holding on this file.  If record-locking on any other
		 * files was detected, the process was marked (SLKDONE) to
		 * run thru this loop again at the next file close.
		 */
		if (locked) {
			ld.l_type = F_UNLCK;    /* set to unlock entire file */
			ld.l_whence = 0;    /* unlock from start of file */
			ld.l_start = 0;
			ld.l_len = 0;       /* do entire file */
			return (VOP_LOCKCTL(vp, &ld, F_SETLK, u.u_cred));
		}
	}
	return (0);
}

/*
* Place an advisory lock on an inode.
*/
int
vno_lock(fp, cmd)
register struct file *fp;
int cmd;
{
register int priority;
	register struct vnode *vp;

	/*
	 * Avoid work.
	 */
	if ((fp->f_flag & FEXLOCK) && (cmd & LOCK_EX) ||
	    (fp->f_flag & FSHLOCK) && (cmd & LOCK_SH))
		return (0);

	priority = PLOCK;
	vp = (struct vnode *)fp->f_data;

	if ((cmd & LOCK_EX) == 0)
		priority++;
	/*
	 * If there's a exclusive lock currently applied
	 * to the file, then we've gotta wait for the
	 * lock with everyone else.
	 */
again:
	while (vp->v_flag & VEXLOCK) {
		/*
		 * If we're holding an exclusive
		 * lock, then release it.
		 */
		if (fp->f_flag & FEXLOCK) {
			vno_unlock(fp, FEXLOCK);
			goto again;
		}
		if (cmd & LOCK_NB)
			return (EWOULDBLOCK);
		vp->v_flag |= VLWAIT;
		(void) sleep((caddr_t)&vp->v_exlockc, priority);
	}
	if (cmd & LOCK_EX) {
		cmd &= ~LOCK_SH;
		/*
		 * Must wait for any shared locks to finish
		 * before we try to apply a exclusive lock.
		 */
		while (vp->v_flag & VSHLOCK) {
			/*
			 * If we're holding a shared
			 * lock, then release it.
			 */
			if (fp->f_flag & FSHLOCK) {
				vno_unlock(fp, FSHLOCK);
				goto again;
			}
			if (cmd & LOCK_NB)
				return (EWOULDBLOCK);
			vp->v_flag |= VLWAIT;
			(void) sleep((caddr_t)&vp->v_shlockc, PLOCK);
		}
	}
	if (fp->f_flag & (FSHLOCK|FEXLOCK))
		panic("vno_lock");
	if (cmd & LOCK_SH) {
		vp->v_shlockc++;
		vp->v_flag |= VSHLOCK;
		fp->f_flag |= FSHLOCK;
	}
	if (cmd & LOCK_EX) {
		vp->v_exlockc++;
		vp->v_flag |= VEXLOCK;
		fp->f_flag |= FEXLOCK;
	}
	return (0);
}

/*
 * Unlock a file.
 */
int
vno_unlock(fp, kind)
	register struct file *fp;
	int kind;
{
	register struct vnode *vp;
	register int flags;

	vp = (struct vnode *)fp->f_data;
	kind &= fp->f_flag;
	if (vp == NULL || kind == 0)
		return;
	flags = vp->v_flag;
	if (kind & FSHLOCK) {
		if ((flags & VSHLOCK) == 0)
			panic("vno_unlock: SHLOCK");
		if (--vp->v_shlockc == 0) {
			vp->v_flag &= ~VSHLOCK;
			if (flags & VLWAIT)
				wakeup((caddr_t)&vp->v_shlockc);
		}
		fp->f_flag &= ~FSHLOCK;
	}
	if (kind & FEXLOCK) {
		if ((flags & VEXLOCK) == 0)
			panic("vno_unlock: EXLOCK");
		if (--vp->v_exlockc == 0) {
			vp->v_flag &= ~(VEXLOCK|VLWAIT);
			if (flags & VLWAIT)
				wakeup((caddr_t)&vp->v_exlockc);
		}
		fp->f_flag &= ~FEXLOCK;
	}
}

int
vno_ostat(vp, sb)
	register struct vnode *vp;
	register struct ostat *sb;
{
	register int error;
	register struct vattr *va;
	struct vattr vattr;

	va = &vattr;
	if (error = VOP_GETATTR(vp, va, u.u_cred))
		return (error);
	sb->ost_mode = va->va_mode;
	sb->ost_uid = va->va_uid;
	sb->ost_gid = va->va_gid;
	sb->ost_dev = va->va_fsid;
	sb->ost_ino = va->va_nodeid;
	sb->ost_nlink = va->va_nlink;
	sb->ost_size = va->va_size;
	sb->ost_atime = va->va_atime.tv_sec;
	sb->ost_mtime = va->va_mtime.tv_sec;
	sb->ost_ctime = va->va_ctime.tv_sec;
	sb->ost_rdev = (dev_t)va->va_rdev;

	return (0);
}
