#ifndef lint	/* .../sys/COMMON/os/vfs_syscalls.c */
#define _AC_NAME vfs_syscalls_c
#define _AC_NO_MAIN "@(#) Copyright (c) 1983-87 Sun Microsystems Inc., All Rights Reserved.  {Apple version 2.1 89/10/13 19:28:33}"
#include <apple_notice.h>

#ifdef _AC_HISTORY
  static char *sccsid = "@(#)Copyright Apple Computer 1989\tVersion 1.3 of vfs_syscalls.c on 87/07/18 18:10:23";
#endif		/* _AC_HISTORY */
#endif		/* lint */

#define _AC_MODS
/*      @(#)vfs_syscalls.c 1.1 86/02/03 SMI     */
/*      NFSSRC @(#)vfs_syscalls.c       2.1 86/04/15 */

#include "compat.h"
#include "sys/types.h"
#include "sys/sysmacros.h"
#include "sys/param.h"
#include "sys/signal.h"
#include "sys/mmu.h"
#include "sys/page.h"
#include "sys/time.h"
#include "sys/user.h"
#include "sys/file.h"
#include "sys/ostat.h"
#include "sys/stat.h"
#include "sys/uio.h"
#include "sys/ioctl.h"
#include "sys/tty.h"
#include "sys/vfs.h"
#include "sys/pathname.h"
#include "sys/systm.h"
#include "sys/vnode.h"
#include "sys/region.h"
#include "sys/proc.h"
#include "svfs/inode.h"
#include "sys/errno.h"
#include "sys/debug.h"

extern	struct fileops vnodefops;

/*
 * Convert inode formats to vnode types
 */
enum vtype iftovt_tab[] = {
        VNON, VFIFO, VCHR, VBAD, VDIR, VBAD, VBLK, VBAD,
        VREG, VBAD, VLNK, VBAD, VSOCK, VBAD, VBAD, VBAD
};

int vttoif_tab[] = {
        0, IFREG, IFDIR, IFBLK, IFCHR, IFLNK, IFSOCK, IFIFO, IFMT
};

/*
 * System call routines for operations on files other
 * than read, write and ioctl.  These calls manipulate
 * the per-process file table which references the
 * networkable version of normal UNIX inodes, called vnodes.
 *
 * Many operations take a pathname, which is read
 * into a kernel buffer by pn_get (see vfs_pathname.c).
 * After preparing arguments for an operation, a simple
 * operation proceeds:
 *
 *	error = lookupname(pname, seg, followlink, &dvp, &vp)
 *
 * where pname is the pathname operated on, seg is the segment that the
 * pathname is in (UIOSEG_USER or UIOSEG_KERNEL), followlink specifies
 * whether to follow symbolic links, dvp is a pointer to the vnode that
 * represents the parent directory of vp, the pointer to the vnode
 * referenced by the pathname. The lookupname routine fetches the
 * pathname string into an internal buffer using pn_get (vfs_pathname.c),
 * and iteratively running down each component of the path until the
 * the final vnode and/or it's parent are found. If either of the addresses
 * for dvp or vp are NULL, then it assumes that the caller is not interested
 * in that vnode. Once the vnode or its parent is found, then a vnode
 * operation (e.g. VOP_OPEN) may be applied to it.
 *
 * One important point is that the operations on vnode's are atomic, so that
 * vnode's are never locked at this level.  Vnode locking occurs
 * at lower levels either on this or a remote machine. Also permission
 * checking is generally done by the specific filesystem. The only
 * checks done by the vnode layer is checks involving file types
 * (e.g. VREG, VDIR etc.), since this is static over the life of the vnode.
 *
 */

/*
 * Change current working directory (".").
 */
chdir(uap)
	register struct a {
		char *dirnamep;
	} *uap;
{
	struct vnode *vp;

	if ((u.u_error = chdirec(uap->dirnamep, &vp)) == 0) {
		VN_RELE(u.u_cdir);
		u.u_cdir = vp;
	}
}

/*
 * Change notion of root ("/") directory.
 */
chroot(uap)
	register struct a {
		char *dirnamep;
	} *uap;
{
	register struct user *up;
	struct vnode *vp;

	if (!suser())
		return;
	up = &u;

	if ((up->u_error = chdirec(uap->dirnamep, &vp)) == 0) {
		if (up->u_rdir != (struct vnode *)0)
			VN_RELE(up->u_rdir);
		up->u_rdir = vp;
	}
}

/*
 * Common code for chdir and chroot.
 * Translate the pathname and insist that it
 * is a directory to which we have execute access.
 * If it is replace up->u_[cr]dir with new vnode.
 */
chdirec(dirnamep, vpp)
	char *dirnamep;
	struct vnode **vpp;
{
	struct vnode *vp;		/* new directory vnode */
	register int error;

        error =
	    lookupname(dirnamep, UIOSEG_USER, FOLLOW_LINK,
		(struct vnode **)0, &vp);
	if (error)
		return (error);

	if (vp->v_type != VDIR)
		error = ENOTDIR;
	else
		error = VOP_ACCESS(vp, VEXEC, u.u_cred);

	if (error) {
		VN_RELE(vp);
	} else
		*vpp = vp;

	return (error);
}

/*
 * Open system call.
 */
open(uap)
	register struct a {
		char *fnamep;
		int fmode;
		int cmode;
	} *uap;
{

	u.u_error = copen(uap->fnamep, uap->fmode - FOPEN, uap->cmode);
}

/*
 * Creat system call.
 */
creat(uap)
	register struct a {
		char *fnamep;
		int cmode;
	} *uap;
{

	u.u_error = copen(uap->fnamep, FWRITE|FCREAT|FTRUNC, uap->cmode);
}

/*
 * Common code for open, creat.
 */
copen(pnamep, filemode, createmode)
	char *pnamep;
	int filemode;
	int createmode;
{
	register struct file *fp;
	register struct user *up;
	register int error;
	register int fd;
	struct vnode *vp;

	up = &u;

	if ((filemode & FGLOBAL) && u.u_gofile)
	        fd = NOFILE;
	else
	        fd = 0;
	/*
	 * allocate a user file descriptor and file table entry.
	 */
	if ((fp = falloc(fd)) == NULL)
		return(up->u_error);
	fd = up->u_rval1;		/* this is bullshit */
	/*
	 * open the vnode.
	 */
	error =
	    vn_open(pnamep, UIOSEG_USER,
		filemode, ((createmode & 07777) & ~(up->u_cmask | VSVTX)), &vp);

	/*
	 * If there was an error, deallocate the file descriptor.
	 * Otherwise fill in the file table entry to point to the vnode.
	 */
	if (error) {
	        if (fd < NOFILE)
		        up->u_ofile[fd] = NULL;
		else
		        up->u_gofile[fd] = NULL;
		crfree(fp->f_cred);
		fp->f_count = 0;
	} else {
		fp->f_flag = filemode & FMASK;
		fp->f_type = DTYPE_VNODE;
		fp->f_data = (caddr_t)vp;
		fp->f_ops = &vnodefops;
	}
	return(error);
}

/*
 * Create a special (or regular) file.
 */
mknod(uap)
	register struct a {
		char		*pnamep;
		int		fmode;
		int		dev;
	} *uap;
{
	register struct user *up;
	register struct vattr *va;
	struct vnode *vp;
	struct vattr vattr;

	/*
	 * Must be super user unless creating a FIFO
	 */
	if (((uap->fmode & IFMT) != IFIFO) && !suser())
		return;
	up = &u;
	va = &vattr;

	/*
	 * Setup desired attributes and vn_create the file.
	 */
	vattr_null(va);

	if ((uap->fmode & IFMT) == 0)
		va->va_type = IFTOVT(IFREG);
	else
		va->va_type = IFTOVT(uap->fmode);
	/*
	 * Can't mknod directories. Use mkdir.
	 */
	if (va->va_type == VDIR)
		va->va_mode = (uap->fmode & 0777) & ~up->u_cmask;
	else
		va->va_mode = (uap->fmode & 07777) & ~up->u_cmask;

	switch ((int) va->va_type) {
	case VBAD:
	case VCHR:
	case VBLK:
		va->va_rdev = uap->dev;
		break;

	case VNON:
		up->u_error = EINVAL;
		return;

	default:
		break;
	}
	if ((up->u_error = vn_create(uap->pnamep, UIOSEG_USER, va, EXCL, 0, &vp)) == 0)
		VN_RELE(vp);
}

/*
 * Make a directory.
 */
mkdir(uap)
	register struct a {
		char	*dirnamep;
		int	dmode;
	} *uap;
{
	register struct vattr *va;
	struct vnode *vp;
	struct vattr vattr;

	va = &vattr;
	vattr_null(va);

	va->va_type = VDIR;
	va->va_mode = (uap->dmode & 0777) & ~u.u_cmask;

	if ((u.u_error = vn_create(uap->dirnamep, UIOSEG_USER, va, EXCL, 0, &vp)) == 0)
		VN_RELE(vp);
}

/*
 * make a hard link
 */
link(uap)
	register struct a {
		char	*from;
		char	*to;
	} *uap;
{

	u.u_error = vn_link(uap->from, uap->to, UIOSEG_USER);
}

/*
 * rename or move an existing file
 */
rename(uap)
	register struct a {
		char	*from;
		char	*to;
	} *uap;
{

	u.u_error = vn_rename(uap->from, uap->to, UIOSEG_USER);
}

/*
 * Create a symbolic link.
 * Similar to link or rename except target
 * name is passed as string argument, not
 * converted to vnode reference.
 */
symlink(uap)
	register struct a {
		char	*target;
		char	*linkname;
	} *uap;
{
	register struct user *up;
	struct vnode *dvp;
	struct vattr vattr;
	struct pathname tpn;
	struct pathname lpn;

	up = &u;
	up->u_error = pn_get(uap->linkname, UIOSEG_USER, &lpn);
	if (up->u_error)
		return;
	up->u_error = lookuppn(&lpn, NO_FOLLOW, &dvp, (struct vnode **)0);
	if (up->u_error) {
		pn_free(&lpn);
		return;
	}
	if (dvp->v_vfsp->vfs_flag & VFS_RDONLY) {
		up->u_error = EROFS;
		goto out;
	}
	up->u_error = pn_get(uap->target, UIOSEG_USER, &tpn);
	vattr_null(&vattr);
	vattr.va_mode = 0777;
	if (up->u_error == 0) {
		up->u_error =
		   VOP_SYMLINK(dvp, lpn.pn_path, &vattr, tpn.pn_path, up->u_cred);
		pn_free(&tpn);
	}
out:
	pn_free(&lpn);
	VN_RELE(dvp);
}

/*
 * Unlink (i.e. delete) a file.
 */
unlink(uap)
	struct a {
		char	*pnamep;
	} *uap;
{

	u.u_error = vn_remove(uap->pnamep, UIOSEG_USER, 0);
}

/*
 * Remove a directory.
 */
rmdir(uap)
	struct a {
		char	*dnamep;
	} *uap;
{

	u.u_error = vn_remove(uap->dnamep, UIOSEG_USER, 1);
}

/*
 * get directory entries in a file system independent format
 */
getdirentries(uap)
	register struct a {
		int	fd;
		char	*buf;
		unsigned count;
		long	*basep;
	} *uap;
{
	register struct user *up;
	register struct file *fp;
	struct file *tfp;
	struct uio auio;
	struct iovec aiov;

	up = &u;
	if (up->u_error = getvnodefp(uap->fd, &tfp))
		return;
	fp = tfp;

	if ((fp->f_flag & FREAD) == 0) {
		up->u_error = EBADF;
		return;
	}
	aiov.iov_base = uap->buf;
	aiov.iov_len = uap->count;
	auio.uio_iov = &aiov;
	auio.uio_iovcnt = 1;
	auio.uio_offset = fp->f_offset;
	auio.uio_seg = UIOSEG_USER;
	auio.uio_resid = uap->count;
	if (up->u_error = VOP_READDIR((struct vnode *)fp->f_data, &auio, fp->f_cred))
		return;
	up->u_error =
	    copyout((caddr_t)&fp->f_offset, (caddr_t)uap->basep, sizeof(long));
	up->u_rval1 = uap->count - auio.uio_resid;
	fp->f_offset = auio.uio_offset;
}

/*
 * Seek on file.  Only hard operation
 * is seek relative to end which must
 * apply to vnode for current file size.
 * 
 * Note: lseek(0, 0, L_XTND) costs much more than it did before.
 */
seek(uap)
	register struct a {
		int	fd;
		off_t	off;
		int	sbase;
	} *uap;
{
	register struct user *up;
	register struct file *fp;
	register struct vnode *vp;
	register off_t oldoffset;
	struct file *tfp;
	extern int mem_no;

	up = &u;
	if (up->u_error = getvnodefp(uap->fd, &tfp))
		return;
	fp = tfp;
	vp = (struct vnode *) fp->f_data;

	if (vp->v_type == VFIFO) {
		up->u_error = ESPIPE;
		return;
	}
	switch (uap->sbase) {

	case L_INCR:
		oldoffset = fp->f_offset;
		fp->f_offset += uap->off;
		break;

	case L_XTND: {
		struct vattr vattr;

		up->u_error =
		    VOP_GETATTR((struct vnode *)fp->f_data, &vattr, up->u_cred);
		if (up->u_error)
			return;
		oldoffset = fp->f_offset;
		fp->f_offset = uap->off + vattr.va_size;
		break;
	}

	case L_SET:
		oldoffset = fp->f_offset;
		fp->f_offset = uap->off;
		break;

	default:
#ifdef	lint
		oldoffset = fp->f_offset;
#endif
		up->u_error = EINVAL;
#ifdef POSIX
		return;
#else
                psignal(up->u_procp, SIGSYS);
#endif
	}

	if (fp->f_offset < 0 && (vp->v_type != VCHR || major(vp->v_rdev) != mem_no)) {
		fp->f_offset = oldoffset;
		up->u_error = EINVAL;
		return;
	}
	up->u_r.r_off = fp->f_offset;
}

/*
 * Determine accessibility of file, by
 * reading its attributes and then checking
 * against our protection policy.
 */
access(uap)
	register struct a {
		char	*fname;
		int	fmode;
	} *uap;
{
	register struct user *up;
	struct vnode *vp;
	register u_short mode;
	register int svuid;
	register int svgid;

#ifdef POSIX
	if (uap->fmode & ~(R_OK|W_OK|X_OK)) {
		u.u_error = EINVAL;
		return;
	}
#endif
	up = &u;
	/*
	 * Lookup file
	 */
	up->u_error =
	    lookupname(uap->fname, UIOSEG_USER, FOLLOW_LINK,
		(struct vnode **)0, &vp);
	if (up->u_error)
		return;

	/*
	 * Use the real uid and gid and check access
	 */
	svuid = up->u_uid;
	svgid = up->u_gid;
	up->u_uid = up->u_ruid;
	up->u_gid = up->u_rgid;

	mode = 0;
	/*
	 * fmode == 0 means only check for exist
	 */
	if (uap->fmode) {
		if (uap->fmode & R_OK)
			mode |= VREAD;
		if (uap->fmode & W_OK) {
			if(vp->v_vfsp->vfs_flag & VFS_RDONLY) {
				up->u_error = EROFS;
				goto out;
			}
			mode |= VWRITE;
		}
		if (uap->fmode & X_OK)
			mode |= VEXEC;
		up->u_error = VOP_ACCESS(vp, mode, up->u_cred);
	}

	/*
	 * release the vnode and restore the uid and gid
	 */
out:
	VN_RELE(vp);
	up->u_uid = svuid;
	up->u_gid = svgid;
}

/*
 * Get attributes from file or file descriptor.
 * Argument says whether to follow links, and is
 * passed through in flags.
 */
stat(uap)
	struct a {
		char	*fname;
		struct	stat *ub;
	} *uap;
{

	u.u_error = stat1(uap, FOLLOW_LINK);
}

lstat(uap)
	struct a {
		char	*fname;
		struct	stat *ub;
	} *uap;
{
	u.u_error = stat1(uap, NO_FOLLOW);
}

stat1(uap, follow)
	register struct a {
		char	*fname;
		struct	stat *ub;
	} *uap;
	enum symfollow follow;
{
	struct vnode *vp;
	struct stat sb;
	register int error;

	error =
	    lookupname(uap->fname, UIOSEG_USER, follow,
		(struct vnode **)0, &vp);
	if (error)
		return (error);
	error = vno_stat(vp, &sb);
	VN_RELE(vp);
	if (error)
		return (error);
	return (copyout((caddr_t)&sb, (caddr_t)uap->ub, sizeof (sb)));
}

/*
 * Read contents of symbolic link.
 */
readlink(uap)
	register struct a {
		char	*name;
		char	*buf;
		int	count;
	} *uap;
{
	register struct user *up;
	struct vnode *vp;
	struct iovec aiov;
	struct uio auio;

	up = &u;
	up->u_error =
	    lookupname(uap->name, UIOSEG_USER, NO_FOLLOW,
		(struct vnode **)0, &vp);
	if (up->u_error)
		return;
	if (vp->v_type != VLNK) {
		up->u_error = EINVAL;
		goto out;
	}
	aiov.iov_base = uap->buf;
	aiov.iov_len = uap->count;
	auio.uio_iov = &aiov;
	auio.uio_iovcnt = 1;
	auio.uio_offset = 0;
	auio.uio_seg = UIOSEG_USER;
	auio.uio_resid = uap->count;
	up->u_error = VOP_READLINK(vp, &auio, up->u_cred);
out:
	VN_RELE(vp);
	up->u_rval1 = uap->count - auio.uio_resid;
}

/*
 * Change mode of file given path name.
 */
chmod(uap)
	register struct a {
		char	*fname;
		int	fmode;
	} *uap;
{
	struct vattr vattr;

	vattr_null(&vattr);
	vattr.va_mode = uap->fmode & 07777;
	u.u_error = namesetattr(uap->fname, FOLLOW_LINK, &vattr);
}

/*
 * Change mode of file given file descriptor.
 */
fchmod(uap)
	register struct a {
		int	fd;
		int	fmode;
	} *uap;
{
	struct vattr vattr;

	vattr_null(&vattr);
	vattr.va_mode = uap->fmode & 07777;
	u.u_error = fdsetattr(uap->fd, &vattr);
}

/*
 * Change ownership of file given file name.
 */
chown(uap)
	register struct a {
		char	*fname;
		int	uid;
		int	gid;
	} *uap;
{
	struct vattr vattr;

	vattr_null(&vattr);
	vattr.va_uid = uap->uid;
	vattr.va_gid = uap->gid;
	u.u_error = namesetattr(uap->fname, NO_FOLLOW,  &vattr);
}

/*
 * Change ownership of file given file descriptor.
 */
fchown(uap)
	register struct a {
		int	fd;
		int	uid;
		int	gid;
	} *uap;
{
	struct vattr vattr;

	vattr_null(&vattr);
	vattr.va_uid = uap->uid;
	vattr.va_gid = uap->gid;
	u.u_error = fdsetattr(uap->fd, &vattr);
}

/*
 * Set access/modify times on named file (SVID/POSIX version).
 */
utime(uap)
	register struct a {
		char	*fname;
		time_t	*tptr;
	} *uap;
{
	register struct vattr *va;
	struct vattr vattr;
	time_t tv[2];

	va = &vattr;
	vattr_null(va);

	if (uap->tptr) {
		if (u.u_error = copyin((caddr_t)uap->tptr, (caddr_t)tv, sizeof (tv)))
			return;
		va->va_atime.tv_sec = tv[0];
		va->va_atime.tv_usec = 0;
		va->va_mtime.tv_sec = tv[1];
		va->va_mtime.tv_usec = 0;
	} else {
		va->va_atime = time;
		va->va_mtime = time;
	}
	u.u_error = namesetattr(uap->fname, FOLLOW_LINK, va);
}

/*
 * Set access/modify times on named file (BSD version).
 */
utimes(uap)
	register struct a {
		char	*fname;
		struct timeval *tptr;
	} *uap;
{
	register struct vattr *va;
	struct vattr vattr;
	struct timeval tv[2];

	va = &vattr;
	vattr_null(va);

	if (uap->tptr) {
		if (u.u_error = copyin((caddr_t)uap->tptr, (caddr_t)tv, sizeof (tv)))
			return;
		va->va_atime = tv[0];
		va->va_mtime = tv[1];
	} else {
		va->va_atime = time;
		va->va_mtime = time;
	}
	u.u_error = namesetattr(uap->fname, FOLLOW_LINK, va);
}

/*
 * Truncate a file given its path name.
 */
truncate(uap)
	register struct a {
		char	*fname;
		int	length;
	} *uap;
{
	struct vattr vattr;

	vattr_null(&vattr);
	vattr.va_size = uap->length;
	u.u_error = namesetattr(uap->fname, FOLLOW_LINK, &vattr);
}

/*
 * Truncate a file given a file descriptor.
 */
ftruncate(uap)
	register struct a {
		int	fd;
		int	length;
	} *uap;
{
	register struct user *up;
	register struct vnode *vp;
	struct file *fp;

	up = &u;
	if (up->u_error = getvnodefp(uap->fd, &fp))
		return;
	vp = (struct vnode *)fp->f_data;

	if ((fp->f_flag & FWRITE) == 0)
		up->u_error = EINVAL;
	else if (vp->v_vfsp->vfs_flag & VFS_RDONLY)
		up->u_error = EROFS;
	else {
		struct vattr vattr;

		vattr_null(&vattr);
		vattr.va_size = uap->length;
		up->u_error = VOP_SETATTR(vp, &vattr, fp->f_cred);
	}
}

/*
 * Common routine for modifying attributes
 * of named files.
 */
namesetattr(fnamep, followlink, vap)
	char *fnamep;
	enum symfollow followlink;
	struct vattr *vap;
{
	struct vnode *vp;
	register int error;

	error =
	    lookupname(fnamep, UIOSEG_USER, followlink,
		 (struct vnode **)0, &vp);
	if (error)
		return(error);	
	if (vp->v_vfsp->vfs_flag & VFS_RDONLY)
		error = EROFS;
	else
		error = VOP_SETATTR(vp, vap, u.u_cred);
	VN_RELE(vp);
	return(error);
}

/*
 * Common routine for modifying attributes
 * of file referenced by descriptor.
 */
fdsetattr(fd, vap)
	int fd;
	struct vattr *vap;
{
	struct file *fp;
	register struct vnode *vp;
	register int error;

	if ((error = getvnodefp(fd, &fp)) == 0) {
		vp = (struct vnode *)fp->f_data;
		if (vp->v_vfsp->vfs_flag & VFS_RDONLY)
			return(EROFS);
		error = VOP_SETATTR(vp, vap, fp->f_cred);
	}
	return(error);
}

/*
 * Flush output pending for file.
 */
fsync(uap)
	struct a {
		int	fd;
	} *uap;
{
	struct file *fp;

	if ((u.u_error = getvnodefp(uap->fd, &fp)) == 0)
		u.u_error = VOP_FSYNC((struct vnode *)fp->f_data, fp->f_cred);
}

/*
 * Set file creation mask.
 */
umask(uap)
	register struct a {
		int mask;
	} *uap;
{
	register struct user *up;

	up = &u;
	up->u_rval1 = up->u_cmask;
	up->u_cmask = uap->mask & 07777;
}

/*
 * Get the file structure entry for the file descrpitor, but make sure
 * its a vnode.
 */
int
getvnodefp(fd, fpp)
	int fd;
	struct file **fpp;
{
	register struct file *fp;

	if ((fp = getf(fd)) == NULL)
		return(EBADF);
	if (fp->f_type != DTYPE_VNODE)
		return(EINVAL);
	*fpp = fp;

	return(0);
}

/*
 * Get attributes from file or file descriptor.
 */
ostat(uap)
	struct a {
		char	*fname;
		struct	ostat *ub;
	} *uap;
{

	u.u_error = ostat1(uap);
}

ostat1(uap)
	register struct a {
		char	*fname;
		struct	stat *ub;
	} *uap;
{
	struct vnode *vp;
	struct ostat sb;
	register int error;

	error =
	    lookupname(uap->fname, UIOSEG_USER, FOLLOW_LINK,
		(struct vnode **)0, &vp);
	if (error)
		return (error);
	error = vno_ostat(vp, &sb);
	VN_RELE(vp);
	if (error)
		return (error);
	return (copyout((caddr_t)&sb, (caddr_t)uap->ub, sizeof (sb)));
}

ofstat()
{
	register struct file *fp;
	register struct a {
		int	fdes;
		struct	ostat *sb;
	} *uap;
	struct ostat ub;
	register struct user *up;

	up = &u;
	uap = (struct a *)up->u_ap;

	if ((fp = getf(uap->fdes)) == NULL)
		return;

	switch (fp->f_type) {

	case DTYPE_VNODE:
		up->u_error = vno_ostat((struct vnode *)fp->f_data, &ub);
		break;

	case DTYPE_SOCKET:
		up->u_error = 0;	/* soo_stat doesn't do anything */
		break;

	default:
		panic("fstat");
		/*NOTREACHED*/
	}
	if (up->u_error == 0)
		up->u_error = copyout((caddr_t)&ub, (caddr_t)uap->sb, sizeof (ub));
}
