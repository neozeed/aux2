#ifndef lint	/* .../sys/COMMON/io/line.c */
#define _AC_NAME line_c
#define _AC_NO_MAIN "@(#) Copyright (c) 1987 Apple Computer, Inc., 1985-87 UniSoft Corporation, All Rights Reserved.  {Apple version 2.2 90/05/03 23:33:52}"
#include <apple_notice.h>

#ifdef _AC_HISTORY
  static char *sccsid = "@(#)Copyright Apple Computer 1987\tVersion 2.2 of line.c on 90/05/03 23:33:52";
#endif		/* _AC_HISTORY */
#endif		/* lint */

#define _AC_MODS
/*	@(#)line.c	UniPlus 2.1.10	*/
/*
 *	Streams line discipline
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
#include <sys/dir.h>
#include <sys/ioctl.h>
#include <sys/tty.h>
#include <sys/termio.h>
#include <sys/stream.h>
#include <sys/stropts.h>
#include <sys/signal.h>
#include <sys/mmu.h>
#include <sys/page.h>
#include <sys/region.h>
#include <sys/systm.h>
#include <sys/time.h>
#include <sys/proc.h>
#include <sys/user.h>
#include <sys/var.h>
#include <sys/errno.h>

#ifndef max
#define max(a, b) ((a) > (b)?a:b)
#endif

#ifndef putnext
#define putnext(q, m) (*(q)->q_next->q_qinfo->qi_putp)((q)->q_next, m)
#endif

#ifdef FEP

/*
 *	FEP stuff is for 'front-end processor' these defines are for the version of
 *	"line" that runs on a board past a forwarder. Since the UDOT is not 
 *	accessible any manipulation of it must be via a message returned after the 
 *	open.
 */

#include <fwd.h>

#else

#define NTTQ 20

#endif FEP

/*
 *	Defines used for fast buffer management when editing streams
 */

#define line_check(ind)	{\
				if (wlen < ind) {\
					if (line_send(q, &m2, m, len, &wlen,\
								 ind)){\
						return(m);\
					}\
				}\
			}

#define line_out(ch)	{\
				*m2->b_wptr++ = ch;\
				wlen--;\
			}

#define putout(c)	{\
				if (m_out && count_out <= 0) {\
					(*(WR(q))->q_qinfo->qi_putp)\
						(WR(q), m_out);\
					m_out = NULL;\
				}\
				if (m_out == NULL) {\
					count_out = count + (count>>2) + 1;\
					m_out = allocb(count_out, BPRI_HI);\
				}\
				if (m_out) {\
					*m_out->b_wptr++ = c;\
					count_out--;\
				}\
			}

/*
 *	Default tty characters
 */

extern char ttcchar[];
extern struct ttychars ttycdef;

static char maptab[] = {
	000, 000, 000, 000, 000, 000, 000, 000,
	000, 000, 000, 000, 000, 000, 000, 000,
	000, 000, 000, 000, 000, 000, 000, 000,
	000, '|', 000, 000, 000, 000, 000, '`',
	'{', '}', 000, 000, 000, 000, 000, 000,
	000, 000, 000, 000, 000, 000, 000, 000,
	000, 000, 000, 000, 000, 000, 000, 000,
	000, 000, 000, 000, 000, 000, 000, 000,
	000, 000, 000, 000, 000, 000, 000, 000,
	000, 000, 000, 000, 000, 000, 000, 000,
	000, 000, 000, 000, 000, 000, 000, 000,
	000, 000, 000, 000, 000, 000, '~', 000,
	000, 'A', 'B', 'C', 'D', 'E', 'F', 'G',
	'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O',
	'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W',
	'X', 'Y', 'Z', 000, 000, 000, 000, 000
};
#ifndef NULL
#define NULL 0
#endif
extern int nulldev();
extern int qenable();
static int line_send();
static int line_rput();
static int line_wput();
static int line_rsrvc();
static int line_wsrvc();
static int line_open();
static int line_close();

/*
 *	Stream interface
 */

static struct module_info line_rinfo = {4321, "LINE", 0, 256, 32767, 256};
static struct module_info line_winfo = {4321, "LINE", 0, 256, 256, 256};
static struct qinit line_rq = {line_rput, line_rsrvc, line_open, line_close,
				nulldev, &line_rinfo, NULL};
static struct qinit line_wq = {line_wput, line_wsrvc, line_open, line_close,
				nulldev, &line_winfo, NULL};
struct streamtab lineinfo = {&line_rq, &line_wq, NULL, NULL};
extern char partab[];

static mblk_t *line_transmit();

/*
 *	line discipline tty structure
 */

struct ttq {
	struct ttq * t_next;
	ushort	t_iflag;	/* input modes */
	ushort	t_oflag;	/* output modes */
	ushort	t_cflag;	/* control modes */
	ushort	t_lflag;	/* line discipline modes */
	short	t_state;	/* internal state */
	unsigned char t_delct;	/* delimiter count */
	unsigned char t_col;		/* current column */
	unsigned char	t_cc[NCC];	/* settable control chars */
	struct ttychars t_chars;	/* BSD style control chars */
};


static struct ttq *free_list;

static struct ttq ttq[NTTQ];

/*
 *	system initialisation routine, makes a free list of
 *		ttqs
 */

lineinit()
{
	register int i;

	free_list = NULL;
	for (i = 0; i < NTTQ;i++) {
		ttq[i].t_next = free_list;
		free_list = &ttq[i];
	}
}

/*
 *	Write put routine, called from stream head, or from read service routine
 *	for echoing
 */

static
line_wput(q, m)
register queue_t *q;
register mblk_t *m;
{
	register struct ttq *tp;
	struct termio *t;
	int i, v;
	struct iocblk *iocbp;

#ifdef DEBUGL
	printf("WP\n");
#endif DEBUGL
	tp = (struct ttq *)q->q_ptr;
	switch(m->b_datap->db_type) {

	/*
 	 *	Data must be edited so put it on the queue
	 */

	case M_EXDATA:
	case M_DATA:
		if (m->b_cont) {
			register mblk_t *m_out;

			while (m) {
				m_out = m->b_cont;
				m->b_cont = NULL;
				line_wput(q,m);
				m = m_out;
			}
			return;
		}
		putq(q, m);
		break;

	/*
	 *	Flushes get done now so do them and pass them on
	 */

	case M_FLUSH:
		if ((*m->b_rptr)&FLUSHW) 
			flushq(q, 0);
		putnext(q, m);
		break;

	/*
	 *	some IOCTLs are done in place, others are queued
	 */

	case M_IOCTL:
		iocbp = (struct iocblk *)m->b_rptr;
		switch(iocbp->ioc_cmd) {
		case TCXONC:
			switch (*(m->b_cont->b_rptr)) {
			case 2:
				tp->t_state |= TBLOCK;
				break;
			case 3:
				tp->t_state &= ~TBLOCK;
				break;
			}
			putnext(q, m);
			break;

		case TCSETA:
			line_set(q, tp, (struct termio *)m->b_cont->b_rptr);
			putnext(q, m);
			break;

		case TIOCSLTC:
			bcopy((caddr_t) m->b_cont->b_rptr, 
				(caddr_t)&tp->tt_suspc, sizeof(struct ltchars));
			m->b_datap->db_type = M_IOCACK;
			qreply(q, m);
			break;

		case TCFLSH:
			i = 0;
			v = *(int *)(m->b_cont->b_rptr);
			if (v == 1 || v == 2) {
				i |= FLUSHW;
				flushq(q, 0);
			}
			if (v == 0 || v == 2) i |= FLUSHR;
			if (i) {
				putctl1(q->q_next, M_FLUSH, i);
			}
			if (v < 0 || v > 2) {
				iocbp->ioc_error = EINVAL;
				m->b_datap->db_type = M_IOCNAK;
			}
			else {
				m->b_datap->db_type = M_IOCACK;
			}
			qreply(q, m);
			break;

		case TCSBRK:
		case TCSETAW:
		case TCSETAF:
			putq(q, m);
			break;

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
				t = (struct termio *)m->b_cont->b_rptr;
				t->c_iflag = tp->t_iflag;
				t->c_oflag = tp->t_oflag;
				t->c_cflag = tp->t_cflag;
				t->c_lflag = tp->t_lflag;
				t->c_line = 0;
				for (i = 0; i < NCC; i++) 
					t->c_cc[i] = tp->t_cc[i];
				putnext(q, m);
			} else {
				m->b_datap->db_type = M_IOCNAK;
				qreply(q, m);
			}
			break;

		case TIOCGLTC:
			if (m->b_cont == NULL) {
				m->b_cont = allocb(sizeof(struct ltchars),
						BPRI_MED);
				if (m->b_cont) {
					m->b_cont->b_wptr +=
						sizeof(struct ltchars);
					iocbp->ioc_count =
						sizeof(struct ltchars);
				}
			}
			if (m->b_cont) {
				bcopy((caddr_t)&tp->tt_suspc, 
					(caddr_t)m->b_cont->b_rptr, 
					sizeof(struct ltchars));
				m->b_datap->db_type = M_IOCACK;
			} else {
				m->b_datap->db_type = M_IOCNAK;
			}
			qreply(q, m);
			break;


		default:
			putq(q, m);
		}
		break;
	default:
	free:
		freemsg(m);
	}
}

/*
 *	Write service routine, its main purpose is to edit data
 *		being transmitted
 */

static
line_wsrvc(q)
register queue_t *q;
{
	register struct ttq *tp;
	register mblk_t *m;
	register struct iocblk *iocbp;

#ifdef DEBUGL
	printf("WS\n");
#endif DEBUGL
	tp = (struct ttq *)q->q_ptr;
	while (m = getq(q)) {
		switch(m->b_datap->db_type) {
		case M_EXDATA:
		case M_DATA:
			if (!(tp->t_oflag&OPOST)) {
				if (canput(q->q_next)) {
					putnext(q, m);
					break;
				}
				putbq(q, m);
				return;
			}
			if (!canput(q->q_next)) {
				putbq(q, m);
				return;;
			}
			m = line_transmit(q, m);	/* call line_transmit to
							   do the hard work  */
			if (m) {
				putbq(q, m);
				return;
			}
			break;

		case M_IOCTL:
			iocbp = (struct iocblk *)m->b_rptr;
			switch(iocbp->ioc_cmd) {
			case TCSETAW:
			case TCSETAF:
				if (!canput(q->q_next)) {
					putbq(q, m);
					return;
				}
				line_set(q, tp,
					(struct termio *)m->b_cont->b_rptr);
				putnext(q, m);
				break;

			default:
				if (!canput(q->q_next)) {
					putbq(q, m);
					return;
				}
				putnext(q, m);
			}
			break;
			
		default:
			freemsg(m);
		}
	}
}

/*
 *	Routine called to transmit a packet downstream and to cause more
 *	buffers to be allocated
 */

static int
line_send(q, m2, m, len, wlen, ind)
register queue_t *q;
register mblk_t **m2, *m;
register int *wlen, len;
int ind;
{
	register int i;

#ifdef DEBUGL
	printf("LS\n");
#endif DEBUGL
	if (*m2) {
		if ((*m2)->b_rptr != (*m2)->b_wptr) {
			putnext(q, (*m2));
		} else {
			freeb((*m2));
		}
	}
	if (!canput(q->q_next)) {
#ifdef DEBUGL
	printf("LX\n");
#endif DEBUGL
		return(1);
	}
	i = len + (len >> 2) + 1;
	if (i < ind) i = ind;
	*wlen = i;
	*m2 = allocb(i, BPRI_LO);
	if (*m2 == NULL) {
#ifdef DEBUGL
	printf("LY\n");
#endif DEBUGL
		timeout(qenable, q, v.v_hz/4);
		return(1);
	}
	return(0);
}

static mblk_t *
line_transmit(q, m)
queue_t *q;
register mblk_t *m;
{
	register struct ttq *tp;
	int wlen, ctype, i, delay;
	register int len, flag, col;
	mblk_t *m1, *m2;
	register unsigned char ch;

#ifdef DEBUGL
	printf("LT\n");
#endif DEBUGL
	tp = (struct ttq *)q->q_ptr;
	wlen = 0;
	m2 = NULL;
	flag = tp->t_oflag;
	col = tp->t_col;
	while (m) {
		len = m->b_wptr - m->b_rptr;
		while (len) {
			ch = *m->b_rptr;
			len--;
			line_check(1);
			if (tp->t_lflag&XCASE) {
				if (ch < 'A') {
					if (ch == '\0') {
						line_check(2);
						line_out('\\');
						ch = '\\';
					}
				} else 
				if (ch > 'Z') {
					if (ch > '|') {
						if (ch == '}') {
							line_check(2);
							line_out('\\');
							ch = ')';
						} else 
						if (ch == '~') {
							line_check(2);
							line_out('\\');
							ch = '^';
						}
					} else
					if (ch < '|') {
						if (ch == '{') {
							line_check(2);
							line_out('\\');
							ch = '(';
						} else 
						if (ch == '`') {
							line_check(2);
							line_out('\\');
							ch = '\'';
						} 
					} else {
						line_check(2);
						line_out('\\');
						ch = '!';
					}
				} else {
					line_check(2);
					line_out('\\');
				}
			}
			if (flag&OLCUC) {
				if (ch >= 'a' && ch <= 'z') {
					ch += 'A' - 'a';
				}
			}
			ctype = partab[ch];
			delay = 0;
			switch(ctype&0x3f) {
		case 0:
				col++;
		case 1:
				break;

		case 2:		if (flag&BSDLY) {
					if (flag&OFILL) {
						delay = 1;
					} else {
						delay = v.v_hz / 20 + 1;
					}
				}
				if (col) {
					col--;
				}
				break;

		case 3:		if (flag&(ONLRET|ONLCR)) {
					switch(flag&(CRDLY)) {
				case CR1:	
						if (col) {
						    delay=max((col>>4),3)*
								 v.v_hz/15+1;
						}
						if (delay && flag&OFILL) {
						    	delay = 2;
						}
						break;

				case CR2:	if (flag&OFILL) {
							delay = 4;
						} else {
							delay = v.v_hz/10+1;
						}
						break;

				case CR3:	if (flag&OFILL) {
							delay = 4;
						} else {
							delay = v.v_hz/6+1;
						}
						break;
					}
					if (flag&ONLCR &&
					    (!(flag&ONOCR) || col)) {
						if (delay && flag&OFILL) {
							line_check(delay+2);
						} else {
							line_check(2);
						}
						line_out('\r');
					}
					col = 0;
				} else 
				if (flag&NLDLY) {
					if (flag&OFILL) {
						delay = 2;
					} else {
						delay = v.v_hz/10+1;
					}
				}
				break;

			case 4:
				col += (i = 8 - (col&0x07));
				switch(flag&TABDLY) {
				case TAB0:
					break;

				case TAB1:
					delay = (i >= 5 ? i*v.v_hz/20+1 : 0);
					if (delay && flag&OFILL) {
						delay = 2;
					}
					break;

				case TAB2:
					if (flag&OFILL) {
						delay = 2;
					} else {
						delay = v.v_hz/10+1;
					}
					break;

				case TAB3:
					line_check(i);
					while (i--) {
						line_out(' ');
					}
					tp->t_col = col;
					m->b_rptr++;
					continue;
				}
				break;

			case 5:	if (flag&VTDLY) {
					delay = 2*v.v_hz;
				}
				break;

			case 6:	if (flag&OCRNL) {
					ch = '\n';
					if (flag&NLDLY) {
						if (flag&OFILL) {
							delay = 2;
						} else {
							delay = v.v_hz/10+1;
						}
					}
					break;
				}
				if (flag&ONOCR && col == 0) {
					m->b_rptr++;
					continue;
				}
				switch(flag&(CRDLY)) {
				case CR1:	
					if (col) {
						delay=max((col>>4),3)*
								 v.v_hz/15+1;
					}
					if (delay && flag&OFILL) {
						delay = 2;
					}
					break;

				case CR2:
					if (flag&OFILL) {
						delay = 2;
					} else {
						delay = v.v_hz/10+1;
					}
					break;

				case CR3:
					if (flag&OFILL) {
						delay = 4;
					} else {
						delay = v.v_hz/6+1;
					}
					break;
				}
				col = 0;
				break;

			case 7:	if (flag&FFDLY) {
					delay = v.v_hz*2;
				}
				break;
			}
			if (delay && delay < v.v_hz && flag&OFILL) {
				line_check(delay+1);
			}

/*
 *	At this point there is no turning back .... we must cast any changes
 *	to globals in stone (hence the bizarre extra space!!!)
 */

			m->b_rptr++;
			tp->t_col = col;
			line_out(ch);
			if (delay) {
				if (delay < 10 && flag&OFILL) {
					ch = (flag&OFDEL?0x7f:0);
					while (delay--) {
						line_out(ch);
					}
				} else {
					putnext(q, m2);
					m2 = NULL;
					wlen = 0;
					putctl1(q->q_next, M_DELAY, delay);
					delay = 0;
					if (!canput(q->q_next)) {
						return(m);
					}
				}
			}
		}
		m1 = unlinkb(m);
		freeb(m);
		m = m1;
	}
	if (m2) {
		if (m2->b_rptr != m2->b_wptr) {
			putnext(q, m2);
		} else {
			freeb(m2);
		}
	}
	return(NULL);
}

static
line_rput(q, m)
queue_t *q;
register mblk_t *m;
{
	register struct ttq *tp;
	register mblk_t *m_out;
	int flushed;
	register int count, flag, count_out;
	register unsigned char c, *cp;
	struct iocblk *iocbp;

#ifdef DEBUGL
	printf("RP\n");
#endif DEBUGL
	tp = (struct ttq *)q->q_ptr;
	switch(m->b_datap->db_type) {
	case M_EXSIG:
	case M_IOCACK:
	case M_IOCNAK:
	case M_BREAK:
	case M_ADMIN:
	case M_SIG:
	case M_PCSIG:
	case M_HANGUP:
	case M_ERROR:
		putnext(q, m);
		break;

	case M_CTL:
		if (*m->b_rptr == L_BREAK) {
			flushq(q, 0);
			tp->t_delct = 0;
			flushq(WR(q), 0);
			m->b_datap->db_type = M_FLUSH;
			*m->b_rptr = FLUSHRW;
			putnext(q, m);
			putctl1(q->q_next, M_PCSIG, SIGINT);
		} else putnext(q, m);
		break;

	case M_FLUSH:
		if ((*m->b_rptr)&FLUSHR) 
			flushq(q, 0);
			tp->t_delct = 0;
		putnext(q, m);
		break;

	case M_DATA:
	case M_EXDATA:
		if (m->b_cont) {
			while (m) {
				m_out = m->b_cont;
				m->b_cont = NULL;
				line_rput(q,m);
				m = m_out;
			}
			return;
		}
#ifndef	FEP
		/*
		** As the FEP can pass lots of data, up the TTYHOG
		** count. As the packets being sent from the forwarder
		** are rarely (if ever) full, if we are close to overrunning
		** TTYHOG, check the actual character count before flushing.
		*/
		if (q->q_count > TTYHOG*16 && countq(q) > TTYHOG*16) {
			flushq(q, 0);
			tp->t_delct = 0;
			putctl1(q->q_next, M_FLUSH, FLUSHRW);
			freemsg(m);
			return;
		}
		if (q->q_count >= TTXOHI*4) {
#else
		if (q->q_count >= TTXOHI*16) {
#endif
			if (tp->t_iflag&IXOFF && (tp->t_state&TBLOCK) == 0) {
				m_out = allocb(sizeof(struct iocblk), BPRI_HI);
				if (m_out) {
					m_out->b_cont = allocb(1, BPRI_HI);
					if (m_out->b_cont) {
						m_out->b_datap->db_type =
							M_IOCTL;
						m_out->b_wptr +=
							sizeof(struct iocblk);
						iocbp = (struct iocblk *)
							m_out->b_rptr;
						iocbp->ioc_id = 0;
						iocbp->ioc_cmd = TCXONC;
						iocbp->ioc_count = 1;
						*m_out->b_cont->b_wptr++ = 2;
						putnext(WR(q), m_out);
						q->q_next->q_flag |= QWANTW;
						tp->t_state |= TBLOCK;
					} else {
						freeb(m_out);
					}
				}
			}
		}
		m_out = NULL;
		count_out = 0;
		if (tp->t_iflag&(INLCR|ICRNL)) {
			cp = (unsigned char *)m->b_rptr;
			while (cp < (unsigned char *)m->b_wptr) {
				c = *cp;
				if (c == '\n') {
					if (tp->t_iflag&INLCR)
						*cp = '\r';
				} else
				if (c == '\r' && (tp->t_iflag&IGNCR) == 0 &&
					tp->t_iflag&ICRNL)
						*cp = '\n';
				cp++;
			}
		}
		flag = tp->t_lflag&0xff;
		flushed = 0;
		if (flag) {
			cp = (unsigned char *)m->b_rptr;
			count = m->b_wptr - m->b_rptr;
			while (count--) {
				c = *cp++;
#ifdef POSIX
				if (flag&ISIG && c != (unsigned char)_POSIX_VDISABLE) {
#else /* !POSIX */
				if (flag&ISIG) {
#endif /* POSIX */
					if (c == tp->t_cc[VINTR]) {
						if ((flag&NOFLSH) == 0) {
							line_flush(q);
							flushed = 1;
						}
						putctl1(q->q_next, M_PCSIG,
								   SIGINT);
						continue;
					}
					if (c == tp->t_cc[VQUIT]) {
						if ((flag&NOFLSH) == 0) {
							line_flush(q);
							flushed = 1;
						}
						putctl1(q->q_next, M_PCSIG,
								   SIGQUIT);
						continue;
					}
					if (c == tp->tt_suspc) {
						if ((flag & NOFLSH) == 0) {
							line_flush(q);
							flushed = 1;
						}
						putctl1(q->q_next, M_PCSIG,
								SIGTSTP);
						continue;
					}
				}
				if (flag&ICANON) {
					if (c == '\n') {
						if (flag&ECHONL)
							flag |= ECHO;
						tp->t_delct++;
						qenable(q);
					} else
#ifdef POSIX
					if (c != (unsigned char)_POSIX_VDISABLE && 
					    (c == tp->t_cc[VEOL] ||
					    c == tp->t_cc[VEOL2])) {
#else /* !POSIX */
					if (c == tp->t_cc[VEOL] ||
					    c == tp->t_cc[VEOL2]) {
#endif /* POSIX */
						tp->t_delct++;
						qenable(q);
					} else 
					if ((tp->t_state&CLESC) == 0) {
						if (c == '\\') 
							tp->t_state |= CLESC;
#ifdef POSIX
						if (c == (unsigned char)_POSIX_VDISABLE)
							;
						else
#endif /* POSIX */
						if (c == tp->t_cc[VERASE]) {
							if (flag&ECHOE) {
								if (flag&ECHO)
								  putout('\b');
								flag |= ECHO;
								putout(' ');
								c = '\b';
							}
						} else
						if (c == tp->t_cc[VKILL]) {
							if (flag&ECHOK) {
								if (flag&ECHO)
								  putout(c);
								flag |= ECHO;
								c = '\n';
							}
						} else
						if (c == tp->t_cc[VEOF]) {
							flag &= ~ECHO;
							tp->t_delct++;
							qenable(q);
						}
					} else {
						if (c != '\\' || flag&XCASE)
							tp->t_state &= ~CLESC;
					}
				}
				if (flag&ECHO)
					putout(c);
			}
		}
		if (m_out) {
			(*(WR(q))->q_qinfo->qi_putp) (WR(q), m_out);
		}
		if (flushed) {
			freemsg(m);
		} else {
			putq(q, m);
		}
		break;

	default:
		freemsg(m);
		break;
		
	}
}

static
line_rsrvc(q)
queue_t *q;
{
	register struct ttq *tp;
	register mblk_t *m_in, *m_out;
	register int count_in, count_out;
	mblk_t *m_tmp;
	int escape;
	unsigned char c;

#ifdef DEBUGL
	printf("RS\n");
#endif DEBUGL
	tp = (struct ttq *)q->q_ptr;
	while (m_in = getq(q)) {
		switch(m_in->b_datap->db_type) {
		case M_DATA:
		case M_EXDATA:
			if (!canput(q->q_next)) {
				putbq(q, m_in);
				goto out;
			}
			if (tp->t_lflag&ICANON && tp->t_delct == 0) {
				putbq(q, m_in);
				goto out;
			}
			if (!(tp->t_lflag&ICANON)) {
				putnext(q, m_in);
				break;
			}
			count_in = m_in->b_wptr - m_in->b_rptr;
			count_out = q->q_count + count_in;
			/*
			if (count_out > 256)
				count_out = 256;
			*/
			m_out = allocb(count_out, BPRI_HI);;
			if (m_out == NULL) {
				putbq(q, m_in);
				goto out;
			}
			escape = 0;
			while (count_in) {
				c = (unsigned char)*m_in->b_rptr++;
				count_in--;
				if (c == '\r') {
					if (tp->t_iflag&IGNCR)
						goto next_ch;
				} else 
				if (tp->t_iflag&IUCLC &&
						c >= 'A' && c <= 'Z')
					c += 'a' - 'A';
				if (escape) {
					escape = 0;
#ifdef POSIX
					if (c != (unsigned char)_POSIX_VDISABLE &&
					    (c == tp->t_cc[VERASE] ||
					     c == tp->t_cc[VKILL] ||
					     c == tp->t_cc[VEOF])) {
#else /* !POSIX */
					if (c == tp->t_cc[VERASE] ||
					    c == tp->t_cc[VKILL] ||
					    c == tp->t_cc[VEOF]) {
#endif /* POSIX */
						m_out->b_wptr--;
						count_out++;
					} else
					if (tp->t_lflag&XCASE) {
						if (c < 0x80 && maptab[c]) {
							m_out->b_wptr--;
							count_out++;
							c = maptab[c];
						} else 
						if (c == '\\')
							goto next_ch;
					} else
					if (c == '\\')
						escape = 1;
				} else {
					if (c == '\\') {
						escape = 1;
					} else
#ifdef POSIX
					if (c == (unsigned char)_POSIX_VDISABLE)
						;
					else
#endif /* POSIX */
					if (c == tp->t_cc[VERASE]) {
						if (m_out->b_wptr >
						    m_out->b_rptr) {
							count_out++;
							m_out->b_wptr--;
						}
						goto next_ch;
					} else
					if (c == tp->t_cc[VKILL]) {
						count_out += m_out->b_wptr -
						    	m_out->b_rptr;
						m_out->b_wptr = m_out->b_rptr;
						goto next_ch;
					} else
					if(c == tp->tt_dsuspc) {
						count_out += m_out->b_wptr -
						    	m_out->b_rptr;
						m_out->b_wptr = m_out->b_rptr;
						putctl1(q->q_next, M_SIG,
								SIGTSTP);
						goto next_ch;
					}
#ifdef POSIX
					if (c == (unsigned char)_POSIX_VDISABLE)
						;
					else
#endif /* POSIX */
					if (c == tp->t_cc[VEOF])
						break;
				}
				*m_out->b_wptr++ = (char)c;
#ifdef POSIX
				if (c != (unsigned char)_POSIX_VDISABLE &&
				    (c == '\n' || c == tp->t_cc[VEOL] ||
				     c == tp->t_cc[VEOL2]))
					 break;
#else /* !POSIX */
				if (c == '\n' || c == tp->t_cc[VEOL] ||
				    c == tp->t_cc[VEOL2]) 
					break;
#endif /* POSIX */
				if (--count_out == 0) {
					m_out->b_wptr--;
					count_out = 1;
				}
			next_ch:for (;count_in == 0;) {
					m_tmp = m_in;
					m_in = unlinkb(m_tmp);
					freeb(m_tmp);
					if (m_in == NULL) {
						m_in = getq(q);
						if (m_in == NULL)
							break;
						if (m_in->b_datap->db_type !=
								M_DATA &&
						    m_in->b_datap->db_type !=
								M_EXDATA) {
							putbq(q, m_in);
							break;
						}
					}
					count_in = m_in->b_wptr - m_in->b_rptr;
				}
			}
			tp->t_delct--;
			if (m_out) {
				putnext(q, m_out);
			}
			if (m_in) {
				if (m_in->b_rptr == m_in->b_wptr) {
					freeb(m_in);
				} else {
					putbq(q, m_in);
				}
			}
			if (!canput(q->q_next))
				goto out;
		}
	}
out:
	if (q->q_count < TTXOLO*4 && tp->t_iflag&IXOFF && tp->t_state&TBLOCK) {
		m_out = allocb(sizeof(struct iocblk), BPRI_HI);
		if (m_out) {
			m_out->b_cont = allocb(1, BPRI_HI);
			if (m_out->b_cont) {
			        register struct iocblk *iocbp;

				m_out->b_datap->db_type = M_IOCTL;
				m_out->b_wptr += sizeof(struct iocblk);
				iocbp = (struct iocblk *)m_out->b_rptr;
				iocbp->ioc_id = 0;
				iocbp->ioc_cmd = TCXONC;
				iocbp->ioc_count = 1;
				*m_out->b_cont->b_wptr++ = 3;
				putnext(WR(q), m_out);
				tp->t_state &= ~TBLOCK;
			} else {
				freeb(m_out);
			}
		}
	}
}


static
line_open(q, dev, flag, sflag, err, devp
#ifdef FEP
	 	, acktags
#endif FEP
	 )
register queue_t *q;
int dev, flag, *devp, *err;
#ifdef FEP
fwd_acktags_t *acktags;
#endif FEP
{
	register struct ttq *tp;
	register mblk_t *m;
	register struct stroptions *sp;
	register int i;
	struct proc *pp;

	if (q->q_ptr == NULL) {
		if (free_list == NULL)
			return(OPENFAIL);
		m = allocb(sizeof(struct stroptions), BPRI_HI);
		if (m == NULL)
			return(OPENFAIL);
		tp = free_list;
		q->q_ptr = (char *)tp;
		WR(q)->q_ptr = (char *)tp;
		free_list = free_list->t_next;
		tp->t_iflag = ICRNL;
		tp->t_oflag = OPOST|ONLCR|TAB3;
		tp->t_lflag = ICANON|ISIG|ECHO|ECHOE|ECHOK;
		tp->t_cflag = B9600|CS8|CREAD;
		tp->t_delct = 0;
		tp->t_col = 0;
		for (i = 0; i < NCC;i++)
			tp->t_cc[i] =  ttcchar[i];
		bcopy((caddr_t)&ttycdef, (caddr_t)&tp->t_chars,
			sizeof(struct ttychars));
		m->b_datap->db_type = M_SETOPTS;
		m->b_wptr += sizeof(struct stroptions);
		sp = (struct stroptions *)m->b_rptr;
		sp->so_flags = SO_READOPT|SO_WROFF|SO_MINPSZ|SO_MAXPSZ|SO_HIWAT|
					  SO_LOWAT;
		sp->so_readopt = RMSGN;
		sp->so_wroff = 0;
		sp->so_minpsz = 0;
		sp->so_maxpsz = 256;
		sp->so_hiwat = 1;
		sp->so_lowat = 1;
		putnext(q, m);
	}
#ifdef FEP
	acktags->setpgrp = 1;
#else
	pp = u.u_procp;
	while (q->q_next)
		q = q->q_next;
	if ((pp->p_pid == pp->p_pgrp) &&
	    (pp->p_ttyp == NULL) &&
	    (((struct stdata *)q->q_ptr)->sd_pgrp == 0)) {
		pp->p_ttyp = &((struct stdata *)q->q_ptr)->sd_pgrp;
	}
#endif FEP
	return(1);
}

static
line_close(q)
register queue_t *q;
{
	register struct ttq *tp;
	register mblk_t *m;
	register struct stroptions *sp;
	
	m = allocb(sizeof(struct stroptions), BPRI_HI);
	if (m != NULL) {
		m->b_datap->db_type = M_SETOPTS;
		m->b_wptr += sizeof(struct stroptions);
		sp = (struct stroptions *)m->b_rptr;
		sp->so_flags = SO_READOPT|SO_WROFF|SO_MINPSZ|SO_MAXPSZ|SO_HIWAT|
					  SO_LOWAT;
		sp->so_readopt = RNORM;
		sp->so_wroff = 0;
		sp->so_minpsz = 0;
		sp->so_maxpsz = INFPSZ;
		sp->so_hiwat = STRHIGH;
		sp->so_lowat = STRLOW;
		putnext(q, m);
	}
	tp = (struct ttq *)q->q_ptr;
	q->q_ptr = NULL;
	tp->t_next = free_list;
	free_list = tp;
}

line_flush(q)
register queue_t *q;
{
	register struct ttq *tp;
	
	tp = (struct ttq *)q->q_ptr;
	flushq(q, 0);
	tp->t_delct = 0;
	flushq(WR(q), 0);
	putctl1(q->q_next, M_FLUSH, FLUSHRW);
}

line_set(q, tp, t)
queue_t *q;
register struct ttq *tp;
register struct termio *t;
{
	register int i;
	
	tp->t_iflag = t->c_iflag;
	tp->t_oflag = t->c_oflag;
	tp->t_cflag = t->c_cflag;
	tp->t_lflag = t->c_lflag;
	for (i = 0; i < NCC; i++) 
		tp->t_cc[i] = t->c_cc[i];
	if (!(tp->t_lflag&ICANON)) {
		tp->t_delct = 0;
		qenable(RD(q));
	}
}

/*
** countq
**	For a given queue, count up the number of bytes of data
**	on the queue. This routine counts actual data, not just
**	allocated space.
*/
countq(q)
register queue_t	*q;	/* The queue to count */
{
	register mblk_t	*m;	/* For each mblk_t in the chain */
	register mblk_t	*cont;	/* For each continuation block */
	register int	count;	/* The count of characters */
	register int	s;	/* Current priority level */

	count = 0;
	s = splstr();
	if ((m = q->q_first) == NULL) {
		splx(s);
		return (0);
	}
	/*
	** For each mblk in the queue.
	*/
	while (m) {
		/*
		** For the mblk, and each of it's continuation blocks,
		** count the number of actual data bytes.
		*/
		for (cont = m ; cont ; cont = cont->b_cont)
			count += (cont->b_wptr - cont->b_rptr);
		m = m->b_next;
	}
	splx(s);
	return (count);
}
