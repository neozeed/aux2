/*
 * @(#)sm.privdesc.h  {Apple version 1.5 90/01/02 11:22:43}
 *
 * Copyright (c) 1987, 1988, 1989 by Apple Computer, Inc.
 * All Rights Reserved.
 *
 * THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF APPLE COMPUTER, INC.
 * The copyright notice above does not evidence any actual or
 * intended publication of such source code.
 */

/*	sm.privdesc.h
 *
 *	This file describes the internal structures of the Sound Manager.
 *	See SM.p or SM.h for the public stuff.
 *
 *	Ported to AUX feb'89 - Rob Smith
 */



#ifndef onMac
#define onMac	0
#endif

#ifndef onMacPP
#define onMacPP	0
#endif

#ifndef onTMac
#define onTMac	0
#endif

#ifndef onNuMac
#define onNuMac	1
#endif

#ifndef onOldNu
#define onOldNu	0
#endif

/* Flag constants for the flags field of the modifier stub
 */
 
#define ModFromResource	0x01	/* set if the modifier code came from a resource */



/* Flag constants for the flags field of the channel
 */
 
#define ChFreeOnQuit	0x01     /* set if the channel should be freed by the Sound Manager */
#define ChQuiet		0x02	/* set when a quiet command has passed through the channel and no other commands have been sent */
#define ChPaused	0x04	/* set when the channel has been paused */
#define ChSyncd		0x08	/* set when the channel is being sync'd up */
#define ChNewlyEmpty	0x10	/* set when a channel goes empty but before an EmptyCmd has be sent */
#define ChHeld		(ChPaused | ChSyncd)	/* test if the channel should be processed */
#define	ChCallBack	0x20	/* callBack pending to toolbox - for select() polling */
#define ChDoNextCmd	0x40	/* set within timer IRQ while ints disabled - processed after timer reset & IRQ enable */
#define ChClsOnQEmpty	0x80	/* close the file desc while kernel SndChan is still busy. Block on open if still busy */

/* Timer and interrupt manipulation calls
 */
extern void		InitTimer();
extern void		WriteMinTimer();

struct SndChannel	channelList[TOTSYNTHS];
struct auxSndCh		auxChanList[TOTSYNTHS];
struct ModifierStub	modList[TOTSYNTHS];

#define MIN(a,b)	(((a) < (b))?(a):(b))
#define min(a,b)	MIN(a,b)

/*
 * 32 Debug flags
 */
#define DB_ALWAYS	0x00000001	/* Catch all */
#define DB_INTR		0x00000002	/* Interrupt */

#ifdef DEBUG

unsigned int snd_debug;

#define Debug(flag,p)	if (snd_debug & flag) printf p
#else
#define Debug(flag,p)
#endif
