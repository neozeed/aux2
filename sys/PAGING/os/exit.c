#ifndef lint	/* .../sys/PAGING/os/exit.c */
#define _AC_NAME exit_c
#define _AC_NO_MAIN "@(#) Copyright (c) 1984-85 AT&T-IS, All Rights Reserved.  {Apple version 2.3 90/03/27 14:20:00}"
#include <apple_notice.h>

#ifdef _AC_HISTORY
  static char *sccsid = "@(#)Copyright Apple Computer 1989\tVersion 2.3 of exit.c on 90/03/27 14:20:00";
#endif		/* _AC_HISTORY */
#endif		/* lint */

#define _AC_MODS
/*	@(#)exit.c	UniPlus VVV.2.1.15	*/

#ifdef HOWFAR
extern int T_exit;
#endif HOWFAR

#include "compat.h"
#include "sys/types.h"
#include "sys/mmu.h"
#include "sys/param.h"
#include "sys/sysmacros.h"
#include "sys/dir.h"
#include "sys/signal.h"
#include "sys/time.h"
#include "sys/user.h"
#include "sys/reg.h"
#include "sys/errno.h"
#include "sys/var.h"
#include "sys/page.h"
#include "sys/region.h"
#include "sys/proc.h"
#include "sys/systm.h"
#include "sys/vnode.h"
#include "sys/pfdat.h"
#include "sys/wait.h"
#include "sys/debug.h"

extern (*lockrel)();
 

/*
 * Release resources.
 * Enter zombie state.
 * Wake up parent and init processes,
 * and dispose of children.
 */

exit(rv)
{
	register struct user *up;
	register int i;
	register struct proc *p, *q;
	register int (**fptr)();
	register preg_t	*prp;
	register reg_t	*rp;
	int flag;
	extern	int	(*exitfunc[])();
	extern int kmemory;
	extern int realitexpire();

	up = &u;
	p = up->u_procp;
	p->p_flag &= ~(STRC);
	p->p_clktim = 0;
	for (i=0; i<NSIG; i++)
		up->u_signal[i] = SIG_IGN;
	p->p_sigignore = ~0;
	untimeout(realitexpire, (caddr_t)p);
	if ((p->p_pid == p->p_pgrp)
	 && (p->p_ttyp != NULL)) {
		/* process group leader */
		/* if spawned by a 4.2 process group controller
		   but not a process group controller, don't hangup */
		if (((p->p_flag & SPGRP42) == 0 && (*p->p_ttyp == p->p_pgrp))
			|| p->p_flag & SPGRPL) {
			*p->p_ttyp = 0;
			signal(p->p_pgrp, SIGHUP);
		}
	}
	for (i=0; i<NOFILE; i++) {
		if (up->u_ofile[i] != NULL)
			(void) (*lockrel)(up->u_ofile[i]);
			closef(up->u_ofile[i]);
	}
	VN_RELE(up->u_cdir);
	if (up->u_rdir) {
		VN_RELE(up->u_rdir);
	}

	for (fptr = exitfunc; *fptr; fptr++)
		(**fptr)();

	acct(rv);

	crfree(up->u_cred);

	/* 
	 * Free data, stack, text, and other regions.
	 * Free U block in swtch.
	 * If a 24-bit process, treat its 24-bit table separately (to
	 *  avoid mainline cruft).
	 */
	prp = p->p_region;
	if (p->p_flag&SMAC24)
		shred_24bit(p, 0);

	while(rp = prp->p_reg){
		reglock(rp);
		detachreg(prp, p);
	}

	/* postponement of assignment of zombie to stat field makes it
	 * unneccessary to use a lock for exiting proc--whole purpose
	 * used to be to prevent a parent in wait from find p here zombie,
	 * and trying to freeproc it before ready. This no longer happens.
	 */

	((struct proc *)p)->p_xstat = rv;
	((struct proc *)p)->p_utime = up->u_cutime + up->u_utime;
	((struct proc *)p)->p_stime = up->u_cstime + up->u_stime;
	flag = 0;
	if ((q = p->p_child) != NULL)
	{
		for (q = p->p_child;   ;   q = q->p_sibling) {
			/* loop termination condition tested in last if */
			/* q is child of exiting proc (p) */
			q->p_ppid = 1;
			q->p_parent = &proc[1];
			if (q->p_stat == SZOMB)
				/* if q itself is szombie,
				 * no need for lock: if q goes szombie
				 * before its parent ID is set to 1, it sends
				 * signal to p, which is now sending signal
				 * to proc 1 anyway. If q goes szombie after
				 * this check is made, then p does not send
				 * signal to proc 1 but q does. If q goes
				 * szombie after p resets its parent to proc 1
				 * but before, p checks for szombie, both
				 * p and q send signals to proc 1, but issig
				 * code doesn't care.
				*/
				flag = 1;
			else
			{
				if(q->p_flag & STRC) {
					q->p_flag &= ~STRC;
					psignal(q, SIGKILL);
#ifdef POSIX
				} else if(p->p_compatflags & COMPAT_POSIXFUS) {
					roustorphans(p, q);
#endif /* POSIX */
				} else if(q->p_stat == SSTOP) {
					psignal(q, SIGHUP);
					psignal(q, SIGCONT);
				}
			}

			if (q->p_sibling == NULL)
			{
				/* attach to proc 1 chain */
				/* lock in case proc 1 is forking now */
				SPL6();
				q->p_sibling = proc[1].p_child;
				proc[1].p_child = p->p_child;
				SPL0();
				p->p_child = NULL;
				break;
			}
		}	/* end for loop */

		/* only send 1 death of child sig to proc 1--also delay
		 * sending sig until reasonably sure not to sleep on
		 * proc 1 parent-child-sib lock
		 */
		if (flag)
			psignal(&proc[1], SIGCLD);
	}	/* end if children */

	/* now take care of all descendants in process group */
	if (p->p_flag & SPGRPL)
	{
		/* only need to worry if exiting proc is a proc
		 * group leader
		 * We do not use parent-child-sibling chain here
		 * since such use would require elaborate locking
		 * of those chains all down the line, and this
		 * case happens infrequently to warrant that
		 * overhead everywhere.
		 */
		for (q = &proc[1];   q < (struct proc *) v.ve_proc; q++)
		{
			if (p->p_pid == q->p_pgrp)
				q->p_pgrp = 0;
		}
	}
#ifndef POSIX
	/*
	 * POSIX-defined waitpid() allows user to wait on processes in
	 * specified pgrp. This usage will never succeed if pgrp is
	 * cleared here.
	 */
	p->p_pgrp = 0;
#endif /* !POSIX */
	p->p_stat = SZOMB;

	/* if parent freeprocs p before following psignal, no problem:
	 * some poor process will get a death of child signal. If it has
	 * dead children, fine. If not, signal has no effect.
	 */
	psignal(p->p_parent, SIGCLD);

	resched();
	/* no deposit, no return */
}

wait()
{
	register struct user *up = &u;

	if ((up->u_ar0[RPS] & RPS_ALLCC) != RPS_ALLCC)
#ifdef POSIX
		up->u_error = wait1(0, (pid_t) -1, 1);
#else
		up->u_error = wait1(0);
#endif /* POSIX */
	else
#ifdef POSIX
		up->u_error = wait1(up->u_ar0[AR0], (pid_t) -1, 1);
#else
		up->u_error = wait1(up->u_ar0[AR0]);
#endif /* POSIX */
}

#ifdef POSIX
waitpid()
{
	register struct a {
		int	pid;
		caddr_t	stat_loc;
		int	options;
	} *uap = (struct a *) u.u_ap;

	if (((uap->pid >= 0) ? uap->pid : -(uap->pid)) >= MAXPID)
		/*
		 * This error should be EINVAL, but POSIX hasn't defined
		 * this behavior yet.
		 */
		u.u_error = ECHILD;
	else
		u.u_error = wait1(uap->options, (pid_t) uap->pid, 0);
}
#endif /* POSIX */

/*
 * Wait system call.
 * Search for a terminated (zombie) child,
 * finally lay it to rest, and collect its status.
 * Look also for stopped (traced) children,
 * and pass back status from them.
 */
#ifdef POSIX
wait1(options, pid, restart)
pid_t pid;
#else /* !POSIX */
wait1(options)
#endif /* POSIX */
{
	register struct user *up;
	register f;
	register struct proc *p;
	register struct proc *parent;

	up = &u;
	parent = up->u_procp;

#ifdef POSIX
	if (options & ~(WNOHANG | WUNTRACED))
		return(EINVAL);
#endif /* POSIX */

loop:
	f = 0;
	for (p = parent->p_child;   p != NULL;	 p = p->p_sibling)
	{
#ifdef POSIX
		if ((pid == 0) && (p->p_pgrp != parent->p_pgrp))
			continue;
		else if ((pid > 0) && (p->p_pid != pid))
			continue;
		else if ((pid < -1) && (p->p_pgrp != -pid))
			continue;
#endif /* POSIX */
		f++;
		if (p->p_stat == SZOMB)
			return (freeproc(p, 1));
		if (p->p_stat == SSTOP && (p->p_flag&SWTED)==0 &&
		    (p->p_flag&STRC || options&WUNTRACED)) {
			if ((p->p_flag&SWTED) == 0) {
				p->p_flag |= SWTED;
				up->u_rval1 = p->p_pid;
				up->u_rval2 = (p->p_cursig<<8) | WSTOPPED;
				p->p_cursig = 0;
				return (0);
			}
			continue;
		}
	}
	if (f) {
		/* being here, means that the current proc has kids but
		 * all are currently non-dead and non-stopped.
		 * The following sequence causes the current proc to sleep
		 * until a SIGCLD signal comes in:
		 * this will sleep
		 * unless a signal is received before it gets to sleep
		 * in which case return is immediate and do wait code again
		 * If asleep, the psignal sent by the child results in a
		 * setrun call, putting the waiting proc back on run q.
		 * It will resume over here.
		 * Ignore error status.
		 */
		if (options&WNOHANG) {
			up->u_rval1 = 0;
			return (0);
		}
		if ((parent->p_compatflags & COMPAT_SYSCALLS) && save(up->u_qsav)) {
#ifdef SIG43
#ifdef POSIX
			if ((up->u_sigintr & sigmask(parent->p_cursig))
				|| !restart)
#else /* !POSIX */
			if (up->u_sigintr & sigmask(parent->p_cursig))
#endif /* POSIX */
			    return (EINTR);
#endif SIG43
			up->u_eosys = RESTARTSYS;
			return (0);
		}
		(void) sleep((caddr_t)parent, PWAIT);
		goto loop;
	}
	else
		return (ECHILD);
}

/*
 *	Remove zombie children from the process table.
 */
freeproc(p, flag)
	register struct proc *p;
{
	int error = 0;

	/* freeproc is called from issig and wait, both times by the parent
	 * of a child proc now gone szombie. Since only this one proc can do
	 * freeproc for a child, no need to protect freeproc with a lock
	 * from itself.
	*/

	register struct user *up;
	register struct proc *q, *lq, *pq;

	up = &u;
	if (flag) {
		register  n;

		n = up->u_procp->p_cpu + p->p_cpu;
		if(n > 80)
			n = 80;
		up->u_procp->p_cpu = n;
		up->u_rval1 = p->p_pid;
		up->u_rval2 = p->p_xstat;
	}
	up->u_cutime += p->p_utime;
	up->u_cstime += p->p_stime;

	/* disconnect child proc from the chain */
	pq = p->p_parent;
	lq = NULL;
	for (q = pq->p_child;	((q != p) && (q != NULL));   q = q->p_sibling)
		lq = q;
	if (q == NULL)
		panic("freeproc - cannot find child on chain");

	if (lq == NULL)		/* first child on list */
		pq->p_child = q->p_sibling;
	else
		lq->p_sibling = q->p_sibling;

	/* parent not nulled, to prevent race at end of exit
	 * Another solution: psignal could check proc arg for null
	 */
	p->p_child = p->p_sibling = NULL;
	p->p_stat = NULL;
	p->p_pid = 0;
	p->p_ppid = 0;
	p->p_sig = 0L;
	p->p_sigcatch = 0;
	p->p_sigignore = 0;
	p->p_sigmask = 0;
	p->p_pgrp = 0;
	p->p_flag = 0;
	p->p_wchan = 0;
	p->p_cursig = 0;
	return (error);
}

pexit(p)
register struct proc *p;
{
	/* clean up common process stuff -- called from 
	 * newproc on error in fork due to no swap space
	 */

	register struct user *up;
	register int i;
	register int (**fptr)();
	extern	int	(*exitfunc[])();

	up = &u;
	/* do not include punlock here since not needed for
	 * newproc clean (?)*/

	for (i = 0;   i < NOFILE;   i++)
		if (up->u_ofile[i] != NULL)
			closef(up->u_ofile[i]);
	VN_RELE(up->u_cdir);
	if (up->u_rdir) {
		VN_RELE(up->u_rdir);
	}
	for (fptr = exitfunc;	*fptr;	 fptr++)
		(**fptr)(p);
}


/*
 * Release virtual memory resources (memory pages, and swap area)
 * associated with the current process.
 */
vrelvm()
{
	register int (**fptr)();
	register preg_t *prp;
	register reg_t *rp;
	struct vnode *vp;
	extern int (*exitfunc[])();

	for (fptr = exitfunc; *fptr; fptr++)
		(**fptr)();
	prp = u.u_procp->p_region;
	while (rp = prp->p_reg) {
		if (vp = rp->r_vptr)
			VN_HOLD(vp);
		reglock(rp);
		detachreg(prp, u.u_procp);
	}
}

/*
 * For a 24-bit process, getting rid of pregions is a little dicey.
 * We have to install the version of the root pointer associated with the
 * pregion before detaching the pregion.  In addition, because of the
 * folding of a 24-bit page table, we free the 24-bit ones here, and leave
 * the 32-bit-only ones to be cleaned up in swtch().
 * The flag indicates that TEXT and DATA pregions are to be specially treated
 *  (as in getxfile()).
 */
shred_24bit(p, flag)
register struct proc *p;
{
	register struct region *rp;
	register struct pregion *prp;

	prp = p->p_region;
	p->p_root = p->p_root24;
	p->p_flag = (p->p_flag | SROOT24) & ~SROOT32;
	/*
	 * Note that detachreg(), as a side effect, moves prp's down, so we
	 *  don't have to bump the pointer.
	 */
	while (rp = prp->p_reg)
	{	reglock(rp);
		if (prp->p_flags&PF_MAC24 &&
		    !(flag && ( prp->p_type == PT_TEXT || prp->p_type == PT_DATA)))
			detachreg(prp, p);
		else
		{	regrele(rp);
			prp++;
		}
	}

	/* Only one L2 table in this world */
	if (!flag)
	{	reglock(&sysreg);
		ptblfree(DT_addr(p->p_root24), 2);
		ptblfree(p->p_root24, 2);
		regrele(&sysreg);
	}

	p->p_root24 = 0;
	p->p_root = p->p_root32;
	p->p_flag = (p->p_flag | SROOT32) & ~SROOT24;
}

/*
 * Shred a 32-bit page table
 */
shred_32bit(p, flag)
register struct proc *p;
int flag;
{	register int *pt;
	register unsigned int x, n;


	reglock(&sysreg);
	/* Shred our page tables */
	pt = (int *)p->p_root;
	for (n = 0; n < NL1TBL; n++, pt++)
	{	if (Lx_valid(pt))
		{	x = (unsigned int)DT_addr(pt);
			ptblfree(x, 2);
			*pt = 0;
		}
	}
	if (flag)
		ptblfree(p->p_root, 2);
	regrele(&sysreg);
	p->p_root32 = 0;
}

#ifdef POSIX
/*
 * Implements 1003.1-1988 3.2.2.2(8).
 *
 *	What about procs which have joined a solitary group via setpgid()?
 */
roustorphans(p, c)
register proc_t *p, *c;
{
	register proc_t *q;
	int stopped = 0;

	for (q = &proc[1]; q < (struct proc *)v.ve_proc; ++q) {
		if (q != p && q->p_stat && q->p_pgrp == c->p_pgrp) {
			/* parent is dying and can't protect us ... */
			if (q->p_parent != p && guardian(q, c))
				return(0);
			if (q->p_stat == SSTOP)
				stopped++;
		}
	}

	if (stopped) {
		signal(c->p_pgrp, SIGHUP);
		signal(c->p_pgrp, SIGCONT);
	}

	return(stopped);
}
#endif /* POSIX */

/* <@(#)exit.c	1.3> */
