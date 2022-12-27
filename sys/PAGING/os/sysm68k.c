#ifndef lint	/* .../sys/PAGING/os/sysm68k.c */
#define _AC_NAME sysm68k_c
#define _AC_NO_MAIN "@(#) Copyright (c) 1985-87 Motorola Inc., 1985-87 UniSoft Corporation, All Rights Reserved.	 {Apple version 2.1 89/10/13 11:42:08}"
#include <apple_notice.h>

#ifdef _AC_HISTORY
  static char *sccsid = "@(#)Copyright Apple Computer 1987\tVersion 2.1 of sysm68k.c on 89/10/13 11:42:08";
#endif		/* _AC_HISTORY */
#endif		/* lint */

#define _AC_MODS
/*	@(#)sysm68k.c	UniPlus VVV.2.1.7	*/

#include "compat.h"
#include "sys/param.h"
#include "sys/types.h"
#include "sys/sysmacros.h"
#include "sys/mmu.h"
#include "sys/page.h"
#include "sys/systm.h"
#include "sys/time.h"
#include "sys/signal.h"
#include "sys/user.h"
#include "sys/errno.h"
#include "sys/region.h"
#include "sys/proc.h"
#include "sys/reg.h"
#include "sys/var.h"
#include "sys/buserr.h"
#include "sys/swap.h"
#include "sys/sysinfo.h"
#include "sys/debug.h"

/*
 * sysm68k - system call
 * 1) Called to grow the user stack on the MC68000 OBSOLETE
 * 2) Called to perform instruction continuation after signal handlers
 * 3-n) implement 3b system calls
 */
sysm68k ()
{
	register  struct  a {
		int	cmd;
		int	arg1, arg2, arg3, arg4;
	} *uap = (struct a *) u.u_ap;

	register struct user *up;
	register i, kind, *regs, *usp;

	up = &u;
	switch (((struct a *)up->u_ap)->cmd) {

	case 2:
		/* continue from signal handler */

	    if ( (up->u_procp->p_compatflags & COMPAT_BSDSIGNALS) == 0) {
		int	userpc;
		int	newsp;

		regs = up->u_ar0;
		usp = (int *) regs[SP];

		/* find the return address for the 'rtr' */
		/* in the user's stack area */
		/* stack format: */
		/*   0 - _sysm68k return addr */
		/*   1 - command */
		/*   2 - old %d0 */
		/*   3 - <kind of resume, previous trap process status reg> */
		/*   4 - previous trap PC */
		/*   5 - old user sp restored to process */
		/*   6 - n optional bus error stack frame info */

		/* get resume kind */
		kind = (fuword((caddr_t) (usp+3)) >> 16) & 0xffff;
		up->u_traptype = kind; /* make it appear that we just got here */
				     /* for the first time for psignal */

		userpc = regs[PC] = fuword((caddr_t) (usp+4)); 
		/* make sure we don't allow transfer to supervisor mode */
		regs[RPS] = (regs[RPS] & 0xff00) |
			   (fuword((caddr_t) (usp+3)) & 0xff);
		up->u_rval1 = fuword((caddr_t) (usp+2));	/* user's D0 */
		if((up->u_traptype = kind) == 0)
			usp += 5;
		else {
			newsp = fuword((caddr_t)(usp+5));
			usp += 6;
		}
		if((regs[SP] = copyframe(NO, regs, usp)) == 0) {
			regs[SP] = (int)usp;
			return;
		}
		if(kind)
			regs[SP] = newsp;
		if(userpc != regs[PC]) {
			((struct buserr *)regs)->ber_format = 0;
			regs[PC] = userpc;
		}
		if (kind & TRAPADDR) /* addr error */
			psignal(up->u_procp, SIGBUS);
		else if (kind & TRAPBUS) /* bus error */
			psignal(up->u_procp, SIGSEGV);
	    } else {
		if (sigcleanup())
			up->u_error = EFAULT;
	    }

		/* else we can just resume here */
		break;

	case 3:	/* Swap function. */
	{
		u.u_error = swapfunc((swpi_t *)uap->arg1);
		break;
	}

	case 4:	/* namelist interface */
	{
		extern	char	putbuf[];
		extern	int	putindx;

		switch(uap->arg1) {
		case 1:
			up->u_rval1 = (int)proc;
			break;
		case 2:
			up->u_rval1 = (int)putbuf;
			break;
		case 3:
			up->u_rval1 = putindx;
			break;
		case 4:
			up->u_rval1 = (int) &v;
			break;
		case 5:
			up->u_rval1 = swaplow;
			up->u_rval2 = swapdev;
			break;
		case 6:
			up->u_rval1 = (int) &sysinfo;
			break;

		default:
			up->u_error = EINVAL;
			break;
		}
		break;
	}

	case 9: /* Change Field Test Set Utility ID */

		if(uap->arg1 < 0 || uap->arg1 > (int)0xFFFF)
			up->u_error = EINVAL;
		else
			up->u_utilid = 0xFF0000 | uap->arg1;
		break;

	case 99: {

			for(i = 0; i < uap->arg1; i++)
				clratb(USRATB);
			break;
	}
	case 100: {

			for(i = 0; i < uap->arg1; i++)
				invsatb(USRATB, 0, 1);
			break;
	}
	case 101: {

			for(i = 0; i < uap->arg1; i++)
				;
			break;
	}
	case 102: {
			int nullsys();

			for(i = 0; i < uap->arg1; i++)
				nullsys();
			break;
	}
	case 103: {
			int nullsys();

			for(i = 0; i < uap->arg1; i++)
				nullsys();
			break;
	}

	case 1:		/* grow/shrink stack - Not Supported */
	default:
		up->u_error = EINVAL;
	}
}
