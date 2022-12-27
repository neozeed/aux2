#ifndef lint	/* .../sys/COMMON/io/tty.c */
#define _AC_NAME tty_c
#define _AC_NO_MAIN "@(#) Copyright (c) 1987 Apple Computer, Inc., 1985-87 UniSoft Corporation, All Rights Reserved.  {Apple version 2.7 90/03/15 15:03:54}"
#include <apple_notice.h>

#ifdef _AC_HISTORY
  static char *sccsid = "@(#)Copyright Apple Computer 1987\tVersion 2.7 of tty.c on 90/03/15 15:03:54";
#endif		/* _AC_HISTORY */
#endif		/* lint */

#define _AC_MODS

/*	@(#)tty.c	UniPlus 2.1.7	*/
/*
 * general TTY subroutines
 */

#include "compat.h"
#include "sys/param.h"
#include "sys/types.h"
#include "sys/time.h"
#include "sys/mmu.h"
#include "sys/page.h"
#include "sys/systm.h"
#include "sys/signal.h"
#include "sys/user.h"
#include "sys/errno.h"
#include "sys/ioctl.h"
#include "sys/tty.h"
#include "sys/ttold.h"
#include "sys/region.h"
#include "sys/proc.h"
#include "sys/file.h"
#include "sys/conf.h"
#include "sys/termio.h"
#include "sys/sysinfo.h"
#include "sys/var.h"
#include "sys/sysmacros.h"

extern int sspeed;
extern int tthiwat[];
extern int ttlowat[];
extern char ttcchar[];
extern struct ttychars ttycdef;

/* null clist header */
struct clist ttnulq;

/* canon buffer */
char	canonb[CANBSIZ];

#define sigbit(a) (1<<(a-1))

/*
 * Input mapping table-- if an entry is non-zero, when the
 * corresponding character is typed preceded by "\" the escape
 * sequence is replaced by the table value.  Mostly used for
 * upper-case only terminals.
 */
char	maptab[] = {
	000,000,000,000,000,000,000,000,
	000,000,000,000,000,000,000,000,
	000,000,000,000,000,000,000,000,
	000,000,000,000,000,000,000,000,
	000,'|',000,000,000,000,000,'`',
	'{','}',000,000,000,000,000,000,
	000,000,000,000,000,000,000,000,
	000,000,000,000,000,000,000,000,
	000,000,000,000,000,000,000,000,
	000,000,000,000,000,000,000,000,
	000,000,000,000,000,000,000,000,
	000,000,000,000,000,000,'~',000,
	000,'A','B','C','D','E','F','G',
	'H','I','J','K','L','M','N','O',
	'P','Q','R','S','T','U','V','W',
	'X','Y','Z',000,000,000,000,000,
};

/*
 * common ioctl tty code
 */
ttiocom(tp, cmd, data, mode)
register struct tty *tp;
caddr_t data;
{
	register struct user *up;
	register short flag;
	register struct termio *cbp;
	register struct proc *pp;
#ifdef POSIX
	register struct proc *q;
	int pgrp;
	int samegroup;
	int samectty;
	int dev;
#endif /* POSIX */

	up = &u;

	/*
	 * If the ioctl involves modification,
	 * insist on being able to write the device,
	 * and hang if in the background.
	 */
	switch (cmd) {

	case TCSETAW:
	case TCSETAF:
	case TCSETA:
	case TCSBRK:
	case TCSBRKM:		
	case TCXONC:
	case TCFLSH:
	case TIOCSLTC:
	case TIOCSPGRP:
#ifdef POSIX
	case TC_PX_SETPGRP:
#endif /* POSIX */
	case TIOCSWINSZ:	/* rpd@apple added TIOCSWINSZ */
		pp = up->u_procp;
		while (
		   (pp->p_flag & SPGRP42) &&
		   pp->p_pgrp != tp->t_pgrp &&
		   &tp->t_pgrp == pp->p_ttyp &&
		   !(pp->p_sigignore & sigbit(SIGTTOU)) &&
		   !(P_SIGMASK(pp) & sigbit(SIGTTOU))) {
			signal(pp->p_pgrp, SIGTTOU);
			sleep((caddr_t)&lbolt, TTOPRI);
		}
		break;
	}

	switch(cmd) {
	case IOCTYPE_MASK:
		up->u_rval1 = TIOC;
		break;

	case TCSETAW:
	case TCSETAF:
		ttywait(tp);
		if (cmd == TCSETAF)
			ttyflush(tp, (FREAD|FWRITE));
	case TCSETA:
		cbp = (struct termio *) data;
		if (tp->t_line != cbp->c_line) {
			if (cbp->c_line < 0 || cbp->c_line >= linecnt) {
				up->u_error = EINVAL;
				break;
			}
			(*linesw[tp->t_line].l_ioctl)(tp, LDCLOSE, 0, mode);
		}
		flag = tp->t_lflag;
		tp->t_iflag = cbp->c_iflag;
		tp->t_oflag = cbp->c_oflag;
		tp->t_cflag = cbp->c_cflag;
		tp->t_lflag = cbp->c_lflag;
		bcopy((caddr_t)cbp->c_cc, (caddr_t)tp->t_cc, NCC);
		if (tp->t_line != cbp->c_line) {
			tp->t_line = cbp->c_line;
			(*linesw[tp->t_line].l_ioctl)(tp, LDOPEN, 0, mode);
		} else if (tp->t_lflag != flag) {
			(*linesw[tp->t_line].l_ioctl)(tp, LDCHG, flag, mode);
		}
		return(1);

	case TCGETA:
		cbp = (struct termio *) data;
		cbp->c_iflag = tp->t_iflag;
		cbp->c_oflag = tp->t_oflag;
		cbp->c_cflag = tp->t_cflag;
		cbp->c_lflag = tp->t_lflag;
		cbp->c_line = tp->t_line;
		bcopy((caddr_t)tp->t_cc, (caddr_t)cbp->c_cc, NCC);
		break;

	case TCSBRK:
		ttywait(tp);
		if (*(int *) data == 0)
			(*tp->t_proc)(tp, T_BREAK);
		break;

	case TCXONC:
		switch (*(int *) data) {
		case 0:
			(*tp->t_proc)(tp, T_SUSPEND);
			break;
		case 1:
			(*tp->t_proc)(tp, T_RESUME);
			break;
		case 2:
			(*tp->t_proc)(tp, T_BLOCK);
			break;
		case 3:
			(*tp->t_proc)(tp, T_UNBLOCK);
			break;
		default:
			up->u_error = EINVAL;
		}
		break;

	case TCFLSH:
		switch (*(int *) data) {
		case 0:
		case 1:
		case 2:
			ttyflush(tp, (*(int *) data - FOPEN)&(FREAD|FWRITE));
			break;

		default:
			up->u_error = EINVAL;
		}
		break;

	case TCSETRAWQ:
		if ((*(short *) data) < TTYHOG)
			break;
		if ((*(short *) data) > ((v.v_clist*v.v_clsize)/2))
			printf("Warning: Need to increase clist size to %d\n",
				((*(short *)data) * 2)/v.v_clsize);
		else
			tp->t_maxrawq = (*(short *) data);
		break;

	/*
	 * The following ioctls were added by UniSoft
	 */

	/*
	 * Return number of characters immediately available.
	 */
	case FIONREAD:
		*(off_t *)data = ttnread(tp);
		break;

	case FIONBIO:
		if (*(int *)data)
			tp->t_state |= TS_NBIO;
		else
			tp->t_state &= ~TS_NBIO;
		break;

	case FIOASYNC:
		if (*(int *)data)
			tp->t_state |= TS_ASYNC;
		else
			tp->t_state &= ~TS_ASYNC;
		break;

	/* set/get local special characters */
	case TIOCSLTC:
		bcopy(data, (caddr_t)&tp->tt_suspc, sizeof (struct ltchars));
		break;

	case TIOCGLTC:
		bcopy((caddr_t)&tp->tt_suspc, data, sizeof (struct ltchars));
		break;

	/* should allow SPGRP and GPGRP only if tty open for reading */
	case TIOCSPGRP:
		tp->t_pgrp = *(int *)data;
		break;

#ifdef POSIX
	/* POSIX form of TIOCSPGRP */
	case TC_PX_SETPGRP:
		pgrp = *(int *)data;
		if (pgrp <= 0 || pgrp > MAXPID) {
		    up->u_error = EINVAL;
		    break;
		}
		if (!up->u_procp->p_ttyp || up->u_procp->p_ttyp != &tp->t_pgrp) {
		    up->u_error = ENOTTY;
		    break;
		}

#ifndef POSIX123
		samegroup = 0;
		samectty = 0;
		for (q = &proc[1]; q < (struct proc *)v.ve_proc; ++q) {
		    if (q->p_pgrp == pgrp) {
			samegroup = 1;
			if (q->p_ttyp == up->u_procp->p_ttyp) {
			    samectty = 1;
			    break;
			}
		    }
		}
		if (samegroup && !samectty) {
		    up->u_error = EPERM;
		    break;
		}
#else
		for (q = &proc[1]; q < (struct proc *)v.ve_proc; ++q) {
		    if (q != up->u_procp && (pgrp == q->p_pid || pgrp == q->p_pgrp) &&
		    p->p_ttyp != q->p_ttyp)
			break;
		}
		if (q < v.ve_proc) {
		    up->u_error = EPERM;
		    break;
		}
#endif POSIX123
		tp->t_pgrp = pgrp;
		break;
#endif /* POSIX */

	case TIOCGPGRP:
#ifdef POSIX
	case TC_PX_GETPGRP:	/* POSIX form of TIOCGPGRP */
#endif /* POSIX */
		*(int *)data = tp->t_pgrp;
		break;

	case TIOCGCOMPAT:
		*(int *)data = (tp->t_lflag & TOSTOP) ? TOSTOP : 0;
		break;

	case TIOCSCOMPAT:
		if(*(int *)data & TOSTOP)
			tp->t_lflag |= TOSTOP;
		else
			tp->t_lflag &= ~TOSTOP;
		break;

	/*
	 * The following two ioctls were added by rpd@apple
	 */

	case TIOCSWINSZ:
		if (bcmp((caddr_t)&tp->t_winsize, data,
		    sizeof (struct winsize))) {
			tp->t_winsize = *(struct winsize *)data;
			signal(tp->t_pgrp, SIGWINCH);
		}
		break;

	case TIOCGWINSZ:
		*(struct winsize *)data = tp->t_winsize;
		break;

	default:
		if ((_IOCTYPE(cmd)) == LDIOC)
			(*linesw[tp->t_line].l_ioctl)(tp, cmd, data, mode);
		else
			up->u_error = EINVAL;
		break;
	}
	return(0);
}

ttinit(tp)
register struct tty *tp;
{
	tp->t_line = 0;
	tp->t_iflag = 0;
	tp->t_oflag = 0;
	tp->t_cflag = sspeed|CS8|CREAD|HUPCL;
	tp->t_lflag = 0;
	bcopy((caddr_t)ttcchar, (caddr_t)tp->t_cc, NCC);
	bcopy((caddr_t)&ttycdef, (caddr_t)&tp->t_chars,
		sizeof(struct ttychars));
	bzero((caddr_t)&tp->t_winsize, sizeof(tp->t_winsize)); /* rpd@apple */
}

ttywait(tp)
register struct tty *tp;
{
	(void) spltty();
	while (tp->t_outq.c_cc || (tp->t_state&(BUSY|TIMEOUT))) {
		tp->t_state |= TTIOW;
		(void) sleep((caddr_t)&tp->t_oflag, TTOPRI);
	}
	SPL0();

	if ((tp->t_cflag&CBAUD) > B2400)
	        delay(v.v_hz / 20);
	else
	        delay(v.v_hz / 4);
}

/*
 * flush TTY queues
 */
ttyflush(tp, cmd)
register struct tty *tp;
{
	register struct cblock *cp;
	register s;

	if (cmd&FWRITE) {
		while ((cp = getcb(&tp->t_outq)) != NULL)
			putcf(cp);
		(*tp->t_proc)(tp, T_WFLUSH);
		if (tp->t_state&OASLP) {
			tp->t_state &= ~OASLP;
			wakeup((caddr_t)&tp->t_outq);
		}
		if (tp->t_state&TTIOW) {
			tp->t_state &= ~TTIOW;
			wakeup((caddr_t)&tp->t_oflag);
		}
	}
	if (cmd&FREAD) {
		while ((cp = getcb(&tp->t_canq)) != NULL)
			putcf(cp);
		s = spltty();
		while ((cp = getcb(&tp->t_rawq)) != NULL)
			putcf(cp);
		tp->t_delct = 0;
		splx(s);
		(*tp->t_proc)(tp, T_RFLUSH);
		ttwakeup(tp);
	}
}

/*
 * Transfer raw input list to canonical list,
 * doing erase-kill processing and handling escapes.
 */
canon(tp)
register struct tty *tp;
{
	register char *bp;
	register struct cblock *cp;
	register c, esc;

	(void) spltty();
	if (tp->t_rawq.c_cc == 0)
		tp->t_delct = 0;
loop:
	if (u.u_procp->p_flag & SPGRP42) {
		/*
		 * Hang process if it's in the background.
		 */
		while (u.u_procp->p_ttyp == &tp->t_pgrp &&
		    u.u_procp->p_pgrp != tp->t_pgrp) {
			if ((u.u_procp->p_sigignore & sigbit(SIGTTIN))||
			   (P_SIGMASK(u.u_procp) & sigbit(SIGTTIN)) )
				return (EIO);
			signal(u.u_procp->p_pgrp, SIGTTIN);
			(void) sleep((caddr_t)&lbolt, TTIPRI);
		}
	}
	while (tp->t_delct == 0) {
		if ((tp->t_state&CARR_ON) == 0 ||
#ifdef POSIX
		    (u.u_fmode&(FNDELAY|FNONBLOCK)) ||
#else /* !POSIX */
		    (u.u_fmode&FNDELAY) ||
#endif /* POSIX */
		    (tp->t_state&TS_NBIO)) {
			SPL0();
			if (u.u_procp->p_compatflags & COMPAT_BSDNBIO)
				u.u_error = EWOULDBLOCK;
#ifdef POSIX
			else if (u.u_fmode & FNONBLOCK)
				u.u_error = EAGAIN;
#endif /* POSIX */
			return;
		}
		if (!(tp->t_lflag&ICANON) && tp->t_cc[VMIN]==0) {
			if (tp->t_cc[VTIME]==0)
				break;
			tp->t_state &= ~RTO;
			if (!(tp->t_state&TACT))
				tttimeo(tp);
		}
		tp->t_state |= IASLP;
		(void) sleep((caddr_t)&tp->t_rawq, TTIPRI);
		if (u.u_procp->p_flag & SPGRP42) 
			goto loop;
	}
	if (!(tp->t_lflag&ICANON)) {
		if (tp->t_canq.c_cc == 0) {
			tp->t_canq = tp->t_rawq;
			tp->t_rawq = ttnulq;
		} else {
			if (tp->t_rawq.c_cc < CLSIZE)
				while (tp->t_rawq.c_cc)
					(void) putc(getc(&tp->t_rawq),&tp->t_canq);
			else {
				tp->t_canq.c_cc += tp->t_rawq.c_cc;
				tp->t_canq.c_cl->c_next = tp->t_rawq.c_cf;
				tp->t_canq.c_cl = tp->t_rawq.c_cl;
				tp->t_rawq = ttnulq;
			}
		}
		tp->t_delct = 0;
		SPL0();
		return;
	}
	SPL0();
	bp = canonb;
	esc = 0;
	while ((c=getc(&tp->t_rawq)) >= 0) {
		if (!esc) {
			if (c == '\\') {
				esc++;
#ifdef POSIX
			} else if (c == _POSIX_VDISABLE) {
				;
#endif /* POSIX */
			} else if (c == tp->t_cc[VERASE]) {
				if (bp > canonb)
					bp--;
				continue;
			} else if (c == tp->t_cc[VKILL]) {
				bp = canonb;
				continue;
			} else if (c == tp->t_cc[VEOF]) {
				break;
			} else if (c == tp->tt_dsuspc) {
				signal(tp->t_pgrp, SIGTSTP);
				bp = canonb;
				sleep((caddr_t)&lbolt, TTIPRI);
				goto loop;
			}
		} else {
			esc = 0;
#ifdef POSIX
			if (c != _POSIX_VDISABLE && 
			      (c == tp->t_cc[VERASE] ||
			       c == tp->t_cc[VKILL] ||
			       c == tp->t_cc[VEOF]))
				bp--;
#else /* !POSIX */
			if (c == tp->t_cc[VERASE] ||
			    c == tp->t_cc[VKILL] ||
			    c == tp->t_cc[VEOF])
				bp--;
#endif /* POSIX */
			else if (tp->t_lflag&XCASE) {
				if ((c < 0200) && maptab[c]) {
					bp--;
					c = maptab[c];
				} else if (c == '\\')
					continue;
			} else if (c == '\\')
				esc++;
		}
		*bp++ = c;
#ifdef POSIX
		if (c != _POSIX_VDISABLE && 
		    (c == '\n' || c == tp->t_cc[VEOL] || c == tp->t_cc[VEOL2]))
			  break;
#else /* !POSIX */
		if (c == '\n' || c == tp->t_cc[VEOL] || c == tp->t_cc[VEOL2])
			break;
#endif /* POSIX */
		if (bp >= &canonb[CANBSIZ])
			bp--;
	}
	tp->t_delct--;
	c = bp - canonb;
	sysinfo.canch += c;
	bp = canonb;
/* faster copy ? */
	while (c--)
		(void) putc(*bp++, &tp->t_canq);
	return;
}

/*
 * Restart typewriter output following a delay timeout.
 * The name of the routine is passed to the timeout
 * subroutine and it is called during a clock interrupt.
 */
ttrstrt(tp)
register struct tty *tp;
{

	(*tp->t_proc)(tp, T_TIME);
}

ttnread(tp)
register struct tty	*tp;
{
	register int	nread = 0;

	SPL6();
	while (tp->t_rawq.c_cc && tp->t_delct)
		canon(tp);
	SPL0();
	nread	= tp->t_canq.c_cc;
	if (!(tp->t_lflag & ICANON))
		nread	+= tp->t_rawq.c_cc;

	return (nread);
}

#define ttminor(dev)	((dev) & 0x7F)
/* ARGSUSED */
ttselect(dev, rw)
	dev_t dev;
	int rw;
{
	register struct tty *tp = &cdevsw[major(dev)].d_ttys[ttminor(dev)];
	int nread;
	int s = spltty();
	extern int	selwait;

	switch (rw) {

	case FREAD:
		 if (tp->t_delct > 0)
			goto win;
		if (tp->t_rsel && tp->t_rsel->p_wchan == (caddr_t)&selwait)
			tp->t_state |= TS_RCOLL;
		else
			tp->t_rsel = u.u_procp;
		break;

	case FWRITE:
		if (tp->t_outq.c_cc <= ttlowat[tp->t_cflag&CBAUD])
			goto win;
		if (tp->t_wsel && tp->t_wsel->p_wchan == (caddr_t)&selwait)
			tp->t_state |= TS_WCOLL;
		else
			tp->t_wsel = u.u_procp;
		break;
	}
	splx(s);
	return (0);
win:
	splx(s);
	return (1);
}

ttwakeup(tp)
register struct tty	*tp;
{
	if (tp->t_rsel) {
		selwakeup(tp->t_rsel, (int) (tp->t_state & TS_RCOLL));
		tp->t_state &= ~TS_RCOLL;
		tp->t_rsel = 0;
	}
	if (tp->t_state & TS_ASYNC)
		signal(tp->t_pgrp, SIGIO); 
	if (tp->t_state & IASLP) {
		tp->t_state &= ~IASLP;
		wakeup((caddr_t)&tp->t_rawq);
	}
}
