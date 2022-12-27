#ifndef lint	/* .../sys/COMMON/os/sys3.c */
#define _AC_NAME sys3_c
#define _AC_NO_MAIN "@(#) Copyright (c) 1987 Apple Computer, Inc., 1983-87 Sun Microsystems Inc., 1980-87 The Regents of the University of California, 1985-87 UniSoft Corporation, All Rights Reserved.  {Apple version 2.3 90/04/06 15:32:48}"
#include <apple_notice.h>

#ifdef _AC_HISTORY
  static char *sccsid = "@(#)Copyright Apple Computer 1989\tVersion 2.3 of sys3.c on 90/04/06 15:32:48";
#endif		/* _AC_HISTORY */
#endif		/* lint */

#define _AC_MODS
/*	@(#)sys3.c	UniPlus 2.1.2	*/

/*	@(#)kern_descrip.c 1.1 85/05/30 SMI; from UCB 6.3 83/11/18	*/

#include "compat.h"
#include "sys/param.h"
#include "sys/types.h"
#include "sys/time.h"
#include "sys/mmu.h"
#include "sys/page.h"
#include "sys/systm.h"
#include "sys/signal.h"
#include "sys/uio.h"
#include "sys/user.h"
#include "sys/socket.h"
#include "sys/socketvar.h"
#include "sys/vfs.h"
#include "sys/vnode.h"
#include "sys/region.h"
#include "sys/proc.h"
#include "sys/conf.h"
#include "sys/file.h"
#include "sys/stat.h"
#include "sys/errno.h"
#include "sys/ioctl.h"
#include "sys/var.h"
#include "sys/acct.h"
#include "sys/flock.h"
#include "sys/sysinfo.h"

extern int (*lockrel)();

/*
 * Descriptor management.
 */

/*
 * TODO:
 *	increase NOFILE
 *	eliminate u.u_error side effects
 */

/*
 * System calls on descriptors.
 */
getdtablesize()
{

	u.u_rval1 = NOFILE;
}

getdopt()
{

}

setdopt()
{

}

dup()
{
	struct a {
		int	i;
	};
	register struct file *fp;
	register struct user *up;
	register int newfd;
	register int oldfd;

	up = &u;
	oldfd = ((struct a *)up->u_ap)->i;

	if ((fp = getf(oldfd)) == NULL)
		return;
	if ((newfd = ufalloc(0)) < 0)
		return;
	if (oldfd < NOFILE) 
	        dupit(newfd, fp, up->u_pofile[oldfd]);
	else
	        dupit(newfd, fp, up->u_gpofile[oldfd]);
}


dupit(fd, fp, flags)
	register int fd;
	register struct file *fp;
	register int flags;
{
	register struct user *up;

	up = &u;
	flags &= ~UF_EXCLOSE;

	if (fd >= NOFILE) {
		up->u_gofile[fd] = fp;
		up->u_gpofile[fd] = flags;
	} else {
		up->u_ofile[fd] = fp;
		up->u_pofile[fd] = flags;
	}
	fp->f_count++;
}

/*
 * The file control system call.
 */
fcntl()
{
	register struct file *fp;
	register struct a {
		int	fdes;
		int	cmd;
		int	arg;
	} *uap;
	register int oldwhence;
	register unsigned int fd;
	register char *pop;
	register struct user *up = &u;
 	struct flock ld;
	int error;

	uap = (struct a *)up->u_ap;
	fd = uap->fdes;

	if ((fp = getf(fd)) == NULL)
		return;
	if (fd >= NOFILE)
		pop = &up->u_gpofile[fd];
	else
		pop = &up->u_pofile[fd];

	switch(uap->cmd) {
	case F_DUPFD:
		if ((fd = uap->arg) >= NOFILE) {
			if (up->u_gofile == NULL || fd >= GNOFILE) {
				up->u_error = EINVAL;
				return;
			}
		}
		if ((fd = ufalloc(fd)) == -1)
			return;
		dupit(fd, fp, *pop);
		break;

	case F_GETFD:
		up->u_rval1 = *pop;
		break;

	case F_SETFD:
		*pop = uap->arg;
		break;

	case F_GETFL:
		up->u_rval1 = fp->f_flag+FOPEN;
		break;

	case F_SETFL:
		fp->f_flag &= FCNTLCANT;
		fp->f_flag |= (uap->arg-FOPEN) &~ FCNTLCANT;
#ifdef POSIX
		if (fp->f_flag & (FNDELAY|FNONBLOCK))
		    up->u_error = fset(fp, fp->f_flag & (FNDELAY|FNONBLOCK), 1);
		else
		    up->u_error = fset(fp, (FNDELAY|FNONBLOCK), 0);
		if (up->u_error)
			break;
		up->u_error = fset(fp, FASYNC, fp->f_flag & FASYNC);
		if (up->u_error) {
			(void) fset(fp, (FNDELAY|FNONBLOCK), 0);
		}
#else
		up->u_error = fset(fp, FNDELAY, fp->f_flag & FNDELAY);
		if (up->u_error)
			break;
		up->u_error = fset(fp, FASYNC, fp->f_flag & FASYNC);
		if (up->u_error)
			(void) fset(fp, FNDELAY, 0);
#endif /* POSIX */
		break;

	case F_GETLK:
	case F_SETLK:
	case F_SETLKW:
		/* Do record locking */
		if (fp->f_type != DTYPE_VNODE) {
			up->u_error = EOPNOTSUPP;
			return;
		}
		if((up->u_error = copyin((caddr_t)uap->arg, (caddr_t)&ld, sizeof ld)))
			return;
		/* *** NOTE ***
		 * The SVID does not say what to return on file access errors!
		 * Here, EBADF is returned, which is compatible with S5R3
		 * and is less confusing than EACCES
		 */
		/* check access permissions */
		if (uap->cmd != F_GETLK) {
			switch (ld.l_type) {
				case F_RDLCK:
					if (!(fp->f_flag & FREAD)) {
						up->u_error = EBADF;
						return;
					}
					break;

				case F_WRLCK:
					if (!(fp->f_flag & FWRITE)) {
						up->u_error = EBADF;
						return;
					}
					break;

				case F_UNLCK:
					break;

				default:
					up->u_error = EINVAL;
					return;
			}
		}
#ifdef POSIX
		/* POSIX is consistent: validate l_type, but not f_flag */
		else if (up->u_procp->p_compatflags & COMPAT_POSIXFUS)
			switch (ld.l_type) {
				case F_RDLCK:
				case F_WRLCK:
				case F_UNLCK:
					break;

				default:
					up->u_error = EINVAL;
					return;
			}
#endif /* POSIX */

		/* convert offset to start of file */
		oldwhence = ld.l_whence;    /* save to renormalize later */
		if (up->u_error = rewhence(&ld, fp, 0))
			return;

		/* convert negative lengths to positive */
		if (ld.l_len < 0) {
			ld.l_start += ld.l_len;     /* adjust start point */
			ld.l_len = -(ld.l_len);     /* absolute value */
		}

		/* check for validity */
		if (ld.l_start < 0) {
			up->u_error = EINVAL;
			return;
		}

		if ((uap->cmd != F_GETLK) && (ld.l_type != F_UNLCK)) {
			/* If any locking is attempted, mark file locked
			 * to force unlock on close.
			 * Also, since the SVID specifies that the FIRST
			 * close releases all locks, mark process to
			 * reduce the search overhead in vno_lockrelease().
			 */
			*pop |= UF_FDLOCK;
			up->u_procp->p_flag |= SLKDONE;
		}
		switch(up->u_error = VOP_LOCKCTL((struct vnode *)fp->f_data, 
						 &ld, uap->cmd, fp->f_cred)) {
			case 0:
				break;      /* continue, if successful */
			case EAGAIN:
				if ( uap->cmd == F_SETLK )
					up->u_error = EACCES; /* For V.3 */
				return;

			case EWOULDBLOCK:
				up->u_error = EACCES;   /* EAGAIN ??? */
				return;
			default:
				return;     /* some other error code */
		}

		/* if F_GETLK, return flock structure to user-land */
		if (uap->cmd == F_GETLK) {
			/* per SVID, change only 'l_type' field if unlocked */
			if (ld.l_type == F_UNLCK) {
				if (up->u_error = copyout((caddr_t) &ld.l_type,
					(caddr_t)&((struct flock*)uap->arg)->l_type,
					sizeof (ld.l_type))) {
					return;
				}
			} else {
#ifdef POSIX
				/* POSIX wants the modified values returned */
				if (!(up->u_procp->p_compatflags & COMPAT_POSIXFUS)
					&& (up->u_error = rewhence(&ld, fp, oldwhence)))
#else
				if (up->u_error = rewhence(&ld, fp, oldwhence))
#endif /* POSIX */
					return;
				if (up->u_error = copyout((caddr_t) &ld, (caddr_t) uap->arg, sizeof (ld))) {
					return;
				}
			}
		}
		break;

	case F_GETOWN:
		up->u_error = fgetown(fp, &up->u_rval1);
		break;

	case F_SETOWN:
		up->u_error = fsetown(fp, uap->arg);
		break;

	default:
		up->u_error = EINVAL;
	}
}

fset(fp, bit, value)
	struct file *fp;
	int bit, value;
{

	if (value)
		fp->f_flag |= bit;
	else
		fp->f_flag &= ~bit;
#ifdef POSIX
	return (fioctl(fp, (int) ((bit & (FNDELAY|FNONBLOCK)) ? 
	    FIONBIO : FIOASYNC), (caddr_t)&value));
#else
	return (fioctl(fp, (int)(bit == FNDELAY ? FIONBIO : FIOASYNC),
	    (caddr_t)&value));
#endif POSIX
}

fgetown(fp, valuep)
	struct file *fp;
	int *valuep;
{
	int error;

	switch (fp->f_type) {

	case DTYPE_SOCKET:
		*valuep = ((struct socket *)fp->f_data)->so_pgrp;
		return (0);

	default:
		error = fioctl(fp, (int)TIOCGPGRP, (caddr_t)valuep);
		*valuep = -*valuep;
		return (error);
	}
}

fsetown(fp, value)
	struct file *fp;
	int value;
{

	if (fp->f_type == DTYPE_SOCKET) {
		((struct socket *)fp->f_data)->so_pgrp = value;
		return (0);
	}
	if (value > 0) {
		struct proc *p = pfind(value);
		if (p == 0)
			return (EINVAL);
		value = p->p_pgrp;
	} else
		value = -value;
	return (fioctl(fp, (int)TIOCSPGRP, (caddr_t)&value));
}

fioctl(fp, cmd, value)
	struct file *fp;
	int cmd;
	caddr_t value;
{

	return ((*fp->f_ops->fo_ioctl)(fp, cmd, value));
}

close()
{
	struct a {
		int	i;
	};
	register int fd;
	register struct file *fp;
	register struct user *up;

	up = &u;
	fd = ((struct a *)up->u_ap)->i;

	if ((fp = getf(fd)) == NULL)
		return;
	/* Release all System-V style record locks, if any */
	(void) (*lockrel)(fp); /* WHAT IF error returned? */

	if (fd >= NOFILE) {
		up->u_gofile[fd] = NULL;
		up->u_gpofile[fd] = 0;
	} else {
		up->u_ofile[fd] = NULL;
		up->u_pofile[fd] = 0;
	}
	closef(fp);
	/* WHAT IF up->u_error ? */
}

fstat()
{
	register struct file *fp;
	register struct a {
		int	fdes;
		struct	stat *sb;
	} *uap;
	struct stat ub;
	register struct user *up;

	up = &u;
	uap = (struct a *)up->u_ap;
	if ((fp = getf(uap->fdes)) == NULL)
		return;
	if ((up->u_error = (*fp->f_ops->fo_stat)(fp->f_data, &ub)) == 0)
		up->u_error = copyout((caddr_t)&ub, (caddr_t)uap->sb, sizeof (ub));
}

/*
 * Allocate a user file descriptor.
 */
ufalloc(fd)
	register int fd;
{
	register struct user *up;

	up = &u;

	if (fd >= NOFILE) {
		for (; fd < GNOFILE; fd++) {
			if (up->u_gofile[fd] == NULL) {
				up->u_rval1 = fd;
				up->u_gpofile[fd] = 0;
				return (fd);
			}
		}
	} else {	
		for (; fd < NOFILE; fd++) {
			if (up->u_ofile[fd] == NULL) {
				up->u_rval1 = fd;
				up->u_pofile[fd] = 0;
				return (fd);
			}
		}
	}
	up->u_error = EMFILE;
	return (-1);
}

ufavail()
{
	register int i, avail = 0;

	for (i = 0; i < NOFILE; i++)
		if (u.u_ofile[i] == NULL)
			avail++;
	return (avail);
}

struct	file *lastf;
/*
 * Allocate a user file descriptor
 * and a file structure.
 * Initialize the descriptor
 * to point at the file structure.
 */
struct file *
falloc(fd)
        register int fd;
{
	register struct file *fp;
	register struct user *up;

	up = &u;
	if ((fd = ufalloc(fd)) < 0)
		return (NULL);
	if (lastf == 0)
		lastf = file;
	for (fp = lastf; fp < (struct file *) v.ve_file; fp++)
		if (fp->f_count == 0)
			goto slot;
	for (fp = file; fp < lastf; fp++)
		if (fp->f_count == 0)
			goto slot;
	tablefull("file");
	syserr.fileovf++;
	up->u_error = ENFILE;
	return (NULL);
slot:
	if (fd >= NOFILE)
		up->u_gofile[fd] = fp;
	else
		up->u_ofile[fd] = fp;
	fp->f_count = 1;
	fp->f_data = 0;
	fp->f_offset = 0;
	crhold(up->u_cred);
	fp->f_cred = up->u_cred;
	lastf = fp + 1;
	return (fp);
}

/*
 * Convert a user supplied file descriptor into a pointer
 * to a file structure.  Only task is to check range of the descriptor.
 */
struct file *
getf(fd)
	register unsigned int fd;
{
	register struct file *fp;
	register struct user *up;

	up = &u;

	if (fd < NOFILE)
	 	fp = up->u_ofile[fd];
	else {
		if (up->u_gofile && fd < GNOFILE)
		        fp = up->u_gofile[fd];
		else 
		        fp = NULL;
	}
	if (fp == NULL)
		up->u_error = EBADF;
	return (fp);
}

/*
 * Internal form of close.
 * Decrement reference count on file structure.
 * If last reference not going away, but no more
 * references except in message queues, run a
 * garbage collect.  This would better be done by
 * forcing a gc() to happen sometime soon, rather
 * than running one each time.
 */
closef(fp)
	register struct file *fp;
{
	extern  struct fileops vnodefops;

	if (fp == NULL)
		return;
	if(fp->f_ops == &vnodefops)
	    unlock((struct vnode *)fp->f_data);
	if (fp->f_count > 1) {
		fp->f_count--;
		return;
	}
	if (fp->f_count < 1)
		panic("closef: count < 1");
	(*fp->f_ops->fo_close)(fp);
	crfree(fp->f_cred);
	fp->f_count = 0;
}

/*
 * Apply an advisory lock on a file descriptor.
 */
flock()
{
	register struct a {
		int	fd;
		int	how;
	} *uap = (struct a *)u.u_ap;
	register struct file *fp;

	if ((fp = getf(uap->fd)) == NULL)
		return;
	if (fp->f_type != DTYPE_VNODE) {
		u.u_error = EOPNOTSUPP;
		return;
	}
	if (uap->how & LOCK_UN) {
		vno_unlock(fp, FSHLOCK|FEXLOCK);
		return;
	}
	/* avoid work... */
	if ((fp->f_flag & FEXLOCK) && (uap->how & LOCK_EX) ||
	    (fp->f_flag & FSHLOCK) && (uap->how & LOCK_SH))
		return;
	u.u_error = vno_lock(fp, uap->how);
}

/*
 * Test if the current user is the super user.
 */
suser()
{

        if (u.u_uid == 0) {
                u.u_acflag |= ASU;
                return(1);
        }
        u.u_error = EPERM;
        return(0);
}
/*
 * Warn that a system table is full.
 */
tablefull(tab)
        char *tab;
{

        printf("%s: table is full\n", tab);
}

/*
 * Normalize SystemV-style record locks
 */
rewhence(ld, fp, newwhence)
	struct flock *ld;
	struct file *fp;
	int newwhence;
{
	struct vattr va;
	register int error;

	/* if reference to end-of-file, must get current attributes */
	if ((ld->l_whence == 2) || (newwhence == 2)) {
		if (error = VOP_GETATTR((struct vnode *)fp->f_data, &va, u.u_cred))
			return(error);
	}

	/* normalize to start of file */
	switch (ld->l_whence) {
		case 0:
			break;
		case 1:
			ld->l_start += fp->f_offset;
			break;
		case 2:
			ld->l_start += va.va_size;
			break;
		default:
			return(EINVAL);
	}

	/* renormalize to given start point */
	switch (ld->l_whence = newwhence) {
		case 1:
			ld->l_start -= fp->f_offset;
			break;
		case 2:
			ld->l_start -= va.va_size;
			break;
	}
	return(0);
}
