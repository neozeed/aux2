#ifndef lint	/* .../sys/PAGING/io/fp.c */
#define _AC_NAME fp_c
#define _AC_NO_MAIN "@(#) Copyright (c) 1985-87 UniSoft Corporation, All Rights Reserved.  {Apple version 2.1 89/10/13 14:37:40}"
#include <apple_notice.h>

#ifdef _AC_HISTORY
  static char *sccsid = "@(#)Copyright Apple Computer 1989\tVersion 2.1 of fp.c on 89/10/13 14:37:40";
#endif		/* _AC_HISTORY */
#endif		/* lint */

#define _AC_MODS
/*	@(#)fp.c	UniPlus VVV.2.1.2	*/

/*
 * MC68881 Floating-Point Coprocessor
 *
 * (C) 1985 UniSoft Corp. of Berkeley CA
 *
 * UniPlus Source Code. This program is proprietary
 * with Unisoft Corporation and is not to be reproduced
 * or used in any manner except as authorized in
 * writing by Unisoft.
 */

#ifdef lint
#include <sys/sysinclude.h>
#include <sys/fpioctl.h>
#else lint
#include <sys/param.h>
#include <sys/uconfig.h>
#include <sys/types.h>
#include <sys/mmu.h>
#include <sys/page.h>
#include <sys/region.h>
#include <sys/sysmacros.h>
#include <sys/errno.h>
#include <sys/dir.h>
#include <sys/buf.h>
#include <sys/time.h>
#include <sys/proc.h>
#include <sys/conf.h>
#include <signal.h>
#include <sys/user.h>
#include <sys/systm.h>
#include <sys/ioctl.h>
#include <sys/uioctl.h>
#include <sys/fpioctl.h>
#endif lint

/*ARGSUSED*/
fpioctl(dev, cmd, addr, flag)
dev_t dev;
caddr_t addr;
{
	register struct user *up;

	up = &u;
	switch (cmd) {
		case FPIOCEXC:	/* return reason for last 881 SIGFPE */
			if (fubyte(addr) == -1) {
				up->u_error = EFAULT;
				return;
			}
			/* u_fpexc is saved in trap.c */
			if (subyte(addr, up->u_fpexc) == -1) {
				up->u_error = EFAULT;
				return;
			}
			break;
		default:
			up->u_error = EINVAL;
	}
}


/*
 *	Set EXC_PEND bit of the BIU flag word on to indicate that
 *	no exception is pending (vis. sec 5.2.2 of '881/'882 manual)
 */
fp_fixup()
{
	register char *istate;
	register int size;
	extern short fp881;

	if (fp881) {
		istate = &u.u_fpstate[0];
		size = istate[1];
		istate[size] |= 0x08;	/* this is the EXC_PEND bit */
	}
}


/*	fp_copyout -- copy floating point status.
 *	    The floating point state, if any, is saved to the user stack
 *	area.  The current format is:
 *	4 byte	checksum
 *	var sz	FPU state info
 *	24 byte	fp0 fp1
 *	12 byte	system and status registers
 *
 *	If information is saved, the u_traptype word is set to reflect what's
 *	saved.  u_traptype will later be used to indicate what needs to
 *	be restored.  By intent, only code in this file knows the precise
 *	contents of the save area.  Since none of the save area is publicly
 *	known, future enhancements need not be tied in any way to this
 *	precise format.
 */

fp_copyout(usp)

unsigned usp;
{
	struct stateinfo {
	u_char ver;
	u_char size;
	short	rsvd;
	} *sp;
	register struct user *up;
	register u_char *cp;
	register sum, size;

	up = &u;
	sp = (struct stateinfo *) up->u_fpstate;
	if((size = sp->size) == 0) {
		up->u_traptype &= ~TRAPSAVFP;
		return((int)usp);
	}
	size += sizeof(int);	/* include format word */
	grow(usp - size - sizeof(up->u_fpsysreg) - 2 * FPDSZ - sizeof(int));
	sum = 0;
	for(cp = &((u_char *)up->u_fpstate)[size-1]; 
				cp >= (u_char *)u.u_fpstate; --cp)
		sum += *cp;
	if(copyout(up->u_fpsysreg, usp -= sizeof(up->u_fpsysreg), sizeof(up->u_fpsysreg))
	  || copyout(up->u_fpdreg, usp -= 2 * FPDSZ, 2 * FPDSZ)
	  || copyout(up->u_fpstate, usp -= size, size)
	  || suword(usp -= sizeof(int), sum) < 0) {
		return(0);
	}
	up->u_traptype |= TRAPSAVFP;
	return((int)usp);
}

/*	fp_copyin -- copyin floating point state from user stack.
 *	     fp_copyout saved floating point information, and set
 *	the u_traptype word to reflect what had been saved.  Now,
 *	we copy the floating point state info back from user space.
 */

fp_copyin(usp)

register unsigned usp;
{
	int state;
	register struct user *up;
	register u_char *cp;
	register sum, size;
	int	checksum;

	up = &u;
	up->u_fpsaved = 1;
	*((int *)up->u_fpstate) = 0;
	/* There is a chance that a page in will occur in the copyins.
	 * In this case we may be rescheduled, and the undefined floating 
	 * point registers will be restored to the chip.  Nulling out the 
	 * state should handle this.
	 */
	checksum = fuword(usp);
	usp += sizeof(int);
	if((state = fuword(usp)) == -1)
		return(0);
	usp += sizeof(int);
	size = (state >> 16) & 0xFF;
	if(size > sizeof(u.u_fpstate) - sizeof(int))
		return(0);
	if(copyin(usp, (caddr_t)u.u_fpstate + sizeof(int), size))
		return(0);
	usp += size;
	size += sizeof(int);	/* 1st word of frame not included in count */
	if(copyin(usp, up->u_fpdreg, FPDSZ * 2)
	  || copyin(usp += FPDSZ * 2, up->u_fpsysreg, sizeof(up->u_fpsysreg)))
		return(0);
	usp += sizeof(up->u_fpsysreg);
	*((int *)up->u_fpstate) = state;
	sum = 0;
	for(cp = &((u_char *)up->u_fpstate)[size-1]; 
				cp >= (u_char *)u.u_fpstate; --cp)
		sum += *cp;
	if(checksum != sum) {
		return(0);
	}
	up->u_fpsaved = 1;
	up->u_traptype &= ~TRAPSAVFP;
	return(usp);
}


/*	fpnull -- zero floating point system.
 *	     All state variables, and all register values are made null.
 *	If a coprocessor format error has occured, this routine must recover
 *	from it.
 */

fpnull()

{
	*((int *)u.u_fpstate) = 0;
	u.u_fpsaved = 1;
}

