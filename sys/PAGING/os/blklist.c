#ifndef lint	/* .../sys/PAGING/os/blklist.c */
#define _AC_NAME blklist_c
#define _AC_NO_MAIN "@(#) Copyright (c) 1984-85 AT&T-IS, 1985-87 UniSoft Corporation, All Rights Reserved.  {Apple version 2.1 89/10/13 12:43:43}"
#include <apple_notice.h>

#ifdef _AC_HISTORY
  static char *sccsid = "@(#)Copyright Apple Computer 1989\tVersion 2.1 of blklist.c on 89/10/13 12:43:43";
#endif		/* _AC_HISTORY */
#endif		/* lint */

#define _AC_MODS
/*	@(#)blklist.c	UniPlus VVV.2.1.2	*/

#ifdef lint
#include "sys/sysinclude.h"
#else lint
#include	"sys/types.h"
#include	"sys/mmu.h"
#include	"sys/param.h"
#include	"sys/sysmacros.h"
#include	"sys/page.h"
#include	"sys/dir.h"
#include	"sys/signal.h"
#include	"sys/time.h"
#include	"sys/vnode.h"
#include	"sys/user.h"
#include	"sys/buf.h"
#include	"sys/region.h"
#include	"sys/proc.h"
#include	"sys/pfdat.h"
#include	"sys/debug.h"
#include	"sys/vfs.h"
#include	"svfs/inode.h"
#endif lint


/*	Build the list of block numbers for a file.  This is used
 *	for mapped files.
 */

bldblklst(lp, vp, blast)
register daddr_t	*lp;
register struct vnode	*vp;
register daddr_t	blast;
{	register daddr_t bfirst;
	register int	 lbsize;
	register int	 pgspblk;
	register int	 i;
	daddr_t		 btemp;

	if ((lbsize = vp->v_vfsp->vfs_bsize) <= NBPP)
		pgspblk = 1;
	else
		pgspblk = lbsize / NBPP;

	for (bfirst = (daddr_t) 0; bfirst < blast; bfirst++) {
		VOP_BMAP(vp, bfirst, &vp->v_mappedvp, &btemp);

		for (i = 0; i < pgspblk; i++) {
			if ((*lp++ = btemp) != -1)
				btemp += (NBPP / DEV_BSIZE);
		}
	}
}



/*	Free the block list attached to an vnode.
 */

freeblklst(vp)
register struct vnode *vp;
{
	if (vp->v_vfsp->vfs_flag & VFS_REMOTE) {
		register u_long	nblks;
		register u_long	blkspp;
		register u_long	i;
		dbd_t		dbd;
		reg_t		reg;

		dbd.dbd_type = DBD_FILE;
		reg.r_vptr = vp;
		if ((blkspp = NBPP / vp->v_vfsp->vfs_bsize) == 0)
			blkspp = 1;
		nblks = vp->v_mapsize - 1;   /* allow for filesize slot */

		for (i = 0; i < nblks; i += blkspp) {
		       dbd.dbd_blkno = i;
		       (void) pbremove(&reg, &dbd);
		}
	}
	vp->v_map--;	   /* allow for filesize slot */
	kvfree((int)vp->v_map, btop(vp->v_mapsize*sizeof (int *)));
	vp->v_map = NULL;
	vp->v_mapsize = 0;
}

/* <@(#)blklist.c	1.3> */
