#ifndef lint	/* .../sys/COMMON/io/shl.c */
#define _AC_NAME shl_c
#define _AC_NO_MAIN "@(#) Copyright (c) 1987 Apple Computer, Inc., 1985-87 UniSoft Corporation, All Rights Reserved.  {Apple version 2.1 89/10/13 18:39:04}"
#include <apple_notice.h>

#ifdef _AC_HISTORY
  static char *sccsid = "@(#)Copyright Apple Computer 1987\tVersion 2.1 of shl.c on 89/10/13 18:39:04";
#endif		/* _AC_HISTORY */
#endif		/* lint */

#define _AC_MODS
/*	@(#)shl.c	UniPlus 2.1.3	*/
/*
 *	Shell layering virtual device
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
#include <sys/dir.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <sys/tty.h>
#include <sys/termio.h>
#include <sys/stream.h>
#include <sys/stropts.h>
#include <sys/signal.h>
#include <sys/region.h>
#include <sys/proc.h>
#include <sys/user.h>
#include <sys/sysmacros.h>
#include <sys/conf.h>
#include <sys/shl.h>
#include <sys/var.h>

#ifndef NULL
#define NULL 0
#endif
#define NDEVICES 30

#define PUTQ(q, m) (*(q)->q_qinfo->qi_putp)((q), m)

int  shl_count = NDEVICES;
struct shl shl_data [NDEVICES];
extern int nulldev();
extern int qenable();
extern int putq();
static int shl_rput();
static int shl_wput();
static int shl_wsrvc();
static int shl_open();
static int shl_close();
/*
 *	Stream interface
 */

static struct module_info shl_rinfo = {4329, "SHL", 0, 256, 1, 1};
static struct module_info shl_winfo = {4329, "SHL", 0, 256, 1, 1};
static struct qinit shl_rq = {shl_rput, 0, shl_open, shl_close,
				nulldev, &shl_rinfo, NULL};
static struct qinit shl_wq = {shl_wput, shl_wsrvc, shl_open, shl_close,
				nulldev, &shl_winfo, NULL};
struct streamtab shlinfo = {&shl_rq, &shl_wq, NULL, NULL};

shlinit()
{
	int i;

	for (i = 0; i < shl_count;i++)
		shl_data[i].s_info = S_FREE;
}

static 
shl_wput(q, m)
register queue_t *q;
register mblk_t *m;
{
	register struct shl *sp;
	register struct iocblk *iocbp;
	register struct termio *t;
	register i;
	char *cp;
	register struct shlr *shlp;

	sp = (struct shl *)q->q_ptr;
	if (m->b_datap->db_type == M_IOCTL) {
		iocbp = (struct iocblk *)m->b_rptr;
		if (iocbp->ioc_cmd == SH_SET) {
			if (iocbp->ioc_count != sizeof(int)) {
				m->b_datap->db_type = M_IOCNAK;
			} else {
				m->b_datap->db_type = M_IOCACK;
				i = *(int *)m->b_cont->b_rptr;
				if (i <= 0 || i > NLAYERS) {
					m->b_datap->db_type = M_IOCNAK;
				} else 
				if (i != sp->s_slot) {
					shlp = sp->s_data;
					if (shlp->sh_layer[i]) {
						m->b_datap->db_type = M_IOCNAK;
					} else {
						shlp->sh_layer[i] = 
						    shlp->sh_layer[sp->s_slot];
						shlp->sh_layer[sp->s_slot] = 0;
						sp->s_slot = i;
					}
				}
			}
			qreply(q, m);
			return;
		}
	}
	if (m->b_datap->db_type == M_FLUSH) {
		i = *m->b_rptr;
		if (i&FLUSHW)
			flushq(q, 0);
		if (sp->s_info == S_RUNNING) {
			PUTQ(sp->s_next, m);
		} else {
			if (i&FLUSHR) {
				*m->b_rptr &= ~FLUSHW;
				flushq(RD(q));
				qreply(q, m);
			} else {
				freemsg(m);
			}
		}
		return;
	}
	if (sp->s_info == S_RUNNING) {
		putq(q, m);
		return;
	}
	if (sp->s_info == S_CLOSING) {
		freemsg(m);
		return;
	}
	if (m->b_datap->db_type != M_IOCTL) {
		putq(q, m);
		return;
	}
	iocbp = (struct iocblk *)m->b_rptr;
	switch (iocbp->ioc_cmd) {
	case TCSETA:
	case TCSETAF:
		t = (struct termio *)(m->b_cont->b_rptr);
		sp->s_lflag = t->c_lflag;
		sp->s_iflag = t->c_iflag;
		sp->s_oflag = t->c_oflag;
		sp->s_cflag = t->c_cflag;
		for (i = 0; i < NCC;i++)
			sp->s_cc[i] = t->c_cc[i];
		break;

	default:
		putq(q, m);
		return;
	}
	iocbp->ioc_count = 0;
	m->b_datap->db_type = M_IOCACK;
	freemsg(unlinkb(m));
	qreply(q, m);
}

static
shl_wsrvc(q)
register queue_t *q;
{
	register mblk_t *m;
	register struct shl *sp;
	struct iocblk *iocbp;
	struct termio *t;
	int i;
	

	sp = (struct shl *)q->q_ptr;
	if (sp->s_info != S_RUNNING)
		return;
	while (m = getq(q)) {
		if (m->b_datap->db_type == M_IOCTL) {
			iocbp = (struct iocblk *)m->b_rptr;
			if (iocbp->ioc_cmd == TCSETA ||
			    iocbp->ioc_cmd == TCSETAW ||
			    iocbp->ioc_cmd == TCSETAF) {
				t = (struct termio *)(m->b_cont->b_rptr);
				sp->s_lflag = t->c_lflag;
				sp->s_iflag = t->c_iflag;
				sp->s_oflag = t->c_oflag;
				sp->s_cflag = t->c_cflag;
				for (i = 0; i < NCC;i++)
					sp->s_cc[i] = t->c_cc[i];
				sp->s_data->sh_swtch = t->c_cc[VSWTCH];
			}
			PUTQ(sp->s_next, m);
		} else 
		if (m->b_datap->db_type != M_DATA &&
		    m->b_datap->db_type != M_EXDATA) {
			PUTQ(sp->s_next, m);
		} else
		if (canput(sp->s_next)) {
			PUTQ(sp->s_next, m);
		} else {
			putbq(q, m);
			return;
		}
	}
}

static
shl_rput(q, m)
queue_t *q;
register mblk_t *m;
{
	register struct shl *sp;
	register struct shlr *shlp;
	register int state;
	char swtch;
	register mblk_t *m1;
	register char *cp;
	register struct sh_state *stp;
	int s;

	sp = (struct shl *)q->q_ptr;
	if (m == sp->s_m) {
		freemsg(m);
		sp->s_m = NULL;
		return;
	}
	if (m->b_datap->db_type != M_DATA) {
		putnext(q, m);
		return;
	}
	shlp = sp->s_data;
	state = sp->s_state;
	swtch = shlp->sh_swtch;
	m1 = m;
	while (m1) {
		for (cp = m1->b_rptr; cp < m1->b_wptr; cp++) {
			if (*cp == '\\') {
				state = 1; 
				continue;
			}
			if (state == 0 && *cp == swtch) {
				m1->b_wptr = cp;
				if (m1->b_cont)
					freemsg(unlinkb(m1));
				if (m->b_cont == NULL &&
					m->b_rptr == m->b_wptr) {
					freemsg(m);
				} else {
					putnext(q, m);
				}
				sp->s_info = S_BLOCKED;
				shlr_switch(shlp, 0);
				return;
			}
			state = 0;
		}
		m1 = m1->b_cont;
	}
	sp->s_state = state;
	putnext(q, m);
}

static
shl_open(q, dev, flag, sflag, err)
register queue_t *q;
register int dev;
int flag;
int *err;
{
	register int i, mn;
	register struct proc *pp;
	register struct shl *sp;
	register struct shlr *shlp;

	if (sflag != CLONEOPEN) {
		if (q->q_ptr == NULL)
			return(OPENFAIL);
		return(0);
	}
	if (q->q_ptr != NULL)
		return(OPENFAIL);
	for (mn = 0; mn < shl_count; mn++) 
	if (shl_data[mn].s_info == S_FREE)
		break;
	if (mn >= shl_count)
		return(OPENFAIL);
	sp = &shl_data[mn];
	pp = u.u_procp;
	for (i = 0;i < shlr_count;i++) {
		if (shlr_data[i].sh_flag != SH_FREE &&
		    pp->p_ppid == shlr_data[i].sh_pid)
			break;
	}
	if (i == shlr_count)
		return(OPENFAIL);
	shlp = &shlr_data[i];
	for (i = 1; i < NLAYERS; i++) {
		if (shlp->sh_layer[i] == NULL)
			break;
	}
	if (i == NLAYERS)
		return(OPENFAIL);
	q->q_ptr = (caddr_t)sp;
	WR(q)->q_ptr = (caddr_t)sp;
	sp->s_data = shlp;
	shlp->sh_layer[i] = q;
	sp->s_slot = i;
	sp->s_next = WR(shlp->sh_q)->q_next;
	sp->s_info = S_BLOCKED;
	sp->s_state = 0;
	sp->s_m = NULL;
	return(mn);
}

static
shl_close(q)
register queue_t *q;
{
	register struct shl *sp;
	mblk_t *m;

	sp = (struct shl *)q->q_ptr;
	q->q_ptr = NULL;
	WR(q)->q_ptr = NULL;
	if (sp->s_info == S_RUNNING) {
		while (m = getq(WR(q))) {
			putnext(q, m);
		}
		flushq(WR(q), 1);
		shlr_switch(sp->s_data, 0);
	} else {
		flushq(WR(q), 1);
	}
	if (sp->s_info != S_CLOSING)
		sp->s_data->sh_layer[sp->s_slot] = NULL;
	sp->s_info = S_FREE;
}
