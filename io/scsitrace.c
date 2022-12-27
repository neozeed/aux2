/*
 * @(#)scsitrace.c  {Apple version 2.4 90/03/13 12:25:30}
 *
 * Copyright (c) 1987, 1988, 1989 by Apple Computer, Inc.
 * All Rights Reserved.
 *
 * THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF APPLE COMPUTER, INC.
 * The copyright notice above does not evidence any actual or
 * intended publication of such source code.
 */

#if !defined(lint)
static char _sccsid[]="@(#)scsitrace.c  {Apple version 2.4 90/03/13 12:25:30}";
#endif

/*
 *           A/UX SCSI MANAGER
 *
 *    scsitrace - scsi trace routines
 *
 */

#ifdef lint
#include "sys/sysinclude.h"
#else lint
#include "sys/types.h"
#ifndef	STANDALONE
#include "sys/param.h"
#include "sys/uconfig.h"
#include "sys/var.h"
#endif	STANDALONE
#include "sys/debug.h"
#endif lint

#include <sys/vio.h>
#include <sys/ncr.h>
#include <sys/scsiccs.h>
#include <sys/scsivar.h>
#include <sys/scsitask.h>
#include <sys/scsireq.h>
#include <sys/scsifsm.h>
#include <sys/via6522.h>


/*	watchdog -- abort an I/O that takes too long.
 *	     There is a two layer timeout.  We monitor specific tasks to see
 *	if they have been inactive greater than the longest permissible
 *	inactive time (set by the caller).  Once a task has gone over its
 *	time limit we start setting the unjam flag.  The unjam flag is
 *	cleared and tested by low level routines.  If it becomes clear, we
 *	assume something is still running.  If it stays set, then we begin
 *	eviction proceedings.  A similar process is used for disconnected
 *	jobs that aren't coming back.  When we set the stuckdisc flag, we
 *	stop bus activity.  If the job has not come back within another timeout
 *	interval, then we reset the bus.
 */
#define TRACESNAP(x) tracesnap(x)

watchdog()

{
	struct ncr5380 *ncr = (struct ncr5380 *)scsi_addr;
	register struct tasks *stp;
	register i;
	register u_long dmactl;

#ifndef	STANDALONE

	timeout(watchdog, 0, TOUTTIME * v.v_hz);

	if (curstp->flags & SF_HWDMA) {		/* can't touch during DMA */
	    return;
	}

	if((1 << sg_state) & ((1 << SG_WRESET) | (1 << SG_ABORTJ) 
	   | (1 << SG_RESET)))
		return;

	/*TRACESCSI( traceltod( unjam,
		    tracescpy("watchdog, unjam ",sstbuf));
		     tracescsi(sstbuf));*/
	if(unjam) {
		if(unjam == 1) {
			++unjam;
			tracescsi("wd:problem with disk noticed");

			/* If the current task is jammed, don't
			 * count the time against the disc jobs.
			 */
			for(i = 0, stp = tasks; i < NSCSI; ++i, ++stp) {
				if(stp->flags & SF_DISCON)
					stp->curtime = 0;
			}
		}
		else if(unjam++ >= 2) {
			unjam = 0;

			if (tracescsi("wd:RST active disk"))
			    TRACESNAP(20);

			if (machineID == MACIIfx) {
			    dmactl = ncr->dmactl;
			    dmactl &= ~(SCSI_WDIRQENAB | SCSI_DMAENAB);
			    ncr->wdog_timer = 0L;
			    HW_SHAKE(0);	/* h/w handshake off */
			}

			SCSI_IRQ(1);		/* enable IRQ */
			ncr->init_comm = SIC_RST;

			if (machineID == MACIIfx) {
			    ncr->dmactl = dmactl;	/* restore it */
			}
			return;
		}

	}
	else if(stuckdisc >= 0) {
	    if( laststuck != jobstep) {
		laststuck = jobstep;
	    }
	    else {
		if (tracescsi("wd:RST disconnected disk"))
		    TRACESNAP(20);

		    if (machineID == MACIIfx) {
			dmactl = ncr->dmactl;
			dmactl &= ~(SCSI_WDIRQENAB | SCSI_DMAENAB);
			ncr->wdog_timer = 0L;
			HW_SHAKE(0);		/* h/w handshake off */
		    }

		    SCSI_IRQ(1);		/* enable IRQ */
		    ncr->init_comm = SIC_RST;

		    if (machineID == MACIIfx) {
			ncr->dmactl = dmactl;	/* restore it */
		    }
		    return;
	    }
	} else {	/* look for a running task on overtime */
	    for(i = 0, stp = tasks; i < NSCSI; ++i, ++stp) {
		if((stp->flags & SF_RUN) &&
		   stp->maxtime != SRT_NOTIME &&
		   (stp->curtime += TOUTTIME) > stp->maxtime + TOUTTIME) {

		    if(stp->flags & SF_DISCON) {
			stuckdisc = stp - tasks;
			laststuck = 0;
			TRACE(1, ("scsi stuck: %d\n", stp - tasks));
			if (tracescsi(&("012345678 dead discon")[stp-tasks]))
			    TRACESNAP(20);
			break;
		    }
		    else {
			if (tracescsi(&("012345678 jammed")[stp-tasks])) {
			    TRACE(1, ("scsi jammed: %d\n", stp - tasks));
			    TRACE(1, ("global state %s phase %s\n",
						state_atribs[sg_state].name,
						pstatenames[stp->pstate]));
			    TRACESNAP(20);
			}
			unjam = 1;
			break;
		    }
		}
	    }
	}
#endif	STANDALONE
}


static char *statep[] =	{"DOU ","DIN ","CMD ","STA ",
			 "004?","005?","MOU ","MIN "};
#undef printscsi
printscsi(s)
	char *s;
{
	register struct ncr5380 *ncr;
	register struct tasks *stp;
	register i;
	register u_long dmactl;
	register int dma;
	u_char	curr_data, init_comm, mode, targ_comm, curr_stat, bus_status,
		in_data;

	ncr = (struct ncr5380 *)scsi_addr;

	dma = curstp->flags & SF_HWDMA;		/* remember if DMA active */

	if (!dma && machineID == MACIIfx) {
	    dmactl = ncr->dmactl;
	    ncr->dmactl = (dmactl & ~SCSI_HSENAB);	/* h/w handshake off */
	}

	curr_data	= dma ? 0 : ncr->curr_data;
	init_comm	= dma ? 0 : ncr->init_comm;
	mode		= dma ? 0 : ncr->mode;
	targ_comm	= dma ? 0 : ncr->targ_comm;
	curr_stat	= dma ? 0 : ncr->curr_stat;
	bus_status	= dma ? 0xff : ncr->bus_status;
	in_data		= dma ? 0 : ncr->in_data;

	if (!dma && machineID == MACIIfx) {
	    ncr->dmactl = dmactl;			/* restore it */
	}

	printf( "\nscsi %s",state_atribs[sg_state].name);
	printf( "(%s)",state_atribs[sg_oldstate].name);
	printf( statep[(curr_stat>>2)&07]);
	printf(curr_stat &0x80?" RST":"");
	printf(curr_stat &0x40?" BSY":"");
	printf(curr_stat &0x20?" REQ":"");
	printf(curr_stat &0x02?" SEL":"");

	printf(bus_status&0x80?" EOD":"");
	printf(bus_status&0x40?" DRQ":"");
	printf(bus_status&0x20?" PTE":"");
	printf(bus_status&0x10?" IRQ":"");
	printf(bus_status&0x08?" PHM":"");
	printf(bus_status&0x04?" BSE":"");
	printf(bus_status&0x02?" ATN":"");
	printf(bus_status&0x01?" ACK":"");

	printf(" IC=%x",init_comm);
	printf(" MD=%x",mode);
	printf(" TC=%x",targ_comm);
	printf(" CD=%x",curr_data);
	printf(" np=%x",newphase);
	printf(" ls=%s",pstatenames[laststate]);
	/*printf(" spl%x", (spl()&0x700)>>8);*/
	printf(" %s\n",s?s:"");

	for(i = 0, stp = tasks; i < NSCSI; ++i, ++stp) {
		if( !(stp->flags & SF_RUN) )
			continue;
		printf("scsi task %d max=%d cur=%d ", i,
		   		stp->maxtime, stp->curtime);
		printf( (stp->flags&SF_DISCON) ? "disc" : "actv" );
		printf(" stp->pstate=%s", pstatenames[stp->pstate]);
		printf(" msi=%x", stp->msgin);
		printf(" sta=%x", stp->stat);
		printf(" flg=%x", stp->flags);
		printf("\n");
	}
}

#define NTRACE 50
struct traces{
	unsigned char tcd, tic, tmd, ttc, tcs, tbs, tid, tps, tspl;
	long tds, tdl, tjs, tiow;
	unsigned short dcl,dct;
	char tchkcncr;
	char ts[25];
} trbuf[NTRACE];
static long tracecnt = 0;
static long prev_iow = 0;

/*
 *  tracescsi -- record scsi bus cycles.  Tracescsi saves key NCR 5380
 *  hardware registers and manager variables in a circular trace buffer.
 */
tracescsi( str )
	char *str;
{
	register struct ncr5380 *ncr;
	register struct traces *tp;
	register u_long dmactl = 0xffff;
	register int dma;
	register int s;
	struct scsireq *req;

	if ( !(curstp->flags & SF_TRACE) )
		return(0);			/* not requested this id */
	
	ncr = (struct ncr5380 *)scsi_addr;
	s = spl7();

	dma = curstp->flags & SF_HWDMA;		/* remember if DMA active */

	if (!dma && machineID == MACIIfx) {
	    dmactl = ncr->dmactl;		/* remember current state */
	    ncr->dmactl = (dmactl & ~SCSI_HSENAB);	/* h/w handshake off */
	}

	tp = &trbuf[tracecnt%NTRACE];
	req = curstp->req;

	tp->tcd = dma ? 0 : ncr->curr_data;
	tp->tic = dma ? 0 : ncr->init_comm;
	tp->tmd = dma ? 0 : ncr->mode;
	tp->ttc = dma ? 0 : ncr->targ_comm;
	tp->tcs = dma ? 0 : ncr->curr_stat;
	tp->tbs = dma ? 0xff : ncr->bus_status;

	if (!dma && machineID == MACIIfx) {
	    ncr->dmactl = dmactl;		/* restore it */
	}

	tracesncpy( str, tp->ts, 20);
	tp->tjs = jobstep;
	tp->tid = curtask;
	tp->tps = curstp->pstate;
	tp->tds = req->datasent;
	tp->tdl = req->datalen;
	tp->tspl= (s >> 8) & 7;
	tp->tchkcncr = checkconcur;

	tp->tiow = iowaits - prev_iow;	/* iowaits to this phase */
	prev_iow = iowaits;

	if (machineID == MACIIfx) {
	    tp->dct = dma ? 0xffff : ncr->dma_count;	/* more useful stuff */
	    tp->dcl = dma ? 0xffff : dmactl;
	}

	splx(s);

	if ( (curstp->flags&SF_TRACEPR) )
		traceprint(0);
	else if ( (tracecnt&0177777) == 999999 )
		tracesnap(8);
	++tracecnt;
	return (1);
}

/* tracesnap - print n lines SCSI bus history */
tracesnap(n)
{
	printscsi("tracesnap");		/* current status */
	while ( n > 0 )
		traceprint( -n--);	/* n lines backwards */
}

#define traceprf printf
int scsitracespeed = 100;		/* larger to slow down */

traceprint(i)
	int i;		/* offset from current tracecnt to print */
{
	int ph;
	register struct traces *tp = &trbuf[(tracecnt+i+NTRACE)%NTRACE];

	/*traceprf( "%d ", tp->tchkcncr);*/
	traceprf( "%x ", tp->tspl);
	traceprf( "%d.", tp->tjs);
	traceprf( "%x ", tp->tid);
	ph =  (tp->tcs>>2)&7;
	if ( tp->tcs&SCS_SEL )
		traceprf( (tp->tcs&04) ? "ReS " : "SEL ");
	else
		traceprf( (tp->tcs&SCS_BSY) ? statep[ph] : "    " );

	traceprf( "%c%c%c", pstatenames[tp->tps][3],pstatenames[tp->tps][4],
						    pstatenames[tp->tps][5]);
	traceprf( " %x%x" , tp->tcd/16, tp->tcd%16 );

	traceprf( " cs=");
	traceprf( (tp->tcs & SCS_RST) ? "X" : " ");
	traceprf( (tp->tcs & SCS_BSY) ? "B" : " ");
	traceprf( (tp->tcs & SCS_REQ) ? "R" : " ");
	/*traceprf( "%s", statep[ph]);	/* phase */
	traceprf( (tp->tcs & SCS_SEL) ? "S" : " ");
	traceprf( (tp->tcs &       1) ? "." : " ");

	traceprf( " bs=");
	if (tp->tbs == 0xff) {		/* n/a due to DMA in progress */
	    traceprf("........");
	} else {
	    traceprf( (tp->tbs & SBS_EOP) ? "E" : " ");
	    traceprf( (tp->tbs & SBS_DMA) ? "D" : " ");
	    traceprf( (tp->tbs & SBS_PTE) ? "P" : " ");
	    traceprf( (tp->tbs & SBS_IRQ) ? "I" : " ");
	    traceprf( (tp->tbs & SBS_PHASE)?"M" : " ");
	    traceprf( (tp->tbs & SBS_BSY) ? "B" : " ");
	    traceprf( (tp->tbs & SBS_ATN) ? "^" : " ");
	    traceprf( (tp->tbs & SBS_ACK) ? "A" : " ");
	}

	if (machineID == MACIIfx) {
	    if (tp->dcl == 0xffff) {	/* n/a due to DMA in progress */
		traceprf("dcl=....");
	    } else {
		traceprf( "dcl=%x%x%x%x",tp->dcl/4096,(tp->dcl/256)%256,
						(tp->dcl/16)%16,tp->dcl%16);
	    }
	} else {
	    traceprf( " ic=");
	    traceprf( (tp->tic & SIC_RST) ? "r" : " ");
	    traceprf( (tp->tic & SIC_AIP) ? "a" : " ");
	    traceprf( (tp->tic & SIC_LA ) ? "L" : " ");
	    traceprf( (tp->tic & SIC_ACK) ? "a" : " ");
	    traceprf( (tp->tic & SIC_BSY) ? "b" : " ");
	    traceprf( (tp->tic & SIC_SEL) ? "s" : " ");
	    traceprf( (tp->tic & SIC_ATN) ? "!" : " ");
	    traceprf( (tp->tic & SIC_DB ) ? "d" : " ");
	}
	traceprf( " md=");
	traceprf( (tp->tmd & SMD_EOP) ? "e" : " ");
	traceprf( (tp->tmd & SMD_BSY) ? "b" : " ");
	traceprf( (tp->tmd & SMD_DMA) ? "d" : " ");
	traceprf( (tp->tmd & SMD_ARB) ? "a" : " ");

	if (machineID == MACIIfx) {		/* show DMA count, not iow */
	    if (tp->dct == 0xffff) {
		traceprf(" .... ");
	    } else {
		traceprf( " %d%d%d%d ",tp->dct/1000,(tp->dct/100)%10,
						(tp->dct/10)%10,tp->dct%10);
	    }
	} else {
	    if ( tp->tiow < 100 ) {
		traceprf(" w%d%d  ", tp->tiow/10, tp->tiow%10);
	    } else {
		traceprf(" %d%d%d  ",tp->tiow/100,(tp->tiow/10)%10,tp->tiow%10);
	    }
	}
	if ( tp->tds && (tp->tcs&SCS_BSY) )
		traceprf( "%d/%d ", tp->tds, tp->tdl);
	traceprf( "%s\n", tp->ts);
	for(i=scsitracespeed;--i;)
		;
}
#ifdef notusedanymore
rdspl()
{
	asm( "mov.w %sr,%d0");
}
#endif
Assfail(e,f,l)
{
	pre_panic();
	tracesnap(20);
	printf("ASSERTION on %s:%d failed: %s\n", f, l, e);
	printf("checkconcur = %d\n",checkconcur);
	panic("assert manure");
}
/* tracescpy returns end+1 of target string */
char *
tracescpy(f,t)
	register char *f, *t;
{
	while ( *t++ = *f++ )
		;
	return (--t);	/* point at zero char */
}

/* tracesncpy returns end+1 of target string */
char *
tracesncpy(f,t,n)
	register char *f, *t; register n;
{
	while ( n-- && (*t = *f++) )
		++t;
	*t = '\0';
	return (t);	/* point at ending zero char */
}

/* tracestox( f, t, n) - string to hex string */
#define addch(s,c) ((*s++ = (c)),s)
#define hexnibb(s,x) addch(s,"0123456789ABCDEF"[(x)&0xF])
#define hexbyte(s,x) (hexnibb(s,(x)>>4),hexnibb(s,(x)))
#define hexshor(s,x) (hexbyte(s,(x)>>8),hexbyte(s,(x)),addch(s,' '))
#define hexlong(s,x) (hexshor(s,(x)>>16),hexshor(s,(x)))

char *
tracestox( f, t, n)
	unsigned char *f;
	unsigned char *t;
{
	for (;n>=4;n-=4)  {
		hexbyte( t, *f); ++f;
		hexbyte( t, *f); ++f;
		hexbyte( t, *f); ++f;
		hexbyte( t, *f); ++f;
		addch(t, ' ');
	}
	for( ; n-- > 0;) {
		hexbyte( t, *f); ++f;
	}
	*t++ = ' ';
	*t   = '\0';
	return (char *)t;
}

static char *
traceltobase( l, s, base)
	long l;
	char *s;
	int base;
{
	if ( l >= base )
		s = traceltobase( l / base, s, base);
	*s++ = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ"[ l % base];
	*s = '\0';
	return (char *)s;
}
/* long to decimal string */
char *
traceltod( l, s)
{
	return traceltobase(l,s,10);
}
/* long to hex string */
char *
traceltox( l, s)
{
	return traceltobase(l,s,16);
}

struct g0t {
	u_char	g0op;
	char   *g0nam;
};

struct g0t g0tab[] = {
	{0x00, "Test Unit Ready"},
	{0x01, "Rezero Unit"},
	{0x03, "Request Sense"},
	{0x08, "Read"},
	{0x0A, "Write"},
	{0x0B, "Seek"},
	{0x12, "Inquiry"},
	{0x15, "Mode Select"},
	{0x16, "Reserve Unit"},
	{0x17, "Release Unit"},
	{0x1A, "Mode Sense"},
	{0x1B, "Start/Stop Unit"},
	{0x1C, "Receive Diagnostic"},
	{0x1D, "Send Diagnostic"},
	{0x1E, "Media Removal"},
	/* g1 */
	{0x25, "Read Capacity"},
	{0x28, "Read Extended"},
	{0x2B, "Seek Extended"},
	{0x2F, "Verify"},
	{0x3B, "Write Buffer"},
	{0x3C, "Read Buffer"},
	/* g6 */
	{0xC0, "Eject Disk"},
	{0xC1, "Read Toc"},
	{0xC2, "Read Q Subcode"},
	{0xC3, "Read Header"},
	{0xC8, "Audio Track Search"},
	{0xC9, "Audio Play"},
	{0xCA, "Audio Pause"},
	{0xCB, "Audio Stop"},
	{0xCC, "Audio Status"},
	{0xCD, "Audio Scan"},
	{0xFF, (char *)0},
};

char *
tracecmd( c0p )
	struct scsig0cmd *c0p;
{
	register struct g0t *gp;
	register i;

	for( i=0, gp = g0tab; gp->g0nam; i++, gp++)
		if ( gp->g0op == c0p->op )
			break;
	return (gp->g0nam) ? gp->g0nam : "?g0op";
}

struct msgt {
	u_char msg;
	char   *name;
};
struct msgt msgtab[] = {
	0x00, "Command Complete",
	0x01, "Extended Message",
	0x02, "Save Pointers",
	0x03, "Restore Pointers",
	0x04, "Disconnect",
	0x05, "Initiator Err",
	0x06, "Abort",
	0x07, "Message Reject",
	0x08, "No Operation",
	0x09, "Message Parity",
	0x0A, "Linked Cmd Complete",
	0x0B, "Lnkd w/flg Complete",
	0x0C, "Bus Device Reset",
	0x80, "Targ Ident Lun 0",
	0xC0, "Init Ident Lun 0",
	0xFF, (char *)0,
};

char *
tracemsg( msg )
	u_char msg;
{
	register struct msgt *gp;
	register i;

	for( i=0, gp = msgtab; gp->name; i++, gp++)
		if ( gp->msg == msg )
			break;
	return (gp->name) ? gp->name : "?msg";
}

char *stattab[16] = {
	"Good", "Check Condition", "Condit Met/Good", "reserved",
	"Busy", "reserved", "reserved", "reserved",
	"Intermediate/Good", "reserved", "Inter Met/Good", "reserved",
	"Reservation Conflict", "reserved", "reserved", "reserved",
};
	
char *
tracestat( stat)
	u_char stat;
{
	register i;

	return stattab[ 0xF & (stat >> 1) ];
}
		
viodmp(s,vp)
	char *s;
	struct vio  *vp;
{
	static int nvp = 0;
	int  i, nv, nb;
	struct iov  *iov;

	if ( vp == (struct vio *)NULL)  {
		printf("null viodmp%d-%s", ++nvp, s);
		return;
	}

	nv = vp->vio_niov;
	nb = vp->vio_ngdiskbp;
	if ( nv > 16 || nb > 16 || nv < 0 || nb < 0 ) {
	    pre_panic();
	    if ( nv < 0 || nv > 16 ) printf("nv???");
	    if ( nb < 0 || nb > 16 ) printf("nb???");
	    panic("viodmp vio");
	}

	printf("viodmp%d %s vio @ %x, nv%d, nb%d\n", nvp, s, vp, nv, nb);

	for ( i = 0; i < nv; i++)  {
		iov = &vp->vio_iov[i];
		printf("iov[%d]=<base=%x,len=%d,flags=%x>",
			i, iov->vio_base, iov->vio_len, iov->vio_flags);
		printf(",  bp[%d]= %x\n", i, i<nb?vp->vio_gdiskbp[i]:0);
	}

}
