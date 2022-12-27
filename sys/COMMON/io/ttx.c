#ifndef lint	/* .../sys/COMMON/io/ttx.c */
#define _AC_NAME ttx_c
#define _AC_NO_MAIN "@(#) Copyright (c) 1987 Apple Computer, Inc., 1985-87 UniSoft Corporation, All Rights Reserved.  {Apple version 2.2 90/05/03 23:34:05}"
#include <apple_notice.h>

#ifdef _AC_HISTORY
  static char *sccsid = "@(#)Copyright Apple Computer 1987\tVersion 2.2 of ttx.c on 90/05/03 23:34:05";
#endif		/* _AC_HISTORY */
#endif		/* lint */

#define _AC_MODS
/*	@(#)ttx.c	UniPlus 2.1.2	*/
/*
 *	Streams character device support
 *
 *	Copyright 1986 Unisoft Corporation of Berkeley CA
 *
 *
 *	UniPlus Source Code. This program is proprietary
 *	with Unisoft Corporation and is not to be reproduced
 *	or used in any manner except as authorized in
 *	writing by Unisoft.
 *
 */

#include "sys/param.h"
#include "sys/types.h"
#include "sys/dir.h"
#include "sys/signal.h"
#include "sys/errno.h"
#include "sys/mmu.h"
#include "sys/page.h"
#include "sys/time.h"
#include "sys/systm.h"
#include "sys/user.h"
#include "sys/file.h"
#include "sys/ioctl.h"
#include "sys/tty.h"
#include "sys/termio.h"
#include "sys/conf.h"
#include "sys/sysinfo.h"
#include "sys/var.h"
#include "sys/reg.h"
#include "sys/sysmacros.h"
#include "sys/stream.h"
#include "sys/stropts.h"
#include "sys/ttx.h"

static int ttx_rcvtimer();

ttxinit(q, tp, sz)
queue_t *q;
struct ttx *tp;
int sz;
{
	tp->t_size = sz;
	tp->t_q = q;
	q->q_ptr = (char *)tp;
	WR(q)->q_ptr = (char *)tp;
	tp->t_rm = NULL;
	tp->t_xm = NULL;
	tp->t_iflag = 0;
	tp->t_oflag = 0;
	tp->t_lflag = 0;
	tp->t_cflag = B9600|CS8|CREAD;
	if (sz) {
		if ((tp->t_rm = allocb(sz, BPRI_HI)) == NULL) {
			if (!(tp->t_state&RCV_TIME)) {
				tp->t_state |= RCV_TIME;
				timeout(ttx_rcvtimer, q, v.v_hz/16);
			}
		} else {
			tp->t_count = sz;
			(*tp->t_proc)(tp, T_INPUT);
		}
	}
}

ttx_wputp(q, m)
register queue_t *q;
register mblk_t *m;
{
	register struct ttx *tp;
	struct iocblk *iocbp;
	register struct termio *t;

	tp = (struct ttx *)(q->q_ptr);
	switch (m->b_datap->db_type) {
	case M_EXDATA:
	case M_DATA:
	case M_DELAY:
		putq(q, m);
		break;

	case M_STOP:
		(*tp->t_proc)(tp, T_SUSPEND);
		freeb(m);
		break;

	case M_START:
		(*tp->t_proc)(tp, T_RESUME);
		freeb(m);
		break;

	case M_BREAK:
		(*tp->t_proc)(tp, T_BREAK);
		freeb(m);
		break;

	case M_IOCTL:
		iocbp = (struct iocblk *)m->b_rptr;
		switch(iocbp->ioc_cmd) {
		case TCXONC:
			switch (*m->b_cont->b_rptr) {
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
			}
			m->b_datap->db_type = M_IOCACK;
			iocbp->ioc_count = 0;
			freemsg(unlinkb(m));
			qreply(q, m);
			break;

		case TCSETA:
			t = (struct termio*)(m->b_cont->b_rptr);
			tp->t_iflag = t->c_iflag;
			tp->t_oflag = t->c_oflag;
			tp->t_cflag = t->c_cflag;
			tp->t_lflag = t->c_lflag;
			(*tp->t_proc)(tp, T_PARM);
			m->b_datap->db_type = M_IOCACK;
			iocbp->ioc_count = 0;
			freemsg(unlinkb(m));
			qreply(q, m);
			return;

		case TCGETA:
			if (m->b_cont == NULL) {
				m->b_cont = allocb(sizeof(struct termio),
						BPRI_MED);
				if (m->b_cont) {
					m->b_cont->b_wptr +=
						sizeof(struct termio);
					iocbp->ioc_count =
						sizeof(struct termio);
				}
			}
			if (m->b_cont) {
				t = (struct termio*)(m->b_cont->b_rptr);
				t->c_iflag = tp->t_iflag;
				t->c_oflag = tp->t_oflag;
				t->c_cflag = tp->t_cflag;
				t->c_lflag = tp->t_lflag;
				m->b_datap->db_type = M_IOCACK;
			} else {
				m->b_datap->db_type = M_IOCNAK;
			}
			qreply(q, m);
			return;

		default:
			putq(q, m);
			break;
		}
		break;

	case M_FLUSH:
		if ((*m->b_rptr)&FLUSHR) {
			flushq(RD(q), 0);
			(*tp->t_proc)(tp, T_RFLUSH);
		}
		switch(*m->b_rptr) {
		case FLUSHR:
			qreply(q, m);
			return;

		case FLUSHRW:
			*m->b_rptr = FLUSHR;
			qreply(q, m);
			break;
	
		case FLUSHW:
			freemsg(m);
			break;

		default:
			freemsg(m);
			return;
		}
		flushq(q, 0);
		(*tp->t_proc)(tp, T_WFLUSH);
		qenable(q);
		if (tp->t_state&OASLP) {
			tp->t_state &= ~OASLP;
			wakeup(&tp->t_state);
		}
		break;
		
	default:
		freemsg(m);
	}
}

ttx_put(tp)
register struct ttx *tp;
{
	register queue_t *q;
	register mblk_t *m;

	if ((q = tp->t_q) == NULL)
		return(1);
	if ((m = tp->t_rm) == NULL)
		return(1);
	if (canput(q->q_next)) {
		tp->t_rm = NULL;
		putnext(q, m);
		if ((tp->t_rm = allocb(tp->t_size, BPRI_HI)) == NULL) {
			if (!(tp->t_state&RCV_TIME)) {
				tp->t_state |= RCV_TIME;
				timeout(ttx_rcvtimer, tp->t_q, v.v_hz/16);
			}
			return(1);
		}
		tp->t_count = tp->t_size;
	}
#ifndef	FEP
	else {
		m->b_wptr = m->b_rptr;	/* throw it away */
		tp->t_count = tp->t_size;
	}
#endif
	(*tp->t_proc)(tp, T_INPUT);
	return(0);
}

ttx_delaytimer(q)
register queue_t *q;
{
	register struct ttx *tp;

	if ((tp = (struct ttx *)q->q_ptr) == NULL)
		return;
	tp->t_state &= ~XMT_DELAY;
	qenable(q);
}

ttx_wsrvc(q)
register queue_t *q;
{
	register struct ttx *tp;
	register mblk_t *m;
	mblk_t *m1;
	struct iocblk *iocbp;
	struct termio *t;

	if ((tp = (struct ttx *)q->q_ptr) == NULL)
		return;
	if (tp->t_xm) {				/* poke it */
		(*tp->t_proc)(tp, T_OUTPUT);
		return;
	}
	while (tp->t_xm == NULL && !(tp->t_state&(XMT_DELAY|TIMEOUT)) &&
	       (m = getq(q))) {
		switch(m->b_datap->db_type) {
		case M_DELAY:
			timeout(ttx_delaytimer, q, (int)*m->b_rptr);
			freemsg(m);
			tp->t_state |= XMT_DELAY;
			return;

		case M_EXDATA:
		case M_DATA:
			while (m && m->b_rptr >= m->b_wptr) {
				m1 = unlinkb(m);
				freeb(m);
				m = m1;
			}
			if (m == NULL)
				break;
			tp->t_xm = m;
			(*tp->t_proc)(tp, T_OUTPUT);
			return;

		case M_IOCTL:
			iocbp = (struct iocblk *)m->b_rptr;
			m->b_datap->db_type = M_IOCACK;
			switch(iocbp->ioc_cmd) {
			case TCSBRK:
				(*tp->t_proc)(tp, T_BREAK);
				iocbp->ioc_count = 0;
				freemsg(unlinkb(m));
				break;

			case TCSETAF:
				(*tp->t_proc)(tp, T_RFLUSH);
				flushq(RD(q), 0);
				putctl1(RD(q)->q_next, M_FLUSH, FLUSHR);
			case TCSETA:
			case TCSETAW:
				t = (struct termio*)(m->b_cont->b_rptr);
				tp->t_iflag = t->c_iflag;
				tp->t_oflag = t->c_oflag;
				tp->t_cflag = t->c_cflag;
				tp->t_lflag = t->c_lflag;
				(*tp->t_proc)(tp, T_PARM);
				iocbp->ioc_count = 0;
				freemsg(unlinkb(m));
				break;

			default:
				if (tp->t_ioctl == NULL ||
					(*tp->t_ioctl)(tp, iocbp, m))
					m->b_datap->db_type = M_IOCNAK;
				break;
			}
			qreply(q, m);
			break;

		default:
			freemsg(m);
		}
	}
	if (m == NULL &&
	    tp->t_xm == NULL &&
 	    !(tp->t_state&(TIMEOUT|XMT_DELAY)) &&
	    tp->t_state&OASLP) {
		tp->t_state &= ~OASLP;
		wakeup(&tp->t_state);
	}
}

ttx_rsrvc(q)
register queue_t *q;
{
	register struct ttx *tp;
	register mblk_t *m;

	if ((tp = (struct ttx *)q->q_ptr) == NULL)
		return;
	if (tp->t_rm == NULL && tp->t_size) {
		if ((tp->t_rm = allocb(tp->t_size, BPRI_HI)) == NULL) {
			if (!(tp->t_state&RCV_TIME)) {
				tp->t_state |= RCV_TIME;
				timeout(ttx_rcvtimer, q, v.v_hz/16);
			}
		} else {
			tp->t_count = tp->t_size;
			(*tp->t_proc)(tp, T_INPUT);
		}
	}
	while (m = getq(q)) {	/* shouldn't ever be anything queued ... */
		freemsg(m);
	}
}

static
ttx_brktimer(tp)
register struct ttx *tp;
{
	tp->t_state &= ~TIMEOUT;
	(*tp->t_proc)(tp, T_TIME);
}

ttx_break(tp)
register struct ttx *tp;
{
	tp->t_state |= TIMEOUT;
	timeout(ttx_brktimer, tp, v.v_hz/2);
}

static
ttx_rcvtimer(q)
register queue_t *q;
{
	register struct ttx *tp;

	if ((tp = (struct ttx *)(q->q_ptr)) == NULL)
		return;
	tp->t_state &= ~RCV_TIME;
	if (tp->t_rm == NULL)
		qenable(q);
}

ttx_close(tp)
register struct ttx *tp;
{
	register queue_t *q;

	q = tp->t_q;
	(*tp->t_proc)(tp, T_RESUME);
	while (tp->t_xm) {
		tp->t_state |= OASLP;
		(void) sleep((caddr_t)&tp->t_state, TTOPRI|PCATCH);
	}
	flushq(WR(q), 0);
	flushq(q, 0);
	if (tp->t_rm) {
		freemsg(tp->t_rm);
		tp->t_rm = NULL;
	}
	tp->t_state &= ~ISOPEN;
	q->q_ptr = NULL;
	WR(q)->q_ptr = NULL;
	tp->t_q = NULL;
}

ttx_sighup(tp)
register struct ttx *tp;
{
	register queue_t *q;

	if ((q = tp->t_q) == NULL)
		return;
	flushq(q, 0);
	flushq(WR(q), 0);
	(*tp->t_proc)(tp, T_RFLUSH);
	(*tp->t_proc)(tp, T_WFLUSH);
	putctl1(q->q_next, M_FLUSH, FLUSHRW);
	putctl(q->q_next, M_HANGUP);
	if (tp->t_state&OASLP) {
		wakeup(&tp->t_state);
	}
}
