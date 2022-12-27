#ifndef lint	/* .../sys/PAGING/os/usys4.c */
#define _AC_NAME usys4_c
#define _AC_NO_MAIN "@(#) Copyright (c) 1987 Apple Computer, Inc., 1985-87 UniSoft Corporation, All Rights Reserved.  {Apple version 2.3 89/11/29 15:15:39}"
#include <apple_notice.h>

#ifdef _AC_HISTORY
  static char *sccsid = "@(#)Copyright Apple Computer 1989\tVersion 2.3 of usys4.c on 89/11/29 15:15:39";
#endif	/* _AC_HISTORY */
#endif	/* lint */

#define _AC_MODS

/*	@(#)usys4.c	UniPlus VVV.2.1.2	*/


#ifdef lint
#include "sys/sysinclude.h"
#else lint
#include "sys/types.h"
#include "sys/mmu.h"
#include "sys/param.h"
#include "sys/sysmacros.h"
#include "sys/page.h"
#include "sys/systm.h"
#include "sys/signal.h"
#include "sys/dir.h"
#include "sys/time.h"
#include "sys/user.h"
#include "sys/region.h"
#include "sys/proc.h"
#include "sys/buf.h"
#include "sys/var.h"
#include "sys/vfs.h"
#include "sys/vnode.h"
#include "sys/errno.h"
#include "sys/reboot.h"
#endif lint


int shutdownflags = 0;

/*
 * phys - Set up a physical address in user's address space.
 */
phys()
{
	register struct a {
		unsigned phnum;		/* phys segment number */
		unsigned laddr;		/* logical address */
		unsigned bcount;	/* byte count */
		unsigned phaddr;	/* physical address */
	} *uap;

	if (suser()) {
	        uap = (struct a *)u.u_ap;
		dophys(uap->phnum, uap->laddr, uap->bcount, uap->phaddr);
	}
}

/*
 * reboot the system
 */
reboot()
{
	register struct a {
		int	 i;
	};
	register int howto;

	if (suser()) {
	        howto = ((struct a *)u.u_ap)->i;

		/*
		 * for now, assume that no magic indicates an old binary
		 */
		if ((howto & RB_MAGIC) != RB_MAGIC)
		        howto = RB_AUTOBOOT | RB_KILLALL | RB_UNMOUNT;
		sys_shutdown(howto);
	}
}

/*
 * powerdown the system
 */
powerdown()
{
	if (suser())
	        sys_shutdown(RB_HALT | RB_KILLALL | RB_UNMOUNT);
}



/*
 * isolate the process from the shutdown procedure
 * by having proc 0 actually do it
 * this allows us to kill all of the processes
 */

sys_shutdown(flags)
{
        shutdownflags = flags;
	setrun(&proc[0]);
        sleep(&shutdownflags, PZERO+1);
}


/*
 * perform various shutdown options
 */
real_shutdown(flags)
register int flags;
{
	register int verbose = flags & RB_VERBOSE;

	if ((flags & RB_MAGIC) != RB_MAGIC)
		return;

	if ((flags & RB_KILLALL) == RB_KILLALL)
		killall(verbose);

	if ((flags & RB_NOSYNC) == 0)
		sync_disks(verbose);

	if ((flags & RB_UNMOUNT) == RB_UNMOUNT)
		unmountall(verbose);

	if ((flags & RB_HALT) == RB_HALT)
		dopowerdown();

	if ((flags & RB_BUSYLOOP) == RB_BUSYLOOP) {
		spl7();
		printf("Processor stopped in tight loop.");
		for (;;) ;
	}

	/* must be last */
	if ((flags & RB_AUTOBOOT) == RB_AUTOBOOT)
		doboot();
	/* NOTREACHED */
}

/* 
 * make an attempt to kill all processes, including init.  If we don't kill
 * init, it will keep spawing shells on the console.
 */
killall(verbose)
register int verbose;
{	register struct proc *p;

	if (verbose)
		printf("Sending all procs SIGTERM\n");

	/* politely suggest that processes die */
	for (p = &proc[1]; p < (struct proc *)v.ve_proc; p++) {
	        if (p->p_flag&SSYS)
		        continue;
		if (p->p_stat == 0 || p->p_stat == SZOMB)
		        continue;
		if (p->p_sigignore&(sigmask(SIGTERM)))
		        psignal(p, SIGKILL);
		else
		        psignal(p, SIGTERM);
	}
	if (check_dead(5))
	        return;


	/* terminate the ones that don't take the hint */
	if (verbose)
		printf("Sending all remaining procs SIGKILL: ");
	for (p = &proc[1]; p < (struct proc *)v.ve_proc; p++) {
	        if (p->p_flag&SSYS)
		        continue;
		if (p->p_stat == 0 || p->p_stat == SZOMB)
		        continue;
		if (verbose)
		        printf("%d ", p->p_pid);
		psignal(p, SIGKILL);
	}
	if (verbose)
		printf("\n");
	check_dead(5);
}


check_dead(secs)
register int secs;
{       register struct proc *p;
	register int i;

	for (i = 0; i < secs; i++) {
	        delay(HZ);

		for (p = &proc[1]; p < (struct proc *)v.ve_proc; p++) {
		        if (p->p_flag&SSYS)
			        continue;
		        if (p->p_stat && p->p_stat != SZOMB)
			        break;
		}
		if (p >= (struct proc *)v.ve_proc)
		        return(1);
	}
	return(0);
}


/*
 * sync the disks and make some effort to wait
 */
sync_disks( verbose )
register int verbose;
{
	extern struct buf *sbuf;
	register struct buf *bp;
	register int iter, nbusy;

	if(verbose)
		printf("syncing disks... ");
	(void) splnet();
	/*
	 * Release inodes held by texts before sync.
	 */
	xumount((struct vfs *)NODEV);
	sync();

	for (iter = 0; iter < 20; iter++) {
		nbusy = 0;
		for (bp = &sbuf[v.v_buf]; --bp >= sbuf; ) {
			if ((bp->b_flags & (B_BUSY|B_INVAL)) == B_BUSY)
				nbusy++;
		}
		if (verbose)
			printf("%d ", nbusy);
		if (nbusy == 0)
			break;
		delay(3 * iter);
	}
	if (verbose)
		printf("done\n");
}

/*
 * unmount all file systems.  we are counting on the fact that file systems
 * in the linked list pointed at by roovfs are ordered such that the most 
 * recently mounted file system is at the top (see vfs_add).  Thus we can
 * believe that we will not remove parnet mount points before children.
 */
unmountall(verbose)
register int verbose;
{
	extern struct vfs *rootvfs;
	register struct vfs *vfsp;
	register int err;
	register struct vnode *coveredvp;

	if (verbose)
		printf("unmounting non-root file systems.\n");

	dnlc_purge();	/* remove dnlc entries for all file sys */

	/*
	 * for each non-root file system, unmount it and release the vnode of
	 * the mount point.  we could call dounmount to do this, but it always
	 * does a vfs_sync and a vfs_remove.  we don't care about vfs_remove or
	 * releasemem because we're dying anyway.
	 */
	for (vfsp = rootvfs->vfs_next ; vfsp ; vfsp = vfsp->vfs_next ) {
		if (vfsp->vfs_flag & VFS_MLOCK) {
			if (verbose)
				printf("unmountall: vfsp=0x%x locked\n", vfsp);
			continue;
		}
		coveredvp = vfsp->vfs_vnodecovered;
		if (err = VFS_UNMOUNT(vfsp)) {
			if (verbose)
				printf("unmountall: failed vfsp=0x%x, error=%d\n", vfsp, err);
		} else {
			VN_RELE(coveredvp);
		}
	}

	if (verbose)
		printf("unmounting root file system.\n");
	VFS_UNMOUNTROOT(rootvfs);
}
