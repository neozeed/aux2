#ifndef lint	/* .../sys/COMMON/os/utssys.c */
#define _AC_NAME utssys_c
#define _AC_NO_MAIN "@(#) Copyright (c) 1983-87 Sun Microsystems Inc., 1985-87 UniSoft Corporation, All Rights Reserved.  {Apple version 2.1 89/10/13 19:27:11}"
#include <apple_notice.h>

#ifdef _AC_HISTORY
  static char *sccsid = "@(#)Copyright Apple Computer 1987\tVersion 2.1 of utssys.c on 89/10/13 19:27:11";
#endif		/* _AC_HISTORY */
#endif		/* lint */

#define _AC_MODS
/* @(#)utssys.c	1.3 */
#include "sys/types.h"
#include "sys/mmu.h"
#include "sys/param.h"
#include "sys/sysmacros.h"
#include "sys/page.h"
#include "sys/systm.h"
#include "sys/time.h"
#include "sys/signal.h"
#include "sys/user.h"
#include "sys/vfs.h"
#include "svfs/filsys.h"
#include "sys/buf.h"
#include "sys/mount.h"
#include "sys/errno.h"
#include "sys/var.h"
#include "sys/utsname.h"
#include "ustat.h"

utssys()
{
	register i;
	register struct a {
		char	*cbuf;
		int	mv;
		int	type;
	} *uap;
	register struct user *up;

	up = &u;
	uap = (struct a *)up->u_ap;
	switch(uap->type) {

case 0:		/* uname */
	if (copyout((caddr_t)&utsname, uap->cbuf, sizeof(struct utsname)))
		up->u_error = EFAULT;
	return;

/* case 1 was umask */

case 2:		/* ustat */
	for(i=0; i<v.v_mount; i++) {
		register struct mount *mp;

		mp = &mounttab[i];
		if (mp->m_bufp && mp->m_dev == uap->mv) {
			struct statfs sb;
			struct ustat ust;

			up->u_error = VFS_STATFS(mp->m_vfsp, &sb);
			if (up->u_error)
				return;

			if (up->u_uid == 0)
				ust.f_tfree = sb.f_bfree
					* (sb.f_bsize / DEV_BSIZE);
			else
				ust.f_tfree = sb.f_bavail
					* (sb.f_bsize / DEV_BSIZE);
			ust.f_tinode = sb.f_ffree;
			bcopy(sb.f_fname, ust.f_fname, sizeof(ust.f_fname));
			bcopy(sb.f_fpack, ust.f_fpack, sizeof(ust.f_fpack));
			if (copyout((caddr_t)&ust, uap->cbuf, sizeof(ust)))
				up->u_error = EFAULT;
			return;
		}
	}
	up->u_error = EINVAL;
	return;

case 33:	/* uvar */
	if (copyout((caddr_t)&v, uap->cbuf, sizeof(struct var)))
		up->u_error = EFAULT;
	return;

default:
	up->u_error = EFAULT;
	}
}
