#ifndef lint	/* .../sys/COMMON/os/acct.c */
#define _AC_NAME acct_c
#define _AC_NO_MAIN "@(#) Copyright (c) 1984-85 AT&T-IS, All Rights Reserved.  {Apple version 2.1 89/10/13 19:26:12}"
#include <apple_notice.h>

#ifdef _AC_HISTORY
  static char *sccsid = "@(#)Copyright Apple Computer 1987\tVersion 2.1 of acct.c on 89/10/13 19:26:12";
#endif		/* _AC_HISTORY */
#endif		/* lint */

#define _AC_MODS
/*	@(#)acct.c	UniPlus 2.1.1	*/

/*	@(#)kern_acct.c 1.1 85/05/30 SMI; from UCB 4.1 83/05/27	*/

#include "sys/types.h"
#include "sys/param.h"
#include "sys/time.h"
#include "sys/mmu.h"
#include "sys/page.h"
#include "sys/systm.h"
#include "sys/signal.h"
#include "sys/user.h"
#include "sys/vnode.h"
#include "sys/vfs.h"
#include "sys/acct.h"
#include "sys/uio.h"
#include "sys/errno.h"
#include "sys/debug.h"
#include "sys/region.h"
#include "sys/proc.h"

/*
 * SHOULD REPLACE THIS WITH A DRIVER THAT CAN BE READ TO SIMPLIFY.
 */
struct	vnode *acctp;

/*
 * Perform process accounting functions.
 */
sysacct(uap)
	register struct a {
		char	*fname;
	} *uap;
{
	struct vnode *vp;

	if (suser()) {
		if (uap->fname==NULL) {
			if (vp = acctp) {
				VN_RELE(vp);
				acctp = NULL;
			}
			return;
		}
		if (acctp) {
			u.u_error = EBUSY;
			return;
		}
		u.u_error =
		    lookupname(uap->fname, UIOSEG_USER, FOLLOW_LINK,
			(struct vnode **)0, &vp);
		if (u.u_error)
			return;
		if (vp->v_type != VREG) {
			u.u_error = EACCES;
			VN_RELE(vp);
			return;
		}
		if (acctp)
			VN_RELE(acctp);
		acctp = vp;
	}
}

int	acctsuspend = 2;	/* stop accounting when < 2% free space left */
int	acctresume = 4;		/* resume when free space risen to > 4% */

struct	acct acctbuf;
/*
 * On exit, write a record on the accounting file.
 */
acct(st)
{
	register struct vnode *vp;
	register struct user *up;
	register struct acct *ap = &acctbuf;

	if ((vp = acctp) == NULL)
		return;
	up = &u;
	bcopy((caddr_t)up->u_comm, (caddr_t)ap->ac_comm, sizeof(ap->ac_comm));
	ap->ac_btime = up->u_start;
	ap->ac_utime = compress(up->u_utime);
	ap->ac_stime = compress(up->u_stime);
	ap->ac_etime = compress(lbolt - up->u_ticks);
	ap->ac_mem = compress(up->u_mem);
	ap->ac_io = compress(up->u_ioch);
	ap->ac_rw = compress(up->u_ior+up->u_iow);
	ap->ac_uid = up->u_ruid;
	ap->ac_gid = up->u_rgid;
	ap->ac_tty = up->u_procp->p_ttyp ? up->u_ttyd : NODEV;
	ap->ac_stat = st;
	ap->ac_flag = up->u_acflag;
	u.u_error =
	    vn_rdwr(UIO_WRITE, vp, (caddr_t)ap, sizeof(acctbuf), 0,
		UIOSEG_KERNEL, IO_UNIT|IO_APPEND, (int *)0);
}

/*
 * Produce a pseudo-floating point representation
 * with 3 bits base-8 exponent, 13 bits fraction.
 */
compress(t)
	register long t;
{
	register exp = 0, round = 0;

	while (t >= 8192) {
		exp++;
		round = t&04;
		t >>= 3;
	}
	if (round) {
		t++;
		if (t >= 8192) {
			t >>= 3;
			exp++;
		}
	}
	return ((exp<<13) + t);
}
