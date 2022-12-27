/*
 * @(#)sm.asc.h  {Apple version 1.6 90/04/01 11:24:41}
 *
 * Copyright (c) 1987, 1988, 1989 by Apple Computer, Inc.
 * All Rights Reserved.
 *
 * THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF APPLE COMPUTER, INC.
 * The copyright notice above does not evidence any actual or
 * intended publication of such source code.
 */

/*	SM.asc.h
 *
 *	This file describes the Apple Sound Chip characteristics
 *	See SM.p or SM.h for the public stuff.
 *
 *	Author: Mark Lentczner, x3802
 *	Copyright 1986, Apple Computer, Inc.
 *
 *	Ported to AUX Feb'89 - Rob Smith
 */

typedef struct {
		char		version;
		char		mode;
		char		chipControl;
		char		fifoControl;
		char		fifoInterrupt;
		char		waveOneShot;
		char		volControl;
		char		clockRate;
		char		filler[7];
		char		testRegister;
		struct {
			long		phase;
			long		inc;
		} waveFreq[4];
		char		ampZeroCross[4];
		char		ampImmediate[4];
} ASCControlSpace;



typedef union {
		char		wavetable[4][512];
		char		fifo[2][1024];
} ASCDataSpace;


typedef struct {
		ASCDataSpace	data;
		ASCControlSpace	ctrl;
} ASCSpace;
		
extern long sound_addr;
#define ASC	sound_addr

#define ASCQuietMode		0x00
#define ASCFifoMode		0x01
#define ASCWaveMode		0x02

#define ASCPWM			0x00
#define ASCAnalog		0x01
#define ASCMono			0x00
#define ASCStereo		0x02
#define ASCOverrun		0x80

#define ASCRomCompand		0x01
#define ASCNonRomCompand	0x02
#define ASCFifoClear		0x80

#define ASCHalfA		0x01
#define ASCFullA		0x02
#define ASCHalfB		0x04
#define ASCFullB		0x08

#define ASCOneShot0		0x01
#define ASCOneShot1		0x02
#define ASCOneShot2		0x04
#define ASCOneShot3		0x08
#define ASCOneShotMode		0x80

#define ASCInternalVolMask	0x1C
#define ASCInternalVolShift	2
#define ASCExternalVolMask	0xE0
#define ASCExternalVolShift	5

#define ASC22kMac		0x00
#define ASC22kAlmostMac		0x02
#define ASC44kCD		0x03

#define ASCNoTest		0x00
#define ASCTestFilterInput	0x10
#define ASCTestSHOutput		0x20
#define ASCTestDriveData	0x40
#define ASCTestSignature	0x80

#define SamplingRate22kMac	(15667200/(512+192))	/* C16M/(MacPixels+HorzBlanking) */
#define FixSamplingRate22kMac	(1458473891)
#define NeedRate22kMac		(long)(512*2000/SamplingRate22kMac)
#define SamplingRate44kCD	(16934400/384)		/* CD-Crystal/ASC-CD-Divisor */
#define FixSamplingRate44kCD	(SamplingRate44kCD*65536)
#define NeedRate44kCD		(long)(512*2000/SamplingRate44kCD)

/*
 * Note: If you wish to handle the SNDIRQ, your driver must stuff
 * an interrupt vector into lvl2funcs[4] (in via.c).
 */

extern short machineID;
extern struct via *via2_addr;

#define SNDIER(on)	\
    if (machineID == MACIIfx) { \
	extern struct oss *oss; \
	oss->oss_intlev[OSS_SOUND] = (on ? OSS_INTLEV_SOUND : OSS_INTLEV_DISABLED); \
    } \
    else \
	via2_addr->ier = (on ? VIE_SET : VIE_CLEAR) | VIE_CB1

#define	CLRSNDIRQ() \
    if (machineID != MACIIfx) \
	via2_addr->ifr = VIE_SET | VIE_CB1

/*
 * Same as above for VIA1 Timer #1
 */
#define VIA_TIMIRQ(on)	(((struct via *) VIA1_ADDR)->ier = (on ? VIE_SET : VIE_CLEAR) | VIE_TIM1)
#define	VIA_CLRTIMIRQ()	(((struct via *) VIA1_ADDR)->ifr = VIE_TIM1)
#define LATCH_LO()	(((struct via *) VIA1_ADDR)->t1ll = 0xfe)
#define LATCH_HI()	(((struct via *) VIA1_ADDR)->t1lh = 0xff)

#define STOP_ASC_INTS(ch)	\
    SNDIER(0); \
    CLRSNDIRQ(); \
    untimeout(catch_asc_timeout, ch); \
    sndintr_cnt = -1;

#define START_ASC_INTS() \
    CLRSNDIRQ(); \
    SNDIER(1);

