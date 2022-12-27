/*
 * @(#)scsifsm.c  {Apple version 2.7 90/04/08 15:14:10}
 *
 * Copyright (c) 1987, 1988, 1989 by Apple Computer, Inc.
 * All Rights Reserved.
 *
 * THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF APPLE COMPUTER, INC.
 * The copyright notice above does not evidence any actual or
 * intended publication of such source code.
 */

#if !defined(lint)
static char _sccsid[]="@(#)scsifsm.c  {Apple version 2.7 90/04/08 15:14:10}";
#endif

/*
 *         A/UX SCSI MANAGER
 *
 *    scsifsm - finite state machine
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
#include "sys/stream.h"
#endif lint

#include <sys/ncr.h>
#include <sys/scsiccs.h>
#include <sys/vio.h>
#include <sys/scsivar.h>
#include <sys/scsitask.h>
#include <sys/scsireq.h>
#include <sys/scsifsm.h>
#include <sys/via6522.h>

#ifndef STANDALONE
/*	The following streams data structures allow us to use the stream
 *	scheduler to run copy operations at cpu chip priority 0.  We do
 *	not use any other part of streams besides the queue scheduler.
 */

extern	nulldev(), scsitask();
static struct  module_info scsimodinfo = {93,"scsi",0,256,256,256,NULL};
static struct  qinit scsidata = { NULL, scsitask, NULL, NULL,
			nulldev, &scsimodinfo, NULL};
static struct  queue scsiqueue = { &scsidata };
#endif	STANDALONE



/*****  scsisched - step through finite state machinery  ***************
 *
 *	     We take the input argument (inp) with the current
 *	global state (sg_state) and index into the statetable to
 *	find our next scheduler state.
 *
 *	     Important parts of the state table are shown below; full
 *	definition is in "scsifsm.h"  Notice with some of the states
 *	there is an action function (e.g. choosetask),
 *	that either advances to next state, or enables an interrupt
 *	which will take us to the next state when it arrives.
 *	
 *	     An important exception occurs when the scheduler enters
 *	state "SG_RUN", and calls "dorun".  In order to perform
 *	lengthy dma transfers at low cpu priority, "dorun" enlists
 *	the System V Streams scheduler to run yet MORE manager code
 *	(scsitask) at spl0.  Therefore various delays occur between
 *	when "dorun" activates streams and when "scsitask"
 *	actually executes.
 *	
 *			      scsireq
 *				 |
 *				 v
 *			      scsifsm
------------------------------------------------------------------------
|	  |           interrupt                                        |
|state    |action     SI_SEL SI_PHASE SI_FREE SI_NEWTSK SI_DONE SI_WAITP
|---------|-------------------------------------------------------------
|SG_IDLE  |-              - |      - |     - |  CHOOSE |     - |     - |
|   CHOOSE|choosetask     - |      - |     - |     RUN |   IDLE|     - |
|   RUN   |dorun     CHOOSE |      - |     - |       - | NOTIFY| WPHASE|
|   WPHASE|-         CHOOSE |    RUN | ABORTJ|       - |     - | WPHASE|
|   NOTIFY|donotify       - |      - |     - |       - | CHOOSE|     - |
|   WRECON|-         CHOOSE |      - | WRECON|  CHOOSE |     - |     - |
|   ABORTJ|doabort        - |      - |     - |       - | NOTIFY|     - |
|----------------------------------------------------------------------|
 *			  dorun => streams
 *				 |
 *				 v
 *			      scsitask
 *
 */


scsisched(inp, comment)
	int	inp;		/* input type SI_XXX */
	char 	*comment;	/* trace reason for sched */
{
	extern int cpuspeed;
	struct ncr5380 *ncr = (struct ncr5380 *)scsi_addr;
	register struct state_atribs *atp;
	register newstate;
	int	i;
	int	s;
	int 	agains = 0;

	++jobstep;
	if ( inp == SI_ABT || inp == SI_BSY )
		tracescsi(comment);	/* record error conditions */
	s = spl7();
	ASSERT((s & 0x700) >= 0x200);
again:
	if ( ++agains > 1000 )
		printscsi("scsisched agains>1000");

	/* pass scheduler inp through the state table */
	sg_oldstate = sg_state;
	sg_state    = statetable[inp][sg_state];
	atp         = &state_atribs[sg_state];

	/* record new scheduler state and input */
	(void) tracescpy( &state_atribs[sg_state].name[3], tracescpy( ">", 
		tracescpy( &eventnames[inp][3], tracescpy( ">",
			tracescpy( &state_atribs[sg_oldstate].name[3],
			    sstbuf)))) );
	tracescsi( sstbuf);	

	VIA_CLRSIRQ();		/* clear any via interrupt.  Ncr will
				 * provide necessary status info.
				 */
#ifdef	STANDALONE
	SCSI_IRQ(0);
#else	STANDALONE
	SCSI_IRQ(atp->irq);
#endif	STANDALONE

	/* test BSY line, enable monitor-bsy intr */
	scsifree = !(ncr->curr_stat & SCS_BSY) ? 1 : 0;
	if( atp->mtrbsy && !scsifree)
		ncr->mode |= SMD_BSY;

	/* enable ourid for select in states SG_CHOOSE, WRECON or WBUS */
	if ( atp->select != state_atribs[sg_oldstate].select )  {
		/* only set if changed instead of blind set */
		if(atp->select)
			ncr->sel_ena = 1 << ourid;
		else
			ncr->sel_ena = 0;
	}

	/* state SI_WAITP - wait for phase (and data) ready */
	if ( sg_state == SG_WPHASE )  {
		/*
		 *  Only if already in DMA mode will chip generate phase-
		 *  change interrupts.  We dare not enable DMA now because
		 *  it will glitch REQ/ACK and lose a byte.
		 *
		 * On the IIfx, an extra DRQ (SBS_DMA) will always occur on
		 * output since the SCSI chip is double-buffered, and it tries
		 * to read-ahead from memory. Therefore that bit is unimportant,
		 * after sending a command ONLY, for detection of the phase
		 * change interrupt.  Ignoring DRQ on SPH_DOUT output seems to
		 * cause a hang, so we only do it for SPH_CMD.
		 *
		 * Note that though CS could change in the chip during
		 * the "if" statement, we can't accidentally misinterpret
		 * the *new* phase bits, since BS(SBS_PHASE) will be false
		 * if the phase changes.
		 */

		if (machineID == MACIIfx) {
		    if (MD(SMD_DMA)	&&	/* in DMA mode */
		      !CS(SCS_REQ)	&&	/* REQ isn't here yet */
		      (CS(7 << 2) == SPH_CMD)	&& /* just sent a cmd */
		      BS(SBS_PHASE))  {		/* still in phase */
			tracescsi("awaiting phase via int");
			SPL0();
			return;		/* see you in scsiirq */
		    }
		} else {
		    if (MD(SMD_DMA)	&&	/* in DMA mode */
		      !CS(SCS_REQ)	&&	/* REQ isn't here yet */
		      BS(SBS_DMA|SBS_PHASE) == SBS_PHASE)  {
				    /* still in phase and no DRQ */
			tracescsi("awaiting phase via int");
			SPL0();
			return;		/* see you in scsiirq */
		    }
		}

		/* otherwise spinwait inline, ugh */
		tracescsi("spinwait req");
		ncr->mode &= ~SMD_DMA;
		i = 0;
		while ( CS(SCS_BSY|SCS_REQ) == SCS_BSY && ++i<MAXLOOP )  {
			++iowaits;
		}
		if ( i >= 125000 )
			printscsi("very long waitp poll");

		/* we should now have REQ active */
		if ( CS(SCS_REQ|SCS_BSY) == (SCS_REQ|SCS_BSY) )  {
			inp = SI_PHASE;	/* pretend phase int */
			tracescsi("pretend phase int");
			goto again;
		}
		tracescsi(" dead phase wait");
		scsisched( SI_ABT, "dead phase wait");
		splx(s);
		return;
	}

	/* state SI_WRECON - enable wait for reconnect*/
	if ( sg_state == SG_WRECON )  {
		ncr->sel_ena = 1 << ourid;
		SCSI_IRQ(1);

		/* spinwait <100usec here, for someone to reselect us */

		i = 50 * cpuspeed / 16;		/* relative to 16MhZ tuning */
		do {
		    if ( CS(SCS_BSY|SCS_SEL|SCS_IO) == (SCS_SEL|SCS_IO) )  {
			inp = SI_SEL;
			goto again;
		    }
		} while (--i);

		/*printf("e");*/

		tracescsi("awaiting reconnect");
		SPL0();
		return;		/* see you in scsiirq */
	}
		
	/* state SG_WBUS - wait bsy to drop */
	if ( sg_state == SG_WBUS )  {
		printscsi("SG_WBUS??");
		i = MAXLOOP;
		while ( (ncr->curr_stat & SCS_BSY) && --i)
			++iowaits;	/* should not be needed */

		if ( ncr->curr_stat & SCS_BSY )  {
			scsisched(SI_ABT, "wbus");
			splx(s);
			return;
		}
		

		if( ncr->curr_stat & SCS_SEL )
			inp = SI_SEL;
		else
			inp = SI_FREE;
			goto again;	/* take us to choose state */
	}
		
	/* 
	 * If there is a function (see state_attribs) to do, do it,
	 * otherwise an interrupt is supposed to kick things off later
	 */
	if(atp->fcn) {
		splx(s);
		(*atp->fcn)(inp);
	}
	else {
		if ( (ncr->curr_stat & SCS_SEL)
			&& !(ncr->bus_status & SBS_IRQ) )
				printscsi("sel no irq");/* chip error */
		/* early irq? yuuch */
		if( ((ncr->bus_status & SBS_IRQ) /*|| scsifree*/) && atp->irq) {
			if ( scsifree)
				tracescsi("Early irq scsifree");
			else
				tracescsi("Early irq IRQ");
			SPLINTR();
			scsiirq();
		}
		else {
#ifndef	STANDALONE
			TRACE(T_scsi, ( 
	"Nothing to call:bus_status 0x%x curr_status 0x%x entry mode = 0x%x\n",
			ncr->bus_status, ncr->curr_stat, ncr->mode));
#else	STANDALONE
			int	lim;
			SPL0();
			switch(sg_state) {
			case SG_WPHASE:
			case SG_WRECON:
			case SG_WBUS:
				TRACE(T_scsi, ( "poll for interrupt\n"));
				if(sg_state == SG_WBUS)
					lim = 10000;	/* TUNE a few mill */
				else
					lim = 5000000;	/* TUNE a few sec */
				for(i = 0; i< lim; ++i) {
					if(ncr->bus_status & SBS_IRQ) {
						break;
					}
				}
				SPLINTR();
				if(!(ncr->bus_status & SBS_IRQ)
				   && sg_state == SG_WBUS) {
					if(tasks[curtask].flags & SF_JOB)
						scsisched(SI_ABT,"scsisched");
					else
						scsisched(SI_FREE,"scsisched");
					splx(s);
					return;
				}
				scsiirq();
				break;
			case SG_WRESET:
				for(i = 0; i < 5000000; ++i) {
					;
				}
				finishreset();
				break;
			case SG_IDLE:
				break;
			default:
				sa_printf(
				"scsisched: waiting for unknown interrupt state %d\n",
				sg_state);
			}
#endif	STANDALONE
		}
	}
	if (!(curstp->flags & SF_HWDMA) &&  ncr->bus_status & SBS_IRQ)
		tracescsi("irq pending");
	splx(s);
}


/*	choosetask -- select next id.
 *	     The routine is called from the global state machine
 *	after advancing from the idle state.  Either software is
 *	requesting I/O, or a device interrupt is selecting us,
 *	presumably following a disconnect.  Basically, choosetask
 *	decides which id to run next, and sets "curtask" and "curstp".
 *
 */


static
choosetask(inp)

{
	extern int cpuspeed;
	register struct ncr5380 *ncr = (struct ncr5380 *)scsi_addr;
	static	lastsched = 0;
	register i, n;
	register id;
	int	s;
	int	agains = 0;

	TRACE(T_scsi, ("choosetask: inp %s\n", eventnames[inp]));
	s = spl7();
again:
	if ( ++agains > 1000 )
		printscsi("choosetask agains>1000");
	switch(inp) {
	case SI_DONE:
	case SI_NEWTASK:
	case SI_FREE:
	case SI_BSY:		/* if bus busy, rechoose */
		/* search once around for next task to start */
		id = lastsched;
		do  {
			id = (id + 6) % 7;	/* id = id - 1(mod 7) */
			/* test reselection first */ 
			if( CS(SCS_BSY|SCS_SEL|SCS_IO) == (SCS_SEL|SCS_IO)
				&& (ncr->curr_data & (1<<ourid)) )  {
				inp = SI_SEL;
				goto again;
			}
			/* if job, but not running, start it going */
			if( (tasks[id].flags & (SF_JOB|SF_RUN)) == SF_JOB)  {
				curtask   = id;
				curstp    = &tasks[id];
				lastsched = id;
				curstp->flags |= SF_RUN;
				scsisched(SI_NEWTASK, "chosen task");
				splx(s);
				return;
			}
		}  while  ( id != lastsched); 
		if(stuckdisc >= 0) {
			tracescsi("choosetask stuckdisk");
			if(!discjobs) {
				/*splx(s);*/
				scsisched(SI_ABT, "choosetask");
				splx(s);
				return;
			}
			break;
		}
		break;
	case SI_FSEL:
	case SI_SEL:
		/* potential reselect, determine reconnecting id. */
		n = ncr->curr_data ^ (1<<ourid);
	   
		/* binary search of 8 bits */
		id = 0;	                  
		if ( n & ((0xF<<4) << id) ) id += 4; /* which 4 of 8? */
		if ( n & ((0x3<<2) << id) ) id += 2; /* which 2 of 4? */
		if ( n & ((0x1<<1) << id) ) id += 1; /* which 1 of 2? */

		/* verify correct 2 id bits, job disconn and resel */
		if (ncr->curr_data != ((1<<id) | (1<<ourid))		||
		    !(discjobs & (1<<id))				||
		    CS(SCS_BSY|SCS_SEL|SCS_IO) != (SCS_SEL|SCS_IO))  {
			printscsi("reselecting whom??");
			i = ncr->respar_int;	/* reset sel int */
			inp = SI_FREE;
			goto again;
		}

		/* 
		 *  Respond to target by asserting bsy.
		 */
		ncr->init_comm |= SIC_BSY; /* assert bsy */
		DELAY;			/* min 2us deskew */

		curtask = id;		/* set global task index */
		curstp  = &tasks[id];	/* and struct pointer */

		tracescsi("reselecting");
		i = ncr->respar_int;		/* reset sel int */

		/* Wait 250ms for target to drop sel.  We don't really
		 * do anything if it *doesn't*, but it probably won't
		 * go well if SEL didn't drop properly...
		 */
		i = 10000 * cpuspeed / 16; 	/* relative to 16MhZ tuning */
		while( (ncr->curr_stat & SCS_SEL) && --i )
			++iowaits; 		/* wait sel */
		if(!i)printscsi("recon seldrop maxloop");

		ncr->init_comm &= ~SIC_BSY;	/* drop bsy */
		ncr->mode &= ~SMD_BSY;		/* reset mon bsy */
		ncr->sel_ena = 0;		/* reset int enable */
		i = ncr->respar_int;		/* reset int */

		/* if target asserted bsy, re-connection ok */
		if( (ncr->curr_stat & SCS_BSY) ) {   
			tracescsi("reselected");
			if(stuckdisc == id)
				stuckdisc = -1;
			curstp->flags &= ~SF_DISCON;
			discjobs &= ~(1 << curtask);
			scsisched(SI_SEL, "choosetask reselected");
			splx(s);
			return;
		}
		else  {
			tracescsi("target failed reselect");
			ncr->sel_ena = 1<<ourid;
			if(ncr->curr_stat & SCS_BSY)
				printscsi("why bsy now you suckah?");
			inp = SI_FREE;
			goto again;
		}
		break;
	default:
		panic("unknown input in choosetask");
		break;
	}
	/*splx(s);*/
	if(discjobs)
	    scsisched(SI_BSY,"choosetask disc"); /* signals pending reconnect */
	else
	    scsisched(SI_DONE,"choosetask idle");	/* signals idle state */
	splx(s);
	return;
}

/*
 * dodmadone gets called on an EOP interrupt or DMA watchdog.
 */

static int
dodmadone(inp)
register int inp;
{
    register struct tasks *stp;
    register struct ncr5380 *ncr = (struct ncr5380 *)scsi_addr;
    register u_long bytesdone;
    register u_long dmactl;
    register int i;

    stp = curstp;

    stp->flags &= ~SF_HWDMA;

    ncr->mode &= ~SMD_EOP;	/* disable EOP intr */

    ncr->wdog_timer = 0L;		/* kill the dog */
    dmactl = ncr->dmactl & ~(SCSI_DMAENAB | SCSI_WDIRQENAB); /* dma, wdog off */
    ncr->dmactl = dmactl;

    bytesdone = stp->dmacount - ncr->dma_count;

    /* If there's a byte stuck in the NCR chip (the DMA hardware already
     * handed it off), then we know it hasn't been sent out.
     *
     * This has been observed on disconnects during long writes to
     * some Quantum drives.  The NCR chip has automatically asked for
     * the next DMA data byte, but the drive has decided to change phase.
     */
    if (!(stp->flags & SF_READ) && !TC(STC_LBS)) {
	bytesdone--;
    }

    stp->datasent += bytesdone;

    TRACESCSI(tracestox( stp->databuf, sstbuf, min(8,stp->datasent));
		tracescsi(sstbuf));

    /*  are we done cooking with this vio segment? */

    if (bytesdone == stp->iovsegcnt)  {	/* done with this iov segment? */
	++stp->niovsent;		/* yup */
    }

    switch (inp) {

	case SI_DMAWDOG :
		scsisched(SI_ABT,"dma wdog");	/* abort it */
		break;

	case SI_EOP :

		/* Wait for target the last byte to be transferred. We
		 * know that he hasn't seen it if ACK is still asserted.
		 */
		i = MAXLOOP;
		while (BS(SBS_ACK) && CS(SCS_REQ)) {
		    ++iowaits;
		    if (--i < 0) {
			tracescsi("dma ackdrop max");
			break;
		    }
		}

		/* If we have no more data, expect a phasechange. */

		if (stp->datasent == stp->datalen) {
		    scsisched(SI_WAITP,"dma all finished");
		    break;
		}

		/* More data to transfer.  Spinwait a short time for
		 * a possible phasechange or REQ (meaning more data).
		 * This loopcount was measured at 5576 counts for the
		 * HD80 and 439 for HD160 drives.
		 */
		
		i = 6500 * cpuspeed / 16;	/* tuned relative to 16MHz */
		do {
		    if (!BS(SBS_PHASE) || CS(SCS_REQ)) {
			break;
		    }
		    ++iowaits;
		} while (--i);

		if (CS(SCS_REQ) || !BS(SBS_PHASE)) {
		    scsisched(SI_PHASE,"dmadone phase/req");
		} else {
		    scsisched(SI_WAITP,"dmadone slow phase");
		}
		break;
	
	case SI_PHASE:
		scsisched(SI_PHASE,"dma lost phase");
		break;
    }

    return(0);
}

/*	dorun -- enable SCSI service queue.
 *	     Most activity using the SCSI chip is done at low processor
 *	priority and is scheduled via the streams mechanism.  Dorun
 *	merely enables our stream service routine "scsitask" and
 *	returns.  Later, the kernel winds up housekeeping, but
 *	before returning to "the user", it calls streams service
 *	routines, and thus we pick back up again in scsitask.
 */

static
dorun(inp)
{
#ifndef	STANDALONE
	TRACE(T_scsi, ("dorun: inp = %s\n", eventnames[inp]));
	taskinp = inp;
	qenable(&scsiqueue);
#else	STANDALONE
	static	level, needrun;

	if(level++ == 0) {
		needrun = 1;
		taskinp = inp;
		while(needrun) {
			needrun = 0;
			scsitask();
		}
	}
	else  {
		taskinp = inp;
		needrun = 1;
	}
	--level;
#endif	STANDALONE
}

/*	donotify -- notify user handler.
 *	When the scsi request was placed, the user included a call back
 *	address.  Now we call it back.
 */

static
donotify(inp)

{
	register struct tasks *stp = curstp;
	register struct scsireq *rqp = stp->req;
	int i;

	ASSERT((spl2() & 0x700) >= 0x200);	/* Call at spl2() or more */
	TRACE(T_scsi, ("donotify(%s) id%d \n", eventnames[inp], curtask));

	if(stp->flags & SF_DISCON) {
		discjobs |= 1 << curtask;
	} else {
	    TRACE(T_scsi, ("complete id %d\n", curtask));
	    rqp->msg = stp->msgin;
	    rqp->stat = stp->stat;

	    if( stp->flags & SF_SENSE )  {
		tracescsi("donotify after sense command");
		/* after sensing, restore cmdbuf */
		stp->cmdbuf = rqp->cmdbuf;
		stp->cmdlen = rqp->cmdlen;
	    }

	    /* If caller wants automatic sense, do it.  We always
	     * return the ORIGINAL command's msgin and stat.
	     */

	    if ((stp->stat & STA_CHK) && !(stp->flags & SF_SENSE) &&
					rqp->sensebuf && rqp->senselen) {
		tracescsi("donotify with stat check");
		stp->savedmsgin = stp->msgin;
		stp->savedstat = stp->stat;
		stp->flags &= ~SF_RUN;
		stp->flags |= SF_SENSE | SF_READ;
		stp->cmdbuf = (caddr_t)&sense_cmd[curtask];
		stp->cmdlen = sizeof(struct scsig0cmd);
		sense_cmd[curtask].addrH = stp->lun << 5;
		stp->databuf = rqp->sensebuf;
		stp->datalen = rqp->senselen;
		sense_cmd[curtask].len = stp->datalen;
		stp->datasent = 0; stp->niovsent = 0;
	
		/* patch in dma vector info for sensebuf */
			
		rqp->vio.vio_tot = 0;
		IOVADD(&rqp->vio, 0,(struct buf *)0, stp->databuf,
							stp->datalen);
		rqp->timeout = 4;
		rqp->ret = SST_STAT;	/* sense set up */

	    } else {

		/* job done */
		stp->flags &= ~(SF_JOB | SF_RUN | SF_DISCON);
		if (stp->flags & SF_SENSE) {		/* restore msg&stat */
		    rqp->msg = stp->savedmsgin;
		    rqp->stat = stp->savedstat;
		}
		if(rqp->ret == 0) {
		    stp->lastnotify = jobstep;
		}
		if(rqp->faddr) {
		    (*rqp->faddr)(rqp);
		}
	    }
	}
	scsisched(SI_DONE, "donotify");
	return;
}


/*	doabort -- give up on job.
 *	     When an I/O fails, we must get the device off the bus.
 */

static
doabort(inp)

{
	struct ncr5380 *ncr = (struct ncr5380 *)scsi_addr;
	register struct tasks *stp;

	/* record conditions at time of abort */
	tracescsi( "do abort");	

	stp = &tasks[curtask];
	stp->flags |= SF_ABTSET;	/* indicate aborting */
	stp->maxtime = 2;
	if ( (stp->flags & (SF_TRACE|SF_TRACEPR)) == SF_TRACE )
		tracesnap(24);		/* print if not already printing */

	/* supply default return code */
	if(!stp->req->ret)
		stp->req->ret = SST_PROT;
	if(stp->flags & SF_SENSE)
		stp->req->ret = SST_SENSE;

	/* If ANY lines active, need to force an abort. */
	if ( (ncr->curr_stat & (SCS_RST|SCS_BSY|SCS_REQ))
		|| (ncr->bus_status & (SBS_DMA|SBS_ATN|SBS_ACK)) )   {
#ifdef		STANDALONE
		if(!(stp->flags & SF_RUN)) {
			stp->req->ret = SST_SEL;	/* huh? */
			scsisched(SI_DONE, "doabort");
			return;
		}
#endif		STANDALONE
		scsisched(SI_ABT, "doabort bsy");
	}
	else  {
		int i;
		ncr->mode = ncr->init_comm = 0;
		i = ncr->respar_int;
		scsisched(SI_DONE, "doabort no bsy");
	}
}


/*	dopanic -- Aieeeeee.
 */

static
dopanic(inp)


{
	register struct ncr5380 *ncr = (struct ncr5380 *)scsi_addr;

	SCSI_IRQ(0);			/* prevent interrupts! */
	pre_panic();			/* force console output */
	tracescsi("dopanic");
	printf("dopanic input = %s %s -> %s\n", 
		eventnames[inp], 
		state_atribs[sg_oldstate].name, state_atribs[sg_state].name);
	/*tracesnap(20);*/
	panic("SCSI manager state table");
	/* Not normally executed.  Might work, though */
	/*sg_state = SG_IDLE;	dont reset state seems better -GD*/
	scsisched(SI_RESET, "dopanic");
}


/*	doreset -- reset the SCSI bus.
 *	     We respond to some timeout error conditions by resetting
 *	the SCSI bus.  This begins the process.  doreset2() turns off
 *	the reset pulse a fraction of a second later, and finishreset
 *	restores things to normalacy a few seconds later.
 *	     The reset pulse will cause the next operation on an ST225
 *	drive, to generate a sense condition.  The driver should not
 *	consider this an error.
 */

static
doreset(inp)

{
	register struct ncr5380 *ncr = (struct ncr5380 *)scsi_addr;
	int	i;

	ASSERT(sg_state == SG_RESET);
	tracescsi("doreset");
	if(!(ncr->curr_stat & SCS_RST)) {
		ncr->init_comm = SIC_RST;
	}

	if( ncr->init_comm & SIC_RST) {
#ifndef	STANDALONE
		timeout(doreset2, 0, FRESET_TICKS);
#else	STANDALONE
		for(i = 0; i < 200000; ++i)
			;
		ncr->init_comm = 0;
#endif	STANDALONE
	}
	lastreset = jobstep;
#ifndef	STANDALONE
	timeout(finishreset, 0, WRESET_TICKS);
#endif	STANDALONE
	scsisched(SI_DONE, "doreset");
}


/*	doreset2 -- turn off reset pulse.
 *	     Started by a timeout, this routine cancels the reset pulse.
 */

static
doreset2()

{
	((struct ncr5380 *)scsi_addr)->init_comm = 0;
}


/*	finishreset -- clean up following reset.
 *	    After pulsing the RST line, we give devices 2 seconds, and then
 *	we complete the offending request and go back to business as usual.
 */
 
static
finishreset()

{
	register struct tasks *stp;
	register struct scsireq *rqp;
	register i;
	
	SPLINTR();
	TRACE(T_scsi, ("finish reset called\n"));
	if(sg_state != SG_WRESET) {
		panic("SCSI finish reset");
	}
	discjobs = 0;
	/* install return codes and execute all call-back functions */
	for(i = 0, stp = tasks; i < NSCSI; ++i, ++stp) {
		if((stp->flags & (SF_JOB | SF_RUN)) == (SF_JOB | SF_RUN)) {
			rqp = stp->req;
			if(i == stuckdisc || (stuckdisc < 0 && i == curtask)) {
				rqp->ret = SST_TIMEOUT;
			}
			else {
				rqp->ret = SST_AGAIN;
			}
			rqp->datasent = 0; rqp->niovsent = 0;
			stp->flags &= ~(SF_JOB | SF_RUN | SF_DISCON);
			if(rqp->faddr)
				(*rqp->faddr)(rqp);
			break;
		}
	}
	if(stuckdisc >= 0) {
		tasks[stuckdisc].devchar |= SDC_NODISC;
		stuckdisc = -1;
	}
	i = ((struct ncr5380 *)scsi_addr)->respar_int;
	VIA_CLRSIRQ();
	scsisched(SI_DONE, "finishreset");
}


scsiirq() 
{ 
	register struct ncr5380 *ncr = (struct ncr5380 *)scsi_addr;
	register type;
        register u_char ncrbs = ncr->bus_status;
	register u_char ncrcs = ncr->curr_stat;
	register u_short flags;
	long stack;
	char tracebuf[25];

	flags = curstp->flags;		/* to force full trace */

	if (machineID == MACIIfx) {
	    curstp->flags &= ~SF_HWDMA;
	    TRACESCSI(traceltox(ncr->dmactl,
			    (tracescpy(" C ",
			      (traceltod(ncr->dma_count,
				tracescpy(" L",
				  traceltod(curstp->dmacount,
				    tracescpy("irq:R",tracebuf))))))));
			tracescsi(tracebuf));
	    curstp->flags = flags;
	}

	type = SI_UNK;	   /* default should be SI_RESET
			   because SCS_RST is not latched */

	/* Silently ignore bogus interrupts */
	if ( !(ncr->bus_status & SBS_IRQ)) {

		/* Might be the DMA watchdog timer. */

		if (machineID == MACIIfx) {
		    u_long dmactl;
		    dmactl = ncr->dmactl & ~SCSI_WDIRQENAB;

		    if (dmactl & SCSI_WDPENDING) {
			type = SI_DMAWDOG;
			ncr->wdog_timer = 0L;
			ncr->dmactl = dmactl;
			goto service;		/* not bogus */
		    }
		    ncr->dmactl = dmactl;
		}
		tracescsi("ignoring bogus irq");
		VIA_CLRSIRQ();
		return;
	}

	/* ncr 5380 interrupt decoding */
	if ( ncrbs & SBS_IRQ )  {	
		if      ( (ncrcs & SCS_RST) )   type = SI_RESET;
	        else if ( (ncrbs & SBS_EOP) && (curstp->flags & SF_HWDMA))
					    type = SI_EOP;
		else if ( (ncrbs & SBS_BSY) )   type = SI_FREE;
		else if ( (ncrbs & SBS_PTE) )   type = SI_PARITY;
		else if ( (ncrcs & SCS_SEL)  && (ncr->curr_data & (1<<ourid)) )
					    type = SI_SEL;
	        else if (!(ncrbs & SBS_PHASE) && (ncrcs & SCS_BSY) )
					    type = SI_PHASE;
		else if (!(ncrcs & SCS_BSY) )  type = SI_FREE;/* no BSY */
	}

	/* if no bsy to monitor turn off monitor-bsy intr */
	if ( !(ncr->curr_stat&SCS_BSY) )
		ncr->mode &= ~SMD_BSY;

	/* manual says to turn off dma mode if phase intr */
	if ( type == SI_PHASE )
		ncr->mode &= ~SMD_DMA;
	else if ( type == SI_SEL )
		ncr->sel_ena = 0;	/* disable further sel intrs */

service:;
	/* record the interrupt */
	curstp->flags &= ~SF_HWDMA;
	TRACESCSI(tracescpy( &eventnames[type][3],
		   tracescpy( "scsiirq-", tracebuf));
		    tracescsi( tracebuf));
	curstp->flags = flags;

	stack = ncr->respar_int;		/* clear ncr chip */
	SCSI_IRQ(NO);				/* disable IRQ intr */
	VIA_CLRSIRQ();				/* clear via chip */

	/* advance job to next step */
	if ( type == SI_SEL )  {
	    int s;
	    s = spl7();		/* answer reselect NOW! */
	    scsisched(type, "scsiselirq");
	    splx(s);
	} else {
	    scsisched(type, "scsiirq");
	}
	return;
}
