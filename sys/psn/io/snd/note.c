/*
 * @(#)note.c  {Apple version 1.6 90/03/10 17:53:05}
 *
 * Copyright (c) 1987, 1988, 1989 by Apple Computer, Inc.
 * All Rights Reserved.
 *
 * THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF APPLE COMPUTER, INC.
 * The copyright notice above does not evidence any actual or
 * intended publication of such source code.
 */

/*	note.c
 *
 *	This is the ASC implementation of the Note
 *	synthesizer.
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

#define SampRateScale		331706.3948	/* (32768.0 * 440.0 * 512.0 / SamplingRate22kMac) */
#define	InverseSampRate		(1.0 / SamplingRate22kMac)
#define thirty2768		32768.0

struct NoteInfo
{
	unsigned long	amp;
	unsigned long	timbre;
	char		status;
}noteInfo;

#define Active			1
#define InActive		0

extern getAuxChan();

Boolean
noteMain(chan, cmd, mod)
	struct SndChannel	*chan;
	struct SndCommand	*cmd;
	struct ModifierStub	*mod;
{
	char		volume;
	int		s;
	long		playNote;
	long		increment;
	struct SndCommand	nextCommand;
	struct NoteInfo		*myInfo;
	struct SampDesc		*myDesc;
	auxSndChPtr		auxCh;

	auxCh = (auxSndChPtr)getAuxChan(chan);
	
	nextCommand.commandNum = NullCmd;
	switch (cmd->commandNum)
	{
	/* these commands can be made from SndControl */
	/* AUX - these are handled in toolbox part of SM */

	case AvailableCmd:
	case VersionCmd:
		return(false);
		break;
			
	default:
	/* these commands can only be made from SndDoCommand */
	
	if (mod == nil) 
		return(false);

	/* myInfo = (struct NoteInfo *)&(mod->userInfo); */
	myInfo = (struct NoteInfo *)&noteInfo;

	if ((cmd->commandNum != InitCmd) && (myInfo->status == InActive))
		return(false);

	switch (cmd->commandNum)
	{
	case InitCmd:
		myInfo->status = InActive;
	
		if (((ASCSpace *) ASC)->ctrl.mode != ASCQuietMode)
			break;
		if (((ASCSpace *) ASC)->ctrl.version == 0)
		{
			((ASCSpace *) ASC)->ctrl.chipControl = ASCMono + ASCPWM;
		}
		else
		{
			((ASCSpace *) ASC)->ctrl.chipControl = ASCMono + ASCAnalog;
		}
		((ASCSpace *) ASC)->ctrl.clockRate = ASC22kMac;
		((ASCSpace *) ASC)->ctrl.waveOneShot = 0;
		
		((ASCSpace *) ASC)->ctrl.waveFreq[0].phase = 0;
		((ASCSpace *) ASC)->ctrl.waveFreq[0].inc = 0;
		((ASCSpace *) ASC)->ctrl.waveFreq[1].phase = 0;
		((ASCSpace *) ASC)->ctrl.waveFreq[1].inc = 0;
		((ASCSpace *) ASC)->ctrl.waveFreq[2].phase = 0;
		((ASCSpace *) ASC)->ctrl.waveFreq[2].inc = 0;
		((ASCSpace *) ASC)->ctrl.waveFreq[3].phase = 0;
		((ASCSpace *) ASC)->ctrl.waveFreq[3].inc = 0;
				
		((ASCSpace *) ASC)->data.wavetable[0][0] = 0x80;
		((ASCSpace *) ASC)->data.wavetable[1][0] = 0x00;
		((ASCSpace *) ASC)->data.wavetable[2][0] = 0x00;
		((ASCSpace *) ASC)->data.wavetable[3][0] = 0x00;
		
		volume = (((ASCSpace *) ASC)->ctrl.version == 0) ? 0xFF : 0x60;
		
		((ASCSpace *) ASC)->ctrl.ampZeroCross[0] = volume;
		((ASCSpace *) ASC)->ctrl.ampImmediate[0] = volume;
		((ASCSpace *) ASC)->ctrl.ampZeroCross[1] = volume;
		((ASCSpace *) ASC)->ctrl.ampImmediate[1] = volume;
		((ASCSpace *) ASC)->ctrl.ampZeroCross[2] = volume;
		((ASCSpace *) ASC)->ctrl.ampImmediate[2] = volume;
		((ASCSpace *) ASC)->ctrl.ampZeroCross[3] = volume;
		((ASCSpace *) ASC)->ctrl.ampImmediate[3] = volume;

		myInfo->amp =  (((ASCSpace *) ASC)->ctrl.version == 0) ? 0x60 : 0xFF;
		myInfo->timbre = 80;
		s = spl6();
		setTimbre(myInfo->amp, myInfo->timbre, &(((ASCSpace *) ASC)->data.wavetable[0][0]));
		splx(s);

		((ASCSpace *) ASC)->ctrl.mode = ASCWaveMode;

		/*
		 * The PlayBlocks are not used for the Note synth
		 * but we set it up with null values for consistency.
		 */

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

		myInfo->status = Active;

		break;			

	case FreeCmd:
		myInfo->status = InActive;
		((ASCSpace *) ASC)->ctrl.mode = ASCQuietMode;
		break;
			
	case NoteCmd:
	case FreqCmd:
		/* AUX passes down pre-computed value here */
		/* amplitude is now sent as seperate command */
		((ASCSpace *) ASC)->ctrl.waveFreq[0].inc = cmd->longArg;
		
		if (cmd->commandNum == NoteCmd)
		{
			nextCommand.commandNum = WaitCmd;
			nextCommand.wordArg = cmd->wordArg & 0xffff;
		}
		break;
		
	case RestCmd:
			nextCommand.commandNum = WaitCmd;
			/* Get duration from first parameter */
			nextCommand.wordArg = cmd->wordArg;
			/* Turn off note */
			((ASCSpace *) ASC)->ctrl.waveFreq[0].inc = 0;
			break;
			
	case EmptyCmd:
	case QuietCmd:
			((ASCSpace *) ASC)->ctrl.waveFreq[0].inc = 0;
			nextCommand = *cmd;
			break;
			
	case AmpCmd:
			if (((ASCSpace *) ASC)->ctrl.version == 0)
			{
				myInfo->amp = (unsigned long) cmd->wordArg & 0xFF;
				s = spl6();
				setTimbre(myInfo->amp, myInfo->timbre, &(((ASCSpace *) ASC)->data.wavetable[0][0]));
				splx(s);
			}
			else
			{
				((ASCSpace *) ASC)->ctrl.ampZeroCross[0] = cmd->wordArg & 0xFF;
			}
			break;
	
	case TimbreCmd:
			myInfo->timbre = (unsigned long) cmd->wordArg & 0xFF;
			s = spl6();
			setTimbre(myInfo->amp, myInfo->timbre, &(((ASCSpace *) ASC)->data.wavetable[0][0]));
			splx(s);
			break;

	default:
			nextCommand = *cmd;
			break;
	} /* end of inside switch */
	break;
	} /* end of outside switch */
	
	*cmd = nextCommand;
	return(false);
}
