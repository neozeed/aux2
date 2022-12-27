#ifndef lint	/* .../sys/PAGING/os/text.c */
#define _AC_NAME text_c
#define _AC_NO_MAIN "@(#) Copyright (c) 1984-85 AT&T-IS, All Rights Reserved.  {Apple version 2.3 90/04/23 10:50:55}"
#include <apple_notice.h>

#ifdef _AC_HISTORY
  static char *sccsid = "@(#)Copyright Apple Computer 1987\tVersion 2.3 of text.c on 90/04/23 10:50:55";
#endif		/* _AC_HISTORY */
#endif		/* lint */

#define _AC_MODS
/*	@(#)text.c	UniPlus VVV.2.1.4	*/

#ifdef lint
#include "sys/sysinclude.h"
#else lint
#include "sys/types.h"
#include "sys/mmu.h"
#include "sys/param.h"
#include "sys/sysmacros.h"
#include "sys/page.h"
#include "sys/systm.h"
#include "sys/time.h"
#include "sys/signal.h"
#include "sys/user.h"
#include "sys/errno.h"
#include "sys/vfs.h"
#include "sys/vnode.h"
#include "svfs/inode.h"
#include "sys/buf.h"
#include "sys/var.h"
#include "sys/sysinfo.h"
#include "sys/pfdat.h"
#include "sys/region.h"
#include "sys/proc.h"
#include "sys/uio.h"
#include "sys/debug.h"
#endif lint


/*	Allocate text region for a process
 */
xalloc(vp,ltxtspec)
register struct vnode *vp;
struct ltxtspcs *ltxtspec;
{
	register struct user *up;
	register reg_t		*rp;
	register preg_t		*prp;
	register int		size;
	register caddr_t	org;
	struct vattr vattr;
	int error;
	register int tstart;
	register int type;
	register int magic;

	up = &u;
/* Check if this is shared library. */
	if (ltxtspec)
	{
	    size = ltxtspec->tsize;
	    org = (caddr_t)ltxtspec->text_start;
	    type = PT_LIBTXT;
	    tstart = ltxtspec->tstart;
	    magic = ltxtspec->magic;
	}    
	else
	{
	    size = up->u_exdata.ux_tsize;
	    org = (caddr_t)up->u_exdata.ux_txtorg;
	    type = PT_TEXT;
	    tstart = up->u_exdata.ux_tstart;
	    magic = up->u_exdata.ux_mag;
	}

	if (size == 0)
	    return(0);

	/*	Search the region table for the text we are
	 *	looking for.
	 */

	VOP_GETATTR(vp, &vattr, u.u_cred);

loop:
	rlstlock();

	for(rp = ractive.r_forw ; rp != &ractive ; rp = rp->r_forw){
		if(rp->r_type == RT_STEXT  &&
		   rp->r_vptr == vp  &&
		   (rp->r_flags & RG_NOSHARE) == 0){
			rlstunlock();
			reglock(rp);
			if(rp->r_type != RT_STEXT || rp->r_vptr != vp){
				regrele(rp);
				goto loop;
			}
			/*	Artificially bump the reference count
			 *	to make sure the region doesn't go away
			 *	while we are waiting
			 */
			
			if((rp->r_flags & RG_DONE) == 0) {
				rp->r_refcnt++;
				regrele(rp);
				while ( (rp->r_flags & RG_DONE) == 0) {
					rp->r_flags |= RG_WAITING;
					sleep((caddr_t)&rp->r_flags, PZERO);
				}
				reglock(rp);
				rp->r_refcnt--;
			}
			prp = attachreg(rp, u.u_procp, (caddr_t)(((long)org)&~L2OFFMASK), type, Lx_RO);
			regrele(rp);
			if(prp == NULL)
				return(u.u_error);

			return(0);
		}
	}
	rlstunlock();
	
	/*	Text not currently being executed.  Must allocate
	 *	a new region for it.
	 */

	if((rp = allocreg(vp, RT_STEXT)) == NULL)
		return(u.u_error);

	if(vattr.va_mode & VSVTX)
		rp->r_flags |= RG_NOFREE;
	
	/*	Attach the region to our process.
	 */
	
	if ((prp = attachreg(rp, u.u_procp, (caddr_t)(((long)org)&~L2OFFMASK), type, Lx_RW)) == NULL) {
		freereg(rp);
		return(u.u_error);
	}
	
	/*	Load the region or map it for demand load.
	 */

	if (magic == 0413) {
		ASSERT(poff(org) == 0);
		if(mapreg(prp, org, vp, tstart, size) < 0){
			detachreg(prp, u.u_procp);
			return(u.u_error);
		}
	} else if (magic == 0410)
		{
		if(loadreg(prp, org, vp, tstart, size) < 0){
			detachreg(prp, u.u_procp);
			return(u.u_error);
		}
	} else
		panic("xalloc - bad magic");

	chgprot(prp, Lx_RO);
	regrele(rp);
	return(0);
}


/*	Free the swap image of all unused shared text regions
 *	which are from device dev (used by umount system call).
 */
xumount(vfsp)
register struct vfs *vfsp;
{
	register reg_t		*rp;
	register reg_t		*nrp;
	register struct vnode	*vp;
loop:
	rlstlock();

	for(rp = ractive.r_forw ; rp != &ractive ; rp = nrp){
		if(rp->r_type != RT_STEXT)
			nrp = rp->r_forw;
		else {
			rlstunlock();
			reglock(rp);
			if(rp->r_type != RT_STEXT){
				regrele(rp);
				goto loop;
			}
			rlstlock();
			nrp = rp->r_forw;
		if (((vfsp == (struct vfs *) NODEV) ||
			((rp->r_vptr != NULL) && (rp->r_vptr->v_vfsp == vfsp)))
			    && rp->r_refcnt == 0) {
				rlstunlock();
				freereg(rp);
				goto loop;
			} else {
				regrele(rp);
			}
		}
	}
	rlstunlock();
}

/*	Remove a shared text region associated with vnode vp from
 *	the region table, if possible.
 */
xrele(vp)
register struct vnode *vp;
{
	register reg_t	*rp;
	register reg_t	*nrp;

	if((vp->v_flag&VTEXT) == 0)
		return;
	
loop:
	rlstlock();

	for(rp = ractive.r_forw ; rp != &ractive ; rp = nrp){
		if(rp->r_type != RT_STEXT || vp != rp->r_vptr)
			nrp = rp->r_forw;
		else {
			rlstunlock();
			reglock(rp);
			if(rp->r_type != RT_STEXT || vp != rp->r_vptr){
				regrele(rp);
				goto loop;
			}
			rlstlock();
			nrp = rp->r_forw;
			if(rp->r_refcnt == 0) {
				rlstunlock();
				freereg(rp);
				goto loop;
			} else {
				regrele(rp);
			}
		}
	}
	rlstunlock();
}


/*	Try to removed unused sticky regions in order to free up swap
 *	space.
 */

swapclup()
{
	register reg_t		*rp;
	register reg_t		*nrp;
	register int		rval;
	register struct vnode	*vp;

	rval = 0;

loop:
	rlstlock();

	for(rp = ractive.r_forw ; rp != &ractive ; rp = nrp){
		nrp = rp->r_forw;
		if (rp->r_lock) {
			continue;
		}
		rp->r_lock = (int)u.u_procp;
		if(rp->r_type == RT_UNUSED){
			regrele(rp);
			continue;
		}
		if(rp->r_refcnt == 0) {
			rlstunlock();
			freereg(rp);
			rval = 1;
			goto loop;
		} else {
			regrele(rp);
		}
	}
	rlstunlock();
	return(rval);
}

/* <@(#)text.c	6.3> */
