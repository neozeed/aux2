#ifndef lint	/* .../sys/SVFS/sys/5_pipe.c */
#define _AC_NAME Z_5_pipe_c
#define _AC_NO_MAIN "@(#) Copyright (c) 1983-87 Sun Microsystems Inc., 1985-87 UniSoft Corporation, All Rights Reserved.  {Apple version 2.1 89/10/14 15:27:40}"
#include <apple_notice.h>

#ifdef _AC_HISTORY
  static char *sccsid = "@(#)Copyright Apple Computer 1987\tVersion 2.1 of 5_pipe.c on 89/10/14 15:27:40";
#endif		/* _AC_HISTORY */
#endif		/* lint */

#define _AC_MODS
/* @(#)pipe.c	1.4 */

#include "sys/param.h"
#include "sys/types.h"
#include "sys/time.h"
#include "sys/mmu.h"
#include "sys/page.h"
#include "sys/region.h"
#include "sys/systm.h"
#include "sys/signal.h"
#include "sys/buf.h"
#include "svfs/filsys.h"
#include "sys/proc.h"
#include "sys/user.h"
#include "sys/errno.h"
#include "sys/vnode.h"
#include "svfs/inode.h"
#include "sys/file.h"
#include "sys/vfs.h"
#include "sys/mount.h"
#include "sys/var.h"

extern  struct fileops vnodefops;

int pipe_5();

int (*pipe_create)() = pipe_5;      /* SVFS pipes are default */

pipe()
{
    (*pipe_create)();
}


/*
 * The sys-pipe entry.
 * Allocate an inode on the root device.
 * Allocate 2 file structures.
 * Put it all together with flags.
 */
pipe_5()
{
	register struct inode *ip;
	register struct file *rf, *wf;
	register struct mount *mp;
	register struct user *up;
	struct vnode *vp;
	int r;

	up = &u;
	if ((rf = falloc()) == NULL)
		return;
	r = up->u_rval1;
	if ((mp = getmp(pipedev)) == NULL)
		mp = getmp(rootdev);
	if (VFS_ROOT(mp->m_vfsp, &vp)) {
		rf->f_count = 0;
		up->u_ofile[r] = NULL;
		crfree(rf->f_cred);
		return;
	}
	if ((ip = ialloc(VTOI(vp), IFIFO, 0)) == NULL) {
		rf->f_count = 0;
		up->u_ofile[r] = NULL;
		crfree(rf->f_cred);
		VN_RELE(vp);
		return;
	}
	VN_RELE(vp);
	rf->f_flag = FREAD;
	rf->f_type = DTYPE_VNODE;
	rf->f_data = (caddr_t)ITOV(ip);
	rf->f_ops = &vnodefops;
	if ((wf = falloc()) == NULL) {
		rf->f_count = 0;
		up->u_ofile[r] = NULL;
		crfree(rf->f_cred);
		iput(ip);
		return;
	}
	wf->f_flag = FWRITE;
	wf->f_type = DTYPE_VNODE;
	wf->f_data = (caddr_t)ITOV(ip);
	wf->f_ops = &vnodefops;
	up->u_rval2 = up->u_rval1;
	up->u_rval1 = r;
	(ITOV(ip))->v_count = 2;
	(ITOV(ip))->v_type = IFTOVT(IFIFO);	/* override VN_INIT in iget() */
	ip->i_frcnt = 1;
	ip->i_fwcnt = 1;
	iunlock(ip);
	return;
}

/*
 * Open a pipe
 * Check read and write counts, delay as necessary
 */

openp(ip, mode)
register struct inode *ip;
register mode;
{
	if (mode&FREAD) {
		if (ip->i_frcnt++ == 0)
			wakeup((caddr_t)&ip->i_frcnt);
	}
	if (mode&FWRITE) {
#ifdef POSIX
		if ((mode & (FNDELAY|FNONBLOCK)) && ip->i_frcnt == 0)
			return(ENXIO);
#else
		if (mode&FNDELAY && ip->i_frcnt == 0)
			return(ENXIO);
#endif POSIX
		if (ip->i_fwcnt++ == 0)
			wakeup((caddr_t)&ip->i_fwcnt);
	}
	if (mode&FREAD) {
		while (ip->i_fwcnt == 0) {
#ifdef POSIX
			if ((mode & (FNDELAY|FNONBLOCK)) || ip->i_size)
				return (0);
#else
			if (mode&FNDELAY || ip->i_size)
				return (0);
#endif POSIX
			(void) sleep((caddr_t)&ip->i_fwcnt, PPIPE);
		}
	}
	if (mode&FWRITE) {
		while (ip->i_frcnt == 0)
			(void) sleep((caddr_t)&ip->i_frcnt, PPIPE);
	}

	return (0);
}

/*
 * Close a pipe
 * Update counts and cleanup
 */

closep(ip, mode)
register struct inode *ip;
register mode;
{
	register i;
	daddr_t bn;

	if (mode&FREAD) {
		if ((--ip->i_frcnt == 0) && (ip->i_fflag&IFIW)) {
			ip->i_fflag &= ~IFIW;
			wakeup((caddr_t)&ip->i_fwcnt);
			/* does not set curpri (no IFIW) */
			p_wakeup(ip, FWRITE);
		}
	}
	if (mode&FWRITE) {
		if ((--ip->i_fwcnt == 0) && (ip->i_fflag&IFIR)) {
			ip->i_fflag &= ~IFIR;
			wakeup((caddr_t)&ip->i_frcnt);
			/* does not set curpri (no IFIR) */
			p_wakeup(ip, FREAD);
		}
	}
	if ((ip->i_frcnt == 0) && (ip->i_fwcnt == 0)) {
		for (i=NFADDR-1; i>=0; i--) {
			bn = ip->i_faddr[i];
			if (bn == (daddr_t)0)
				continue;
			ip->i_faddr[i] = (daddr_t)0;
			free(ip, bn, (off_t) FsBSIZE(ip->i_fs));
		}
		ip->i_size = 0;
		ip->i_frptr = 0;
		ip->i_fwptr = 0;
		ip->i_fflag = 0;	/* Insurance */
		ip->i_flag |= IUPD|ICHG;
	}
}

p_select(ip, rw)
register struct inode	*ip;
int	rw;
{
	switch(rw) {
	case FREAD:
		if (ip->i_size || ip->i_fwcnt == 0)
			return(1);
		p_qsel(ip, IFRDSEL, IFRDCOLL);
		break;

	case FWRITE:
		if (ip->i_frcnt && ip->i_size < PIPSIZ)
			return(1);
		p_qsel(ip, IFWRSEL, IFWRCOLL);
			break;
	}
	return(0);
}

p_qsel(ip, indx, flag)		/* Queue up a select sleep */
register struct inode	*ip;
register int	indx, flag;
{
	register struct proc *p;
	extern int	selwait;

	if ((p = ip->i_select[indx]) && p->p_wchan == (caddr_t) &selwait)
		ip->i_fflag |= flag;
	else
		ip->i_select[indx] = u.u_procp;
}

/* ARGSUSED */
p_wakeup(ip, rw)
register struct inode *ip;
int	rw;
{
	switch(rw) {
	case FREAD:
		if (ip->i_select[IFRDSEL]) {
			selwakeup(ip->i_select[IFRDSEL], ip->i_fflag & IFRDCOLL);
			ip->i_select[IFRDSEL] = NULL;
			ip->i_fflag &= ~IFRDCOLL;
		}
		if (ip->i_fflag & IFIR) {
			ip->i_fflag &= ~IFIR;
			curpri = PPIPE;
			wakeup((caddr_t)&ip->i_frcnt);
		}
		break;
	case FWRITE:
		if (ip->i_select[IFWRSEL]) {
			selwakeup(ip->i_select[IFWRSEL], ip->i_fflag & IFWRCOLL);
			ip->i_select[IFWRSEL] = NULL;
			ip->i_fflag &= ~IFWRCOLL;
		}
		if (ip->i_fflag & IFIW) {
			ip->i_fflag &= ~IFIW;
			curpri = PPIPE;
			wakeup((caddr_t)&ip->i_fwcnt);
		}
		break;
	}
}
