/*	@(#)if_sl.c	7.1 (Berkeley) 6/4/86 */

/*
 * Serial Line interface
 *
 * Rick Adams
 * Center for Seismic Studies
 * 1300 N 17th Street, Suite 1450
 * Arlington, Virginia 22209
 * (703)276-7900
 * rick@seismo.ARPA
 * seismo!rick
 *
 * Pounded on heavily by Chris Torek (chris@mimsy.umd.edu, umcp-cs!chris).
 * N.B.: this belongs in netinet, not net, the way it stands now.
 * Should have a link-layer type designation, but wouldn't be
 * backwards-compatible.
 *
 * Converted to 4.3BSD Beta by Chris Torek.
 * Other changes made at Berkeley, based in part on code by Kirk Smith.
 *
 * Added to A/UX by Holly Knight
 * 2/18/88
 */

/* $Header: if_sl.c,v 1.12 85/12/20 21:54:55 chris Exp $ */
/* from if_sl.c,v 1.11 84/10/04 12:54:47 rick Exp */

#include "sys/types.h"
#include "sys/param.h"
#include "sys/time.h"
#include "sys/mmu.h"
#include "sys/page.h"
#include "sys/systm.h"
#include "sys/conf.h"
#include "sys/signal.h"
#include "sys/user.h"
#include "sys/region.h"
#include "sys/proc.h"
#include "sys/mbuf.h"
#include "sys/buf.h"
#include "sys/socket.h"
#include "sys/ioctl.h"
#include "sys/file.h"
#include "sys/tty.h"
#include "sys/termio.h"
#include "sys/errno.h"

#include "net/if.h"
#include "net/route.h"
#include "net/netisr.h"

#include "netinet/in.h"
#include "netinet/in_systm.h"
#include "netinet/in_var.h"
#include "netinet/ip.h"



/*
 * N.B.: SLMTU is now a hard limit on input packet size.
 * SLMTU must be <= CLBYTES - sizeof(struct ifnet *).
 */
#define	SLMTU	1006
#define	SLIP_HIWAT	1000	/* don't start a new packet if HIWAT on queue */
#define	CLISTRESERVE	1000	/* Can't let clists get too low */

struct sl_softc {
	struct	ifnet sc_if;	/* network-visible interface */
	short	sc_flags;	/* see below */
	short	sc_ilen;	/* length of input-packet-so-far */
	struct	tty *sc_ttyp;	/* pointer to tty structure */
	char	*sc_mp;		/* pointer to next available buf char */
	char	*sc_buf;	/* input buffer */
} sl_softc[NSL];

/* flags */
#define	SC_ESCAPED	0x0001	/* saw a FRAME_ESCAPE */

#define FRAME_END	 	0300		/* Frame End */
#define FRAME_ESCAPE		0333		/* Frame Esc */
#define TRANS_FRAME_END	 	0334		/* transposed frame end */
#define TRANS_FRAME_ESCAPE 	0335		/* transposed frame esc */

int sloutput(), slifioctl();
struct sl_softc *find_sc();

/*
 * Called from boot code to establish sl interfaces.
 */
slinit()
{
	register struct sl_softc *sc;
	register int i = 0;

	for (sc = sl_softc; i < NSL; sc++) {
		sc->sc_if.if_name = "sl";
		sc->sc_if.if_unit = i++;
		sc->sc_if.if_mtu = SLMTU;
		sc->sc_if.if_flags = IFF_POINTOPOINT;
		sc->sc_if.if_ioctl = slifioctl;
		sc->sc_if.if_output = sloutput;
		sc->sc_if.if_snd.ifq_maxlen = IFQ_MAXLEN;
		if_attach(&sc->sc_if);
	}
}

/*
 * Line specific open routine.
 * Attach the given tty to the first available sl unit.
 */
/* ARGSUSED */
slopen(tp)
	register struct tty *tp;
{
	register struct sl_softc *sc;
	register int nsl;
	dev_t dev = tp->t_index;

	if (!suser())
		return (EPERM);

	for (nsl = 0, sc = sl_softc; nsl < NSL; nsl++, sc++)
		if (sc->sc_ttyp == NULL) {
			sc->sc_flags = 0;
			sc->sc_ilen = 0;
			if (slsetup(sc) == 0)
				return (ENOBUFS);
			sc->sc_ttyp = tp;
			ttyflush(tp, FREAD | FWRITE);
			return (0);
		}

	return (ENXIO);
}

/*
 * Line specific close routine.
 * Detach the tty from the sl unit.
 * Mimics part of ttyclose().
 */
slclose(tp)
	struct tty *tp;
{
	register struct sl_softc *sc;
	int s;
	
	ttywait(tp);
	ttyflush(tp, FREAD);
	tp->t_line = 0;

	tp->t_state &= ~(WOPEN|ISOPEN);
	tp->t_lflag &= ~(TOSTOP);
	tp->t_pgrp = 0;
	if (u.u_procp->p_ttyp == &tp->t_pgrp)
	    u.u_procp->p_ttyp = NULL;

	s = splimp();		/* paranoid; splnet probably ok */
	sc = find_sc(tp);
	if (sc != NULL) {
		if_down(&sc->sc_if);
		sc->sc_ttyp = NULL;
		MCLFREE((struct mbuf *)sc->sc_buf);
		sc->sc_buf = 0;
	}
	splx(s);
}

/*
 * Line specific (tty) ioctl routine.
 * Provide a way to get the sl unit number.
 */
/* ARGSUSED */
slioctl(tp, cmd, data, flag)
	struct tty *tp;
	caddr_t data;
{
        struct sl_softc *sc;
	
	switch(cmd) {
	case LDGETU:
		if ((sc = find_sc(tp)) != NULL) {
			*(int *)data = sc->sc_if.if_unit;
			return (0);
		}
		break;
        case LDOPEN:
		slopen(tp);
		ttioctl(tp, cmd, data, flag);
		return 0;
        case LDCLOSE:
		slclose(tp);
		ttioctl(tp, cmd, data, flag);
		return 0;
	}
	return (-1);
}

/*
 * Queue a packet.  Start transmission if not active.
 */
extern int useloopback;
extern struct ifnet loif;
#define	satosin(sa)	((struct sockaddr_in *)(sa))

sloutput(ifp, m, dst)
	register struct ifnet *ifp;
	register struct mbuf *m;
	struct sockaddr *dst;
{
	register struct sl_softc *sc;
	struct ip *ip = mtod(m, struct ip *);
	int s;

	/*
	 * `Cannot happen' (see slioctl).  Someday we will extend
	 * the line protocol to support other address families.
	 */
	if (dst->sa_family != AF_INET) {
		printf("sl%d: af%d not supported\n", ifp->if_unit,
			dst->sa_family);
		m_freem(m);
		return (EAFNOSUPPORT);
	}
	if (ip->ip_dst.s_addr == 
	    satosin(&ifp->if_addrlist->ifa_addr)->sin_addr.s_addr) {
		/*
		 * It is now necessary to clear "useloopback"
		 * to force loopback traffic out to the hardware.
		 */
		if (useloopback) {
			(void) looutput(&loif, m, dst);
			/*
			 * The packet has already been sent and freed.
			 */
			return (0);
		}
	}

	sc = &sl_softc[ifp->if_unit];
	if (sc->sc_ttyp == NULL) {
		m_freem(m);
		return (ENETDOWN);	/* sort of */
	}
	if ((sc->sc_ttyp->t_state & CARR_ON) == 0) {
		m_freem(m);
		return (EHOSTUNREACH);
	}
	s = splimp();
	if (IF_QFULL(&ifp->if_snd)) {
		IF_DROP(&ifp->if_snd);
		splx(s);
		m_freem(m);
		sc->sc_if.if_oerrors++;
		return (ENOBUFS);
	}
	IF_ENQUEUE(&ifp->if_snd, m);
	if (!(sc->sc_ttyp->t_state&BUSY)) {
		if (slout(sc->sc_ttyp) == CPRES)
			(*sc->sc_ttyp->t_proc)(sc->sc_ttyp, T_OUTPUT);
	}
	splx(s);
	return (0);
}

/*
 * Start output on interface.  Get another datagram
 * to send from the interface queue and map it to
 * the interface before starting output.
 */
slout(tp)
	register struct tty *tp;
{
	register struct sl_softc *sc = find_sc(tp);
	register struct clist *outqp;
	register struct ccblock *tbuf;
	register struct mbuf *m;
	register int len;
	register u_char *cp;
	int nd, np, n, s;
	struct cblock *cbp;
	struct mbuf *m2;
	int retval;

	register c;
	register char *cptr;
	extern ttrstrt();

	outqp = &tp->t_outq;
	if (tp->t_state&TTIOW && outqp->c_cc==0) {
		tp->t_state &= ~TTIOW;
		wakeup((caddr_t)&tp->t_oflag);
	}

	for (;;) {
		tbuf = &tp->t_tbuf;

		/* free up transmit buffer from last send */
		if (tbuf->c_ptr)
			putcf(CMATCH(tbuf->c_ptr));
		if ((cbp = getcb(outqp)) == NULL) {
			tbuf->c_ptr = NULL;
		} else {
			tbuf->c_count = cbp->c_last - cbp->c_first;
			tbuf->c_size = cbp->c_last;
			tbuf->c_ptr = &cbp->c_data[cbp->c_first];
			return CPRES;
		}

		/*
		 * This happens briefly when the line shuts down.
		 */
		if (sc == NULL)
			return 0;

		/*
		 * Get a packet and send it to the interface.
		 */
		s = splimp();
		IF_DEQUEUE(&sc->sc_if.if_snd, m);
		splx(s);
		if (m == NULL)
			return 0;

		/*
		 * The extra FRAME_END will start up a new packet, and thus
		 * will flush any accumulated garbage.  We do this whenever
		 * the line may have been idle for some time.
		 */
		if (tp->t_outq.c_cc == 0)
			(void) putc(FRAME_END, &tp->t_outq);

		while (m) {
			cp = mtod(m, u_char *);
			len = m->m_len;
			while (len > 0) {
				/*
				 * Find out how many bytes in the string we can
				 * handle without doing something special.
				 */
				nd = locc(FRAME_ESCAPE, len, cp);
				np = locc(FRAME_END, len, cp);
				n = len - MAX(nd, np);
				if (n) {
					/*
					 * Put n characters at once
					 * into the tty output queue.
					 */
					if (b_to_q((char *)cp, n, &tp->t_outq))
						break;
					len -= n;
					cp += n;
				}
				/*
				 * If there are characters left in the mbuf,
				 * the first one must be special..
				 * Put it out in a different form.
				 */
				if (len) {
					if (putc(FRAME_ESCAPE, &tp->t_outq))
						break;
					if (putc(*cp == FRAME_ESCAPE ?
					   TRANS_FRAME_ESCAPE : TRANS_FRAME_END,
					   &tp->t_outq)) {
						(void) unputc(&tp->t_outq);
						break;
					}
					cp++;
					len--;
				}
			}
			MFREE(m, m2);
			m = m2;
		}

		if (putc(FRAME_END, &tp->t_outq)) {
			/*
			 * Not enough room.  Remove a char to make room
			 * and end the packet normally.
			 * If you get many collisions (more than one or two
			 * a day) you probably do not have enough clists
			 * and you should increase "nclist" in param.c.
			 */
			(void) unputc(&tp->t_outq);
			(void) putc(FRAME_END, &tp->t_outq);
			sc->sc_if.if_collisions++;
		} else
			sc->sc_if.if_opackets++;
	}
}

slsetup(sc)
	register struct sl_softc *sc;
{
	struct mbuf *p;

	if (sc->sc_buf == (char *) 0) {
		MCLALLOC(p, 1);
		if (p) {
			sc->sc_buf = (char *)p;
			sc->sc_mp = sc->sc_buf + sizeof(struct ifnet *);
		} else {
			printf("sl%d: can't allocate buffer\n", sc - sl_softc);
			sc->sc_if.if_flags &= ~IFF_UP;
			return (0);
		}
	}
	return (1);
}

/*
 * Copy data buffer to mbuf chain; add ifnet pointer ifp.
 */
struct mbuf *
sl_btom(sc, len, ifp)
	struct sl_softc *sc;
	register int len;
	struct ifnet *ifp;
{
	register caddr_t cp;
	register struct mbuf *m, **mp;
	register unsigned count;
	struct mbuf *top = NULL;

	cp = sc->sc_buf + sizeof(struct ifnet *);
	mp = &top;
	while (len > 0) {
		MGET(m, M_DONTWAIT, MT_DATA);
		if ((*mp = m) == NULL) {
			m_freem(top);
			return (NULL);
		}
		if (ifp)
			m->m_off += sizeof(ifp);
		/*
		 * If we have at least NBPG bytes,
		 * allocate a new page.  Swap the current buffer page
		 * with the new one.  We depend on having a space
		 * left at the beginning of the buffer
		 * for the interface pointer.
		 */
		if (len >= MCLBYTES/2) {
			MCLGET(m);
			if (m->m_len == MCLBYTES) {
				cp = mtod(m, char *);
				m->m_off = (int)sc->sc_buf - (int)m;
				sc->sc_buf = cp;
				if (ifp) {
					m->m_off += sizeof(ifp);
					count = MIN(len,
					    MCLBYTES - sizeof(struct ifnet *));
				} else
					count = MIN(len, MCLBYTES);
				goto nocopy;
			}
		}
		if (ifp)
			count = MIN(len, MLEN - sizeof(ifp));
		else
			count = MIN(len, MLEN);
		bcopy(cp, mtod(m, caddr_t), count);
nocopy:
		m->m_len = count;
		if (ifp) {
			m->m_off -= sizeof(ifp);
			m->m_len += sizeof(ifp);
			*mtod(m, struct ifnet **) = ifp;
			ifp = NULL;
		}
		cp += count;
		len -= count;
		mp = &m->m_next;
	}
	return (top);
}

/*
 * tty interface receiver interrupt.
 */
slin(tp, code)
	register struct tty *tp;
{
	register c;
	register struct sl_softc *sc;
	register struct mbuf *m;
	register char *cp;
	ushort nchar;
	int s;

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
	cp = tp->t_rbuf.c_ptr;

	sc = find_sc(tp);
	if (sc == NULL)
		return;
	while (nchar-- > 0) {
		c = *cp++ & 0xff;
		if (sc->sc_flags & SC_ESCAPED) {
			sc->sc_flags &= ~SC_ESCAPED;
			switch (c) {

			case TRANS_FRAME_ESCAPE:
				c = FRAME_ESCAPE;
				break;

			case TRANS_FRAME_END:
				c = FRAME_END;
				break;

			default:
				sc->sc_if.if_ierrors++;
				sc->sc_mp = sc->sc_buf
					+ sizeof(struct ifnet *);
				sc->sc_ilen = 0;
				continue;
			}
		} else {
			switch (c) {

			case FRAME_END:
				if (sc->sc_ilen == 0) /* ignore */
					continue;
				m = sl_btom(sc, sc->sc_ilen, &sc->sc_if);
				if (m == NULL) {
					sc->sc_if.if_ierrors++;
					sc->sc_mp = sc->sc_buf + 
					    sizeof(struct ifnet *);
					sc->sc_ilen = 0;
					continue;
				}
				sc->sc_mp = sc->sc_buf + sizeof(struct ifnet *);
				sc->sc_ilen = 0;
				sc->sc_if.if_ipackets++;
				s = splimp();
				if (IF_QFULL(&ipintrq)) {
					IF_DROP(&ipintrq);
					sc->sc_if.if_ierrors++;
					m_freem(m);
				} else {
					IF_ENQUEUE(&ipintrq, m);
					schednetisr(NETISR_IP);
				}
				splx(s);
				continue;

			case FRAME_ESCAPE:
				sc->sc_flags |= SC_ESCAPED;
				continue;
			}
		}
		if (++sc->sc_ilen > SLMTU) {
			sc->sc_if.if_ierrors++;
			sc->sc_mp = sc->sc_buf + sizeof(struct ifnet *);
			sc->sc_ilen = 0;
			continue;
		}
		*sc->sc_mp++ = c;
	}
}

/*
 * Process an ioctl request.
 */
slifioctl(ifp, cmd, data)
	register struct ifnet *ifp;
	int cmd;
	caddr_t data;
{
	register struct ifaddr *ifa = (struct ifaddr *)data;
	int s = splimp(), error = 0;

	switch (cmd) {

	case SIOCSIFADDR:
		if (ifa->ifa_addr.sa_family == AF_INET)
			ifp->if_flags |= IFF_UP;
		else
			error = EAFNOSUPPORT;
		break;

	case SIOCSIFDSTADDR:
		if (ifa->ifa_addr.sa_family != AF_INET)
			error = EAFNOSUPPORT;
		break;

	default:
		error = EINVAL;
	}
	splx(s);
	return (error);
}


struct sl_softc *
find_sc(tp)
    struct tty *tp;
{
    register struct sl_softc *sc;

    for (sc = sl_softc; sc < &sl_softc[NSL]; sc++)
	if (sc->sc_ttyp == tp)
	    return sc;
    return NULL;
}

locc(mask, size, cp)
	register u_char mask;
	u_int size;
	register u_char *cp;
{
	register u_char *end = &cp[size];
	
	while (cp < end && *cp != mask)
		cp++;
	return (end - cp);
}

/*
 * copy buffer to clist.
 * return number of bytes not transfered.
 */
b_to_q(cp, cc, q)
	register char *cp;
	struct clist *q;
	register int cc;
{
	register struct cblock *obp, *bp;
	register s, nc;
	int acc;

	if (cc <= 0)
		return (0);
	acc = cc;
	s = spl6();
	while (cc) {
		if ((bp = q->c_cl) == NULL || bp->c_last == cfreelist.c_size) {
			obp = bp;
			if (cfreelist.c_next == NULL) 
				goto out;
			bp = cfreelist.c_next;
			cfreelist.c_next = bp->c_next;
			bp->c_next = NULL;
			bp->c_first = bp->c_last = 0;
			if (q->c_cf == NULL)
				q->c_cf = bp;
			else {
				obp->c_next = bp;
			}
			q->c_cl = bp;

		}
		nc = MIN(cc, cfreelist.c_size - bp->c_last);
		(void) bcopy(cp, &bp->c_data[bp->c_last], (unsigned)nc);
		cp += nc;
		bp->c_last += nc;
		cc -= nc;
	}
out:
	q->c_cc += acc - cc;
	splx(s);
	return (cc);
}

