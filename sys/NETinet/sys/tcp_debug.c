#ifndef lint	/* .../sys/NETinet/sys/tcp_debug.c */
#define _AC_NAME tcp_debug_c
#define _AC_NO_MAIN "@(#) Copyright (c) 1983-87 Sun Microsystems Inc., All Rights Reserved.  {Apple version 1.3 88/05/22 20:22:00}"
#include <apple_notice.h>

#ifdef _AC_HISTORY
  static char *sccsid = "@(#)Copyright Apple Computer 1987\tVersion 1.3 of tcp_debug.c on 88/05/22 20:22:00";
#endif		/* _AC_HISTORY */
#endif		/* lint */

#define _AC_MODS
/*	@(#)tcp_debug.c 1.1 86/02/03 SMI; from UCB 4.10 83/05/14        */
/*	@(#)tcp_debug.c	2.1 86/04/15 NFSSRC */

#ifdef TCPDEBUG
#define TCPSTATES
#define	TCPTIMERS
#define	TANAMES
#define PRUREQUESTS
#endif

#include "sys/param.h"
#include "sys/errno.h"
#include "sys/types.h"
#include "sys/time.h"
#ifdef	PAGING
#include "sys/mmu.h"
#include "sys/page.h"
#endif
#include "sys/systm.h"
#include "sys/mbuf.h"
#include "sys/socket.h"
#include "sys/socketvar.h"
#define PRUREQUESTS
#include "sys/protosw.h"
#include "netinet/in.h"
#include "net/route.h"
#include "netinet/in_pcb.h"
#include "netinet/in_systm.h"
#include "net/if.h"
#include "netinet/ip.h"
#include "netinet/ip_var.h"
#include "netinet/tcp.h"
#define TCPSTATES
#include "netinet/tcp_fsm.h"
#include "netinet/tcp_seq.h"
#define TCPTIMERS
#include "netinet/tcp_timer.h"
#include "netinet/tcp_var.h"
#include "netinet/tcpip.h"
#define TANAMES
#include "netinet/tcp_debug.h"
#include "errno.h"

#ifdef TCPDEBUG
struct	tcp_debug tcp_debug[TCP_NDEBUG];
int	tcp_debx;
#endif TCPDEBUG
int	tcpconsdebug = 0;

/*
 * Tcp debug routines
 */
tcp_trace(act, ostate, tp, ti, req)
	short act, ostate;
	struct tcpcb *tp;
	struct tcpiphdr *ti;
	int req;
{
	tcp_seq seq, ack;
	int len, flags;
	struct tcp_debug *td = &tcp_debug[tcp_debx++];

	if (tcp_debx == TCP_NDEBUG)
		tcp_debx = 0;
	td->td_time = iptime();
	td->td_act = act;
	td->td_ostate = ostate;
	td->td_tcb = (caddr_t)tp;
	if (tp)
		td->td_cb = *tp;
	else
		bzero((caddr_t)&td->td_cb, sizeof (*tp));
	if (ti)
		td->td_ti = *ti;
	else
		bzero((caddr_t)&td->td_ti, sizeof (*ti));
	td->td_req = req;
	if (tcpconsdebug == 0)
		return;
	if (tp)
		printf("%x %s:", tp, tcpstates[ostate]);
	else
		printf("???????? ");
	printf("%s ", tanames[act]);
	switch (act) {

	case TA_INPUT:
	case TA_OUTPUT:
	case TA_DROP:
		if (ti == 0)
			break;
		seq = ti->ti_seq;
		ack = ti->ti_ack;
		len = ti->ti_len;
		if (act == TA_OUTPUT) {
			seq = ntohl(seq);
			ack = ntohl(ack);
			len = ntohs((u_short)len);
		}
		if (act == TA_OUTPUT)
			len -= sizeof (struct tcphdr);
		if (len)
			printf("[%x..%x)", seq, seq+len);
		else
			printf("%x", seq);
		printf("@%x, urp=%x", ack, ti->ti_urp);
		flags = ti->ti_flags;
		if (flags) {
#ifndef lint
			char *cp = "<";
#define pf(f) { if (ti->ti_flags&TH_/**/f) { printf("%s%s", cp, "f"); cp = ","; } }
			pf(SYN); pf(ACK); pf(FIN); pf(RST); pf(PUSH); pf(URG);
#endif
			printf(">");
		}
		break;

	case TA_USER:
		printf("%s", prurequests[req&0xff]);
		if ((req & 0xff) == PRU_SLOWTIMO)
			printf("<%s>", tcptimers[req>>8]);
		break;
	}
	if (tp)
		printf(" -> %s", tcpstates[tp->t_state]);
	/* print out internal state of tp !?! */
	printf("\n");
	if (tp == 0)
		return;
	printf("\trcv_(nxt,wnd,up) (%x,%x,%x) snd_(una,nxt,max) (%x,%x,%x)\n",
	    tp->rcv_nxt, tp->rcv_wnd, tp->rcv_up, tp->snd_una, tp->snd_nxt,
	    tp->snd_max);
	printf("\tsnd_(wl1,wl2,wnd) (%x,%x,%x)\n",
	    tp->snd_wl1, tp->snd_wl2, tp->snd_wnd);
}
