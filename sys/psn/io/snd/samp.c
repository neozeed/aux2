/*
 * @(#)samp.c  {Apple version 1.8 90/03/28 18:37:41}
 *
 * Copyright (c) 1987, 1988, 1989 by Apple Computer, Inc.
 * All Rights Reserved.
 *
 * THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF APPLE COMPUTER, INC.
 * The copyright notice above does not evidence any actual or
 * intended publication of such source code.
 */

/*	sm.samp.c
 *
 *	This is the Sampled Sound synthesizer.
 *	Current limitations:
 *		one channel (fake stereo!!!)
 *		no companding mode
 *		no amplitude control
 *		no hardware arbitration
 *		no standard timbres
 *		no parameter scaling support
 *
 *	Ported to AUX Feb'89 - Rob Smith
 */
 
#include "sys/types.h"
#include "sys/uio.h"
#include "sys/ioctl.h"
#include "sys/errno.h"
#include "sys/buf.h"
#include "sys/param.h"
#include "sys/sysmacros.h"
#include "sys/file.h"
#include "sys/via6522.h"
#include "sys/uconfig.h"
#include "sys/mmu.h"
#include "sys/page.h"
#include "sys/map.h"
#include "sys/systm.h"
#include "sys/time.h"
#include "sys/user.h"
#include "sm.h"
#include "sys/sm_aux.h"
#include "sm.privdesc.h"
#include "samp.desc.h"
#include "sm.asc.h"

extern int  sampleType;		/* buffering technique */
extern getAuxChan();
extern snd_proc;

/*
 *	SampledSynthCode -
 *		The whole sheebang...
 */

Boolean
sampMain(chan, comm, mod)
	SndChannelPtr		chan;
	struct SndCommand	*comm;
	ModifierStubPtr		mod;
{
	struct SampDesc		* myDesc;
	struct PlayBlock	* currPB;
	struct SndCommand	nextCommand;
	long			playNote;
	Boolean			requestMore;
	Boolean			bufferDone;
	unsigned long		threeSecsWorth;
	auxSndChPtr		auxCh;
	
	Debug (DB_ALWAYS, ("sampMain() - ch 0x%x  cmd(0x%x 0x%x 0x%x)\n", chan,
	    comm->commandNum, comm->wordArg, comm->longArg));
	
	auxCh = (auxSndChPtr)getAuxChan(chan);

	/* Any command named in the case statement below gets turned into a 
		NullCmd and is not passed to the channel UNLESS you explicitly set
		nextCommand.commandNum in the case.  Watch out for QuietCmd, 
		FlushCmd, WaitCmd, PauseCmd, ResumeCmd, CallBackCmd, SyncCmd,
		HowOftenCmd, and WakeUpCmd */
		
	nextCommand.commandNum = NullCmd;
	requestMore = false;
	switch (comm->commandNum)
	{
 	/* these commands can be made from SndControl, ie. regardless of the mod arg */
	/* AUX - these get caught and processed in the toolbox part of the SM */
	case AvailableCmd:
	case VersionCmd:
		return(false);
		break;
			
	default:
	
	/* To make these commands, mod must be non-zero */
		if (mod == nil)
			return(false);

		myDesc = (struct SampDesc *)mod->userInfo;
	
		/* if we have no storage, only accept an init command */
		if (myDesc == nil)
		{
			if (comm->commandNum != InitCmd)
			{
				/* *comm = nextCommand; */
				return(false);
			}
		}
		else
		{
			if (myDesc->block) 
			{
				if (comm->commandNum != GainCmd)
				{
				return(false);
				}
				/* keep out all commands except GainCmd if we are blocked */
			}
			if (myDesc->whyTickled == FlushCleanlyCmd) 
			{
				if ((comm->commandNum != QuietCmd) && (comm->commandNum != CallBackCmd))
				{
					*comm = nextCommand;	/* pass on a NullCmd */
					return(false);
				}
				/* keep out all commands except QuietCmd if we are flushing */
			}
			currPB = myDesc->nowPlaying;
		}


		switch (comm->commandNum)
		{
		case InitCmd:
			
			if (ASCAlloc() == false)
			{
			break; /* can't get the sound chip */
			}

			mod->userInfo = (long)(myDesc = &SampDescTbl[auxCh->aux_min_num]);

			myDesc->nowPlaying = &(myDesc->blocks[0]);
			myDesc->reallyQuiet = true;
			myDesc->block = false;
			myDesc->whyTickled = NoReason;

			myDesc->blocks[0].playState = Quiet;
			myDesc->blocks[0].wantsToQuit = false;

			myDesc->nowPlaying->getSamp = auxCh->aux_beg;
			myDesc->nowPlaying->putSamp = auxCh->aux_beg;
			myDesc->nowPlaying->currEnd = auxCh->aux_beg;
			myDesc->nowPlaying->sndStart = auxCh->aux_beg;
			myDesc->nowPlaying->sndEnd = auxCh->aux_end;
			myDesc->nowPlaying->loopStart = 0;
			myDesc->nowPlaying->loopEnd = 0;
			myDesc->nowPlaying->totalLength = 0;

			ASCInit();

			break;
	
		case FreeCmd:
	
			ASCFree(chan);
			myDesc = (struct SampDesc *)nil;
			mod->userInfo = nil;			/* just to make sure */
			break;
					
				
		case NoteCmd:
		case FreqCmd:
			if (comm->commandNum == NoteCmd)
			{
				/* set up note durration */
				nextCommand.commandNum = WaitCmd;
				nextCommand.wordArg = comm->wordArg & 0xffff;
			}
			
			if (comm->longArg == 0)
			{
				if (currPB->playState == Quiet)	/* already resting */
					break;
					
				/* Let it play to the end of its samples. */
				currPB->playState = Decay;
				break;
			}
			
			/* AUX passes down pre-computed value here */
			/* amplitude is now sent as seperate command */
			currPB->rate = (unsigned long)comm->longArg;
			
			/* compute stop/loop point for this sampleType */
			switch (sampleType)
			{
			    case A_NLOOP:
			    case AS_NLOOP:
				currPB->currEnd = auxCh->aux_beg + curSndHdr.length;
				currPB->totalLength = curSndHdr.length;
				break;

			    case A_LOOP:
			    case AS_LOOP:
				currPB->currEnd = auxCh->aux_beg + curSndHdr.loopEnd;
				currPB->totalLength =  curSndHdr.loopEnd;
				break;

			    case X_NLOOP:
				currPB->currEnd = auxCh->aux_beg + ATTACKBUFSIZ + (SUSTBUFSIZ/2);
				currPB->totalLength = curSndHdr.length;
				break;

			    case X_LOOPSUST:
			    case X_LOOPX:
				currPB->currEnd = auxCh->aux_beg + ATTACKBUFSIZ + (SUSTBUFSIZ/2);
				currPB->totalLength = curSndHdr.loopEnd;
				break;

			    default:
				currPB->currEnd = auxCh->aux_beg + 1; /* illegal */
			}

			/* send down sustain data if duration > 3 secs */
			threeSecsWorth = (unsigned long)((unsigned long)(ATTACKBUFSIZ * 2000) / (unsigned long)(curSndHdr.sampleRate >> 16));
			if (((unsigned long)comm->wordArg >= threeSecsWorth) && (curSndHdr.length > ATTACKBUFSIZ))
			{
				psignal(snd_proc, SIGEMT);
			}

			myDesc->nowPlaying = currPB;

			currPB->getSamp = auxCh->aux_beg;
			currPB->putSamp = auxCh->aux_beg + ATTACKBUFSIZ;
			currPB->sndStart = auxCh->aux_beg;
			currPB->sndEnd = auxCh->aux_end;
			currPB->loopStart = curSndHdr.loopStart; /* offset */
			currPB->loopEnd = curSndHdr.loopEnd;     /* offset */
			auxCh->aux_state &= ~(LOOPFILL | LASTFILL | FINISHED);
			auxCh->aux_state |= FIRSTFILL;
			currPB->fraction = 0;
			auxCh->aux_cnt = 0;
			chan->flags &= ~ChQuiet;

			ASCAttack(chan);
			break;
			
		case RateCmd:
			currPB->rate = comm->longArg;
			break;
				
		case RestCmd:
			nextCommand.commandNum = WaitCmd;
			nextCommand.wordArg = comm->wordArg;
			
			if (currPB->playState == Quiet)		/* already resting */
				break;
				
			/* turn current note into a rest */
			/* Let it play to the end of its samples.  If you want silence,
			   use short notes as the timbre. */
			
			currPB->playState = Decay;
			break;
			
		case EmptyCmd:
			break;
	
		case SoundCmd:
			currPB->playState = Quiet;
			currPB->doAllZero = true;		/* silence last buffer full */
			currPB->sndStart = auxCh->aux_beg;	 /* AUX - this is now start of attackBuf */
			currPB->sndEnd = auxCh->aux_end;	 /* AUX - this is now end of sustainBuf */
			currPB->loopStart = curSndHdr.loopStart; /* this is now rel offset */
			currPB->loopEnd = curSndHdr.loopEnd;     /* this is now rel offset */
			currPB->baseRate = curSndHdr.sampleRate;
			currPB->baseNote = curSndHdr.baseNote;
			currPB->fraction = 0;
			currPB->totalLength = curSndHdr.length;

			break;

		case BufferCmd:
			currPB->playState = Quiet;
			currPB->doAllZero = true;		/* silence last buffer full */
			currPB->sndStart = auxCh->aux_beg;	 /* AUX - this is now start of attackBuf */
			currPB->sndEnd = auxCh->aux_end;	 /* AUX - this is now end of sustainBuf */
			currPB->loopStart = 0;
			currPB->loopEnd = 0;
			currPB->baseRate = curSndHdr.sampleRate;
			currPB->baseNote = curSndHdr.baseNote;
			currPB->fraction = 0;
			currPB->totalLength = curSndHdr.length;

			/* AUX passes down pre-computed value here */
			/* amplitude is now sent as seperate command */
			currPB->rate = (unsigned long)comm->longArg;
			
			/* compute stop/loop point for this sampleType */
	    		if (curSndHdr.length < ATTACKBUFSIZ)
			{
	    			sampleType = A_NLOOP;
				currPB->currEnd = auxCh->aux_beg + curSndHdr.length;
				currPB->totalLength = curSndHdr.length;
			}
	    		else if (curSndHdr.length < (ATTACKBUFSIZ + SUSTBUFSIZ))
			{
	    			sampleType = AS_NLOOP;
				currPB->currEnd = auxCh->aux_beg + curSndHdr.length;
				currPB->totalLength = curSndHdr.length;
			}
	    		else
			{
	    			sampleType = X_NLOOP;
				currPB->currEnd = auxCh->aux_beg + ATTACKBUFSIZ + (SUSTBUFSIZ/2);
				currPB->totalLength = curSndHdr.length;
			}


			/* send down sustain data if sample size > AttackBuf */
			if (curSndHdr.length > ATTACKBUFSIZ)
			{
				psignal(snd_proc, SIGEMT);
			}

			myDesc->nowPlaying = currPB;

			currPB->getSamp = auxCh->aux_beg;
			currPB->putSamp = auxCh->aux_beg + ATTACKBUFSIZ;
			auxCh->aux_state &= ~(LOOPFILL | LASTFILL | FINISHED);
			auxCh->aux_state |= FIRSTFILL;
			auxCh->aux_cnt = 0;
			chan->flags &= ~ChQuiet;

			ASCAttack(chan);
			break;

		case QuietCmd:
			currPB->playState = Quiet;
			bufferDone = true;
			if (bufferDone) 
				nextCommand.commandNum = QuietCmd;	/* pass it, we are done */
			else
			{	
				/* modify current block */
				currPB->wantsToQuit = true;
				currPB->playState = Quiet;	/* again */
				currPB->doAllZero = true;	/* silence last buffer full */
			}
			break;
			
		case TimbreCmd:
		case AmpCmd:
			break;
		
		case GainCmd:
			/* init hardware and let other commands in */
			if (ASCAlloc())   /* can we get the sound chip? */
			{
				myDesc->block = false;
				ASCInit();	/* start the interrupt */
			}
			break;

		case ReleaseCmd:
			/* give up the hardware and block all commands besides GainCmd */
			/* So other sound channels such as SysBeep can run */
			myDesc->block = true;
			ASCFree(chan);	/* release the sound chip */
			break;
	
		default:
			nextCommand = *comm;
			break;
				
		} /* end of switch for non-SndControl commands */
		
		break; /* end of default */
	}	/* end of general switch */
	
	comm->commandNum = nextCommand.commandNum;
	comm->wordArg = nextCommand.wordArg;
	comm->longArg = nextCommand.longArg;

	return(requestMore);
} /* end of main */

