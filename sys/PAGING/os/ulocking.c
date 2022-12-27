#ifndef lint	/* .../sys/PAGING/os/ulocking.c */
#define _AC_NAME ulocking_c
#define _AC_NO_MAIN "@(#) Copyright (c) 1985-87 UniSoft Corporation, All Rights Reserved.  {Apple version 2.1 89/10/13 12:09:24}"
#include <apple_notice.h>

#ifdef _AC_HISTORY
  static char *sccsid = "@(#)Copyright Apple Computer 1987\tVersion 2.1 of ulocking.c on 89/10/13 12:09:24";
#endif		/* _AC_HISTORY */
#endif		/* lint */

#define _AC_MODS
/*	@(#)ulocking.c	UniPlus VVV.2.1.1	*/

#ifdef lint
#include "sys/sysinclude.h"
#else lint
#include "sys/param.h"
#include "sys/types.h"
#include "sys/mmu.h"
#include "sys/sysmacros.h"
#include "sys/page.h"
#include "sys/systm.h"
#include "sys/sysinfo.h"
#include "sys/time.h"
#include "sys/vfs.h"
#include "sys/mount.h"
#include "sys/vnode.h"
#include "svfs/inode.h"
#include "svfs/filsys.h"
#include "sys/signal.h"
#include "sys/user.h"
#include "sys/errno.h"
#include "sys/file.h"
#include "sys/buf.h"
#include "sys/var.h"
#include "sys/region.h"
#include "sys/proc.h"
#include "sys/locking.h"
#endif lint


/*
 * file lock routines
 * John Bass, PO Box 1223, San Luis Obispo, CA 93401
 * Original design spring 1976, CalPoly, San Luis Obispo
 * Deadlock idea from Ed Grudzien at Basys April 1980
 * Extensions Fall 1980, Onyx Systems Inc., San Jose
 * Linted and ported to System V by UniSoft Systems, Berkeley, CA.
 */

#define MAXSIZE (long)(1L<<30)	/* number larger than any request */
#define	LLWANT	0x1

/*
 * locking -- handles syscall requests
 */
locking() 
{
	struct file *fp;
	struct vnode *vp;
	/*
	 * define order and type of syscall args
	 */
	register struct a {
		int fdes;
		int flag;
		off_t size;
	} *uap = (struct a *)u.u_ap;
	register struct locklist *cl, *nl;
	off_t LB, UB;

	/*
	 * check for valid open file
	 */
	fp = getf(uap->fdes);
	if(fp == NULL) return;
	if (fp->f_type != DTYPE_VNODE)
		return(EOPNOTSUPP);
	vp = (struct vnode *) fp->f_data;
	if (vp->v_vfsp->vfs_flag & VFS_REMOTE)
		return (EREMOTE);
	if (vp->v_type != VREG) {
		u.u_error = EACCES;
		return;
	}

	/*
	 * validate ranges
	 * kludge for zero length
	 */
	LB = fp->f_offset;
	if( uap->size ) {
		UB = LB + uap->size;
		if(UB <= 0) UB = MAXSIZE;
	}
	else UB = MAXSIZE;

	/*
	 * test for unlock request
	 */
	if(uap->flag == 0) {
		/*
		 * starting at list head scan
		 * for locks in the range by
		 * this process
		 */
		cl = (struct locklist *)&vp->v_locklist;/* addr is pointer */
		while(nl = cl->ll_link) {
			/*
			 * if not by this process skip to next lock
			 */
			if(nl->ll_proc != u.u_procp) {
				cl = nl;
				continue;
			}
			/*
			 * check for locks in proper range
			 */
			if( UB <= nl->ll_start )
				break;
			if( nl->ll_end <= LB ) {
				cl = nl;
				continue;
			}
			/*
			 * for locks fully contained within
			 * requested range, just delete the item
			 */
			if( LB <= nl->ll_start && nl->ll_end <= UB) {
				cl->ll_link = nl->ll_link;
				lockfree(nl);
				continue;
			}
			/*
			 * if some one is sleeping on this lock
			 * do a wakeup, we may free the region
			 * being slept on
			 */
			if(nl->ll_flags & LLWANT) {
				nl->ll_flags &= ~LLWANT;
				wakeup((caddr_t)nl);
			}
			/*
			 * middle section is being removed
			 * add new lock for last section
			 * modify existing lock for first section.
			 * if no locks, return in error
			 */
			if( nl->ll_start < LB && UB < nl->ll_end) {
				if( lockadd(nl,UB,nl->ll_end) ) return;
				nl->ll_end = LB;
				break;
			}
			/*
			 * first section is being deleted
			 * just move starting point up
			 */
			if( LB <= nl->ll_start && UB < nl->ll_end) {
				nl->ll_start = UB;
				break;
			}
			/*
			 * must be deleting last part of this section
			 * move ending point down
			 * continue looking for locks covered by upper
			 * limit of unlock range
			 */
			nl->ll_end = LB;
			cl = nl;
		}
		/*
		 * end of scan for unlock request
		 */
		return;
	}
	/*
	 * request must be a lock of some kind
	 * check to see if the region is lockable by this
	 * process
	 */
	u.u_error = locked(uap->flag, vp, LB, UB);
	if (u.u_error)
		return;
	cl = (struct locklist *)&vp->v_locklist;/* note addr is pointer */
	/*
	 * simple case, no existing locks, simply add new lock
	 */
	if( (nl=cl->ll_link) == NULL ) {
		(void) lockadd(cl, LB, UB);
		return;
	}
	/*
	 * simple case, lock is before existing locks,
	 * simply insert at head of list
	 */
	if( UB < nl->ll_start ) {
		(void) lockadd(cl,LB,UB);
		return;
	}
	/*
	 * ending range of lock is same as start of lock by
	 * another process, simply insert at head of list
	 */
	if( UB <= nl->ll_start && u.u_procp != nl->ll_proc ) {
		(void) lockadd(cl, LB, UB);
		return;
	}
	/*
	 * request range overlaps with begining of first request
	 * modify starting point in lock to include requested region
	 */
	if( UB >= nl->ll_start && LB < nl->ll_start ) {
		nl->ll_start = LB;
	}
	/*
	 * scan thru remaining locklist
	 */
	cl = nl;
	for (;;) {
		/*
		 * actions for requests at end of list
		 */
		if( ( nl = cl->ll_link ) == NULL ) {
			/*
			 * request overlaps tail of last entry
			 * extend end point
			 */
			if( LB <= cl->ll_end && u.u_procp == cl->ll_proc ) {
				if( UB > cl->ll_end ) cl->ll_end = UB;
				return;
			}
			/*
			 * otherwise add new entry
			 */
			(void) lockadd(cl, LB, UB);
			return;
		}
		/*
		 * if more locks in range skip to next
		 * otherwise stop scan
		 */
		if( nl->ll_start < LB ) {
			cl = nl;
		}
		else {
			break;
		}
	}
	/*
	 * if upper bound is fully resolved were done
	 * otherwise fix end of last entry or add new entry
	 */
	if(UB <= cl->ll_end) return;
	if(LB <= cl->ll_end && u.u_procp == cl->ll_proc) cl->ll_end = UB;
	else {
		if( lockadd(cl, LB, UB) ) return;
		cl = cl->ll_link;
	}
	/*
	 * end point set above may overlap later entries
	 * if so delete or modify them to perform the compaction
	 */
	while( (nl=cl->ll_link) != NULL) {
		/*
		 * if we found lock by another process we must
		 * be done since we validated the range above
		 */
		if(u.u_procp != nl->ll_proc) return;
		/*
		 * if the new endpoint no longer overlaps were done
		 */
		if(cl->ll_end < nl->ll_start) return;
		/*
		 * if the new range overlaps the first part of the
		 * next lock, take its end point
		 * and delete the next lock
		 * we should be done
		 */
		if(cl->ll_end <= nl->ll_end) {
			cl->ll_end = nl->ll_end;
			cl->ll_link = nl->ll_link;
			lockfree(nl);
			return;
		}
		/*
		 * the next lock is fully included in the new range
		 * so it may be deleted
		 */
		cl->ll_link = nl->ll_link;
		lockfree(nl);
	}
}

/*
 * locked -- routine to scan locks and check for a locked condition
 */
locked(flag, vp, LB, UB)
register struct vnode *vp;
off_t LB, UB;
{
	register struct locklist *nl = vp->v_locklist;
	int error;

	/*
	 * scan list while locks are in requested range
	 */
	while(nl != NULL && nl->ll_start < UB) {
		/*
		 * skip locks for this process
		 * and those out of range
		 */
		if( nl->ll_proc == u.u_procp || nl->ll_end <= LB) {
			nl = nl->ll_link;
			if(nl == NULL) return(NULL);
			continue;
		}
		/*
		 * must have found lock by another process
		 * if request is to test only, then exit with
		 * error code
		 */
		if(flag>1)
			return(EACCES);
		/*
		 * will need to sleep on lock, check for deadlock first
		 * abort on error
		 */
		if(error = deadlock(nl)) return(error);
		/*
		 * post want flag to get awoken
		 * then sleep till lock is released
		 */
		nl->ll_flags |= LLWANT;
		(void) sleep( (caddr_t)nl, PSLEP);
		/*
		 * set scan back to begining to catch
		 * any new areas locked
		 * or a partial delete
		 */
		nl = vp->v_locklist;
		/*
		 * abort if any errors
		 */
		if(u.u_error) return(u.u_error);
	}
	return(0);
}

/*
 * deadlock -- routine to follow chain of locks and proc table entries
 *	to find deadlocks on file locks.
 */
deadlock(lp)
register struct locklist *lp;
{
	register struct locklist *nl;
	
	/*
	 * scan while the process owning the lock is sleeping
	 */
	while(lp->ll_proc->p_stat == SSLEEP) {
		/*
		 * if the object the process is sleeping on is
		 * NOT in the locktable every thing is ok
		 * fall out of loop and return NULL
		 */
		nl = (struct locklist *) lp->ll_proc->p_wchan;
		if( nl < &locklist[0] || nl >= &locklist[(short)v.v_flock] )
			break;
		/*
		 * the object was a locklist entry
		 * if the owner of that entry is this
		 * process then a deadlock would occur
		 * set error exit and return
		 */
		if(nl->ll_proc == u.u_procp)
			return(EDEADLOCK);
		/*
		 * the object was a locklist entry
		 * owned by some other process
		 * continue the scan with that process
		 */
		lp = nl;
	}
	return(0);
}

/*
 * unlock -- called by close to release all locks for this process
 */
unlock(vp)
struct vnode *vp;
{
	register struct locklist *nl;
	register struct locklist *cl;

	cl = (struct locklist *)&vp->v_locklist;
	while( (nl = cl->ll_link) != NULL) {
		if(nl->ll_proc == u.u_procp) {
			cl->ll_link = nl->ll_link;
			lockfree(nl);
		}
		else cl = nl;
	}
}

/*
 * lockalloc -- allocates free list, returns free lock items
 */
struct locklist *
lockalloc()
{
	register struct locklist *fl = &locklist[0];
	register struct locklist *nl;

	/*
	 * if first entry has never been used
	 * link the locklist table into the freelist
	 */
	if(fl->ll_proc == NULL) {
		fl->ll_proc = &proc[0];
		for(nl= &locklist[1]; nl < &locklist[(short)v.v_flock]; nl++) {
			lockfree(nl);
		}
	}
	/*
	 * if all the locks are used error exit
	 */
	if( (nl=fl->ll_link) == NULL) {
		u.u_error = EDEADLOCK;
		return(NULL);
	}
	/*
	 * return the next lock on the list
	 */
	fl->ll_link = nl->ll_link;
	nl->ll_link = NULL;
	return(nl);
}

/*
 * lockfree -- returns a lock item to the free list
 */
lockfree(lp)
register struct locklist *lp;
{
	register struct locklist *fl = &locklist[0];

	/*
	 * if some process is sleeping on this lock
	 * wake them up
	 */
	if(lp->ll_flags & LLWANT) {
		lp->ll_flags &= ~LLWANT;
		wakeup((caddr_t)lp);
	}
	/*
	 * add the lock into the free list
	 */
	lp->ll_link = fl->ll_link;
	fl->ll_link = lp;
}

/*
 * lockadd -- routine to add item to list
 */
lockadd(cl,LB,UB)
register struct locklist *cl;
off_t LB,UB;
{
	register struct locklist *nl;

	/*
	 * get a lock, return if none available
	 */
	nl = lockalloc();
	if(nl == NULL) {
		return(1);
	}
	/*
	 * link the new entry into list at current spot
	 * fill in the data from the args
	 */
	nl->ll_link = cl->ll_link;
	cl->ll_link = nl;
	nl->ll_proc = u.u_procp;
	nl->ll_start = LB;
	nl->ll_end = UB;
	return(0);
}
