/*
 * @(#)scsitask.c  {Apple version 2.8 90/03/13 12:25:51}
 *
 * Copyright (c) 1987, 1988, 1989 by Apple Computer, Inc.
 * All Rights Reserved.
 *
 * THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF APPLE COMPUTER, INC.
 * The copyright notice above does not evidence any actual or
 * intended publication of such source code.
 */

#if !defined(lint)
static char _sccsid[]="@(#)scsitask.c  {Apple version 2.8 90/03/13 12:25:51}";
#endif

/*
 *         A/UX SCSI MANAGER
 *
 *    scsitask - NCR 5380 task control
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

#include <sys/via6522.h>
#include <sys/vio.h>
#include <sys/ncr.h>
#include <sys/scsiccs.h>
#include <sys/scsireq.h>
#include <sys/scsifsm.h>
#include <sys/scsivar.h>
#include <sys/scsitask.h>

extern int scsitracespeed;

/*****  scsitask -- run task on chip  **********************************
 *
 *	     This routine moves a task through the various bus phases
 *	until the task is completed.  This routine is only called via
 *	the streams service queue.  It is never called directly.
 *
 *	     We are called during state SG_RUN (dorun).  On entry, the
 *	current task (curtask) and task pointer (stp) have already
 *	been set during state SG_CHOOSE.  An action code has been also
 *	placed in taskinp.
 */

scsitask()

{
	extern int cpuspeed;
	register struct ncr5380 *ncr = (struct ncr5380 *)scsi_addr;
	register struct tasks *stp = curstp;
	register i;
	int	atn = 0;
	struct vio *vp;
	int s;
	int agains = 0;
	int dmaspeed;

	ASSERT(checkconcur++ == 0);
#ifndef	STANDALONE
	ASSERT((spl0() & 0x700) == 0);
#endif	STANDALONE
again:
	if ( ++agains > 1000 )
		printscsi("scsitask agains>1000");
	tracescsi("scsitask top");
	switch(taskinp) {
	case SI_NEWTASK:
		ASSERT((stp->flags & (SF_RUN | SF_JOB | SF_DISCON)) == 
			(SF_RUN | SF_JOB));
		SPL7();
		if((i = scsiget()) == SI_DONE) {
			if(scsiselect(curtask, DODIS(stp)) == SI_DONE) {
				laststate = stp->pstate = SP_SEL;
			}
			else {
				tracescsi("scsitask unable to scsiselect");
				stp->req->ret = SST_SEL;
				stp->flags &= ~(SF_RUN|SF_ABTSET);
				SPLINTR();
				scsisched(SI_ABT, "scsitask fail select");
				ASSERT(--checkconcur == 0);
				return;
			}
		}
		else {
			tracescsi("scsitask cannot scsiget");
			stp->flags &= ~SF_RUN;
			SPLINTR();
			scsisched( i, "scsitask fail scsiget");
			ASSERT(--checkconcur == 0);
			return;
		}
		taskinp = SI_PHASE;
		goto again;
		break;	/* could just fall thru! */
	case SI_PHASE:
	case SI_SEL:
		/*
		 *  Spinwait for REQ from target to begin handling phase.
		 */
		ASSERT((stp->flags & (SF_JOB | SF_DISCON)) == SF_JOB);
		/* run at spl0, unless priority job */
		if ( stp->flags & SF_NOINTR )
			SPL7();
		else if ( stp->flags & SF_SPLINT )
			SPLINTR();
		else
			SPL0();
		laststate = stp->pstate;	/* save previous */
		ncr->init_comm &= SIC_ATN;	/* reset loose lines */
		ncr->mode &= ~SMD_DMA;		/* reset DMA mode */
		ncr->sel_ena = 0;		/* reset select intr */
		i = ncr->respar_int;		/* reset ncr irq */
		/*tracescsi("after top off dma");*/

		/* wait on REQ or SEL active before sampling phase */
		i = 0;
		while ( CS(SCS_BSY|SCS_REQ|SCS_SEL) == (SCS_BSY) ) {
			/* ST225N drives take several seconds after reset */
			++iowaits;
			if( ++i > MAXLOOP) {
				stp->req->ret = SST_PROT;
				stp->pstate = SP_ABT;
				printscsi("REQ wait failed");
				break;
			}
		}
		tracescsi("task req wait");
		break;
	case SI_ABT:
		scsiabt(stp);	/* abort scsi connection */
		ASSERT(--checkconcur == 0);
		return;
	default:
		pre_panic();
		printf("unknown task input %d\n", taskinp);
		panic("unknown input to scsitask");
	}


	/*
	 *  We are to perform the phase.
	 *  If no BSY signal or RST asserted, set abort state.
	 */

	if((ncr->curr_stat & (SCS_BSY | SCS_RST)) != SCS_BSY) {
		if(!(stp->req->ret) && !(ncr->curr_stat & SCS_BSY))
			stp->req->ret = SST_BSY;
		stp->pstate = SP_ABT;
		stp->flags  &= ~SF_ABTSET;
		printscsi("no BSY, or RST on");
	}

	/*
	 *  Implement two-step abort.  Default return code, and
	 *  if 1st step or BSY is still asserted, schedule bus abortion.
	 */
	if( stp->pstate == SP_ABT )  {
abort:
		if ( !(stp->flags & SF_ABTSET) || (ncr->curr_stat&SCS_BSY) )  {
			tracescsi("scsitask first abortion step");
			if(!stp->req->ret) {
				stp->req->ret = sp_errs[laststate];
				if( unjam > 1)
					stp->req->ret = SST_TIMEOUT;
			}
			SPLINTR();
			scsisched(SI_ABT, "scsitask SP_ABT");
			ASSERT(--checkconcur == 0);
			return;
		}
		else  {/* sf_abtset set in doabort */
			tracescsi("scsitask old final abortion step");
			SPLINTR();
			stp->datasent = 0, stp->niovsent = 0;
			scsisched(SI_DONE, "scsitask SP_ABT done");
			ASSERT(--checkconcur == 0);
			return;
		}
	}

	/*
	 *  Read current phase from hardware.
	 */
	newphase = (ncr->curr_stat >> 2) & 0x7;
	/*printscsi("newphase to enter");*/
	ncr->targ_comm = newphase;	/* set phase to match against */

	/*
	 *  FSM table "sp_states" encodes the first four data-transfer
	 *  states and their legal transitions.  Here we consult it
	 *  and note illegal transitions by shifting to state SP_ABT.
	 *  The two remaining SCSI states, SPH_MIN and SPH_MOUT,
	 *  are not constrained by this table, and may occur freely.
	 */
	if(newphase <= SPH_STAT && !(stp->flags & SF_ABTSET)) {
		if(stp->pstate == SP_DATA && newphase > SPH_DIN)  {
			/* If finishing data, check all requested
			 * bytes were transferred.
			 */
			if(stp->datasent != stp->datalen 
			   && !(stp->flags & SF_SENSE)) {
				tracescsi("err = SST_MORE");
				stp->pstate = SP_ABT;
				stp->req->ret = SST_MORE;
				goto abort;	/* abort connection */
			}
		}
		ASSERT(stp->pstate != SP_ABT ||
			spstates[stp->pstate][newphase] == SP_ABT);
		stp->pstate = spstates[stp->pstate][newphase];
		/*
		 *  If entering data states, make sure task
		 *  is a READ for data-In, and write for data-
		 *  out.
		 */
		if(stp->pstate == SP_DATA) {
			if( ((stp->flags & SF_READ) && newphase != SPH_DIN) ||
			   (!(stp->flags & SF_READ) && newphase != SPH_DOUT) ) {
				tracescsi("data direction err");
				stp->pstate = SP_ABT;
				stp->req->ret = SST_CMD;
				goto abort;	/* abort connection */
			}
			if ( stp->datasent >= stp->datalen) {
				tracescsi("data length err");
				stp->pstate = SP_ABT;
				stp->req->ret = SST_COMP;
				goto abort;	/* abort connection */
			}
		}
	}

	/* 
	 *  The new phase is in the target register,
	 *  get ready to do useful work.
	 */
	ASSERT(ncr->bus_status & SBS_PHASE);

	if((newphase & 1)) {		/* If input */
	    ncr->init_comm = atn;
	} else {
	    ncr->init_comm = SIC_DB | atn;
	}

dophase:
	/*tracescsi("scsitask dophase");*/

	stp->curtime = 0;
	unjam = 0;
	switch(newphase) {

	case SPH_DIN:
		ASSERT( (ncr->mode&SMD_DMA) == 0);
		dmaspeed = scsi_dmatype(stp);

		TRACESCSI(traceltod(dmaspeed,
		    tracescpy(" spd ",
			traceltod(stp->datalen,tracescpy("SPH_DIN ",sstbuf))));
		       tracescsi(sstbuf));

		i = scsi_vio(stp,scsi_in,dmaspeed);

		/* If we started a hardware DMA operation, then scsi_vio()
		 * has changed to SG_DMA, knowing that when the DMA is done
		 * it'll call scsisched to handle the phase change.  When
		 * that happens, we'll reschedule scsitask() to finish up.
		 */
		if (stp->flags & SF_HWDMA) { /* then we started hardware DMA */
		    ASSERT(--checkconcur == 0);
		    return;		/* we'll get scheduled later */
		}

		TRACESCSI(tracestox( stp->databuf, sstbuf,
			   min(12,stp->datasent)); tracescsi(sstbuf));
		if ( unjam > 1)
			stp->req->ret = SST_TIMEOUT;
		if ( berrflag || i < 0) {
			berrflag = 0;
			stp->req->ret = SST_AGAIN;
			stp->pstate = SP_ABT;
			tracescsi("din berrflag");
			break;
		}
		break;

	case SPH_DOUT:
		dmaspeed = scsi_dmatype(stp);

		TRACESCSI(traceltod(dmaspeed,
		    tracescpy(" spd ",
			traceltod(stp->datalen,tracescpy("SPH_DOUT ",sstbuf))));
		       tracescsi(sstbuf));

		i = scsi_vio(stp,scsi_out,dmaspeed);

		/* If we started a hardware DMA operation, then scsi_vio()
		 * has changed to SG_DMA, knowing that when the DMA is done
		 * it'll call scsisched to handle the phase change.  When
		 * that happens, we'll reschedule scsitask() to finish up.
		 */
		if (stp->flags & SF_HWDMA) { /* then we started hardware DMA */
		    ASSERT(--checkconcur == 0);
		    return;		/* we'll get scheduled later */
		}

		if( unjam > 1)
			stp->req->ret = SST_TIMEOUT;
		if ( berrflag || i < 0) {
			berrflag = 0;
			stp->req->ret = SST_AGAIN;
			stp->pstate = SP_ABT;
			tracescsi("dout berrflag");
			break;
		}

		/* The custom-cell 53C80 correctly drops ACK after output,
		 * but "standard" 5380/C80 chips leave it high.
		 */
		if (machineID != MACIIfx) {
		    *(char *)sdma_addr = 0;	/* one extra to drop ack */
		}
		TRACESCSI(tracestox( stp->databuf, sstbuf,
			   min(12,stp->datasent)); tracescsi(sstbuf));
		break;

	case SPH_CMD:
		TRACESCSI(tracescpy(tracecmd(stp->cmdbuf),
			tracescpy("SPH_CMD ", sstbuf)); tracescsi(sstbuf));

		HW_SHAKE(1);		/* h/w handshake on */
		i = scsi_out(stp->cmdbuf, stp->cmdlen, DMA_P1);
		HW_SHAKE(0);		/* h/w handshake off */

		if ( i != stp->cmdlen) {
			printscsi("cmd didnt go out");
			if(unjam > 1)
				stp->req->ret = SST_TIMEOUT;
			else
				stp->req->ret = SST_CMD;
			stp->pstate = SP_ABT;
		}

		/* The custom-cell 53C80 correctly drops ACK after output,
		 * but "standard" 5380/C80 chips leave it high.
		 */
		if (machineID != MACIIfx) {
		    *(char *)sdma_addr = 0;	/* one extra to drop ack */
		}
		TRACESCSI(tracestox( stp->cmdbuf, sstbuf,
			   min(12,stp->cmdlen)); tracescsi(sstbuf));

		/* On a faster machine, we get caught by the tc40 drive's
		 * extra REQ at the end of the command.  Delaying a few
		 * microseconds prevents us from seeing it.  If the drive
		 * changes phase, we don't waste the time here.  This
		 * prevents slowdowns on fast drives.
		 */
		if (machineID == MACIIfx) {
		    i = 250;			/* just a short spin */
		    while (--i && BS(SBS_PHASE))
			;
		}
		break;

	case SPH_STAT:
		/* wait inline for DRQ and pull status in */
		tracescsi("SPH_STAT");
		ncr->mode |= SMD_DMA;
		ncr->start_Ircv = 0;
		i = MAXLOOP;
		while ( BS(SBS_DMA|SBS_PHASE) == (SBS_PHASE) &&
			unjam < 2 && --i > 0 )
				++iowaits;
		if(!i)printscsi("stat maxloop");
		stp->stat = 0xFF;

		if ( BS(SBS_DMA|SBS_PHASE) == (SBS_PHASE|SBS_DMA) ) {
		    HW_SHAKE(1);		/* h/w handshake on */
		    stp->stat = *(char *)sdma_addr;
		} else {
		    if (BS(SBS_DMA)) {
			HW_SHAKE(1);		/* h/w handshake on */
			stp->stat = *(char *)sdma_addr;
			tracescsi("after dmafake");
		    } else {
			printscsi("strange status");
			stp->pstate = SP_ABT;
		    }
		}
		HW_SHAKE(0);			/* h/w handshake off */

		TRACESCSI(tracescpy( tracestat(stp->stat), tracestox(
			&stp->stat, sstbuf, 1)); tracescsi(sstbuf));
		if ( !i )  {
			printscsi("status failure");
			stp->pstate = SP_ABT;
		}
		break;

	case SPH_MIN:
		tracescsi("SPH_MIN");
		ASSERT( (ncr->mode&SMD_DMA) == 0);
		SPLINTR();
		ncr->init_comm = 0;
		/* hand-poll message bytes as long as msgin phase */

		while ((ncr->curr_stat & (SCS_BSY|SCS_PMK))
					== (SCS_BSY|(SPH_MIN<<2))) {
		    
		    int msginret;

		    /*  Input msg, but wait for decoding to ACK,
		     *  so message can be rejected if needed.
		     *  We are supposed to be in phase presently.
		     */

		    /* Wait for REQ before trying for the message byte. */

		    i = MAXLOOP;
		    while ((ncr->curr_stat & (SCS_RST|SCS_BSY|SCS_REQ|SCS_PMK))
				== (SCS_BSY|(SPH_MIN<<2)) && --i ) {
			++iowaits;
		    }
		    if (!i) printscsi("msgreq maxloop");

		    if ((ncr->curr_stat & SCS_PMK) == (SPH_MIN << 2)) {
			stp->msgin = ncr->curr_data;
		    } else {
			break;
		    }

		    TRACESCSI(tracescpy( tracemsg(stp->msgin),
			       tracestox( &stp->msgin, sstbuf, 1));
				tracescsi(sstbuf));

		    /* after this message maybe job is done */

		    msginret = scsimsgin(stp,1);
		    if (msginret == 0) {
			scsisched( SI_DONE, (stp->msgin==0x04) ?
			    "SPH_MIN: discon":"SPH_MIN: compl");
			ASSERT(--checkconcur == 0);
			return;
		    } else {
			if (msginret == -1) {	/* msginret set SP_ABT */
			    return;
			}
		    }
		}

		SPL0();
		break;

	case SPH_MOUT:
		ncr->mode |= SMD_DMA;
		ncr->start_xmt = 0;
		ncr->init_comm &= ~SIC_ATN;	/* for 1byte MOUT */
		atn = 0;
		if(stp->pstate == SP_SEL) {
			stp->msgout = 0xC0 | stp->lun;
			stp->pstate = SP_IDENT;
		}
		else if(stp->pstate == SP_ABT) {
			stp->msgout = SMG_ABT;
		}
		else {
			tracescsi("Unexpected message out request\n");
			stp->pstate = SP_ABT;
			break;
		}

		tracescsi("SPH_MOUT");
		HW_SHAKE(1);		/* h/w handshake on */
		i = scsi_out(&stp->msgout, 1, DMA_P1);
		HW_SHAKE(0);		/* h/w handshake off */
		TRACESCSI(tracescpy( tracemsg(stp->msgout),
			   tracestox( &stp->msgout, sstbuf, 1));
			    tracescsi(sstbuf));
		if ( i != 1 )  {
			tracescsi("message did not send\n");
			stp->pstate = SP_ABT;
			break;
		}

		/* The custom-cell 53C80 correctly drops ACK after output,
		 * but "standard" 5380/C80 chips leave it high.
		 */
		if (machineID != MACIIfx) {
		    *(char *)sdma_addr = 0;	/* one extra to drop ack */
		}
		break;
	default:
		tracescsi("dophase unknown");
		break;
	}
	/*tracescsi("after dophase");*/

	/* Wait for target to drop REQ. Some devices are slowpokes. */

	i = MAXLOOP;
	while (BS(SBS_ACK) && CS(SCS_REQ) && BS(SBS_PHASE)) {
	    ++iowaits;
	    if (--i < 0) {
		tracescsi("task ackdrop max");
		break;
	    }
	}

#ifdef notdef  /*XXX*/
	/* Detector for glitch of req on tc40 drive */
	/* we want to report REQ being asserted while phase still == SPH_CMD */

	if (newphase == SPH_CMD && (stp - tasks) == 3) {

	    /* wait for either phase or req to change */

	    while (!CS(SCS_REQ) && BS(SBS_PHASE))
		;

	    /* which one changed? if it was req, he blew it */

	    if (CS(SCS_REQ) && BS(SBS_PHASE)) {

		/* wait for either to drop. req first means req-glitch.
		 *  phase first means sequence error in drive
		 */
		while (CS(SCS_REQ) && BS(SBS_PHASE))
		    ;
		if (BS(SBS_PHASE)) {
		    printf("req glitch\n");
		} else {
		    printf("req sequence error\n");
		}
	    }
	}
#endif

	/* done with phase, spinwait 200usec for phase-change or req */

	i = 100 * cpuspeed / 16;	/* tune relative to 16MhZ tuning */
	s = spl7();			/* must SPL for proper timing! */

	while (!CS(SCS_REQ) && BS(SBS_PHASE) && --i) {
	    ++iowaits;
	}

	/* If right conditions, wait using phase-change interrupt */

	if ( !CS(SCS_REQ) && MD(SMD_DMA) && BS(SBS_PHASE) &&
			 !(stp->flags & (SF_NOINTR|SF_SPLINT)) )  {
		unjam = 0;
		stp->curtime = 0;
		scsisched(SI_WAITP, "slow phasechange");
		splx(s);
		ASSERT(--checkconcur == 0);
		return;
	}
	/* otherwise, go spinwait at top */
	splx(s);
	taskinp = SI_PHASE;
	++jobstep;
	goto again;
}


/*  Abort current SCSI bus connection task.  Shake REQ/ACK lines until target
 *  drops BSY.  Will reschedule task as necessary (done/reset),
 *  and then return.
 */

scsiabt(stp)
	register struct tasks *stp;
{
	register struct ncr5380 *ncr = (struct ncr5380 *)scsi_addr;
	register int i;

	/*
	 * Until BSY drops, assert ATN, and 
	 * handshake away (ignore) all target phases except
	 * to send "abort" message byte endlessly.
	 */

	SPLINTR();
	ncr->init_comm = SIC_ATN;	/* set atn, unset the rest */
	ncr->mode = 0;			/* raw chip */
	ncr->sel_ena = 0;		/* clear sel-enable too */
	ncr->respar_int;		/* clear interrupts */

	for(;;)  {

		/*  Wait for target to assert REQ or drop BSY */
		i = 0;
		while ( CS(SCS_REQ|SCS_BSY) == SCS_BSY ) {
			++iowaits;
			if( ++i > MAXLOOP) {
reset:
				scsisched(SI_RESET, "scsiabt");
				return;
			}
		}
	
		/* successfully dropped when BSY false 400ns */
		if ( !(ncr->curr_stat & (SCS_RST|SCS_BSY|SCS_REQ)) ) {
			scsisched(SI_DONE, "scsiabt");
			return;
		}
			
		/* Otherwise set newphase and answer REQ with ACK
		 * if target asks, send abort message byte but watch for
		 * dead ones
		 */
		ncr->out_data = stp->msgout = SMG_ABT;	/* message */
		if ( (ncr->targ_comm = ((ncr->curr_stat>>2) & 7)) == SPH_MOUT )
			ncr->init_comm &= ~SIC_ATN;	/* 1 byte msg */
		if ( ncr->curr_stat & SCS_REQ )
			ncr->init_comm |= SIC_ACK | SIC_DB;
		else if ( ncr->curr_stat & SCS_BSY )
			goto reset;	/* bsy but no req, hung target */

		/* wait until the target drops REQ... */
		i = 0;
		while ( ncr->curr_stat & SCS_REQ )  {
			++iowaits;
			if( ++i > MAXLOOP) {
				goto reset;
			}
		}
		/* ... after which we can drop ACK. */
		ncr->init_comm &= ~(SIC_ACK|SIC_DB);
	}
}


/*	scsiget -- perform SCSI bus arbitration.
 *	Must be called at spl7().
 */

int
scsiget()

{
	register struct ncr5380 *ncr = (struct ncr5380 *)scsi_addr;
	register i;
	register gets;

	ASSERT(!(ncr->mode & (SMD_BSY | SMD_DMA)));

	/* we can't always get what we want */
	ncr->mode = 0;			/* clear any aip */
	ncr->out_data = 1 << ourid;	/* say who we are */
	ncr->out_data = 0;
	if ( (ncr->curr_stat & (SCS_SEL|SCS_BSY)) == 0 )  {
		/* probably clear for arbitration */
		ncr->mode = SMD_ARB;	/* start arbitration */

		/* Wait for chip to detect bus free.  If it takes very
		 * long, give up.  Multiple initiators or reselectors
		 * can cause bus to be unfree for significant durations.
		 */
		i = 10;		/* about 20 usec */
		while( !CS(SCS_SEL) && !(ncr->init_comm & SIC_AIP) && --i)
			++iowaits;
		if(!i)tracescsi("scsiget maxloop");

		/* delay at least 2.2usec before examining bus data */
		DELAY;
		DELAY;

		/* lost if anybody selected or higher arbitrator */
		/* softened to any arbitrator to prevent lockout */
		if( CS(SCS_SEL)
			|| ncr->curr_data /*> (1<<ourid) */
				|| (ncr->init_comm & SIC_LA)
					|| !i /* ... or timeout */) {
			ncr->mode = 0;		/* clear arbitration */
			if ( CS(SCS_BSY|SCS_SEL|SCS_IO) ==
					(SCS_SEL|SCS_IO) )  {
				tracescsi("scsiget resel return");
				return(SI_SEL);
			}  else  {
				return (SI_BSY);
			}
		}
		else {
			ncr->init_comm = SIC_SEL | SIC_BSY;
			ncr->mode = 0;
			/*tracescsi("arb successful");*/
			DELAY;			/* at least 1.2usec */
			return(SI_DONE);
		}
	}
	else if ( (ncr->curr_stat & (SCS_BSY|SCS_SEL|SCS_IO)) ==
						(SCS_SEL|SCS_IO) )  {
		tracescsi("scsiget resel return");
		return(SI_SEL);
	}
	tracescsi("scsiget bsy return");
	return(SI_BSY);
}

/*	scsiselect -- select SCSI device.
 *	    Selects the given device prior to an I/O operation.  
 *	Before a device is selected the bus should be arbitrarted via 
 *	scsiget().
 *	Must be called at spl7();
 *	On entry SEL and BSY are both asserted.
 */

int
scsiselect(id, use_atn)

int	id;	/* device number (0 - 7) */
int	use_atn;	/* if true, ATN is held high */
{
	register struct ncr5380 *ncr = (struct ncr5380 *)scsi_addr;
	register int i;
	register int j;

	ASSERT((spl7() & 0x700) == 0x700);
	ncr->targ_comm = 0;
	ncr->out_data = (1 << id) | (1 << ourid);	/* put id-s */
	ncr->sel_ena = 0;
	/* drop BSY, leaving SEL with possible ATN */
	if(use_atn)
		ncr->init_comm = SIC_SEL | SIC_DB | SIC_ATN;
	else
		ncr->init_comm = SIC_SEL | SIC_DB;

	/* delay at least 400ns, then wait for selected target to assert BSY */
	i = 0;
	DELAY;

	while ( !(ncr->curr_stat & SCS_BSY) ) {
		++iowaits;
		if( ++i > 100000) {	/* TUNE HERE < 250 millisec */
			tracescsi("scsiselect timeout");
			ncr->init_comm &= ~SIC_DB;
			/* delay 250 usec */
			for (j = 0; j < 250; j++) {
			    DELAY;		/* 1 usec */
			}
			if(ncr->curr_stat & SCS_BSY)
				break;
			tracescsi("after selection abort");
			
			ncr->init_comm = 0;
			ncr->sel_ena = 1 << ourid;
			return(SI_BSY);
		}
	}
	tracescsi("selected");
	ncr->init_comm &= SIC_ATN;		/* drop all but atn */
	ncr->sel_ena = 1 << ourid;		/* enable our id */
	if (CS(SCS_BSY|SCS_SEL) == SCS_BSY) {
	    return(SI_DONE);
	} else {
	    return(SI_BSY);
	}
}


#define INITIATOR 1
#define TARGET    2
#define TRUE 1
/*
 *  Decode target-to-initiator message byte and perform response.
 *  Returns 1 if (target needs) more jobsteps, 0 if none.
 */
scsimsgin( stp, role)
	register struct tasks *stp;
	int role;
{
	register struct ncr5380 *ncr = (struct ncr5380 *)scsi_addr;
	register int i;
	int more = 1;		/* true if more jobsteps after return */

	if ( role != INITIATOR )
		panic("TARGET role not supported");

	if(stp->msgin & 0x80) {
		/* identify */
		if(stp->lun == (stp->msgin & 0x7)) {
			if( !(stp->flags & (SF_SENSE | SF_ABTSET)))
			    /* do implicit restore pointer */
			    stp->datasent = stp->req->datasent,
			    stp->niovsent = stp->req->niovsent;
		}
		else {
			printf("Lun %d does not match expected %d\n", stp->lun, stp->msgin);
			stp->pstate = SP_ABT;
		}
	}
	else {
		switch(stp->msgin) {
		case SMG_LNK:
		case SMG_LNKFLG:
		case SMG_COMP:
			more = 0;		/* indicate no more */
			ncr->mode &= ~SMD_BSY;	/* disable mon busy int */
			if(stp->pstate == SP_STAT || stp->pstate == SP_ABT) {
				/* JOB COMPLETED */
				if (stp->flags & SF_SENSE)
				    	stp->req->sensesent = stp->datasent;
				else
				    	stp->req->datasent  = stp->datasent,
				    	stp->req->niovsent  = stp->niovsent;
				if (stp->msgin == SMG_LNKFLG)
				    	stp->flags |= SF_LINKFLAG;
				else if(stp->msgin == SMG_LNK)
				    	stp->flags |= SF_LINK;
			}
			else  {
				printscsi("no stat before msg00");
				stp->pstate = SP_ABT;
			}
			break;
		case SMG_SAVEP:
			if( !(stp->flags & (SF_SENSE|SF_ABTSET)) ) {
			       	stp->req->datasent = stp->datasent,
			   	stp->req->niovsent = stp->niovsent;
				TRACESCSI( traceltod( stp->datasent,
				            tracescpy( "savep ",sstbuf));
					     tracescsi(sstbuf));
			}  else  {
			    	printscsi("sense savep");
				stp->pstate = SP_ABT;
			}
			break;
		case SMG_RESTP:
			if( !(stp->flags & (SF_SENSE|SF_ABTSET)) )  {
			    	stp->datasent = stp->req->datasent,
			    	stp->niovsent = stp->req->niovsent;
				TRACESCSI( traceltod( stp->datasent,
				            tracescpy( "restp",sstbuf));
					     tracescsi(sstbuf));
			}  else  {
			    	printscsi("sense restp");
			    	stp->pstate = SP_ABT;
			}
			break;
		case SMG_DISC:
			TRACE(T_scsi, ("disconnect%d id %d\n", curtask));
			ncr->mode &= ~SMD_BSY;	/* disable mon busy int */
			more = 0;		/* indicate no more busy */
			stp->flags |= SF_DISCON;

			/* Do implicit "save pointers" just in case. */

			if( !(stp->flags & (SF_SENSE|SF_ABTSET)) ) {
			    stp->req->datasent = stp->datasent,
			    stp->req->niovsent = stp->niovsent;
			}

			break;
		default:
			printf("scsitask: unrecognized message 0x%x\n",
				stp->msgin);
			stp->pstate = SP_ABT;
		}
	}
	/* bad job or good message acknowledge? */
	if ( stp->pstate == SP_ABT )  {
		return (-1);		/* abort any bad */
	}

	/*
	 *  If job complete or disconnect message, target must drop
	 *  busy.  Reset the busy-error enable and its chip interrupt
	 *  request which sometimes appears with HAMMER drives.
	 */
	if ( !more )  {
		ncr->targ_comm = 0;	/* so follow nothing forever */
		ncr->mode &= ~SMD_BSY;	/* disable monitor busy int */
		ncr->sel_ena = 1<<ourid;/* enable sel int */
		i = ncr->respar_int;	/* clear bsy/sel/phase int request */
		/* finally do handshake for message */
		ncr->init_comm = SIC_ACK;	/* set ack */

		/* spinwait for target to notice ack and drop BSY */
		for(i=MAXLOOP;(ncr->curr_stat & SCS_REQ) && --i ;)
			++iowaits;		/* waiting req to drop */
		if (!i)printscsi("msg ack maxloop");
		ncr->init_comm &= ~SIC_ACK;	/* drop ack */

		i = MAXLOOP;
		while (CS(SCS_BSY) && --i) {
		    ++iowaits;
		}
		if (i == 0) {
		    scsisched(SI_ABT, "scsimsgin");
		}
		

		/* slowdown when hand is quicker than the eye */
		if ( (stp->flags & SF_TRACEPR) )
			for(i=(scsitracespeed * 10); --i;)
				;
		return ( 0 );
	}  else  {
		/* finally do handshake for message */
		ncr->init_comm = SIC_ACK;	/* set ack */

		/* spinwait for target to notice before returning */
		for(i=MAXLOOP;(ncr->curr_stat & SCS_REQ) && --i ;)
			++iowaits;		/* waiting req to drop */
		if (!i)printscsi("msg ack maxloop");
		ncr->init_comm &= ~SIC_ACK;	/* drop ack */
		return ( more );
	}
}
