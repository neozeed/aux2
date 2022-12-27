/*
 * @(#)wave.c  {Apple version 1.4 89/12/12 22:57:13}
 *
 * Copyright (c) 1987, 1988, 1989 by Apple Computer, Inc.
 * All Rights Reserved.
 *
 * THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF APPLE COMPUTER, INC.
 * The copyright notice above does not evidence any actual or
 * intended publication of such source code.
 */

/*	Wave.c
 *
 *	This is the ASC implementation of the Wave Table
 *	synthesizer.  Current limitations:
 *		monophonic
 *		only zero cross amplitude control
 *		Last data point interpolated wrong
 *		no standard timbres
 *		no phase support
 *		no parameter scaling support
 *
 *	Ported to AUX Feb'89 - Rob Smith
 */

#include "sys/types.h"
#include "sys/uconfig.h"
#include "sm.h"
#include "sys/sm_aux.h"
#include "sm.privdesc.h"
#include "samp.desc.h"
#include "sm.asc.h"

static int assignment;			/* total number of wave synths open */

extern long Interp();
extern getAuxChan();

typedef struct
{
	char				status;
	char				channel;
} WaveInfo;

#define Active			1
#define InActive		0


Boolean
waveMain(chan, comm, mod)
	SndChannelPtr		chan;
	struct SndCommand	*comm;
	ModifierStubPtr		mod;
{
	struct SndCommand	nextCommand;
	WaveInfo		*myInfo;
	long			playNote;
	struct SampDesc		* myDesc;
	struct PlayBlock	* currPB;
	auxSndChPtr		auxCh;
	
	auxCh = (auxSndChPtr)getAuxChan(chan);

	nextCommand.commandNum = NullCmd;
	switch (comm->commandNum)
	{
	/* these commands can be made from SndControl */
	/* AUX - these are handled in toolbox part of SM */
	case AvailableCmd:
	case VersionCmd:
			return(false);
			
	default:
	/* these commands can only be made with SndDoCommand */
		if (mod == nil) 
			return(false);
		myInfo = (WaveInfo *)&(mod->userInfo);
		
		if ((comm->commandNum != InitCmd) && (myInfo->status == InActive))
			return(false);

		switch (comm->commandNum)
		{
		    case InitCmd:
		    {
			short	zero;
			short	shiftAmt;
			short	control;
			
			myInfo->status = InActive;
			
			if (((ASCSpace *) ASC)->ctrl.mode != ASCWaveMode)
			{
				if (((ASCSpace *) ASC)->ctrl.mode != ASCQuietMode) break;
	
				shiftAmt = 0;
				
				if (((ASCSpace *) ASC)->ctrl.version == 0)
					control = ASCPWM;
				else
					control = ASCAnalog;
					
				if ((comm->longArg & WaveInitSRateMask) == WaveInitSRate44k)
				{
					((ASCSpace *) ASC)->ctrl.clockRate = ASC44kCD;
					if (control == ASCPWM) shiftAmt++;
				}
				else
					((ASCSpace *) ASC)->ctrl.clockRate = ASC22kMac;
				
				
				if ((comm->longArg & WaveInitStereoMask) == WaveInitStereoStereo)
				{
					control |= ASCStereo;
					if (control == ASCPWM) shiftAmt++;
				}
				else
				{
					control |= ASCMono;
					if (control == ASCPWM) shiftAmt += 2;
				}

				((ASCSpace *) ASC)->ctrl.chipControl = control;
				((ASCSpace *) ASC)->ctrl.waveOneShot = 0;
	
				SetShift(shiftAmt);

				((ASCSpace *) ASC)->ctrl.waveFreq[0].phase = 0;
				((ASCSpace *) ASC)->ctrl.waveFreq[0].inc = 0;
				((ASCSpace *) ASC)->ctrl.waveFreq[1].phase = 0;
				((ASCSpace *) ASC)->ctrl.waveFreq[1].inc = 0;
				((ASCSpace *) ASC)->ctrl.waveFreq[2].phase = 0;
				((ASCSpace *) ASC)->ctrl.waveFreq[2].inc = 0;
				((ASCSpace *) ASC)->ctrl.waveFreq[3].phase = 0;
				((ASCSpace *) ASC)->ctrl.waveFreq[3].inc = 0;
				
				zero = 0x8080;
				SetWaveTable(0, 0, &zero);
				SetWaveTable(1, 0, &zero);
				SetWaveTable(2, 0, &zero);
				SetWaveTable(3, 0, &zero);
		
				((ASCSpace *) ASC)->ctrl.testRegister = 0;
				((ASCSpace *) ASC)->ctrl.mode = ASCWaveMode;
				((ASCSpace *) ASC)->ctrl.testRegister = 0;

			}
			
			/* This is for the WaveTableCmd buffers */
			myDesc = &SampDescTbl[auxCh->aux_min_num];
			myDesc->nowPlaying = &(myDesc->blocks[0]);
			myDesc->nowPlaying->getSamp = auxCh->aux_beg;
			myDesc->nowPlaying->putSamp = auxCh->aux_beg;
			myDesc->nowPlaying->currEnd = auxCh->aux_beg;
			myDesc->nowPlaying->sndStart = auxCh->aux_beg;
			myDesc->nowPlaying->sndEnd = auxCh->aux_end;
			myDesc->nowPlaying->loopStart = 0;
			myDesc->nowPlaying->loopEnd = 0;
			myDesc->nowPlaying->totalLength = 0;

			/*  AUX - next line assumes all wavesynth minor numbers are consecutive */
			myInfo->channel = auxCh->aux_min_num - MIN_WAVE1;
			myInfo->status = Active;

			assignment++;			/* total active synths */

			break;			
		    }
		
		case FreeCmd:
			myInfo->status = InActive;
			
			if (--assignment == 0)
			{
				((ASCSpace *) ASC)->ctrl.mode = ASCQuietMode;
			}
			
			break;
				
		case NoteCmd:
		case FreqCmd:
			/* AUX passes down pre-computed value here */
			/* amplitude is now sent as seperate command */

			((ASCSpace *) ASC)->ctrl.waveFreq[myInfo->channel].inc = comm->longArg;
			
			if (comm->commandNum == NoteCmd)
			{
				nextCommand.commandNum = WaitCmd;
				nextCommand.wordArg = comm->wordArg & 0xffff;
			}

			break;
			
		case RestCmd:
			nextCommand.commandNum = WaitCmd;
			/* Get duration from first parameter */
			nextCommand.wordArg = comm->wordArg;
			/* Turn off note */
			((ASCSpace *) ASC)->ctrl.waveFreq[myInfo->channel].inc = 0;
			break;
				
		case EmptyCmd:
		case QuietCmd:
			((ASCSpace *) ASC)->ctrl.waveFreq[myInfo->channel].inc = 0;
			nextCommand = *comm;
			break;
	
	
		case WaveTableCmd:
			SetWaveTable(myInfo->channel, comm->wordArg, auxCh->aux_beg);
			break;
		
	
		case PhaseCmd:
		case TimbreCmd:
			break;
	
		case AmpCmd:
			((ASCSpace *) ASC)->ctrl.ampZeroCross[myInfo->channel] = comm->wordArg & 0xFF;
			break;
				
		default:
			nextCommand = *comm;
		} /* end of inside switch */
	} /* end of outside switch */
			
	comm->commandNum = nextCommand.commandNum;
	comm->wordArg = nextCommand.wordArg;
	comm->longArg = nextCommand.longArg;
	return(false);
}

/*
 * AUX - we store the data from a write to /dev/snd/wave into a buffer pointed at
 *	 by auxCh->aux_beg
 *	 This means we generate own own size and data args instead of using
 *	 wordArg & longArg.
 */
SetWaveTable(which, size, area)
	short	which;
	short	size;
	unsigned char	*area;
{
	register unsigned char 	*samp;
	register Fixed		frac;
	register Fixed		inc;
	register short		i;
	register short		shift;

	frac = 0;
	inc = size << 7;
	samp = area;
	shift = GetShift();
	
	if (inc == 65536)
	{
		for (i=0; i<512; i++)
			((ASCSpace *) ASC)->data.wavetable[which][i] = *(samp++) >> shift;
	}
	else
	{
		for (i=0; i<512; i++)
		{
			/* note this must be a logical shift, not an arithmetic one */
			((ASCSpace *) ASC)->data.wavetable[which][i] = Interp(frac, samp) >> shift;
				
			frac += inc;			
			samp += frac >> 16;
			frac &= 0xFFFF;
		}
	}
}
