#ifndef lint	/* .../sys/COMMON/os/cmachdep.c */
#define _AC_NAME cmachdep_c
#define _AC_NO_MAIN "@(#) Copyright (c) 1985-87 UniSoft Corporation, All Rights Reserved.  {Apple version 2.2 90/03/20 19:01:02}"
#include <apple_notice.h>

#ifdef _AC_HISTORY
#endif		/* _AC_HISTORY */
#endif		/* lint */

/*	@(#)Copyright Apple Computer 1989	Version 2.2 of cmachdep.c on 90/03/20 19:01:02 */

/*	@(#)cmachdep.c UniPlus VVV.2.1.7	*/

#ifdef HOWFAR
extern int T_sendsig;
#endif HOWFAR

#include "sys/types.h"
#include "sys/param.h"
#include "sys/mmu.h"
#include "sys/sysmacros.h"
#include "sys/page.h"
#include "sys/systm.h"
#include "sys/dir.h"
#include "sys/errno.h"
#include "sys/signal.h"
#include "sys/time.h"
#include "sys/user.h"
#include "sys/reg.h"
#include "sys/acct.h"
#include "sys/map.h"
#include "sys/file.h"
#include "sys/region.h"
#include "sys/proc.h"
#include "sys/vnode.h"
#include "sys/debug.h"

#include "sys/var.h"
#include "sys/psl.h"
#include "sys/buserr.h"

#include "sys/pathname.h"
#include "sys/uio.h"
#include "sys/trace.h"
#include "sys/clock.h"
#include "compat.h"

static	badstack();

static struct sigframe {
	char	*sf_retaddr;		/* 0x0: return address from handler */
	int	sf_signum;		/* 0x4 */
	int	sf_code;		/* 0x8 */
	struct sigcontext *sf_scp;	/* 0xC */
	int	sf_kind;		/* 0x10: u_traptype */
	int	sf_regs[4];		/* 0x14: saved user registers */
	char	sf_retasm[8];		/* 0x24: return to kernel */
};

/*	This code is processor and assembler specific.	It is copied onto
 *	the user stack.	 After a user signal handler exits, this code is
 *	executed.
 */
#ifndef	lint
extern	char	siglude[];
	asm("siglude:");		
	asm("	mov.l	&150,%d0");	/* system call number for sysreturn */
	asm("	trap &15");		/* make a system call */
#else	lint
	char	siglude[1];
#endif	lint



int framelock = 0;
int framewant = 0;
unsigned char framedata[92];


/*
 * Send a signal to a process - simulate an interrupt.
 */
/*ARGSUSED*/
sendsig(hdlr, signo, arg)
int	(*hdlr) ();
register int signo;
{	register int	*regs;
	register user_t *up;

	up = &u;
	regs = up->u_ar0;

	if (signo == SIGSEGV || signo == SIGBUS) {
		up->u_traptype &= ~(TRAPBUS | TRAPADDR);
		if (signo == SIGBUS)	/* will never restart ADDRERR */
			up->u_traptype &= ~TRAPTMASK;
	}
	if ((up->u_procp->p_compatflags & COMPAT_BSDSIGNALS) == 0) {
		register int	*usp;

		/* We need to save more state.	Floating point and
		 * bus error info are now saved in the user space.
		 * The precise form is subject to change.  In general, there
		 * may be a varying number of bytes of state info between
		 * the user's old sp, and the signal interface frame known
		 * to application routines.  If traptype is nonzero, then
		 * the kernel "must" be called to clean this info.
		 */	
		if ((usp = (int *)copyframe(YES, regs, regs[SP])) == 0)
			return;
		grow(usp - 6);

		/* New rule for COFF binaries:
		 * If traptype is nonzero, the old user stack pointer is
		 * pushed below the state info.	 Custom user signal handlers 
		 * may take advantage of this to pop off the state info.
		 */
		if (up->u_traptype)
			suword(--usp, regs[SP]);
			
		/* The following code is the "traditional" Coff format */
		/* simulate an interrupt on the user's stack */
		suword(--usp, regs[PC]);
		suword(--usp, (regs[RPS] & 0xffff) | (up->u_traptype << 16));
		suword(--usp, arg);
		suword(--usp, signo);

		regs[SP] = (int)usp;
		regs[PC] = (int)hdlr;
	} else {
		register struct sigcontext *uscp;
		register struct sigframe   *ufp;
		struct sigcontext lscp;
		struct sigframe	  lfp;
		int    oonstack;

		oonstack = up->u_onstack;

		if (!up->u_onstack && (up->u_sigonstack & sigmask(signo))) {
			uscp = (struct sigcontext *)copyframe(YES, regs,
								  up->u_sigsp);
			up->u_onstack = 1;
		} else
			uscp = (struct sigcontext *)copyframe(YES, regs,
								  regs[SP]);
		if (uscp-- == 0)
			return;
		ufp = (struct sigframe *)uscp - 1;
		/*
		 * Must build signal handler context on stack to be returned to
		 * so that rei instruction in sigcode will pop ps and pc
		 * off correct stack.  The remainder of the signal state
		 * used in calling the handler must be placed on the stack
		 * on which the handler is to operate so that the calls
		 * in sigcode will save the registers and such correctly.
		 */
		if (!up->u_onstack)
			grow((unsigned)ufp);
		lfp.sf_signum = signo;
		if (signo == SIGILL || signo == SIGFPE) {
			/*
			 * THIS IS AN ATTEMPT TO PASS THE FPE CODE TO THE USER
			 * NO ONE EVER SETS IT UP
			 * probably psig should put its arg in u_code
			 * for 4.3 signals
			 * and someone later should restore it?
			 */
			lfp.sf_code = up->u_code;
			up->u_code = 0;
		} else
			lfp.sf_code = 0;
		lfp.sf_scp = uscp;
		lfp.sf_regs[0] = regs[R0];
		lfp.sf_regs[1] = regs[R1];
		lfp.sf_regs[2] = regs[AR0];
		lfp.sf_regs[3] = regs[AR1];
		lfp.sf_retaddr = ufp->sf_retasm;
		bcopy(siglude, lfp.sf_retasm, sizeof(lfp.sf_retasm));
		lfp.sf_kind = up->u_traptype;

		/* sigcontext goes on previous stack */
		lscp.sc_onstack = oonstack;
		lscp.sc_mask = arg;
		/* setup rei */
		lscp.sc_sp = regs[SP];
		lscp.sc_pc = regs[PC];
		lscp.sc_ps = regs[RPS] | up->u_sr;
		regs[SP] = (int)ufp;
		regs[RPS] &= ~PS_T;
		regs[PC] = (int)hdlr;

		if (copyout(&lfp, ufp, sizeof(lfp))) {
			badstack();
			return;
		}
		if (copyout(&lscp, uscp, sizeof(lscp))) {
			badstack();
			return;
		}
	}
	up->u_traptype = 0;
}


/*	copyframe -- copy CPU state to user area.
 *	    Relying on the u_traptype word, this routine will copyin
 *	or copyout the CPU specific bus error information to the user stack.
 *	The format is:
 *		checksum prior to encryption
 *		stack info x'ord with address of proc struct
 *		floating point info, if any
 *	returns the new value for the user stack pointer.
 */

copyframe(out, regs, usp)
int	out;		      /* copyout if true, else copyin */
register int	 *regs;	      /* kernel stack regs structure */
register caddr_t  usp;	      /* user's stack pointer */
{	register u_char *data;
	register int	n;
	register int	i;
	int	checksum;

	/* Determine number of bytes of status to copy to/from user space */
	TRACE(T_sendsig, ("copyframe %s kern = 0x%x usp = 0x%x\n", 
		out ? "out" : "in", regs, usp));

	if ((u.u_traptype & TRAPSAVFP) && !out) {
		if ((usp = (caddr_t)fp_copyin((unsigned)usp)) == 0) {
			badstack();
			return(0);
		}
	}

	switch(u.u_traptype & TRAPTMASK) {
	case TRAPLONG:
		ASSERT(out ? ((struct buserr *)regs)->ber_format == FMT_LONG : 1);
		ASSERT(out ? (((struct buserr *)regs)->ber_sstat & 0x4) == 0 : 1);
		i = 92 - 2;
		break;
	case TRAPSHORT:
		ASSERT(out ? ((struct buserr *)regs)->ber_format == FMT_SHORT : 1);
		ASSERT(out ? (((struct buserr *)regs)->ber_sstat & 0x4) == 0  : 1);
		i = 32 - 2;
		break;
	default:
		i = 0;
		break;
	}
	if (i) {
		if (!out) {
			register short *sp;
			register short x;

			while (framelock) {
				framewant = 1;
				sleep(&framelock, PZERO);
			}
			framelock = 1;
			framewant = 0;
			data = framedata;

			checksum = fuword(usp);
			TRACE(T_sendsig, ("copyin frame from 0x%x to 0x%x\n",
				usp+4, data));
			if (copyin(usp+4, data, i))
				goto frame_error;

			x = (int)u.u_procp;
			for (sp = (short *)(&data[i]); sp > (short *)data; )
				*--sp ^= x;

		} else
			data = (u_char *)&regs[PC];
	
		/* caluclate checksum */
		{
			register u_char *cp;
			
			n = 0;

			for (cp = &data[i]; cp > data; )
				n += *--cp;
		}
		if (out) {
			register short *sp;
			register short x;

			x = (short)u.u_procp;
			for (sp = (short *)(&data[i]); sp > (short *)data; )
				*--sp ^= x;

			(void)grow((unsigned)usp - i - 4);
			if (copyout(data, usp - i, i) || suword(usp - i - 4, n) < 0) {
				sp = (short *)data;
				*sp++ ^= x;
				*sp ^= x;
				badstack();
				return(0);
			}
			sp = (short *)data;
			*sp++ ^= x;
			*sp++ ^= x;	 /* restore the pc */
			*sp ^= x;	 /* plus the vector */
			usp -= i + 4;
		} else {
			register u_short *sp;

			if ((u_short)n != checksum)
				goto frame_error;
			sp = (u_short *)data;

			switch(u.u_traptype & TRAPTMASK) {
			case TRAPLONG:
			case TRAPSHORT:
				if (sp[4] & 0x4)	       /* make sure super space */
					goto frame_error;      /* isnt set in the ssw */

				switch(sp[2] >> 12) {
				case FMT_LONG:
				case FMT_SHORT:
					break;
				default:
					goto frame_error;
				}
				break;
			default:
				goto frame_error;
			}
			usp += i + 4;
			u.u_traptype |= TRAPREST;
		}
	}
	if (out) {
		fpsave();

		if ((usp = (caddr_t)fp_copyout((unsigned)usp)) == 0) {
			badstack();
			return(0);
		}
	}
	return((int)usp);

frame_error:
	framelock = 0;
	if (framewant)
		wakeup(&framelock);
	badstack();
	return(0);
}


/*	badstack -- kill off a process with a bad stack.
 *	Process has trashed its stack; give it an illegal
 *	instruction to halt it in its tracks.
 */

static
badstack()
{	register int	signo;
	register struct user *up;
	register struct proc *p;

	TRACE(T_sendsig, ("badstack called from 0x%x\n", caller()));
	up = &u;
	p = up->u_procp;
	fpnull();
	up->u_traptype = 0;
	up->u_signal[SIGILL-1] = SIG_DFL;
	signo = sigmask(SIGILL);
	p->p_sigignore &= ~signo;
	p->p_sigcatch &= ~signo;
	p->p_sigmask &= ~signo;
	psignal(p, SIGILL);
	if (issig())
		psig(0);	    /* force signal to be handled right now */
}

/*
 * Routine to cleanup state after a signal
 * has been taken.  Reset signal mask and
 * stack state from context left by sendsig (above).
 * Pop these values in preparation for rei which
 * follows return from this routine.
 */
sigcleanup()
{	register struct user *up;
	register int	*regs;
	struct sigframe lfp;
	struct sigcontext lscp;

	up = &u;
	regs = up->u_ar0;

	if ((up->u_procp->p_compatflags & COMPAT_BSDSIGNALS) == 0)
		return(EINVAL);
	if (copyin(regs[SP]-sizeof(lfp.sf_retaddr), &lfp, 
				sizeof(lfp)-sizeof(lfp.sf_retasm))) {
		badstack();
		return(EFAULT);
	}
	if (copyin(lfp.sf_scp, &lscp, sizeof(lscp))) {
		badstack();
		return(EFAULT);
	}
	up->u_onstack = lscp.sc_onstack & 01;
	up->u_procp->p_sigmask =
	  lscp.sc_mask &~ (sigmask(SIGKILL)|sigmask(SIGSTOP));

	regs[R0]  = lfp.sf_regs[0];
	regs[R1]  = lfp.sf_regs[1];
	regs[AR0] = lfp.sf_regs[2];
	regs[AR1] = lfp.sf_regs[3];
	regs[SP]  = lscp.sc_sp;
	regs[PC]  = lscp.sc_pc;
	regs[RPS] = (lscp.sc_ps & 0xFF) | (regs[RPS] & 0xFF00);

	up->u_traptype = lfp.sf_kind;
	(void)copyframe(NO, regs, (caddr_t) (lfp.sf_scp+1));

	if (up->u_traptype & TRAPREST) {
		if (lscp.sc_pc != *(int *)framedata) {
			up->u_traptype &= ~TRAPREST;	
			framelock = 0;
			if (framewant)
				wakeup(&framelock);
		}
	}
	if (up->u_traptype & TRAPADDR) /* addr error */
		psignal(up->u_procp, SIGBUS);
	else if (up->u_traptype & TRAPBUS) /* bus error */
		psignal(up->u_procp, SIGSEGV);
	return(0);
}


/*
 * ovbcopy(f, t, l)
 *	copy from one buffer to another that are possibly overlapped
 *	this is like bcopy except that f and t may be overlapped in
 *	any manner, so you may need to copy from the rear rather than
 *	from the front
 * parameters
 *	char *f;	from address
 *	char *t;	to address
 *	int l;		length
 */
ovbcopy(f, t, l)
register char *f, *t;
register int l;
{
	if (f != t)
		if (f < t) {
			f += l;
			t += l;
			while (l-- > 0)
				*--t = *--f;
		} else {
			while (l-- > 0)
				*t++ = *f++;
		}
}
