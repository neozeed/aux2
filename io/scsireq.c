/*
 * @(#)scsireq.c  {Apple version 2.3 90/03/13 12:25:12}
 *
 * Copyright (c) 1987, 1988, 1989 by Apple Computer, Inc.
 * All Rights Reserved.
 *
 * THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF APPLE COMPUTER, INC.
 * The copyright notice above does not evidence any actual or
 * intended publication of such source code.
 */

#if !defined(lint)
static char _sccsid[]="@(#)scsireq.c  {Apple version 2.3 90/03/13 12:25:12}";
#endif

/*
 *         A/UX SCSI MANAGER 
 *
 *  scsireq - driver request interface
 *	   scsireq
 *	   scsichar
 *	   scsig0cmd
 *	   scsicancel
 *
 */



/*
 *
 *			       A/UX Scsi Manager
 *
 *  	             Version 1.0 Manager by Randy Zachs/Unisoft
 *  	            Version 1.1 Manager by Gene Dronek/Vulcan Lab
 *
 *
 *	                   Ver 1.1 File Logical Flow:
 *
 *				      driver
 *	     	      ========================================
 *		      |      scsireq.h         scsireq.c     |
 *		      ----------------------------------------
 *		      |           | scsifsm.h     scsifsm.c  |
 *		      | scsivar.h |--------------------------|
 *		      |           | scsitask.h    scsitask.c |
 *		      |           | scsiccs.h                |
 *		      |           | ncr.h                    |
 *		      |           |--------------------------|
 *		      |           |              scsitrace.c |
 *		      ========================================
 *				     ncr5380
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
/* #include "sys/stream.h"*/
#endif lint

#include <sys/vio.h>
#include <sys/ncr.h>
#include <sys/scsiccs.h>
#include <sys/scsireq.h>
#include <sys/scsivar.h>
#include <sys/scsifsm.h>
#include <sys/scsitask.h>
#include <sys/via6522.h>

extern int curpri;
extern int switching;

#ifdef HOWFAR
int	T_scsi = 0;
#endif HOWFAR

/*
 *	field debugging - forced scsi bus trace and print control.
 *	Manager will record or record and print scsi bus activity for
 *	each phase of requests for this id;  -1 to ignore, 8 means all.
 */
short	scsitraceid = -1;	/* record only     requests this id */
short	scsitraceprid = -1;	/* print trace all requests this id */

static char sstbuf[100];	/* scratch string trace buffer */
static	short isinit = 0;	/* manager initialization is done */



/*****  Scsireq -- Enter request to the A/UX Scsi Manager *******************
 *
 *	     This is the driver interface to the Scsi Manager.
 *	We copy request pointers into a global "tasks" array, and
 *	kick start the scsitask finite-state-machine (FSM)
 *	if it is not already running, and RETURN IMMEDIATELY.  (Note,
 *	it is possible to get SST_MULT here among others)
 *
 *	     Later, the scsitask FSM will run and, (mostly at spl0), 
 *	control the NCR 5380 chip through various Initiator/Target bus
 *	actions, until the given transfer is completed.  Only then,
 *	the FSM will call the the request's "call-back" completion
 *	function (donotify) with the request completion code set.
 *
 *	See "scsireq.h" for request flags and completion codes.
 *
 */

scsireq(id, req, vp)
	register struct scsireq *req;	/* request pointer */
	struct  vio *vp;		/* vio dma pointer */
{
	register s;
	register struct tasks *stp;
	extern int runrun;
	static long numscsireq = 0;
	extern long sys_ncrbusy, sys_ncrfree;
	int bus;

	TRACE(T_scsi, ("scsirequest(%d,0x%x,0x%x)\n",id,req,vp));

	/*  detect using old request struct */
	if ( req->reserve1 )  {	/* test old timeout byte */
		printf("must recompile, id %d\n", id);
		return (req->ret = SST_REVCODE);/* must recompile */
	}

	/*  patch up recompiled 1.0 call  */
	if ( req->revcode[0] == 0)  {
		req->revcode[0] = REVCODE[0];
		req->revcode[1] = REVCODE[1];
		req->revcode[2] = REVCODE[2];
	}

	/*
	 *  Call alternate scsimanager if indicated
	 */
	bus = id / 8;
	if ( bus < 0 || bus >= NSCSIMGRS )
		return (req->ret = SST_SEL);
	else  {
		if ( scsimgrs[bus].s_req != scsireq )
			return ( (*scsimgrs[bus].s_req) (id,req,vp) );
		else
			id  = id % 8;
	}

	if(!isinit)
		scsiinit();
	ASSERT(isinit);

	/*
	 *       Make sure caller and manager versions compatible.
	 *  The first 3 characters of revcode must match.  It is
	 *  reccommended the user "initialize" the revcode field with
	 *  the string REVCODE, but just copying three chars is
	 *  effective too.
	 */
	if ( !(req->revcode[0] == REVCODE[0] &&
	       req->revcode[1] == REVCODE[1] &&
	       req->revcode[2] == REVCODE[2]) )  {
		printf("revcode mismatch, id %d\n", id);

		return(req->ret = SST_REVCODE);
	}

	/* must have completion funct */
	if ( req->faddr == NULL )	
		return (req->ret = SST_PROT);	

	/* sense boot-time scsi trace/print */
	if ( id == scsitraceid || scsitraceid == 8)
		req->flags |= SRQ_TRACE;
	if ( id == scsitraceprid || scsitraceprid == 8)
		req->flags |= SRQ_TRACEPR;

	/*
	 *  If vp null, build a single iov from the supplied databuf
	 *  and datalen.  Otherwise, copy supplied dma vectors
	 *  into scsitask struct.  This way, 1.0 users need only
	 *  recompile to use the 1.1 manager.  Also, 1.1 users who
	 *  don't need dma control can instead pass a null vp.
	 */

	if ( vp == (struct vio *)NULL )  {
		req->vio.vio_tot = 0;
		IOVADD( &req->vio, 0,(struct buf *)0, req->databuf, req->datalen);
	}  else  {
		VIOCOPY( vp, &req->vio);
	}
	req->sensesent = 0;
	req->datasent  = 0;
	req->niovsent  = 0;
	req->stat      = 0;
	req->msg       = 0;
	req->ret       = 0;
	if(id >= NSCSI) {
		return(req->ret = SST_SEL);
	}
	s = splintr();
	stp = &tasks[id];
	if(stp->flags & SF_JOB)  {
		splx(s);
		return(req->ret = SST_MULT);
	}
	stp->cmdbuf   = req->cmdbuf;
	stp->cmdlen   = req->cmdlen;
	stp->databuf  = req->databuf;
	stp->datalen  = req->datalen;
	stp->datasent = 0;
	stp->niovsent = 0;
	stp->msgin    = 0;
	stp->stat     = 0;
	stp->flags    = SF_JOB;
	stp->lun =  ((struct scsig0cmd *)(req->cmdbuf))->addrH >> 5 & 7;
	/* if timeout zero, supply default */
	if ( (stp->maxtime = req->timeout) == 0 )	
		stp->maxtime  = 1 + 1*(vp->vio_tot>>10);
	if((req->flags & SRQ_SPLINT) || numscsireq%2==2)
		stp->flags |= SF_SPLINT;
	if((req->flags & SRQ_READ))
		stp->flags |= SF_READ;
	if((req->flags & SRQ_EXCL))
		stp->flags |= SF_EXCL;
	if((req->flags & SRQ_NOINTR))
		stp->flags |= SF_NOINTR;
	if((req->flags & SRQ_TRACE) || (stp->devchar & SDC_TRACEPR))
		stp->flags |= SF_TRACE;
	if((req->flags & SRQ_TRACEPR)||(stp->devchar & SDC_TRACEPR))
		stp->flags |= (SF_TRACEPR|SF_TRACE);
	stp->req = req;


#ifdef notdef
	/*  bill at 30 requests/sec, ie 32ms/req */
	if ( u.u_procp->p_cpu < 80)	/* what means 80? */
		u.u_procp->p_cpu += 2;
	traceltod( runrun,
         tracescpy(" r-",
	  traceltod( switching,
           tracescpy(" s-",
	    traceltod( u.u_procp->p_pri,
	     tracescpy("/",
	      traceltod( calcppri(u.u_procp),
	       tracescpy("pri ",sstbuf))))))));
	tracescsi(sstbuf);
#endif
	/*
	 *  If chip if not busy, kick it.  Otherwise simply
	 *  flag multiple requests are present.  The scsitask FSM will
	 *  eventually find it.
	 */
	if(sg_state == SG_IDLE || sg_state == SG_WRECON)
		scsisched(SI_NEWTASK, "scsirequest");
	else if(!(sg_state == SG_NOTIFY || sg_state == SG_WRESET))
		lastmult = jobstep;
	splx(s);
#ifdef notdef
	if ( !(++numscsireq % 100) ) {
		traceltod( sys_ncrbusy*100/(sys_ncrbusy+sys_ncrfree),
			tracescpy("bus util ",sstbuf));
		if (tracescsi(sstbuf))
		    if ( !(numscsireq % 1000) )
			    tracesnap(15);
		sys_ncrbusy = 0; sys_ncrfree = 0;
	}
#endif
	return(req->ret);	/* note, returns immediately */
}



/*****  scsichar -- set permanent scsi device characteristics. *************
 *	    Inserts SDC_XXX bits (see scsireq.h) into global "tasks"
 *	array for given id; these affect how the Scsimanager will
 *	handle future requests.  For example, setting SDC_NODISC 
 *	prevents disconnecting by the target on all requests.
 *	Scsiflags will alter only indicated bits.  Returns previous value.  
 */

scsichar(id, stuff, mask)
	int id;		/* id number 0...7 (+ 8*bus) */
	int stuff;	/* new bits */
	int mask;	/* the mask */
{
	register previous;
	int bus;

	/*
	 *  Call alternate scsimanager if indicated
	 */
	bus = id / 8;
	if ( bus < 0 || bus >= NSCSIMGRS )
		return ( -1 );
	else  {
		if ( scsimgrs[bus].s_char != scsichar )
			return ((*scsimgrs[bus].s_char) (id,stuff,mask));
		else
			id  = id % 8;
	}

	previous = tasks[id].devchar;
	tasks[id].devchar = (~mask & previous) | (mask & stuff);
	if(neverdisc)
		tasks[id].devchar |= SDC_NODISC;
	return(previous);
}



/*****  scsig0cmd -- build group 0 Scsi command block. *********************
 *	Fills in the blanks of the request command bytes.  (See
 *	"scsiccs.h")  Sets request command-length to 6 bytes.  
 */
scsig0cmd(req, op, lun, addr, len, ctl)
	struct scsireq *req;	/* request being assembled */
	int	op;		/* the SCSI op code */
	int	lun;		/* The logical unit number (1..8) */
	register addr;		/* The address field of the command */
	int	len;		/* The length field of the command */
	int	ctl;		/* contents of byte 5 */
{
	struct scsig0cmd *cmd = (struct scsig0cmd *)req->cmdbuf;
	cmd->op = op;
	cmd->addrH = ((lun & 0x7) << 5) | (((int)addr >> 16) & 0x1F);
	cmd->addrM = (int)addr >> 8;
	cmd->addrL = (int)addr;
	cmd->len = len;
	cmd->ctl = ctl;
	req->cmdlen = 6;
}



/*****  scsicancel -- cancel scsi request. ************************************
 *	     If request is running for id, it is aborted.  If request is
 *	disconnected, or in any non-zero state, all evidence is cleared,
 *	and the return code set to SST_CANCEL.  Returns entry task flags.
 */

scsicancel( id )
	int id;		/* scsi id to cancel */
{
	struct tasks *tp = &tasks[id];
	struct scsireq *req = tp->req;
	register flags = tp->flags;	/* entry flags */
	int s;
	int bus;	/* scsimgr bus to cancel */


	/*
	 *  Call alternate scsimanager if indicated
	 */
	bus = id / 8;
	if ( bus < 0 || bus >= NSCSIMGRS )
		return (req->ret = SST_SEL);
	else  {
		if ( scsimgrs[bus].s_cancel != scsicancel )
			return ( (*scsimgrs[bus].s_cancel) (id) );
		else
			id  = id % 8;
	}

	if ( !(flags & SF_JOB) )
		return (flags);		/* no job */

	/*  terminate job with extreme prejudice */
	tracescsi("scsicancel");
	s = spl7();
	if ( req )
		req->ret = SST_CANCEL;	/* set return code */

	if ( flags & SF_RUN )  {
		ASSERT( curtask == id && curstp == tp);
		scsisched(SI_ABT,"scsicancel");	/* kill running job */
	}
	
	discjobs &= ~(1 << id);		/* clear disconnect bitmap */
	tp->flags &= ~(SF_JOB | SF_RUN | SF_DISCON); /* clear job */
	splx(s);

	return (flags);			/* return entry flags */
}



/*	scsiinit -- do initialization.
 *	     Called at boot up.
 */

scsiinit()

{
	extern short scsi_dmaon[];

	register struct ncr5380 *ncr;
	int	jnk;

#ifdef	STANDALONE
	register struct via *vp = VIA1_ADDR;

	if (vp->rega & RBV_CPUID2) {	/* check for RBV instead of VIA2 */
	    rbv_exists = 1;
	    via2_addr = RBV_ADDR;
	}

	SCSI_IRQ(NO);
	sdma_addr = SDMA_ADDR_R8;

#endif	STANDALONE

	sg_state = SG_IDLE;
	sg_oldstate = SG_IDLE;
	stuckdisc = -1;
	nextexcl = -1;
	ourid = OURID;
	jobstep = 1000;
	curtask = 0;
	curstp = &tasks[0];
	ncr = (struct ncr5380 *)scsi_addr;
	ncr->init_comm = 0;	/* reset any asserted lines first */
	ncr->mode = 0;
	ncr->sel_ena = 0;

	if (machineID == MACIIfx) {		/* enable DMA_H1 transfers */
	    scsi_dmaon[0] = scsi_dmaon[1] = 1;
	    ncr->dmactl = 0L;
	} else {
	    scsi_dmaon[0] = scsi_dmaon[1] = 0;
	}

#ifndef	STANDALONE
	timeout(watchdog, 0, TOUTTIME * v.v_hz);
#else	STANDALONE
	if(ncr->curr_stat & SCS_BSY) {
		int	i;
		
		S_WR(ncr->init_comm) = SIC_RST;
		for(i = 0; i< 5000; i++)	/* TUNE to 5 or 10 millisec */
				;
		S_WR(ncr->init_comm) = 0;
		for(i = 0; i< 2000000; i++)	/* TUNE to over 5 seconds */
				;
	}
#endif	STANDALONE
	jnk = ncr->respar_int;			/* clear ncr chip IRQ */
	ncr->sel_ena = 1 << ourid;
	VIA_CLRSIRQ();				/* clear VIA IRQ */
	SCSI_IRQ(1);				/* enable IRQ */
	isinit = 1;
	TRACE(T_scsi, ("scsiinit -- complete\n"));
}

/*
 * scsimux - translate 3-char name to scsimgrs index
 */

scsimux( revcode)
	register char *revcode;
{
	register i;
	for (i = 0; i < NSCSIMGRS; i++)  {
		if ( revcode[0] == scsimgrs[i].s_rev[0] &&
		     revcode[1] == scsimgrs[i].s_rev[1] &&
		     revcode[2] == scsimgrs[i].s_rev[2] )
		 	return (i);
	}
	return (-1);
}
