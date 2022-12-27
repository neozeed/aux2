/*
 * @(#)sm.asc.c  {Apple version 1.9 90/04/01 11:29:38}
 *
 * Copyright (c) 1987, 1988, 1989 by Apple Computer, Inc.
 * All Rights Reserved.
 *
 * THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF APPLE COMPUTER, INC.
 * The copyright notice above does not evidence any actual or
 * intended publication of such source code.
 */

#include "sys/types.h"
#include "sys/via6522.h"
#include "sys/oss.h"
#include "sys/uconfig.h"
#include "sm.h"
#include "sys/sm_aux.h"
#include "samp.desc.h"
#include "sm.privdesc.h"
#include "sm.asc.h"

int sndintr_cnt;	/* inc this at every snd interrupt */
extern int (*lvl1funcs[])();
extern int (*lvl2funcs[])();
extern sndintr();
extern OnTimeInterrupt();
extern getAuxChan();
extern int synthType;

Boolean
ASCAlloc()
{
	if (((ASCSpace *) ASC)->ctrl.mode != ASCQuietMode)
		return(false);
	
	((ASCSpace *) ASC)->ctrl.clockRate = ASC22kMac /* ASC44kCD */;
	((ASCSpace *) ASC)->ctrl.chipControl = ASCStereo + ASCPWM;
	((ASCSpace *) ASC)->ctrl.mode = ASCFifoMode;
	((ASCSpace *) ASC)->ctrl.fifoControl |= ASCFifoClear;
	((ASCSpace *) ASC)->ctrl.fifoControl &= ~ASCFifoClear;

	return(true);
}


int
catch_asc_timeout(ch)	/* Watchdog timer in case ASC loses an interrupt */
SndChannelPtr	ch;
{
	if (sndintr_cnt == 0)	/* if no sndintr()'s since last, restart */
		ASCAttack(ch);
	else if (sndintr_cnt == -1)
		return;
	else
		timeout(catch_asc_timeout, ch, 50);

	sndintr_cnt = 0;	/* inc this at every snd interrupt */
}


void
ASCInit()
{
	/* 
	 * Setup the interrupt routine.  On Mac IIfx machines, this 
	 * sound interrupt goes through the OSS chip. The Mac IIfx
	 * interrupt handler routine grabs the function from
	 * lvl2funcs.
	 */
	lvl2funcs[4] = sndintr;

	SNDIER(0);
	CLRSNDIRQ();
	sndintr_cnt = -1;
}


short
ASCRate()
{
	return(NeedRate22kMac);
}


void
ASCFree(ch)
SndChannelPtr	ch;
{
	STOP_ASC_INTS(ch);
	((ASCSpace *) ASC)->ctrl.mode = ASCQuietMode;
}



ASCAttack(ch)
SndChannelPtr	ch;
{
	struct SampDesc *desc;
	struct PlayBlock *currPB;
	auxSndChPtr auxCh;
	int catch_asc_timeout();

	if ((synthType == MIN_SAMP) || (synthType == MIN_RAW))
	{
		auxCh = (auxSndChPtr)getAuxChan(ch);

		desc = &SampDescTbl[auxCh->aux_min_num];
		currPB = desc->nowPlaying;
		currPB->playState = Attack;

		untimeout(catch_asc_timeout, ch);
		sndintr_cnt = 0;	/* inc this at every snd interrupt */
		timeout(catch_asc_timeout, ch, 50);

		sndintr();			/* kick out the jams */
	}
}


Boolean
ASCTickle()
{
	/* use playState == Quite now */
	/* return (TestResume(0) == -1);	 read with out changing, true if sound done */
}

#define HALFMSEC 390	/* timer ticks per .5 msec */
unsigned short fractionalCnt, fullCnts;
unsigned short lastFracCnt, lastFullCnt;

void
StartTimer(halfMSecCnt)	/* time to interrupt in number of .5 msec units */
long halfMSecCnt;
{
	unsigned long viaTicks;
	unsigned char loByte, hiByte;

	VIA_CLRTIMIRQ();

	if (halfMSecCnt == 0)	/* this should not happen - but just to be safe */
	{
		StopTimer();
		return;
	}

	viaTicks = HALFMSEC * halfMSecCnt;
	fractionalCnt = (viaTicks & 0x0ffff);
	lastFracCnt = fractionalCnt;
	fullCnts = (viaTicks >> 16) & 0x0ffff;
	lastFullCnt = fullCnts;
	if (fullCnts)
	{
		((struct via *) VIA1_ADDR)->t1cl = 0xff;
		((struct via *) VIA1_ADDR)->t1ch = 0xff;
		fullCnts--;
	}
	else
	{
		loByte = fractionalCnt & 0x0ff;
		hiByte = (fractionalCnt >> 8) & 0x0ff;
		((struct via *) VIA1_ADDR)->t1cl = loByte;
		((struct via *) VIA1_ADDR)->t1ch = hiByte;
		fractionalCnt = 0;
	}

	VIA_TIMIRQ(1);
}

StopTimer()
{
	VIA_TIMIRQ(0);
	VIA_CLRTIMIRQ();
}

timeintr()
{
	char loByte, hiByte;
	int s;
	long lastTime, et;

	VIA_CLRTIMIRQ();
	VIA_TIMIRQ(0);
	s = spl6();
	if (fullCnts)
	{
		((struct via *) VIA1_ADDR)->t1cl = 0xff;
		((struct via *) VIA1_ADDR)->t1ch = 0xff;
		fullCnts--;
		VIA_TIMIRQ(1);
	}
	else if (fractionalCnt)
	{
		loByte = fractionalCnt & 0x0ff;
		hiByte = (fractionalCnt >> 8) & 0x0ff;
		((struct via *) VIA1_ADDR)->t1cl = loByte;
		((struct via *) VIA1_ADDR)->t1ch = hiByte;
		fractionalCnt = 0;
		VIA_TIMIRQ(1);
	}
	else
	{
		/* figure elapsed time since last call to OnTimeInterrupt() - timer now zero */
		lastTime = (lastFullCnt << 16) + lastFracCnt;
		et = (lastTime / HALFMSEC);

		OnTimeInterrupt(et);	/* this routine resets timer & vars via WriteMinTimer() */
	}
	splx(s);

}

void
InitTimer()
{
	/* 
	 * Stuff the timer interrupt address into the via1 interrupt table.
	 */
	lvl1funcs[6] = timeintr;

	VIA_CLRTIMIRQ();
	VIA_TIMIRQ(0);
}

void WriteMinTimer(timeVal)
long timeVal;
{
	int save;
	char loByte, hiByte, tstChange;
	long newTicks, currTicks;

	if (timeVal == -1)
	{
		StopTimer();
		fullCnts = 0;
		fractionalCnt = 0;
		return;
	}

	save = spl6();
	if ((fullCnts + fractionalCnt) == 0)
	{
		StartTimer(timeVal);
		splx(save);
		return;
	}

	newTicks = HALFMSEC * timeVal;
	
	save = spl6();
	do {
		hiByte = ((struct via *) VIA1_ADDR)->t1ch;
		loByte = ((struct via *) VIA1_ADDR)->t1ch;
		tstChange = ((struct via *) VIA1_ADDR)->t1ch;
	} while (hiByte != tstChange);

	currTicks = (fullCnts << 16) + (hiByte << 8) + loByte;

	if (newTicks < (currTicks - HALFMSEC)) /* if within .5msec, reload */
	{
		StopTimer();
		StartTimer(timeVal);
	}
	splx(save);
}
