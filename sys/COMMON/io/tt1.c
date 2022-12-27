#ifndef lint	/* .../sys/COMMON/io/tt1.c */
#define _AC_NAME tt1_c
#define _AC_NO_MAIN "@(#) Copyright (c) 1987 Apple Computer, Inc., 1985-87 UniSoft Corporation, All Rights Reserved.  {Apple version 2.5 90/03/27 14:15:50}"
#include <apple_notice.h>

#ifdef _AC_HISTORY
  static char *sccsid = "@(#)Copyright Apple Computer 1987\tVersion 2.5 of tt1.c on 90/03/27 14:15:50";
#endif		/* _AC_HISTORY */
#endif		/* lint */

#define _AC_MODS
/*	@(#)tt1.c	UniPlus 2.1.6	*/
/*
 * Line discipline 0
 * No Virtual Terminal Handling
 */

#include "compat.h"
#include "sys/param.h"
#include "sys/types.h"
#include "sys/time.h"
#include "sys/mmu.h"
#include "sys/page.h"
#include "sys/systm.h"
#include "sys/conf.h"
#include "sys/signal.h"
#include "sys/user.h"
#include "sys/errno.h"
#include "sys/region.h"
#include "sys/proc.h"
#include "sys/file.h"
#include "sys/ioctl.h"
#include "sys/tty.h"
#include "sys/termio.h"
#include "sys/sysinfo.h"
#include "sys/var.h"
#include "sys/reg.h"
#include "sys/uio.h"

extern char partab[];

#define sigbit(a) (1<<(a-1))

/*
 * routine called on first teletype open.
 * establishes a process group for distribution
 * of quits and interrupts from the tty.
 */
#ifdef POSIX
ttopen(tp, oflag)
#else /* !POSIX */
ttopen(tp)
#endif /* POSIX */
register struct tty *tp;
#ifdef POSIX
int oflag;
#endif /* POSIX */
{
	register struct proc *pp;
	register struct user *up;

	up = &u;
	pp = up->u_procp;
#ifdef POSIX
	if (oflag & FNOCTTY)
		;
	else if ((pp->p_pid == pp->p_pgrp)
#else /* !POSIX */
	if ((pp->p_pid == pp->p_pgrp)
#endif /* POSIX */
	 && (pp->p_ttyp == NULL)
#ifdef POSIX
	 && ((tp->t_pgrp == 0) || (oflag & FGETCTTY))) {
#else /* !POSIX */
	 && (tp->t_pgrp == 0)) {
#endif /* POSIX */
		pp->p_ttyp = &tp->t_pgrp;
		tp->t_pgrp = pp->p_pgrp;
		pp->p_flag |= SPGRPL;
	}
	ttioctl(tp, LDOPEN, 0, 0);
	tp->t_state &= ~WOPEN;
	tp->t_state |= ISOPEN;
	tp->t_maxrawq = TTYHOG;
}

ttclose(tp)
register struct tty *tp;
{
	tp->t_state &= ~WOPEN;
	if ((tp->t_state&ISOPEN) == 0)
		return;
	tp->t_state &= ~(ISOPEN);
	tp->t_lflag &= ~(TOSTOP);
	tp->t_pgrp = 0;
	if (u.u_procp->p_ttyp == &tp->t_pgrp)
	    u.u_procp->p_ttyp = NULL;
	ttioctl(tp, LDCLOSE, 0, 0);
}

/*
 * Called from device's read routine after it has
 * calculated the tty-structure given as argument.
 */
ttread(tp, uio)
register struct tty *tp;
register struct uio *uio;
{
	register struct clist *tq;
	register struct iovec *iov;
	int error = 0;

	if (uio->uio_iovcnt == 0)
		goto done;

	if (u.u_procp->p_flag & SPGRP42) {
		/*
		 * Hang process if it's in the background.
		 */
		while (u.u_procp->p_ttyp == &tp->t_pgrp &&
		    u.u_procp->p_pgrp != tp->t_pgrp) {
			if ((u.u_procp->p_sigignore & sigbit(SIGTTIN)) ||
			   (P_SIGMASK(u.u_procp) & sigbit(SIGTTIN)) )
				return (EIO);
#ifdef POSIX
			if ((u.u_procp->p_compatflags & COMPAT_POSIXFUS)
				&& orphanage(u.u_procp))
				return (EIO);
#endif /* POSIX */
			signal(u.u_procp->p_pgrp, SIGTTIN);
			(void) sleep((caddr_t)&lbolt, TTIPRI);
		}
	}

	tq = &tp->t_canq;
	if (tq->c_cc == 0)
		canon(tp);

#ifdef _THIS_IS_A_BUG
#else
	if (u.u_error)
		return(u.u_error);
#endif

nextiov:
	iov = uio->uio_iov;
	while (iov->iov_len != 0 && error == 0) {
		if (iov->iov_len >= CLSIZE) {
			register n;
			register struct cblock *cp;

			if ((cp = getcb(tq)) == NULL)
				break;
			n = MIN(iov->iov_len,
				(unsigned)(cp->c_last - cp->c_first));
			error = copyout((caddr_t)&cp->c_data[cp->c_first],
			    (caddr_t)iov->iov_base, (uint) n);
			putcf((struct cblock *)cp);
			iov->iov_base += n;
			iov->iov_len -= n;
			uio->uio_resid -= n;
			uio->uio_offset += n;
		} else {
			register c;

			if ((c = getc(tq)) < 0)
				break;
			uio->uio_resid = iov->iov_len;
			error = ureadc(c, uio);
		}
	}
	uio->uio_iov++;
	uio->uio_iovcnt--;
	if ((error == 0) && (uio->uio_iovcnt > 0))
		goto nextiov;
done:
	if (tp->t_state&TBLOCK) {
		if (tp->t_rawq.c_cc<TTXOLO) {
			(*tp->t_proc)(tp, T_UNBLOCK);
		}
	}
	return(error);
}

/*
 * Called from device's write routine after it has
 * calculated the tty-structure given as argument.
 */
ttwrite(tp, uio)
register struct tty *tp;
register struct uio *uio;
{
	register struct cblock *cp;
	register struct iovec *iov;
	register a, n;
	register int error = 0;
	int	cnt;

	if (uio->uio_iovcnt == 0)
		goto done;
	if (!(tp->t_state&CARR_ON))
		return(EIO);

	if (u.u_procp->p_flag & SPGRP42) {
		/*
		 * Hang the process if it's in the background.
		 */
		while (u.u_procp->p_ttyp == &tp->t_pgrp &&
		    u.u_procp->p_pgrp != tp->t_pgrp &&
		    (tp->t_lflag&TOSTOP) &&
		    !(u.u_procp->p_sigignore & sigbit(SIGTTOU)) &&
		    !(P_SIGMASK(u.u_procp) & sigbit(SIGTTOU)) ) {
#ifdef POSIX
			if ((u.u_procp->p_compatflags & COMPAT_POSIXFUS)
				&& orphanage(u.u_procp))
				return (EIO);
#endif /* POSIX */
			signal(u.u_procp->p_pgrp, SIGTTOU);
			(void) sleep((caddr_t)&lbolt, TTOPRI);
		}
	}
	a = tthiwat[tp->t_cflag&CBAUD];

nextiov:
	iov = uio->uio_iov;
	cnt = iov->iov_len;

	while (iov->iov_len) {
		while (tp->t_outq.c_cc > a) {
			spltty();
			(*tp->t_proc)(tp, T_OUTPUT);
			if (tp->t_state & TS_NBIO) {
				SPL0();
				if ((iov->iov_len == cnt) &&
				    (u.u_procp->p_compatflags & COMPAT_BSDNBIO))
					error	= EWOULDBLOCK;
				return(error);
			}
			/*
			 * For non-interrupting output devices sleep
			 * only when characters are still pending.
			 */
			if (tp->t_state&(TIMEOUT|TTSTOP|BUSY)) {
				tp->t_state |= OASLP;
				(void) sleep((caddr_t)&tp->t_outq, TTOPRI);
			}
			SPL0();
		}
		if (iov->iov_len >= (CLSIZE/4)) {
		        spltty();

			while ((cp = getcf()) == NULL) {
			        if (tp->t_state & TS_NBIO) {
				        SPL0();
					if ((iov->iov_len == cnt) &&
					    (u.u_procp->p_compatflags & COMPAT_BSDNBIO))
					        error = EWOULDBLOCK;
					return(error);
				}
			        cfreelist.c_flag = 1;
				(void) sleep((caddr_t)&cfreelist, TTOPRI);
			}
			SPL0();

			n = MIN(iov->iov_len, (unsigned)cp->c_last);
			error = copyin((caddr_t)iov->iov_base,
			    (caddr_t)cp->c_data, (unsigned) n);
			if (error) {
				putcf((struct cblock *)cp);
				break;
			}
			/*
			 * Put trailing '\n' in a separate cblock
			 */
			if (n == iov->iov_len && cp->c_data[n-1] == '\n' && n >= 3)
				n--;
			iov->iov_base += n;
			iov->iov_len -= n;
			uio->uio_resid -= n;
			uio->uio_offset += n;
			cp->c_last = n;
			ttxput(tp, cp, n);
		} else {
			if ((n = uwritec(uio)) < 0) {
				error = EFAULT;
				break;
			}
		        spltty();

			if (tp->t_outq.c_cl == NULL ||
			   (tp->t_outq.c_cl->c_last == cfreelist.c_size)) {
			        while (cfreelist.c_next == NULL) {
				    if (tp->t_state & TS_NBIO) {
				        SPL0();
					if ((iov->iov_len == cnt) &&
					    (u.u_procp->p_compatflags & COMPAT_BSDNBIO))
					        error = EWOULDBLOCK;
					return(error);
				    }
				    cfreelist.c_flag = 1;
				    (void) sleep((caddr_t)&cfreelist, TTOPRI);
				}
			}
			ttxput(tp, n, 0);
			SPL0();
		}
	}
	uio->uio_iov++;
	uio->uio_iovcnt--;
	if ((error == 0) && (uio->uio_iovcnt > 0))
		goto nextiov;
done:
	(void) spltty();
	if (!(tp->t_state&BUSY))
		(*tp->t_proc)(tp, T_OUTPUT);
	SPL0();

	return(error);
}

/*
 * Place a character on raw TTY input queue, putting in delimiters
 * and waking up top half as needed.
 * Also echo if required.
 */
#define	LCLESC	0400

ttin(tp, code)
register struct tty *tp;
{
	register c;
	register flg;
	register char *cp;
	ushort nchar, nc;

	if (code == L_BREAK){
		signal(tp->t_pgrp, SIGINT);
		ttyflush(tp, (FREAD|FWRITE));
		return;
	}
	nchar = tp->t_rbuf.c_size - tp->t_rbuf.c_count;
	/* reinit rx control block */
	tp->t_rbuf.c_count = tp->t_rbuf.c_size;
	if (nchar==0)
		return;
	flg = tp->t_iflag;
	nc = nchar;
	cp = tp->t_rbuf.c_ptr;
	if (nc < cfreelist.c_size || (flg & (INLCR|IGNCR|ICRNL|IUCLC))) {
			/* must do per character processing */
		for ( ;nc--; cp++) {
			c = *cp;
			if (c == '\n' && flg&INLCR)
				*cp = c = '\r';
			else if (c == '\r')
				if (flg&IGNCR)
					continue;
				else if (flg&ICRNL)
					*cp = c = '\n';
			if (flg&IUCLC && 'A' <= c && c <= 'Z')
				c += 'a' - 'A';
			if (putc(c, &tp->t_rawq))
				continue;
			sysinfo.rawch++;
		}
		cp = tp->t_rbuf.c_ptr;
	} else {
		/* may do block processing */
		struct cblock *cbp;
		(void) putcb(CMATCH(cp), &tp->t_rawq);
		sysinfo.rawch += nc;
		/* allocate new rx buffer */
		if ( (cbp = getcf()) == NULL) {
			tp->t_rbuf.c_ptr = NULL;
			return;
		}
		tp->t_rbuf.c_ptr = cbp->c_data;
		tp->t_rbuf.c_count = cfreelist.c_size;
		tp->t_rbuf.c_size = cfreelist.c_size;
	}


	if (tp->t_rawq.c_cc > TTXOHI) {
		if (flg&IXOFF && !(tp->t_state&TBLOCK))
			(*tp->t_proc)(tp, T_BLOCK);
		if (tp->t_rawq.c_cc > tp->t_maxrawq) {
			ttyflush(tp, FREAD);
			return;
		}
	}
	flg = lobyte(tp->t_lflag);
	if (tp->t_outq.c_cc > (tthiwat[tp->t_cflag&CBAUD] + TTECHI))
		flg &= ~(ECHO|ECHOK|ECHONL|ECHOE);
	if (flg) while (nchar--) {
#ifdef POSIX
		c = *cp++ & 0377;
		if (flg&ISIG && c != _POSIX_VDISABLE) {
#else /* !POSIX */
		c = *cp++;
		if (flg&ISIG) {
#endif /* POSIX */
			if (c == tp->t_cc[VINTR]) {
				signal(tp->t_pgrp, SIGINT);
				if (!(flg&NOFLSH))
					ttyflush(tp, (FREAD|FWRITE));
				continue;
			}
			if (c == tp->t_cc[VQUIT]) {
				signal(tp->t_pgrp, SIGQUIT);
				if (!(flg&NOFLSH))
					ttyflush(tp, (FREAD|FWRITE));
				continue;
			}
			if (c == tp->t_cc[VSWTCH]) {
				if (!(flg&NOFLSH))
					ttyflush(tp, FREAD);
				(*tp->t_proc)(tp, T_SWTCH);
				continue;
			}
			if (c == tp->tt_suspc) {
				if ((flg & NOFLSH) == 0)
					ttyflush(tp, FREAD);
				signal(tp->t_pgrp, SIGTSTP);
				continue;
			}
		}
		if (flg&ICANON) {
#ifdef POSIX
			if (c == _POSIX_VDISABLE)
				;
			else
#endif /* POSIX */
			if (c == '\n') {
				if (flg&ECHONL)
					flg |= ECHO;
				tp->t_delct++;
			} else if (c == tp->t_cc[VEOL] || c == tp->t_cc[VEOL2])
				tp->t_delct++;
			if (!(tp->t_state&CLESC)) {
				if (c == '\\')
					tp->t_state |= CLESC;
#ifdef POSIX
				if (c == _POSIX_VDISABLE)
					;
				else
#endif /* POSIX */
				if (c == tp->t_cc[VERASE] && flg&ECHOE) {
					if (flg&ECHO)
						ttxput(tp, '\b', 0);
					flg |= ECHO;
					ttxput(tp, ' ', 0);
					c = '\b';
				} else if (c == tp->t_cc[VKILL] && flg&ECHOK) {
					if (flg&ECHO)
						ttxput(tp, c, 0);
					flg |= ECHO;
					c = '\n';
				} else if (c == tp->t_cc[VEOF]) {
					flg &= ~ECHO;
					tp->t_delct++;
				}
			} else {
				if (c != '\\' || (flg&XCASE))
					tp->t_state &= ~CLESC;
			}
		}
		if (flg&ECHO) {
			ttxput(tp, c, 0);
			(*tp->t_proc)(tp, T_OUTPUT);
		}
	}
	if (!(flg&ICANON)) {
		tp->t_state &= ~RTO;
		if (tp->t_rawq.c_cc >= tp->t_cc[VMIN])
			tp->t_delct = 1;
		else if (tp->t_cc[VTIME]) {
			if (!(tp->t_state&TACT))
				tttimeo(tp);
		}
	}
	if (tp->t_delct)
		ttwakeup(tp);
}

/*
 * Scan a list of characters and assure that they require no
 * post processing
 */
ttxchk(ncode, cp)
register short ncode;
register unsigned char *cp;
{
	register c, n;

	n = 0;
	ncode--;
	do {
		c = *cp++;
		if (c & 0200)
			return(-1);
		c = partab[c] & 077;
		if (c == 0)
			n++;
		else if (c != 1)
			return(-1);
	} while (--ncode != -1);
	return(n);
}

/*
 * Put character(s) on TTY output queue, adding delays,
 * expanding tabs, and handling the CR/NL bit.
 * It is called both from the base level for output, and from
 * interrupt level for echoing.
 */
/* VARARGS1 */
ttxput(tp, ucp, ncode)
register struct tty *tp;
register ncode;
union {
	struct ch {		/*  machine dependent union */
		char dum[3];
		unsigned char theaddr;
	} ch;
	int thechar;
	struct cblock *ptr;
} ucp;
{
	register struct clist *outqp;
	register unsigned char *cp;
	register c, flg, ctype;
	register char *colp;
	struct cblock *scf;
	int cs;

	flg = tp->t_oflag;
	outqp = &tp->t_outq;
	if (ncode == 0) {
		if (!(flg&OPOST)) {
			sysinfo.outch++;
			(void) putc(ucp.thechar, outqp);
			return;
		}
		ncode++;
		cp = (unsigned char *)&ucp.ch.theaddr;
		scf = NULL;
	} else {
		if (!(flg&OPOST)) {
			sysinfo.outch += ncode;
			(void) putcb(ucp.ptr, outqp);
			return;
		}
		cp = (unsigned char *)&ucp.ptr->c_data[ucp.ptr->c_first];
		scf = ucp.ptr;
	}
	if ((tp->t_lflag&XCASE)==0 && (flg&OLCUC)==0) {
		colp = &tp->t_col;
		if (ncode > 1 && (c = ttxchk(ncode, cp)) >= 0) {
			(*colp) += c;
			sysinfo.outch += ncode;
			(void) putcb(ucp.ptr, outqp);
			return;
		}
		while (ncode--) {
			ctype = partab[c = *cp++] & 077;
			if (ctype==0) {
				(*colp)++;
				sysinfo.outch++;
				(void) putc(c, outqp);
				continue;
			}
			else if (ctype==1) {
				sysinfo.outch++;
				(void) putc(c, outqp);
				continue;
			}
			if (c >= 0200) {
				if (c == QESC)
					(void) putc(QESC, outqp);
				sysinfo.outch++;
				(void) putc(c, outqp);
				continue;
			}
			cs = c;
			/*
			 * Calculate delays.
			 * The numbers here represent clock ticks
			 * and are not necessarily optimal for all terminals.
			 * The delays are indicated by characters above 0200.
			 */
			c = 0;
			switch (ctype) {

			case 0:	/* ordinary */
				(*colp)++;

			case 1:	/* non-printing */
				break;

			case 2:	/* backspace */
				if (flg&BSDLY)
					c = 2;
				if (*colp)
					(*colp)--;
				break;

			case 3:	/* line feed */
				if (flg&ONLRET)
					goto qcr;
				if (flg&ONLCR) {
					if (!(flg&ONOCR && *colp==0)) {
						sysinfo.outch++;
						(void) putc('\r', outqp);
					}
					goto qcr;
				}
			qnl:
				if (flg&NLDLY)
					c = 5;
				break;

			case 4:	/* tab */
				c = 8 - ((*colp)&07);
				*colp += c;
				ctype = flg&TABDLY;
				if (ctype == TAB0) {
					c = 0;
				} else if (ctype == TAB1) {
					if (c < 5)
						c = 0;
				} else if (ctype == TAB2) {
					c = 2;
				} else if (ctype == TAB3) {
					sysinfo.outch += c;
					do
						(void) putc(' ', outqp);
					while (--c);
					continue;
				}
				break;

			case 5:	/* vertical tab */
				if (flg&VTDLY)
					c = 0177;
				break;

			case 6:	/* carriage return */
				if (flg&OCRNL) {
					cs = '\n';
					goto qnl;
				}
				if (flg&ONOCR && *colp == 0)
					continue;
			qcr:
				ctype = flg&CRDLY;
				if (ctype == CR1) {
					if (*colp)
						c = MAX((unsigned)((*colp>>4) + 3), 6);
				} else if (ctype == CR2) {
					c = 5;
				} else if (ctype == CR3) {
					c = 9;
				}
				*colp = 0;
				break;

			case 7:	/* form feed */
				if (flg&FFDLY)
					c = 0177;
				break;
			}
			sysinfo.outch++;
			(void) putc(cs, outqp);
			if (c) {
				if ((c < 32) && flg&OFILL) {
					if (flg&OFDEL)
						cs = 0177;
					else
						cs = 0;
					(void) putc(cs, outqp);
					if (c > 3)
						(void) putc(cs, outqp);
				} else {
					(void) putc(QESC, outqp);
					(void) putc(c|0200, outqp);
				}
			}
		}
	} else
	while (ncode--) {
		c = *cp++;
		if (c >= 0200) {
	/* spl5-0 */
			if (c == QESC)
				(void) putc(QESC, outqp);
			sysinfo.outch++;
			(void) putc(c, outqp);
			continue;
		}
		/*
		 * Generate escapes for upper-case-only terminals.
		 */
		if (tp->t_lflag&XCASE) {
			colp = "({)}!|^~'`\\\\";
			while(*colp++)
				if (c == *colp++) {
					ttxput(tp, '\\'|0200, 0);
					c = colp[-2];
					break;
				}
			if ('A' <= c && c <= 'Z')
				ttxput(tp, '\\'|0200, 0);
		}
		if (flg&OLCUC && 'a' <= c && c <= 'z')
			c += 'A' - 'a';
		cs = c;
		/*
		 * Calculate delays.
		 * The numbers here represent clock ticks
		 * and are not necessarily optimal for all terminals.
		 * The delays are indicated by characters above 0200.
		 */
		ctype = partab[c];
		colp = &tp->t_col;
		c = 0;
		switch (ctype&077) {

		case 0:	/* ordinary */
			(*colp)++;

		case 1:	/* non-printing */
			break;

		case 2:	/* backspace */
			if (flg&BSDLY)
				c = 2;
			if (*colp)
				(*colp)--;
			break;

		case 3:	/* line feed */
			if (flg&ONLRET)
				goto cr;
			if (flg&ONLCR) {
				if (!(flg&ONOCR && *colp==0)) {
					sysinfo.outch++;
					(void) putc('\r', outqp);
				}
				goto cr;
			}
		nl:
			if (flg&NLDLY)
				c = 5;
			break;

		case 4:	/* tab */
			c = 8 - ((*colp)&07);
			*colp += c;
			ctype = flg&TABDLY;
			if (ctype == TAB0) {
				c = 0;
			} else if (ctype == TAB1) {
				if (c < 5)
					c = 0;
			} else if (ctype == TAB2) {
				c = 2;
			} else if (ctype == TAB3) {
				sysinfo.outch += c;
				do
					(void) putc(' ', outqp);
				while (--c);
				continue;
			}
			break;

		case 5:	/* vertical tab */
			if (flg&VTDLY)
				c = 0177;
			break;

		case 6:	/* carriage return */
			if (flg&OCRNL) {
				cs = '\n';
				goto nl;
			}
			if (flg&ONOCR && *colp == 0)
				continue;
		cr:
			ctype = flg&CRDLY;
			if (ctype == CR1) {
				if (*colp)
					c = MAX((unsigned)((*colp>>4) + 3), 6);
			} else if (ctype == CR2) {
				c = 5;
			} else if (ctype == CR3) {
				c = 9;
			}
			*colp = 0;
			break;

		case 7:	/* form feed */
			if (flg&FFDLY)
				c = 0177;
			break;
		}
		sysinfo.outch++;
		(void) putc(cs, outqp);
		if (c) {
			if ((c < 32) && flg&OFILL) {
				if (flg&OFDEL)
					cs = 0177;
				else
					cs = 0;
				(void) putc(cs, outqp);
				if (c > 3)
					(void) putc(cs, outqp);
			} else {
				(void) putc(QESC, outqp);
				(void) putc(c|0200, outqp);
			}
		}

	}
	if (scf != NULL)
		putcf(scf);
}

/*
 * Get next packet from output queue.
 * Called from xmit interrupt complete.
 */

ttout(tp)
register struct tty *tp;
{
	register struct ccblock *tbuf;
	register c;
	register char *cptr;
	register retval;
	register struct clist *outqp;
	extern ttrstrt();

	outqp = &tp->t_outq;
	if (tp->t_state&TTIOW && outqp->c_cc==0) {
		tp->t_state &= ~TTIOW;
		wakeup((caddr_t)&tp->t_oflag);
	}
delay:
	tbuf = &tp->t_tbuf;
#ifdef POSIX
	if (tp->t_lflag & 0x7f00) {
		if (tbuf->c_ptr) {
			putcf(CMATCH(tbuf->c_ptr));
			tbuf->c_ptr = NULL;
		}
		tp->t_state |= TIMEOUT;
		timeout(ttrstrt, (caddr_t)tp,
			(int)(((tp->t_lflag >> 8) & 0x7f) + 6));
		tp->t_lflag &= ~0x7f00;
		return(0);
	}
#else /* !POSIX */
	if (hibyte(tp->t_lflag)) {
		if (tbuf->c_ptr) {
			putcf(CMATCH(tbuf->c_ptr));
			tbuf->c_ptr = NULL;
		}
		tp->t_state |= TIMEOUT;
		timeout(ttrstrt, (caddr_t)tp,
			(int)((hibyte(tp->t_lflag)&0177)+6));
		hibyte(tp->t_lflag) = 0;
		return(0);
	}
#endif /* POSIX */
	retval = 0;
	if (!(tp->t_oflag&OPOST)) {
		struct cblock *cbp;

		if (tbuf->c_ptr)
			putcf(CMATCH(tbuf->c_ptr));
		if ((cbp = getcb(outqp)) == NULL) {
			tbuf->c_ptr = NULL;
			goto out;
		}
		tbuf->c_count = cbp->c_last - cbp->c_first;
		tbuf->c_size = cbp->c_last;
		tbuf->c_ptr = &cbp->c_data[cbp->c_first];
		retval = CPRES;
	} else {		/* watch for timing	*/
		if (tbuf->c_ptr == NULL) {
			struct cblock *cbp;

			if ( (cbp = getcf()) == NULL) {
				tbuf->c_ptr = NULL;
				goto out;
			}
			tbuf->c_ptr = cbp->c_data;
		}
		tbuf->c_count = 0;
		cptr = tbuf->c_ptr;
		while ((c=getc(outqp)) >= 0) {
			if (c == QESC) {
				if ((c = getc(outqp)) < 0)
					break;
				if (c > 0200) {
#ifdef POSIX
					tp->t_lflag &= ~0x7f00;
					tp->t_lflag |= ((c & 0x7f) << 8);
#else /* !POSIX */
					hibyte(tp->t_lflag) = c;
#endif /* POSIX */
					if (!retval)
						goto delay;
					break;
				}
			}
			retval = CPRES;
			*cptr++ = c;
			tbuf->c_count++;
			if (tbuf->c_count >= cfreelist.c_size)
				break;
		}
		tbuf->c_size = tbuf->c_count;
	}
out:
	if (outqp->c_cc<=ttlowat[tp->t_cflag&CBAUD]) {
		if (tp->t_state & OASLP) {
			tp->t_state &= ~OASLP;
			wakeup((caddr_t)outqp);
		}
		if (tp->t_wsel) {
			selwakeup(tp->t_wsel, (int) (tp->t_state & TS_WCOLL));
			tp->t_wsel = 0;
			tp->t_state &= ~TS_WCOLL;
		}
	}
	return(retval);
}

tttimeo(tp)
register struct tty *tp;
{
	tp->t_state &= ~TACT;
	if (tp->t_lflag&ICANON || tp->t_cc[VTIME] == 0)
		return;
	if (tp->t_rawq.c_cc == 0 && tp->t_cc[VMIN])
		return;
	if (tp->t_state&RTO) {
		tp->t_delct = 1;
		ttwakeup(tp);
	} else {
		tp->t_state |= RTO|TACT;
		timeout(tttimeo, (caddr_t)tp,
			(int)(tp->t_cc[VTIME]*(short)((short)v.v_hz/10)));
	}
}

/*
 * I/O control interface
 */
/* ARGSUSED */
ttioctl(tp, cmd, arg, mode)
register struct tty *tp;
{
	ushort	chg;

	switch(cmd) {
	case LDOPEN:
		if (tp->t_rbuf.c_ptr == NULL) {
			struct cblock *cbp;

			/* allocate RX buffer */
			spltty();
			while ( (cbp = getcf()) == NULL) {
				tp->t_rbuf.c_ptr = NULL;
				cfreelist.c_flag = 1;
				(void) sleep((caddr_t)&cfreelist, TTOPRI);
			}
			SPL0();
			tp->t_rbuf.c_ptr = cbp->c_data;
			tp->t_rbuf.c_count = cfreelist.c_size;
			tp->t_rbuf.c_size  = cfreelist.c_size;
			(*tp->t_proc)(tp, T_INPUT);
		}
		break;

	case LDCLOSE:
		(void) spltty();
		(*tp->t_proc)(tp, T_RESUME);
		SPL0();
		ttywait(tp);
		if (tp->t_state & (ISOPEN|WOPEN)) {
			break;
		}
		ttyflush(tp, (FREAD|FWRITE));
		if (tp->t_tbuf.c_ptr) {
			putcf(CMATCH(tp->t_tbuf.c_ptr));
			tp->t_tbuf.c_ptr = NULL;
			tp->t_tbuf.c_count = 0;
			tp->t_tbuf.c_size = 0;
		}
		if (tp->t_rbuf.c_ptr) {
			putcf(CMATCH(tp->t_rbuf.c_ptr));
			tp->t_rbuf.c_ptr = NULL;
			tp->t_rbuf.c_count = 0;
			tp->t_rbuf.c_size = 0;
		}
		tp->t_tmflag = 0;
		break;

	case LDCHG:
		chg = tp->t_lflag^arg;
		if (!(chg&ICANON))
			break;
		(void) spltty();
		if (tp->t_canq.c_cc) {
			if (tp->t_rawq.c_cc) {
				tp->t_canq.c_cc += tp->t_rawq.c_cc;
				tp->t_canq.c_cl->c_next = tp->t_rawq.c_cf;
				tp->t_canq.c_cl = tp->t_rawq.c_cl;
			}
			tp->t_rawq = tp->t_canq;
			tp->t_canq = ttnulq;
		}
		tp->t_delct = tp->t_rawq.c_cc;
		wakeup((caddr_t)&tp->t_rawq);
		SPL0();
		break;

	default:
		break;
	}
}
