/*
 * @(#)samp.desc.h  {Apple version 1.4 89/12/12 22:56:25}
 *
 * Copyright (c) 1987, 1988, 1989 by Apple Computer, Inc.
 * All Rights Reserved.
 *
 * THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF APPLE COMPUTER, INC.
 * The copyright notice above does not evidence any actual or
 * intended publication of such source code.
 */

/*
 * 2/2/89 rms		Ported to AUX
 */

#include "types.h"
#include "retrace.h"
 
typedef struct PlayBlock
{
	unsigned char	* getSamp;	/* sndintr() fills ASC from here */
	unsigned char	* putSamp;	/* sndwrite() puts new data here */
	unsigned char	* currEnd;	/* the sndintr() stop/loop pt */
	unsigned char	* sndStart;	/* start of AttackBuf */
	unsigned char	* sndEnd;	/* end of SustainBuf */
	long		loopStart;	/* rel. loop points - zero if none */
	long		loopEnd;

	Fixed		baseRate;
	short		baseNote;
	
	short		playState;
	long		fraction;	/* this is the phase ptr into the sample */
	Fixed		rate;
	
	long		totalLength;	/* used by sndintr() to know when to stop */

	Boolean		wantsToQuit;
	Boolean		doAllZero;
	Boolean		unused;		/* for allignment */
	Boolean		suppressClick;	/* smooth out old note */
} PlayBlock;

typedef struct SampDesc			/* master */
{
	PlayBlock	* nowPlaying;	/* pointer to currently playing block */
	PlayBlock	* nextToPlay;	/* pointer to dormant block - not used in AUX */
	Boolean		reallyQuiet;	/* same as (Quiet & not doAllZero) */
	Boolean		block;		/* true if we gave up the sound hardware so another channel could play */
	short		whyTickled;	/* reason we are expecting tickle commands */
	PlayBlock	blocks[2];	/* the play descriptors - AUX only uses 1st */
	Boolean		reallyFree;	/* true iff installtask VBL block is free *//* <S493 12may88 mgl> */
} SampDesc;

#define Quiet			0	/* values for playState */
#define Buffer			10
#define Attack			21
#define Loop			22
#define Decay			23

#define NoReason		0	/* whyTickled shares values with playState */
#define WantQuiet		30
#define Flush			4

#define GainCmd			100	/* claim the sound hardware */
#define ReleaseCmd		101	/* give up the sound hardware */
#define FlushCleanlyCmd	102		/* process all commands in queue
					   quickly, but make no sound and do
					   the callBacks */

/* added for AUX */

struct SampDesc		SampDescTbl[TOTSYNTHS];
