#ifndef lint	/* .../sys/PAGING/os/sys4.c */
#define _AC_NAME sys4_c
#define _AC_NO_MAIN "@(#) Copyright (c) 1984-85 AT&T-IS, All Rights Reserved.  {Apple version 2.1 89/10/13 12:07:40}"
#include <apple_notice.h>

#ifdef _AC_HISTORY
  static char *sccsid = "@(#)Copyright Apple Computer 1987\tVersion 2.1 of sys4.c on 89/10/13 12:07:40";
#endif		/* _AC_HISTORY */
#endif		/* lint */

#define _AC_MODS
/*	@(#)sys4.c UniPlus VVV.2.1.7	*/

#include "compat.h"
#include "sys/debug.h"
#include "sys/types.h"
#include "sys/param.h"
#include "sys/mmu.h"
#include "sys/page.h"
#include "sys/systm.h"
#include "sys/time.h"
#include "sys/signal.h"
#include "sys/errno.h"
#include "sys/user.h"
#include "sys/vnode.h"
#include "svfs/inode.h"
#include "sys/file.h"
#include "sys/region.h"
#include "sys/proc.h"
#include "sys/var.h"
#include "sys/buf.h"

/*
 * Everything in this file is a routine implementing a system call.
 */
gtime()
{
	register struct a {
		caddr_t stat;
	} *uap;
	register struct user *up;

	up = &u;
	uap = (struct a *)up->u_arg;

	up->u_rtime = time.tv_sec;
	if (uap->stat != NULL) {
		if (suword(uap->stat, (int)time.tv_sec) == -1)
			up->u_error = EFAULT;
	}
}

stime()
{
    struct timeval tv;
    register struct a {
	time_t time;
    } *uap;

    uap = (struct a *) u.u_ap;

    tv.tv_sec = uap->time;
    tv.tv_usec = 0;
    setthetime (&tv);
}

setuid()
{
	register unsigned uid;
	register struct a {
		int	uid;
	} *uap;
	register struct user *up;

	up = &u;
	uap = (struct a *)up->u_ap;
	uid = uap->uid;
	if (uid >= MAXUID) {
		up->u_error = EINVAL;
		return;
	}
	if (up->u_procp->p_compatflags & COMPAT_BSDSETUGID) {
		if (up->u_ruid == uid || up->u_uid == uid || suser()) {
#ifdef QUOTA
			if (u.u_quota->q_uid != uid) {
				qclean();
				qstart(getquota(uid, 0, 0));
			}
#endif
			up->u_cred = crcopy(up->u_cred);
			up->u_uid = uid;
			up->u_procp->p_uid = uid;
			up->u_procp->p_suid = uid;
			up->u_ruid = uid;
		}
	} else {
		if (up->u_uid && (uid == up->u_ruid || uid == up->u_procp->p_suid)) {
			up->u_cred = crcopy(up->u_cred);
			up->u_uid = uid;
		}
		else if (suser()) {
			up->u_cred = crcopy(up->u_cred);
			up->u_uid = uid;
			up->u_procp->p_uid = uid;
			up->u_procp->p_suid = uid;
			up->u_ruid = uid;
		}
	}
}

getuid()
{
	register struct user *up;

	up = &u;
	up->u_rval1 = up->u_ruid;
	up->u_rval2 = up->u_uid;
}

setgid()
{
	register unsigned gid;
	register struct a {
		int	gid;
	} *uap;

	register struct user *up;

	up = &u;
	uap = (struct a *)up->u_ap;
	gid = uap->gid;
	if (gid >= MAXUID) {
		up->u_error = EINVAL;
		return;
	}
	if (up->u_procp->p_compatflags & COMPAT_BSDSETUGID) {
		if (up->u_rgid == gid || up->u_gid == gid || suser()) {
			up->u_cred = crcopy(up->u_cred);
			leavegroup(up->u_rgid);
			(void) entergroup(gid);
			up->u_gid = gid;
			up->u_procp->p_sgid = gid;
			up->u_rgid = gid;
		}
	} else {
		if (up->u_uid && (gid == up->u_rgid || gid == up->u_procp->p_sgid)) {
			up->u_cred = crcopy(up->u_cred);
			up->u_gid = gid;
		}
		else if (suser()) {
			up->u_cred = crcopy(up->u_cred);
			up->u_gid = gid;
			up->u_procp->p_sgid = gid;
			up->u_rgid = gid;
		}
	}
}

getgid()
{
	register struct user *up;

	up = &u;
	up->u_rval1 = up->u_rgid;
	up->u_rval2 = up->u_gid;
}

getpid()
{
	register struct user *up;

	up = &u;
	up->u_rval1 = up->u_procp->p_pid;
	up->u_rval2 = up->u_procp->p_ppid;
}

setpgrp()
{
	register struct proc *p = u.u_procp;
	register struct a {
		int	flag;
		int	pid;
		int	pgrp;
	} *uap;

	uap = (struct a *)u.u_ap;
	if (uap->flag) {
	    if ( (p->p_compatflags & COMPAT_BSDTTY) == 0) {
		if (p->p_pgrp != p->p_pid)
			p->p_ttyp = NULL;
		p->p_pgrp = p->p_pid;
	    } else {
		if (uap->pid == 0)
			uap->pid = u.u_procp->p_pid;
		p = pfind(uap->pid);
		if (p == 0) {
			u.u_error = ESRCH;
			return;
		}
		/* need better control mechanisms for process groups */
		if (p->p_uid != u.u_uid && u.u_uid && !inferior(p)) {
			u.u_error = EPERM;
			return;
		}
		p->p_pgrp = uap->pgrp;
		u.u_rval1 = 0;
		return;
	    }
	}
	u.u_rval1 = p->p_pgrp;
}

#ifdef POSIX
setsid()
{
	register struct proc *p = u.u_procp;
	register struct proc *q;
	
	for (q = &proc[1]; q < (struct proc *)v.ve_proc; ++q) {
	    if (q->p_stat && q->p_pgrp == p->p_pid) {	/* include case of p==q */
		u.u_error = EPERM;
		return;
	    }
	}

	if (p->p_pgrp != p->p_pid) {
	    p->p_pgrp = p->p_pid;
	    p->p_ttyp = NULL;
	}
	u.u_rval1 = p->p_pgrp;
}

setpgid()
{
	register struct proc *p;
	register struct proc *q;
	register struct a {
		int	pid;
		int	pgid;
	} *uap;
	int samegroup;
	int samectty;

	uap = (struct a *)u.u_ap;

	if (uap->pid == 0 || uap->pid == u.u_procp->p_pid)
	    p = u.u_procp;
	else if ((p = pfind(uap->pid)) == NULL || p->p_pptr != u.u_procp) {
	    u.u_error = ESRCH;
	    return;
	}

#ifdef POSIX12				/* 12.0 style */

	if (uap->pgid <= 0 || uap->pgid > MAXPID ||
	(p->p_pgrp == p->p_pid && uap->pgid != p->p_pid)) {
	    u.u_error = EINVAL;
	    return;
	}
	if (u.u_procp->p_ttyp == NULL) {
	    u.u_error = ENOTTY;
	    return;
	}

	samegroup = 0;
	samectty = 0;
	for (q = &proc[1]; q < (struct proc *)v.ve_proc; ++q) {
	    if (q->p_pgrp == uap->pgid) {
		samegroup = 1;
		if (q->p_ttyp == p->p_ttyp) {
		    samectty = 1;
		    break;
		}
	    }
	}
	if (samegroup && !samectty) {
	    u.u_error = EPERM;
	    return;
	}

	p->p_pgrp = uap->pgid;


#else /* !POSIX12 (i.e., Full-Use Standard) */

	if (uap->pgid == 0)
	    uap->pgid = p->p_pid;
	else if (uap->pgid < 0 || uap->pgid >= MAXPID) {
	    u.u_error = EINVAL;
	    return;
	}

	if (p->p_pptr == u.u_procp && !(p->p_flag & SFORK)) {
	    u.u_error = EACCES;
	    return;
	}

	if (p == u.u_procp) {		/* self */
	    if ((p->p_pid == p->p_pgrp)
		&& (!p->p_ttyp || (p->p_flag & SPGRPL))) {
		u.u_error = EPERM;	/* can't be a session group leader */
		return;
	    }
	}
	else {
	    if (p->p_ttyp != u.u_procp->p_ttyp) {
	        u.u_error = EPERM;	/* must be in same session */
	        return;
	    }
	}
	if (uap->pgid != p->p_pid) {
	    for (q = &proc[1]; q < (struct proc *)v.ve_proc; ++q) {
		if (q->p_pgrp == uap->pgid && q->p_ttyp == u.u_procp->p_ttyp)
		    break;
	    }
	    if (q == (struct proc *)v.ve_proc) {
		u.u_error = EPERM;
		return;
	    }
	}
	p->p_pgrp = uap->pgid;

#endif /* POSIX12 */
}
#endif /* POSIX */

nice()
{
	register n;
	register struct a {
		int	niceness;
	} *uap;
	register struct user *up;

	up = &u;
	uap = (struct a *)up->u_ap;
	n = uap->niceness;
	if ((n < 0 || n > 2*NZERO) && !suser())
		n = 0;
	n += up->u_procp->p_nice;
	if (n >= 2*NZERO)
		n = 2*NZERO -1;
	if (n < 0)
		n = 0;
	up->u_procp->p_nice = n;
	up->u_rval1 = n - NZERO;
}

ssig()
{
	register sig;
	register struct proc *p;
	struct a {
		int	signo;
		int	(*fun)();
	} *uap;
	register struct user *up;
	int mask;
	register s;

	up = &u;
	p = up->u_procp;
	if (p->p_compatflags & COMPAT_BSDSIGNALS) {
		up->u_error = EINVAL;
		return;
	}
	uap = (struct a *)up->u_ap;
	sig = uap->signo;
	if (sig <= 0 || sig >= NSIG || sig == SIGKILL) {
		up->u_error = EINVAL;
		return;
	}
	up->u_rval1 = (int)up->u_signal[sig-1];
	up->u_signal[sig-1] = uap->fun;
	mask = sigmask(sig);
	s = splhi();
	p->p_sig &= ~mask;
	splx(s);
	if (sig == SIGCLD) {
		for (p = p->p_child; p != NULL; p = p->p_sibling)
			if (p->p_stat == SZOMB)
				psignal(up->u_procp, SIGCLD);
		p = up->u_procp;
	}
	if ((int)uap->fun&1) {
		p->p_sigignore |= mask;
		p->p_sigcatch &= ~mask;
	} else {
		p->p_sigignore &= ~mask;
		if (uap->fun)
			p->p_sigcatch |= mask;
		else
			p->p_sigcatch &= ~mask;
	}
}

kill()
{
	register struct proc *p, *q;
	register arg;
	register struct a {
		int	pid;
		int	signo;
	} *uap;
	int f, error;
	register struct user *up;

	up = &u;

	uap = (struct a *)up->u_ap;
	if (uap->signo < 0 || uap->signo >= NSIG) {
		up->u_error = EINVAL;
		return;
	}

	/* BOBJ: this check is from the Vax version */
	/* Prevent proc 1 (init) from being SIGKILLed */
	if (uap->signo == SIGKILL && uap->pid == 1) {
		up->u_error = EINVAL;
		return;
	}

	f = error = 0;
	arg = uap->pid;
	if (arg > 0)
		p = &proc[1];
	else
		p = &proc[2];
	q = up->u_procp;
	if (arg == 0 && q->p_pgrp == 0) {
		up->u_error = ESRCH;
		return;
	}
	for(; p < (struct proc *)v.ve_proc; p++) {
		/* continue if stat is null, or zombie, since nothing to do */
		if ((p->p_stat == NULL) || (p->p_stat == SZOMB))
			continue;
		if (arg > 0 && p->p_pid != arg)
			continue;
		if (arg == 0 && p->p_pgrp != q->p_pgrp)
			continue;
		if (arg < -1 && p->p_pgrp != -arg)
			continue;
		if ((! (up->u_uid == 0 ||
			up->u_uid == p->p_uid ||
			up->u_ruid == p->p_uid ||
			up->u_uid == p->p_suid ||
			up->u_ruid == p->p_suid ||
			((p->p_flag & SPGRP42) && uap->signo == SIGCONT))) ||
			((p == &proc[1]) && (uap->signo == SIGKILL)))
			if (arg > 0) {
				up->u_error = EPERM;
				return;
			} else {
				error = EPERM;
				continue;
			}
		f++;
		if (uap->signo)
			psignal(p, uap->signo);
		if (arg > 0)
			break;
	}
	if (f == 0)
		up->u_error = (error) ? error : ESRCH;
}

times()
{
	register struct a {
		time_t	(*times)[4];
	} *uap;
	register struct user *up;
	time_t loctime[4];

	up = &u;
	uap = (struct a *)up->u_ap;
	if (v.v_hz==60) {
		if (copyout((caddr_t)&up->u_utime, (caddr_t)uap->times,
		    sizeof(*uap->times)))
			up->u_error = EFAULT;
		SPLHI();
		up->u_rtime = lbolt;
		SPL0();
	} else {
		loctime[0] = up->u_utime * 60 / v.v_hz;
		loctime[1] = up->u_stime * 60 / v.v_hz;
		loctime[2] = up->u_cutime * 60 / v.v_hz;
		loctime[3] = up->u_cstime * 60 / v.v_hz;
		if (copyout((caddr_t)&loctime[0], (caddr_t)uap->times,
		    sizeof(loctime)) < 0)
			up->u_error = EFAULT;
		SPLHI();
		up->u_rtime = lbolt*60/v.v_hz;
		SPL0();
	}
}

/*
 * alarm clock signal
 */
alarm()
{
	register struct a {
		int	deltat;
	} *uap = (struct a *)u.u_ap;
	register struct proc *p = u.u_procp;
	extern int realitexpire();
	int s = splclock();

	untimeout(realitexpire, (caddr_t)p);
	timerclear(&p->p_realtimer.it_interval);
	u.u_rval1 = 0;
	if (timerisset(&p->p_realtimer.it_value) &&
	    timercmp(&p->p_realtimer.it_value, &time, >))
		u.u_rval1 = p->p_realtimer.it_value.tv_sec - time.tv_sec;
	if (uap->deltat == 0) {
		timerclear(&p->p_realtimer.it_value);
		splx(s);
		return;
	}
	p->p_realtimer.it_value = time;
	p->p_realtimer.it_value.tv_sec += uap->deltat;
	timeout(realitexpire, (caddr_t)p, hzto(&p->p_realtimer.it_value));
	splx(s);
}

/*
 * indefinite wait.
 * no one should wakeup(&u)
 */
pause()
{

	for(;;)
		(void) sleep((caddr_t)&u, PSLEP);
}

/* <@(#)sys4.c	6.2> */
