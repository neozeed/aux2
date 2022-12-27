/*
 * @(#)chipsubs.c  {Apple version 1.1 89/08/15 11:38:55}
 *
 * Copyright (c) 1987, 1988, 1989 by Apple Computer, Inc.
 * All Rights Reserved.
 *
 * THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF APPLE COMPUTER, INC.
 * The copyright notice above does not evidence any actual or
 * intended publication of such source code.
 */

#if !defined(lint) && !defined(NO_SCCS_IDS)
static char _sccsid[]="@(#)chipsubs.c  {Apple version 1.1 89/08/15 11:38:55}";
#endif

#include "fd.h"
#include "fdhw.h"
#include <sys/via6522.h>
#include <sys/uconfig.h>

/* These subroutines do chip-oriented things.  */

/* These delays are CPU-independent, since the CPU must slowdown to sync
 * with the 16MHz I/O clock used by the VIA.  Each access takes 63.82uSec
 * (except the first, which could go off immediately, but we don't really
 * worry about that).
 */
#ifdef LINT
#define DELAY500NS
#define	DELAY1US
#else
#define DELAY500NS { DELAY256NS; DELAY256NS; }
#define	DELAY1US { DELAY256NS; DELAY256NS; DELAY256NS; DELAY256NS; }
#endif LINT

    extern long iwm_addr;

    extern struct chipparams *fd_chip;
    extern struct chipparams fd_c_params[];

    static short statusbits[] = {
	S_OUTWARD, S_STEPOK,   S_MOTOROFF,S_EJECTING,
	S_RDDATA0, S_FDHD     ,S_TWOSIDED,S_NODRIVE,
	S_NODISK,  S_WRTENAB,  S_NCYLZERO,S_TACH,
	S_RDDATA1, S_MFM,      S_NOTREADY,S_1MBMEDIA };

/*--------------------------------*/
int
fd_getchipmode()
{
    register struct iwm *iwm = (struct iwm *)iwm_addr;
    register u_char junk;
    register u_char mode;

    junk = iwm->Q7L;			/* address the mode reg */
    junk = iwm->Q6H;

    mode = iwm->Q7L & 0x1f;		/* get mode reg */

    junk = iwm->Q6L;			/* back to read mode */

#ifdef LINT
    junk = junk;
#endif

    if (mode == IWMMODE) {
	return(0);			/* WOZ mode */
    } else {
	return(1);			/* swim mode */
    }
}

/*--------------------------------*/
int
ism_addr(cmd)
register u_char cmd;
{
    register struct via *vp = (struct via *)VIA1_ADDR;
    register struct swim *sp = (struct swim *)iwm_addr;

    register u_char bits = 0;

    (void)ism_seltrack(cmd & M_SEL);	/* set SEL */

    bits |= (cmd & M_CA2) ? ISM_PHASE2 : 0;
    bits |= (cmd & M_CA1) ? ISM_PHASE1 : 0;
    bits |= (cmd & M_CA0) ? ISM_PHASE0 : 0;

    sp->wphase = bits | 0xf0;

    return(0);
}

/*----------------------------------*/
int
ism_command(cmd)
register u_char cmd;
{
    register struct via *vp = (struct via *)VIA1_ADDR;
    register struct swim *sp = (struct swim *)iwm_addr;
    register u_char c;

    (void)ism_addr(cmd);

    /* Phases must be set 0.5us before strobing. */

    DELAY500NS;
    c = sp->rphase;
    DELAY256NS;
    sp->wphase = c | ISM_PHASE3;	/* strobe that mother */

    DELAY1US;
    sp->wphase = c;			/* off with the strobe after 1us */

    return(0);
}

/*----------------------------------*/
int
ism_disable()
{
    register struct swim *sp = (struct swim *)iwm_addr;

    sp->wzeroes = (ISM_ENABLE | ISM_DRIVE0 | ISM_DRIVE1);    

    return(0);
}

/*----------------------------------*/
int
ism_enable(drive)
register int drive;
{
    register struct via *vp = (struct via *)VIA1_ADDR;
    register struct swim *sp = (struct swim *)iwm_addr;
    register u_char which;

    which = (drive? ISM_DRIVE1 : ISM_DRIVE0);

    /* Disable the other drive, enable the desired one. */

    sp->wzeroes = which ^ (ISM_DRIVE0 | ISM_DRIVE1);
    DELAY256NS;
    sp->wones   = ISM_ENABLE | which;

    DELAY1US;			/* wait 1us for valid status */
    
    return(0);
}

/*----------------------------------*/
int
ism_init()
{
    register struct via *vp = (struct via *)VIA1_ADDR;
    register struct swim *sp = (struct swim *)iwm_addr;

    register u_char s;

    s = 0x20;				/* set to write pulses */

    sp->wsetup = s;

    DELAY256NS;
    if (sp->rsetup != s) {		/* make sure it wrote correctly */
	return(-1);
    }

    DELAY256NS;
    sp->wzeroes = ~ISM_SETWOZ;		/* clear all mode register bits */

    DELAY256NS;
    sp->wphase = 0xf0;			/* all phase lines are outputs */

    return(0);
}

/*----------------------------------*/
int
ism_seltrack(track)
register int track;
{
    register struct swim *sp	= (struct swim *)iwm_addr;
    register struct via *vp	= (struct via *)VIA1_ADDR;

    /* They say that CA0 and CA1 must be high before SEL goes to 0 */

    sp->wphase = (ISM_PHASE0 | ISM_PHASE1 | 0xf0);

    if (track == 0) {
	vp->rega &= ~(1 << SELBIT);
    } else {
	vp->rega |= (1 << SELBIT);
    }

    sp->wphase = ISM_PHASE2 | 0xf0;	/* assume data transfer cmd */

    return(0);
}

/*----------------------------------*/
/*ARGSUSED*/
int
ism_setgcr()
{
    (void)(*fd_chip->command)(C_MOTOROFF); /* drive motor off */
    (void)(*fd_chip->disable)();	/* disable the drive */
    (void)ism_setwoz();			/* swap chip to woz mode */
    (void)woz_init();			/* init it */
    fd_chip = &fd_c_params[0];
    return(0);
}

/*----------------------------------*/
int
ism_setwoz()
{
    register struct swim *sp = (struct swim *)iwm_addr;
    register struct iwm *iwm = (struct iwm *)iwm_addr;
    register struct via *vp = (struct via *)VIA1_ADDR;
    register u_char junk;

    sp->wzeroes = (ISM_ENABLE | ISM_DRIVE0 | ISM_DRIVE1);
    DELAY256NS;
    sp->wzeroes = ISM_SETWOZ;		/* select IWM register set */

    junk = iwm->Q7L;			/* ensure not in write mode */
    junk = iwm->Q6L;
    junk = iwm->disable;

#ifdef LINT
    junk = junk;
#endif

    return(0);
}

/*----------------------------------*/
/*ARGSUSED*/
int
ism_setparams(cyl)
{
    /* Params taken from the 68030 Mac ROM code.
     * These are in halfclocks, so bit 0 is the 0.5 part.
     * Min is reduced by 3 clks, and others by 2 clks
     * to compensate for gate delays in the chip.
     */

    static u_char param1[] = {
	((15 - 3) << 1) + 0,		/* Min  = 15.0 */
	65,				/* Mult = 65 */
	((25 - 2) << 1) + 0,		/* SSL  = 25.0 */
	((25 - 2) << 1) + 0,		/* SSS  = 25.0 */
	((14 - 2) << 1) + 0,		/* SLL  = 14.0 */
	((14 - 2) << 1) + 0,		/* SLS  = 14.0 */
	((15 - 2) << 1) + 1,		/* RPT  = 15.5 */
	((15 - 2) << 1) + 1,		/* CSLS = 15.5 */
	((25 - 2) << 1) + 1,		/* LSL  = 25.5 */
	((25 - 2) << 1) + 1,		/* LSS  = 25.5 */
	((14 - 2) << 1) + 1,		/* LLL  = 14.5 */
	((14 - 2) << 1) + 1,		/* LLS  = 14.5 */
	0x97,				/* Late/Norm   */
	((15 - 2) << 1) + 1,		/* Time0 = 15.5 */
	0x57,				/* Early/Norm   */
	((31 - 2) << 1) + 1,		/* Time1 = 31.5 */
    };

    register struct via *vp = (struct via *)VIA1_ADDR;
    register struct swim *sp = (struct swim *)iwm_addr;
    register int i;

    /* Reset the param counter index. Bits here are as in Mac ROM */
    sp->wzeroes = (ISM_ACTION | ISM_WRITE | ISM_HDSEL);
    DELAY1US;				/* delay to let it reset */

    /* Right now, we only use a single set of params for all cylinders. */

    for (i = 0; i < 16; i++) {
	sp->wparams = param1[i];
	DELAY256NS;
    }
    return(0);
}

/*----------------------------------*/
int
ism_status()
{
    register struct via *vp = (struct via *)VIA1_ADDR;
    register struct swim *sp = (struct swim *)iwm_addr;
    register short stat = 0;
    register int i;

    for (i = 15; i >= 0; i--) {
	ism_addr(i);			/* send out address */
	if (sp->rhandshake & ISM_SENSE) {
	    stat |= statusbits[i];
	}
	DELAY256NS;
    }
    return(stat);
}

/*----------------------------------*/
int
woz_addr(cmd)
register u_char cmd;
{
    register struct iwm *iwm = (struct iwm *)iwm_addr;
    register u_char junk;

    (void)woz_seltrack(cmd & M_SEL);	/* set SEL */

    junk = (cmd & M_CA2) ? iwm->phase2H : iwm->phase2L;
    junk = (cmd & M_CA1) ? iwm->phase1H : iwm->phase1L;
    junk = (cmd & M_CA0) ? iwm->phase0H : iwm->phase0L;

#ifdef LINT
    junk = junk;
#endif

    return(0);
}

/*----------------------------------*/
int
woz_command(cmd)
register u_char cmd;
{
    register struct via *vp = (struct via *)VIA1_ADDR;
    register struct iwm *iwm = (struct iwm *)iwm_addr;
    register u_char junk;

    (void)woz_addr(cmd);

    /* Phases must be set 0.5us before strobing. */

    DELAY500NS;
    junk = iwm->phase3H;		/* strobe that mother */

    DELAY1US;
    junk = iwm->phase3L;		/* off after 1usec */

#ifdef LINT
    junk = junk;
#endif

    return(0);
}

/*----------------------------------*/
int
woz_disable()
{
    register struct iwm *iwm = (struct iwm *)iwm_addr;
    register u_char junk;

    junk = iwm->Q7L;			/* ensure read mode */
    junk = iwm->Q6L;

    junk = iwm->disable;

#ifdef LINT
    junk = junk;
#endif

    return(0);
}

/*----------------------------------*/
int
woz_enable(drive)
register int drive;
{
    register struct via *vp = (struct via *)VIA1_ADDR;
    register struct iwm *iwm = (struct iwm *)iwm_addr;
    register u_char junk;

    junk = iwm->Q7L;			/* ensure read mode */
    junk = iwm->Q6L;

    junk = drive ? iwm->drive1 : iwm->drive0;	/* select desired drive */

    junk = iwm->enable;

    DELAY1US;				/* wait about 1us for valid status */

#ifdef LINT
    junk = junk;
#endif

    return(0);
}

/*----------------------------------*/
int
woz_init()
{
    register struct iwm *iwm = (struct iwm *)iwm_addr;
    register u_char junk;

    junk = iwm->disable;
    junk = iwm->Q7L;			/* address the mode reg */
    junk = iwm->Q6H;

    iwm->Q7H = IWMMODE;			/* set the mode reg */

    junk = iwm->Q6L;

#ifdef LINT
    junk = junk;
#endif

    return(0);
}

/*----------------------------------*/
int
woz_seltrack(track)
register int track;
{
    register struct iwm *iwm	= (struct iwm *)iwm_addr;
    register struct via *vp	= (struct via *)VIA1_ADDR;
    register u_char junk;

    /* The Mac ROM seems to want the ph0 & ph1 high, but I doubt it...*/

    junk = iwm->phase0H | iwm->phase1H | iwm->phase2L;

    if (track == 0) {
	vp->rega &= ~(1 << SELBIT);
    } else {
	vp->rega |= (1 << SELBIT);
    }
    
    /* Now set the phases up for a data transfer command. */

    junk = iwm->phase0L | iwm->phase1L | iwm->phase2H;

#ifdef LINT
    junk = junk;
#endif

    return(0);
}

/*----------------------------------*/
int
woz_setism()
{
    register struct iwm *iwm = (struct iwm *)iwm_addr;
    register u_char junk;

    /* The magic sequence is to set mode reg select bit to 1011 */

    junk = iwm->Q7L;
    junk = iwm->disable;
    junk = iwm->Q6H;			/* address the mode reg */

    iwm->Q7H = IWMMODE | 0x40;		/* 1 */
    iwm->Q7H = IWMMODE;			/* 0 */
    iwm->Q7H = IWMMODE | 0x40;		/* 1 */
    iwm->Q7H = IWMMODE | 0x40;		/* 1 */

#ifdef LINT
    junk = junk;
#endif

    return(0);
}

/*----------------------------------*/
/*ARGSUSED*/
int
woz_setmfm()
{
    (void)(*fd_chip->command)(C_MOTOROFF); /* drive motor off */
    (void)(*fd_chip->disable)();	/* disable the drive */
    (void)woz_setism();			/* swap chip to ism mode */
    (void)ism_init();			/* init it */
    fd_chip = &fd_c_params[1];
    return(0);
}

/*----------------------------------*/
int
woz_status()
{
    register struct iwm *iwm = (struct iwm *)iwm_addr;
    register u_char junk;
    register short stat = 0;
    register int addr;

    addr = 15;

    junk = iwm->Q7L;			/* set sense mode */
    junk = iwm->Q6H;

    do {				/* from 15 thru zero */
	(void)woz_addr(addr);
	if (iwm->Q7L & 0x80) {
	    stat |= statusbits[addr];
	}
    } while (addr--);

    junk = iwm->Q6L;			/* back to read-ones mode */

#ifdef LINT
    junk = junk;
#endif

    return(stat);
}
