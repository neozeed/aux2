#ifndef lint	/* .../sys/SVFS/sys/5_subr.c */
#define _AC_NAME Z_5_subr_c
#define _AC_NO_MAIN "@(#) Copyright (c) 1983-87 Sun Microsystems Inc., 1985-87 UniSoft Corporation, All Rights Reserved.  {Apple version 2.1 89/10/14 15:29:33}"
#include <apple_notice.h>

#ifdef _AC_HISTORY
  static char *sccsid = "@(#)Copyright Apple Computer 1987\tVersion 2.1 of 5_subr.c on 89/10/14 15:29:33";
#endif		/* _AC_HISTORY */
#endif		/* lint */

#define _AC_MODS
/*      @(#)ufs_subr.c 1.1 86/02/03 SMI; from UCB 4.5 83/03/21  */
/*      @(#)ufs_subr.c  2.1 86/04/14 NFSSRC */

#include "sys/types.h"
#include "sys/param.h"
#include "sys/time.h"
#include "sys/mmu.h"
#include "sys/page.h"
#include "sys/systm.h"
#include "sys/conf.h"
#include "sys/buf.h"
#include "sys/signal.h"
#include "sys/user.h"
#include "sys/errno.h"
#include "sys/vfs.h"
#include "sys/vnode.h"
#include "sys/sysmacros.h"
#ifdef QUOTA
#include "sys/quota.h"
#endif QUOTA
#include "svfs/inode.h"
#include "sys/mount.h"
#include "svfs/filsys.h"
#include "sys/var.h"
#include "sys/tuneable.h"

int	syncprt = 0;
static	int updlock = 0;

/*
 * Update is the internal name of 'sync'.  It goes through the disk
 * queues to initiate sandbagged IO; goes through the inodes to write
 * modified nodes; and it goes through the mount table to initiate
 * the writing of the modified super blocks.
 */
update(vfsp, flag)
struct vfs 	*vfsp;
int		flag;
{
	register struct inode *ip;
	register struct mount *mp;
	struct filsys *fs;

	if(updlock) {
		updlock |= B_WANTED;
		sleep(&updlock, PRIBIO+1);
		return;
	}
	updlock = B_BUSY;

	mp = (struct mount *)vfsp->vfs_data;
	fs = mp->m_bufp->b_un.b_fs;
	if (fs->s_ronly == 0 && flag)
		fs->s_fmod = 1;
	if (fs->s_fmod != 0 && fs->s_ronly == 0) {
		fs->s_fmod = 0;
		fs->s_time = time.tv_sec;
		sbupdate(mp);

	}
	/*
	 * Write back each (modified) inode.
	 */
	for (ip = inode; ip < (struct inode *)v.ve_inode; ip++) {
		if ((ip->i_flag & ILOCKED) != 0 || (ip->i_flag & IREF) == 0 ||
		    (ip->i_flag & (IACC | IUPD | ICHG)) == 0)
			continue;
		ip->i_flag |= ILOCKED;
		ip->i_lockedfile = __FILE__;
		ip->i_lockedline = __LINE__;
#ifdef ITRACE
		itrace(ip, caller(), 1);
		timeout(ilpanic, ip, 10 * v.v_hz);
#endif
		VN_HOLD(ITOV(ip));
		iupdat(ip, 0);
		iput(ip);
	}
	/*
	 * Force stale buffer cache information to be flushed,
	 * for all devices.
	 */
	bflush((struct vnode *) 0);
	if(updlock & B_WANTED)
		wakeup(&updlock);
	updlock = 0;
}

/*
 * Flush all the blocks associated with an inode.
 * Note that we make a more stringent check of
 * writing out any block in the buffer pool that may
 * overlap the inode. This brings the inode up to
 * date with recent mods to the cooked device.
 */
syncip(ip)
	register struct inode *ip;
{
	long lbn, lastlbn;
	daddr_t blkno;

	lastlbn = howmany(ip->i_size, FsBSIZE(ip->i_fs));
	for (lbn = 0; lbn < lastlbn; lbn++) {
		blkno = FsLTOP(ip->i_fs, vbmap(ip, lbn, B_READ,0,0,0,0));
		blkflush(ip->i_devvp, blkno, (long) FsBSIZE(ip->i_fs));
	}
	imark(ip, ICHG);
	iupdat(ip, 1);
}

struct filsys *
trygetfs(dev)
	dev_t dev;
{
	register struct mount *mp;

	mp = getmp(dev);
	if (mp == NULL) {
		return(NULL);
	}
	return (mp->m_bufp->b_un.b_fs);
}

/* 
 * do SVFS initializations
 */
svfs_init()
{
	extern  struct vfsops   svfs_vfsops;
	int n;
    
        n = btop(v.v_inode * sizeof(struct inode));
        availrmem -= n;
        availsmem -= n;

        if(availrmem < tune.t_minarmem || availsmem < tune.t_minasmem) {
                pre_panic();
                printf("svfsinit - can't get %d pages\n", n);
                panic("svfsinit - will not allocate inodes");
        }
        if((inode=(struct inode *)kvalloc(n, PG_ALL, -1)) == NULL) {
                panic("svfsinit cannot allocate inodes");
        }

	/*
	 * Note that although we've allocacted enough space for x inodes,
	 * where x = ptob(n) / sizeof(struct inode) and x >= v.v_inode,
	 * we still tell the system that we only have v.v_inode available
	 * inodes.  This is because UFS also uses v.v_inode and if we
	 * bump it up, we will also bump up the number of UFS inodes.
	 * There should be a global for the number of SVFS inode instead of
	 * v.v_inode. 
	 */
        v.ve_inode  = (char *)&inode[v.v_inode];;
        clear((caddr_t)inode, sizeof(*inode) * v.v_inode);

        /*
         * initialize the inodes
         */
        svfs_inoinit();

	/*
	 * install pointer to our vfsops in the vfssw table
	 */
        vfssw[MOUNT_SVFS] = &svfs_vfsops;
}
