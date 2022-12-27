#ifndef lint	/* .../sys/NET/sys/tty_pty.c */
#define _AC_NAME tty_pty_c
#define _AC_NO_MAIN "@(#) Copyright (c) 1987 Apple Computer, Inc., 1980-87 The Regents of the University of California, 1985-87 UniSoft Corporation, All Rights Reserved.  {Apple version 2.6 89/12/01 09:35:22}"
#include <apple_notice.h>

#ifdef _AC_HISTORY
  static char *sccsid = "@(#)Copyright Apple Computer 1989\tVersion 2.6 of tty_pty.c on 89/12/01 09:35:22";
#endif		/* _AC_HISTORY */
#endif		/* lint */

#define _AC_MODS
/*	pty.c	4.21	82/03/23	*/

/*
 * Pseudo-teletype Driver
 * (Actually two drivers, requiring two entries in 'cdevsw')
 */


#include "sys/param.h"
#include "sys/types.h"
#include "sys/mmu.h"
#include "sys/sysmacros.h"
#include "sys/time.h"
#include "sys/page.h"
#include "sys/region.h"
#include "sys/systm.h"
#include "sys/ioctl.h"
#include "sys/tty.h"
#include "sys/ttold.h"
#include "sys/termio.h"
#include "sys/signal.h"
#include "sys/errno.h"
#include "sys/user.h"
#include "sys/conf.h"
#include "sys/buf.h"
#include "sys/file.h"
#include "sys/proc.h"
#include "sys/var.h"
#include "sys/sxt.h"
#include "sys/uio.h"
#include "sys/vnode.h"
#include "sys/stropts.h"
#include "mac/types.h"
#include "mac/osutils.h"
#include "mac/segload.h"
#include "mac/files.h"
#include "mac/quickdraw.h"
#include "mac/devices.h"
#include "sys/video.h"

#define BUFSIZ 256              	/* Chunk size iomoved from user */

/*
 * pts == /dev/tty[pP]?
 * ptc == /dev/pty[pP]?
 */

extern struct tty	*pts_tty;
extern struct pt_ioctl	*pts_ioctl;
extern char chorddata[32];

struct cdevsw ptoldcons;
struct proc *ptconsproc = NULL;		/* proc wanting a SIGUSR1 posted upon read */
dev_t ptconspty = -1;			/* dev of pty that is now the console */
dev_t ptcons = -1;			/* dev of original console */

#define	PF_RCOLL	0x001
#define	PF_WCOLL	0x002
#define	PF_NBIO		0x004
#define	PF_PKT		0x008		/* packet mode */
#define	PF_STOPPED	0x010		/* user told stopped */
#define	PF_REMOTE	0x020		/* remote and flow controlled input */
#define	PF_NOSTOP	0x040
#define PF_WTIMER       0x080           /* waiting for timer to flush */
#define PF_OASLP        0x100
#define PF_IASLP        0x200
#define PF_ISOPEN       0x400

#define ptc_output(tp)	  ((((tp)->t_state&TTSTOP) == 0) && (tp)->t_outq.c_cc)
#define ptc_mustwait(tp,x)  ((tp)->t_delct && ((tp)->t_rawq.c_cc + (x)) >= TTYHOG)



/*ARGSUSED*/
ptsopen(dev, flag)
register dev_t dev;
{	register struct tty *tp;

	if (dev == ptcons) {		/* come here if console is redirected
					   to a pty. Flush first if we are
					   to read from console */
		dev = minor(ptconspty);
		tp = &pts_tty[dev];
		if ((flag & FREAD) && (tp->t_state & ISOPEN))
			ttyflush(tp,FREAD);
	}
	else {
		if ((dev = minor(dev)) >= v.v_npty)
			return(ENXIO);
		tp = &pts_tty[dev];
	}

	while ((tp->t_state & CARR_ON) == 0) {
		tp->t_state |= WOPEN;
		(void) sleep((caddr_t)&tp->t_rawq, TTIPRI);
	}
	(*linesw[tp->t_line].l_open)(tp, flag);

	return(0);
}


ptsclose(dev)
register dev_t dev;
{	register struct tty *tp;
	register struct pt_ioctl *pti;

	if (dev == ptcons)
		return;		/* should never get here */
	dev = minor(dev);
	tp = &pts_tty[dev];
	pti = &pts_ioctl[dev];

	ptcwakeup(tp, FREAD);
	(*linesw[tp->t_line].l_close)(tp);
	pti->pt_flags &= ~PF_ISOPEN;
	ptcwakeup(tp, FREAD|FWRITE);
	if (minor(ptconspty) == dev) {
		cdevsw[major(ptcons)] = ptoldcons;
		ptconspty = -1;
		ptcons = -1;
		ptconsproc = NULL;
	}
}


ptsread(dev, uio)
dev_t dev;
struct uio *uio;
{	register struct tty *tp;
	register int rv;

	if (dev == ptcons) {
		dev = ptconspty;
		psignal(ptconsproc,SIGUSR1);
	}
	tp = &pts_tty[minor(dev)];
	rv = (*linesw[tp->t_line].l_read)(tp, uio);

	ptcwakeup(tp, FWRITE);
	return(rv);
}


/*
 * Write to pseudo-tty.
 * Wakeups of controlling tty will happen
 * indirectly, when tty driver calls ptproc.
 */
ptswrite(dev, uio)
dev_t dev;
struct uio *uio;
{	register struct tty *tp;

	if (dev == ptcons)
		dev = ptconspty;
	tp = &pts_tty[minor(dev)];
	return((*linesw[tp->t_line].l_write)(tp, uio));
}


/*ARGSUSED*/
ptsioctl(dev, cmd, addr, flag)
caddr_t addr;
register dev_t dev;
{	register struct tty *tp;
	register struct pt_ioctl *pti;
	register int stop;
	register struct file *fp;
	struct vnode *vp;
	struct strioctl *s;
	long oldcount;
	dev_t consdev;

	tp = &pts_tty[minor(dev)];
	pti = &pts_ioctl[minor(dev)];
	switch (cmd) {
	case TIOCSETCONS:
		if (ptcons !=  (dev_t)-1) {
			u.u_error = EINVAL;	/* already in use */
			return(u.u_error);
		}
		consdev = *(dev_t *)addr;
		if (consdev == dev)
			return(u.u_error);	/* same dev */
		if (major(consdev) > cdevcnt) {
			u.u_error = EINVAL;
			return(u.u_error);
		}
		ptconspty = dev;
		ptcons = consdev;
		ptoldcons = cdevsw[major(ptcons)];
		cdevsw[major(ptcons)] = cdevsw[major(dev)];
		return(u.u_error);
	case TIOCSETRDSIG:
		if (ptcons == (dev_t)-1) {
			u.u_error = ENXIO;
			return(u.u_error);
		}
		if (ptconsproc) {
			u.u_error = EBUSY;
			return(u.u_error);
		}
		ptconsproc = u.u_procp;
		return(u.u_error);
	}

	if (dev == ptcons)
		dev = ptconspty;
	ttiocom(tp, cmd, (int)addr, dev);
	stop = tp->t_iflag & IXON;

	if (pti->pt_flags & PF_NOSTOP) {
		if (stop) {
			pti->pt_send &= ~TIOCPKT_NOSTOP;
			pti->pt_send |= TIOCPKT_DOSTOP;
			pti->pt_flags &= ~PF_NOSTOP;
			if (pti->pt_flags & PF_PKT)
			        ptcwakeup(tp, FREAD);
		}
	} else {
		if (stop == 0) {
			pti->pt_send &= ~TIOCPKT_DOSTOP;
			pti->pt_send |= TIOCPKT_NOSTOP;
			pti->pt_flags |= PF_NOSTOP;
			if (pti->pt_flags & PF_PKT)
			        ptcwakeup(tp, FREAD);
		}
	}
	return(u.u_error);
}


ptsxtproc(tp)
register struct tty *tp;
{	register struct clist *outq = &tp->t_outq;
	register struct cblock *this;
	register struct ccblock *tbuf = &tp->t_tbuf;

	/* get cblocks from virt tty(s) & link them to real tty */

	while (CPRES & (*linesw[(short)tp->t_line].l_output)(tp)) {
		this = CMATCH(tbuf->c_ptr);
		this->c_next = NULL;
		if (outq->c_cl == NULL) {
			outq->c_cl =  this;
			outq->c_cf =  this;
		}
		else {
			outq->c_cl->c_next = this;
			outq->c_cl = this;
		}
		outq->c_cc += tbuf->c_count;
		/* fixup clist structure -- should be done by ttout() */
		this->c_last = this->c_first + tbuf->c_count;
		/* clear tbuf */
		tbuf->c_ptr = NULL;
		tbuf->c_count = 0;
		/* wait for drainage */
		while (outq->c_cc > tthiwat[tp->t_cflag&CBAUD]) {
			if ((tp->t_state & CARR_ON) == 0)
			      return;
			ptcwakeup(tp, FREAD);
			tp->t_state |= OASLP;
			sleep((caddr_t) outq, TTOPRI);
		}
	}
}






/*ARGSUSED*/
ptcopen(dev, flag)
register dev_t dev;
int flag;
{	register struct tty *tp;
	register struct pt_ioctl *pti;
	static first;
	int ptproc();

	if ((dev = minor(dev)) >= v.v_npty)
		return(ENXIO);
	if (first == 0) {
		first++;
		ptctimer();
	}
	tp = &pts_tty[dev];

	if (tp->t_state & CARR_ON)
		return(EIO);
	ttinit(tp);
	tp->t_iflag = ICRNL|ISTRIP|IGNPAR;
	tp->t_oflag = OPOST|ONLCR|TAB3;
	tp->t_lflag = ISIG|ICANON; /* no echo */
	tp->t_proc = ptproc;

	tp->t_state |= CARR_ON;
	if (tp->t_state & WOPEN)
		wakeup((caddr_t)&tp->t_rawq);

	pti = &pts_ioctl[dev];
	pti->pt_flags = PF_ISOPEN;
	pti->pt_send = 0;
	return(0);
}


ptcclose(dev)
dev_t dev;
{	register struct tty *tp;

	tp = &pts_tty[minor(dev)];

	tp->t_state &= ~CARR_ON;	/* virtual carrier gone */
	if (tp->t_state & ISOPEN)
		signal(tp->t_pgrp, SIGHUP);
	ttyflush(tp, FREAD|FWRITE);
}


ptcread(dev, uio)
register dev_t dev;
register struct uio *uio;
{	register struct tty *tp;
	register struct pt_ioctl *pti;
	register char *p;
	register int c;
	register int n;
	register int error = 0;
	char buffer[BUFSIZ];

	dev = minor(dev);
	tp = &pts_tty[dev];
	pti = &pts_ioctl[dev];

	for (;;) {
	        if ((pti->pt_flags & PF_ISOPEN) == 0)
		        return(EIO);

	        if (pti->pt_flags & PF_PKT) {
		        if (pti->pt_send) {
			        if (error = ureadc(pti->pt_send, uio))
				        return(error);
				pti->pt_send = 0;
				return(0);
			}
		}
		if (ptc_output(tp))
		        break;
		if (pti->pt_flags & PF_NBIO)
			return(EWOULDBLOCK);
		pti->pt_flags |= PF_IASLP;
		sleep((caddr_t)&tp->t_outq.c_cf, TTIPRI);
	}
        if (pti->pt_flags & PF_PKT) {
	        if (error = ureadc(0, uio))
		        return(error);
	}
	while (tp->t_outq.c_cc && uio->uio_resid > 0) {
	       n = MIN(tp->t_outq.c_cc, uio->uio_resid);
	       n = MIN(n, BUFSIZ);

	       for (p = buffer,c = NULL; p < &buffer[n]; ) {
			c = getc(&tp->t_outq);
			if (c == QESC) {
				n--;
			    	c = getc(&tp->t_outq);
				if (c > 0200) {
					n--;
					continue;
				}
			}
			*p++ = c;
               }
	       if (error = uiomove(buffer, n, UIO_READ, uio))
		       break;
	}
	tp->t_state &= ~BUSY;

        if (tp->t_outq.c_cc <= ttlowat[tp->t_cflag&CBAUD]) {
		if (tp->t_outq.c_cc == 0 && (tp->t_state&TTIOW)) {
		        tp->t_state &= ~TTIOW;
			wakeup((caddr_t)&tp->t_oflag);
		}
		if (tp->t_state & OASLP) {
			tp->t_state &= ~OASLP;
			wakeup((caddr_t)&tp->t_outq);
		}
		if (tp->t_wsel) {
			selwakeup(tp->t_wsel, (int) (tp->t_state & TS_WCOLL));
			tp->t_wsel = 0;
			tp->t_state &= ~TS_WCOLL;
		}
        }
	return(error);
}


ptcwrite(dev, uio)
register dev_t dev;
register struct uio *uio;
{	register struct tty *tp;
	register struct iovec *iov;
	register char *cp, *ce;
	struct pt_ioctl *pti;
	register int cc, c;
	char locbuf[BUFSIZ];
	register int cnt = 0;
	int error;

	dev = minor(dev);
	tp = &pts_tty[dev];
	pti = &pts_ioctl[dev];

	do {
	        if ((pti->pt_flags & PF_ISOPEN) == 0)
		        return(EIO);
		if (uio->uio_iovcnt == 0)
			break;

		iov = uio->uio_iov;
		if (iov->iov_len == 0) {
			uio->uio_iovcnt--;
			uio->uio_iov++;
			if (uio->uio_iovcnt < 0)
				panic("ptcwrite");
			continue;
		}
		cc = MIN(iov->iov_len, BUFSIZ);
		cp = locbuf;
		if (error = uiomove(cp, cc, UIO_WRITE, uio))
		        return(error);
		ce = cp + cc;
		cc = 0;

		while (cp < ce) {
			while (tp->t_rbuf.c_ptr == NULL || ptc_mustwait(tp, cc)) {
			        if (cc) {
				        cnt += cc;
					tp->t_rbuf.c_ptr -= cc;
					(*linesw[tp->t_line].l_input)(tp, L_BUF);
				}
				if (pti->pt_flags & PF_NBIO) {
				        cc = ce - cp;
					iov->iov_base -= cc;
					iov->iov_len += cc;
					uio->uio_resid += cc;
					uio->uio_offset -= cc;
					if (cnt == 0)
						return(EWOULDBLOCK);
					return(0);
				}
				pti->pt_flags |= PF_OASLP;
				sleep((caddr_t)&tp->t_rawq.c_cf, TTOPRI);

				if ((pti->pt_flags & PF_ISOPEN) == 0)
				        return(EIO);
			}
			if (tp->t_iflag & ISTRIP)
				c = *cp++ & 0x7f;
			else
				c = *cp++;

			if (tp->t_iflag & IXON) {
				if (tp->t_state & TTSTOP) {
					if (c == CSTART || (tp->t_iflag & IXANY))
						(*tp->t_proc)(tp, T_RESUME);
				} else
					if (c == CSTOP)
						(*tp->t_proc)(tp, T_SUSPEND);
				if (c == CSTART || c == CSTOP)
					continue;
			}
			*tp->t_rbuf.c_ptr++ = c;
			cc++;

			if (--tp->t_rbuf.c_count == 0) {
				cnt += cc;
			        tp->t_rbuf.c_ptr -= cc;
				(*linesw[tp->t_line].l_input)(tp, L_BUF);
				cc = 0;
			}
		}
		if (cc) {
			cnt += cc;
		        tp->t_rbuf.c_ptr -= cc;
			(*linesw[tp->t_line].l_input)(tp, L_BUF);
		}
	} while (uio->uio_resid);

	return(0);
}


ptcioctl(dev, cmd, data, flag)
caddr_t data;
register int cmd;
register dev_t dev;
{	register struct tty *tp;
	register struct pt_ioctl *pti;

	tp = &pts_tty[minor(dev)];
	pti = &pts_ioctl[minor(dev)];

	if (cmd == TIOCPKT) {
		if (*(int *)data)
			pti->pt_flags |= PF_PKT;
		else
			pti->pt_flags &= ~PF_PKT;
		return(0);
	}
	if (cmd == FIONBIO) {
		if (*(int *)data)
			pti->pt_flags |= PF_NBIO;
		else
			pti->pt_flags &= ~PF_NBIO;
		return(0);
	}
	/* IF CONTROLLER STTY THEN MUST FLUSH TO PREVENT A HANG ???  */
        if ((cmd == TIOCSETP) || (cmd == TCSETAW)) {
		while (getc(&tp->t_outq) >= 0)
			;
		tp->t_state &= ~BUSY;
	}
	return(ptsioctl(dev, cmd, data, flag));
}


ptproc(tp, cmd)
register struct tty *tp;
{       register int lowat;
	register struct pt_ioctl *pti;
        extern ttrstrt(), sxtout();

	lowat = ttlowat[tp->t_cflag&CBAUD];
	pti = &pts_ioctl[tp - pts_tty];

        switch(cmd) {
        case T_TIME:
                tp->t_state &= ~TIMEOUT;
                goto start;

        case T_WFLUSH:
		pti->pt_send = TIOCPKT_FLUSHWRITE;
		tp->t_state &= ~BUSY;
		/* fall through */

        case T_RESUME:
                tp->t_state &= ~TTSTOP;
		pti->pt_send &= ~TIOCPKT_STOP;
		pti->pt_send |= TIOCPKT_START;
		if (pti->pt_flags & PF_PKT)
		        ptcwakeup(tp, FREAD);
		/* fall through */

        case T_OUTPUT:
start:
		/*
		 * The following is not a general solution.  Someone may use
		 * some other line disc. besides sxt someday.
		 */
		if (linesw[(short)tp->t_line].l_output == sxtout)
			ptsxtproc(tp);
                if (tp->t_state&(TIMEOUT|TTSTOP|BUSY))
                        break;
		if (tp->t_outq.c_cc < (tthiwat[tp->t_cflag&CBAUD] + lowat)/2) {
		        pti->pt_flags |= PF_WTIMER;
			break;
		}
		tp->t_state |= BUSY;
		ptcwakeup(tp, FREAD);
                break;

        case T_SUSPEND:
                tp->t_state |= TTSTOP;
		tp->t_state &= ~BUSY;
		pti->pt_send &= ~TIOCPKT_START;
		pti->pt_send |= TIOCPKT_STOP;
		if (pti->pt_flags & PF_PKT)
		        ptcwakeup(tp, FREAD);
                break;

        case T_BLOCK:
		tp->t_state |= TBLOCK | TTXOFF;
                tp->t_state &= ~TTXON;
                break;

        case T_RFLUSH:
		pti->pt_send = TIOCPKT_FLUSHREAD;
                if (!(tp->t_state&TBLOCK))
                        break;
		/* fall through */

        case T_UNBLOCK:
                tp->t_state &= ~(TTXOFF|TBLOCK);
		tp->t_state |= TTXON;
		if (pti->pt_flags & PF_PKT)
		        ptcwakeup(tp, FREAD);
		goto start;

        case T_BREAK:
                tp->t_state |= TIMEOUT;
                timeout(ttrstrt, (caddr_t)tp, v.v_hz/4);
                break;

	case T_PARM:
		break;
        }
}


ptcselect(dev, rw)
register dev_t dev;
int  rw;
{	register struct tty *tp;
	register struct pt_ioctl *pti;

	dev = minor(dev);
	tp  = &pts_tty[dev];
	pti = &pts_ioctl[dev];

	if ((pti->pt_flags & PF_ISOPEN) == 0)
		return(1);
	switch(rw) {

	case FREAD:
	        if (ptc_output(tp) || ((pti->pt_flags&PF_PKT) && pti->pt_send))
		       return(1);
	        if (pti->pt_selr && pti->pt_selr->p_wchan == (caddr_t)&selwait)
		        pti->pt_flags |= PF_RCOLL;
		else
		        pti->pt_selr = u.u_procp;
		break;

	case FWRITE:
		if ( !tp->t_delct || tp->t_rawq.c_cc < (TTYHOG/4))
		        return(1);
	        if (pti->pt_selw && pti->pt_selw->p_wchan == (caddr_t)&selwait)
		        pti->pt_flags |= PF_WCOLL;
		else
		        pti->pt_selw = u.u_procp;
		break;

	}
	return(0);
}



ptcwakeup(tp, flag)
register struct tty *tp;
{	register struct pt_ioctl *pti;

	pti = &pts_ioctl[tp - pts_tty];
	pti->pt_flags &= ~PF_WTIMER;

	if (flag & FREAD) {
	        if (pti->pt_selr) {
		        selwakeup(pti->pt_selr, pti->pt_flags & PF_RCOLL);
			pti->pt_selr = 0;
			pti->pt_flags &= ~PF_RCOLL;
		}
		if (pti->pt_flags & PF_IASLP) {
		        pti->pt_flags &= ~PF_IASLP;
			wakeup((caddr_t)&tp->t_outq.c_cf);
		}
	}
	if (flag & FWRITE) {
	        if (pti->pt_selw) {
		        selwakeup(pti->pt_selw, pti->pt_flags & PF_WCOLL);
			pti->pt_selw = 0;
			pti->pt_flags &= ~PF_WCOLL;
		}
		if (pti->pt_flags & PF_OASLP) {
		        pti->pt_flags &= ~PF_OASLP;
			wakeup((caddr_t)&tp->t_rawq.c_cf);
		}
	}
}


ptctimer()
{	register struct tty *tp;
	register struct tty *ltp;
	register struct pt_ioctl *pti;

	pti = pts_ioctl;
	ltp = &pts_tty[v.v_npty];

	for (tp = pts_tty; tp < ltp; pti++, tp++) {
		if (pti->pt_flags & PF_WTIMER)
		        ptcwakeup(tp, FREAD);
	}
	timeout(ptctimer, (caddr_t)0, v.v_hz / 6);
}
