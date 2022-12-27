#ifndef lint	/* .../sys/COMMON/io/shlr.c */
#define _AC_NAME shlr_c
#define _AC_NO_MAIN "@(#) Copyright (c) 1987 Apple Computer, Inc., 1985-87 UniSoft Corporation, All Rights Reserved.  {Apple version 2.1 89/10/13 18:39:09}"
#include <apple_notice.h>

#ifdef _AC_HISTORY
  static char *sccsid = "@(#)Copyright Apple Computer 1987\tVersion 2.1 of shlr.c on 89/10/13 18:39:09";
#endif		/* _AC_HISTORY */
#endif		/* lint */

#define _AC_MODS
/*	@(#)shlr.c	UniPlus 2.1.3	*/
/*
 *	Shell layering line discipline
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

#include <sys/param.h>
#include <sys/types.h>
#include <sys/mmu.h>
#include <sys/page.h>
#include <sys/systm.h>
#include <sys/time.h>
#include <sys/dir.h>
#include <sys/ioctl.h>
#include <sys/tty.h>
#include <sys/termio.h>
#include <sys/stream.h>
#include <sys/stropts.h>
#include <sys/signal.h>
#include <sys/region.h>
#include <sys/proc.h>
#include <sys/user.h>
#include <sys/sxt.h>
#include <sys/shl.h>

#define PUTQ(q, m) (*(q)->q_qinfo->qi_putp)((q), m)

#ifndef NULL
#define NULL 0
#endif
#define NSHLR	10
int shlr_count = NSHLR;
struct shlr  shlr_data [NSHLR];

extern int nulldev();
extern int qenable();
extern int putq();
static int shlr_rput();
static int shlr_wput();
static int shlr_open();
static int shlr_close();
static int shlr_timeout();
/*
 *	Stream interface
 */

static struct module_info shlr_rinfo = {4328, "SHLR", 0, 256, 1, 1};
static struct module_info shlr_winfo = {4328, "SHLR", 0, 256, 1, 1};
static struct qinit shlr_rq = {shlr_rput, NULL, shlr_open, shlr_close,
				nulldev, &shlr_rinfo, NULL};
static struct qinit shlr_wq = {shlr_wput, NULL, shlr_open, shlr_close,
				nulldev, &shlr_winfo, NULL};
struct streamtab shlrinfo = {&shlr_rq, &shlr_wq, NULL, NULL};

shlrinit()
{
	int i;

	for (i = 0; i < shlr_count;i++)
		shlr_data[i].sh_flag = SH_FREE;
}

static
shlr_wput(q, m)
register queue_t *q;
register mblk_t *m;
{
	register struct shlr *shlp;
	register struct iocblk *iocbp;
	
	shlp = (struct shlr *)q->q_ptr;
	switch(m->b_datap->db_type) {
	case M_IOCTL:
		iocbp = (struct iocblk *)m->b_rptr;
		switch(iocbp->ioc_cmd) {
		case SXTIOCWF:	/* wait for a switch */
			if (shlp->sh_current == 0) {
				m->b_datap->db_type = M_IOCACK;
				qreply(q, m);
			} else {
				if (shlp->sh_wait)
					freemsg(shlp->sh_wait);
				shlp->sh_wait = m;
			}
			return;
		case SXTIOCBLK:	/* block a layer */
		case SXTIOCUBLK:/* unblock a layer */
			if (iocbp->ioc_count == sizeof(int)) {
				register int i;

				i = *(int *)m->b_cont->b_rptr;
				iocbp->ioc_count = 0;
				freeb(unlinkb(m));
				if (i <= 0 || i > NLAYERS ||
					shlp->sh_layer[i] == NULL) {
					m->b_datap->db_type = M_IOCNAK;
				} else {
					register struct shl *sp;

					sp = (struct shl *)
						shlp->sh_layer[i]->q_ptr;
					m->b_datap->db_type = M_IOCACK;
					if (iocbp->ioc_cmd == SXTIOCBLK &&
						sp->s_info == S_RUNNING) {
						sp->s_info = S_BLOCKED;
					} else
					if (iocbp->ioc_cmd == SXTIOCUBLK &&
						sp->s_info == S_BLOCKED) {
						sp->s_info = S_RUNNING;
						qenable(WR(shlp->sh_layer[i]));
					}
				}
			} else {
				m->b_datap->db_type = M_IOCNAK;
			}
			qreply(q, m);
			break;

		case SXTIOCSTAT:	/* layer status */
			if (iocbp->ioc_count == sizeof(struct sxtblock)) {
				register int i;
				int x, r, mask;
				struct sxtblock *sxp;

				x = 0;
				r = 0;
				m->b_datap->db_type = M_IOCACK;
				mask = 1<<1;
				for (i = 1; i < NLAYERS; i++) {
					register queue_t *q2;

					q2 = shlp->sh_layer[i];
					if (q2) {
						if (WR(q2)->q_count) 
							x |= mask;
						while (q2->q_next) 
							q2 = q2->q_next;
						if (((struct stdata *)q2->q_ptr)
							->sd_flag&RSLEEP)
							r |= mask;
					}
					mask <<= 1;
				}
				sxp = (struct sxtblock *)m->b_cont->b_rptr;
				sxp->input = r;
				sxp->output = x;
			} else {
				m->b_datap->db_type = M_IOCNAK;
			}
			qreply(q, m);
			break;

		case SXTIOCSWTCH:	/* switch to a layer */
			if (iocbp->ioc_count == sizeof(int)) {
				register int i;

				i = *(int *)m->b_cont->b_rptr;
				iocbp->ioc_count = 0;
				freeb(unlinkb(m));
				if (i < 0 || i > NLAYERS ||
					shlp->sh_layer[i] == NULL) {
					m->b_datap->db_type = M_IOCNAK;
				} else {
					m->b_datap->db_type = M_IOCACK;
					shlr_switch(shlp, i);
				}
			} else {
				m->b_datap->db_type = M_IOCNAK;
			}
			qreply(q, m);
			break;

		case TCSETA:
		case TCSETAW:
		case TCSETAF:
		{
			register struct termio *t;

			t = (struct termio *)m->b_cont->b_rptr;
			shlp->sh_swtch = t->c_cc[VSWTCH];
		}
		default:
			putnext(q, m);
		}
		break;
	default:
		putnext(q, m);
	}
}

static
shlr_rput(q, m)
queue_t *q;
register mblk_t *m;
{
	putnext(q, m);
}

/*ARGSUSED*/			/* lint: dev, flag, and sflag unused */
static
shlr_open(q, dev, flag, sflag)
register queue_t *q;
int dev;
int flag;
int sflag;
{
	register int i;
	register struct shlr *shlp;

	if (q->q_ptr != NULL)
		return(0);
	for (i = 0;i < shlr_count;i++)
	if (shlr_data[i].sh_flag == SH_FREE)
		break;
	if (i == shlr_count)
		return(OPENFAIL);
	shlp = &shlr_data[i];
	shlp->sh_flag = SH_USED;
	q->q_ptr = (char *)shlp;
	WR(q)->q_ptr = (char *)shlp;
	shlp->sh_q = q;
	shlp->sh_swtch = 26;
	for (i = 0;i < NLAYERS; i++) {
		shlp->sh_layer[i] = NULL;
	}
	shlp->sh_pid = u.u_procp->p_pid;
	shlp->sh_current = 0;
	shlp->sh_wait = NULL;
	return(0);
}

static
shlr_close(q)
register queue_t *q;
{
	register struct shlr *shlp;
	register struct shl *sp;
	register int i;
	register queue_t *q2;

	shlp = (struct shlr *)q->q_ptr;
	q->q_ptr = NULL;
	WR(q)->q_ptr = NULL;
	for (i = 0; i < NLAYERS; i++)
		if ((q2 = shlp->sh_layer[i]) != NULL) {
			sp = (struct shl *)q2->q_ptr;
			if (sp) {
				sp->s_info = S_CLOSING;
				WR(q2)->q_next = NULL;
				putctl(q2, M_HANGUP);
			}
		}
	if (shlp->sh_wait)
		freemsg(shlp->sh_wait);
	shlp->sh_flag = SH_FREE;
}

shlr_switch(shlp, inc)
register struct shlr *shlp;
int inc;
{
	int i;
	register struct shl *sp;
	register mblk_t *m;
	queue_t *q;
	register struct iocblk *iocbp;
	register struct termio *t;

	if (inc < 0 || inc >= NLAYERS) {
		inc = 0;
	}
	if (inc > 0 && shlp->sh_layer[inc] == NULL) {
		inc = 0;
	}
	if (inc != shlp->sh_current && shlp->sh_current > 0) {
		sp = (struct shl *)shlp->sh_layer[shlp->sh_current]->q_ptr;
		if (sp) 
			sp->s_info = S_BLOCKED;
	}
	shlp->sh_current = inc;
	if (inc > 0) {
		q = shlp->sh_layer[inc];
		sp = (struct shl *)q->q_ptr;
		sp->s_info = S_RUNNING;
	} else {
		q = shlp->sh_q;
	}
	RD(WR(shlp->sh_q)->q_next)->q_next = q;
	if (inc > 0) {
		qenable(WR(q));
		qenable(RD(WR(shlp->sh_q)->q_next));
	}
	if (inc == 0) {
		if (shlp->sh_wait) {
			m = shlp->sh_wait;
			shlp->sh_wait = 0;
			m->b_datap->db_type = M_IOCACK;
			putnext(q, m);
		}
	} else {
		if (sp->s_m != NULL)
			return;
		m = allocb(sizeof(struct iocblk), BPRI_MED);
		if (m == NULL) 
			return;
		m->b_cont = allocb(sizeof(struct iocblk), BPRI_MED);
		if (m->b_cont == NULL) {
			freeb(m);
			return;
		}
		m->b_datap->db_type = M_IOCTL;
		m->b_wptr += sizeof (struct iocblk);
		iocbp = (struct iocblk *)m->b_rptr;
		iocbp->ioc_cmd = TCSETAW;
		iocbp->ioc_count = sizeof(struct termio);
		iocbp->ioc_uid = 0;
		iocbp->ioc_gid = 0;
		iocbp->ioc_error = 0;
		m->b_cont->b_wptr += sizeof(struct termio);
		t = (struct termio *)m->b_cont->b_rptr;
		t->c_lflag = sp->s_lflag;
		t->c_iflag = sp->s_iflag;
		t->c_oflag = sp->s_oflag;
		t->c_cflag = sp->s_cflag;
		for (i = 0; i < NCC;i++)
			t->c_cc[i] = sp->s_cc[i];
		sp->s_m = m;
		PUTQ(sp->s_next, m);
	}
}
