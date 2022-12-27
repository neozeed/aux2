#ifndef lint	/* .../sys/psn/io/via.c */
#define _AC_NAME via_c
#define _AC_NO_MAIN "@(#) Copyright (c) 1987 Apple Computer Inc., All Rights Reserved.  {Apple version 1.7 90/03/13 11:52:40}"
#include <apple_notice.h>

#ifdef _AC_HISTORY
  static char *sccsid = "@(#)Copyright Apple Computer 1989\tVersion 1.7 of via.c on 90/03/13 11:52:40";
#endif		/* _AC_HISTORY */
#endif		/* lint */

#define _AC_MODS
/*	@(#)via.c	UniPlus VVV.2.1.13	*/
/*
 * VIA device driver
 *
 *	Copyright 1986 UniSoft Corporation
 */

#ifdef lint
#include "sys/sysinclude.h"
#else lint
#include "sys/types.h"
#include "sys/uconfig.h"
#include "sys/reg.h"
#endif lint
#include "sys/via6522.h"

extern  int  rbv_exists;
extern  int  rbv_monitor;
extern	struct via *via2_addr;

extern	noint();
extern	noviaint();
extern	onesec(), clock();
extern  fdb_intr(), scsiirq(), slotintr();

int	(*lvl1funcs[7])() = {
	onesec,			/* CA 2, one per sec */
	clock,			/* CA 1, 60 HZ */
	fdb_intr,		/* Shift Register */
	noviaint,		/* CB 2 */	
	noviaint,		/* CB 1 Serial from TOD clock shift clock */
#define VIA1_TIMER2		5
	noviaint,		/* Timer 2 */
	noviaint,		/* Timer 1 */
	};

int	(*lvl2funcs[7])() = {
	noviaint,		/* CA 2, SCSI */
	slotintr,		/* CA 1, Slots */
	noviaint,		/* Shift Register */
	scsiirq,		/* CB 2 */
	noviaint,		/* CB 1 */
	noviaint,
	noviaint,
	};

#define NUM_SLOTFUNCS	7
int	(*slotfuncs[NUM_SLOTFUNCS])() = {
	noint,
	noint,
	noint,
	noint,
	noint,
	noint,
	noint,
        };

char via1_soft = 0;
char via2_soft = 0;
static	struct via *iusvia;
static	u_char iusmsk;
static	char *iussoft;

/*	viaclrius -- clear interrupt under service.
 *	     This routine clears the current interrupt.
 */
viaclrius()
{
	if(iusvia == 0)
		panic("viaclrius called from non-interrupt");

	iusvia->ifr = iusmsk | VIE_SET;		/* RBV wants VIE_SET bit */
	*iussoft &= ~iusmsk;
}

/*
 *	via1init -- perform VIA 1 initialization.
 */
via1init()
{
	extern short machineID;
	register struct via *vp = (struct via *) VIA1_ADDR;
	
	vp->pcr = 0x20;
	vp->ier = 0x7f | VIE_CLEAR;	/* disable all interrupts */

	vp->ier = VIE_CA2 | VIE_SET;

	/*
	 * Enable 60Hz interrupt in via1 for non Mac IIfx machines.  
	 */
	if (machineID != MACIIfx)
	    vp->ier = VIE_CA1 | VIE_SET;
}

#define	TIMECONST ((783360/60)/2)
/*
 *	via2init -- perform VIA 2/RBV initialization.
 */
via2init()
{
	extern short machineID;

	if (machineID == MACIIfx)
	    return;

	via2_addr->regb |= 0x02;		/* clear NuBus lock */
	
	if (rbv_exists) {
	    via2_addr->ier = 0x7f | VIE_CLEAR;	/* disable all interrupts */
	    via2_addr->rbv_slotier = 0x7f | VIE_CLEAR;
	    via2_addr->rbv_slotier = 0x3f | VIE_SET;
	}
	else {
	    via2_addr->pcr = 0x66;
	    via2_addr->ddrb |= 0x02;

	    /*
	     * The vias are clocked off the "E clock frequency", which
	     * is 783.36 KHz. We arrange for via2 to send out a 30 HZ
	     * square wave.  This will cause an interrupt to be generated
	     * by via1 on each edge of the square wave.
	     */
	    via2_addr->acr  = VAC_T1CONT | VAC_T1PB7;
	    via2_addr->t1cl = TIMECONST;
	    via2_addr->t1ch = TIMECONST >> 8;
	}
	via2_addr->ier = VIE_CA1 | VIE_SET;	/* enable all slot interrupts */
}

via1intr (args)
    struct args *args;
{
	register ifr, msk;
	register (**fp)();
	register struct via *via1 = (struct via *)VIA1_ADDR;
	struct via *saviusvia;
	int	saviusmsk;
	char *saviussoft;

	saviusvia = iusvia;
	saviusmsk = iusmsk;
	saviussoft = iussoft;
	iusvia = via1;
	iussoft = &via1_soft;
	do {
		ifr = (via1_soft|via1->ifr) & via1->ier & 0x7F;
		msk = 1;
		fp = lvl1funcs;
		do {
			if(msk & ifr) {
				iusmsk = msk;
				(*fp)(args);
				ifr = (via1_soft|via1->ifr) & via1->ier & 0x7F;
			}
			msk <<= 1;
			++fp;
		} while(msk <= ifr);
	} while(via1->ifr & 0x80);

	iusvia = saviusvia;;
	iusmsk = saviusmsk;
	iussoft = saviussoft;
}

via2intr(args)
    struct args *args;
{
	int saviusmsk;
	char *saviussoft;
	register ifr, msk;
	register (**fp)();
	register struct via *vp = via2_addr;
	struct via *saviusvia;

	saviusvia = iusvia;
	saviusmsk = iusmsk;
	saviussoft = iussoft;
	iussoft = &via2_soft;

	iusvia = vp;

	do {
	    ifr = (via2_soft|vp->ifr) & vp->ier & 0x7F;
	    msk = 1;
	    fp = lvl2funcs;
	    do {
		if(msk & ifr) {
		    iusmsk = msk;
		    (*fp)(args);
		    ifr = (via2_soft|vp->ifr) & vp->ier & 0x7F;
		}
		msk <<= 1;
		++fp;
	    } while(msk <= ifr);
	} while (vp->ifr & 0x80);

	iusvia = saviusvia;
	iusmsk = saviusmsk;
	iussoft = saviussoft;
}
	
/*	viamkslotintr -- make slot interrupt handler.
 *	     A slot interrupt handler is installed for a given slot.  This
 *	routine must be called by the device specific initialization code.
 *	If intr is non-zero slot interrupts are enabled.
 */
viamkslotintr (num, fp, intr)
    int	num;					/* nu bus slot number */
    int	(*fp)();
{
    register s;
    int rbvtimer();

    s = splhi();

    if (num == 0)
	slotfuncs[NUM_SLOTFUNCS - 1] = fp;	/* slot 0 ==> last slot */
    else
	slotfuncs[num - SLOT_LO] = fp;		/* SLOT_LO is slot bias */

    if (intr) {
	if (rbv_monitor && num == 0) {		/* enable rbv slot 0 int */
	    lvl1funcs[VIA1_TIMER2] = rbvtimer;
	    via2_addr->rbv_slotier = 0x40 | VIE_SET;
	}
    }
    splx(s);
}
	

/*
 * Slot interrupt handler/dispatcher.
 */
slotintr(args)
    register struct args *args;
{
    register struct via *vp = via2_addr;
    register (**fp)();
    register int num;
    register int msk;
    register int rega;
    
    /* We get the slot IRQ bits once, dispatch them all, then make sure
     * they haven't been updated while before exiting the outer loop.
     *
     * On the RBV, we have to explicitly clear the "anyslot" IRQ flag; on
     * a real VIA, it clears automatically when we read Register A, but
     * it doesn't hurt on a VIA because the outer loop will check it again.
     */

    for (;;) {

	vp->ifr = VIE_CA1 | VIE_SET;		/* explicit clear for RBV */

	rega = ~vp->rega & 0x7f;		/* get bits in positive logic */

	/* On a VIA or RBV-without-internal-video, the Slot Zero IRQ
	 * is not applicable.
	 */
	if (!rbv_monitor) {
	    rega &= ~RBV_SZEROIRQ;		/* slot 0 is N/A */
	}

	if (rega == 0) {			/* no more to do */
	    break;
	}

	msk = 1;
	fp = slotfuncs;
	num = SLOT_LO;

	/* Dispatch the slot interrupt routines. */
	while (rega) {
	    if (msk & rega) {
		args->a_dev = (num == 15) ? 0 : num;
		(*fp)(args);
		rega &= ~msk;			/* done, drop the bit */
	    }
	    msk <<= 1;
	    ++num;
	    ++fp;
	}
    }
}

static int
rbvtimer(args)
    struct args *args;
{
    ((struct via *)VIA1_ADDR)->ier = VIE_CLEAR | VIE_TIM2;
    via2_addr->rbv_slotier = 0x40 | VIE_SET;
}


/*	noint -- default handler.
 */
noviaint()
{
    viaclrius();
#ifndef	ANYKINDOFDEBUG
    printf("no via interrupt handler:via @ %x,ifr mask=%x\n",iusvia,iusmsk);
#endif	ANYKINDOFDEBUG
}

noint(args)
    struct args *args;
{
    viaclrius();
#ifndef	ANYKINDOFDEBUG
    printf("no slot %d interrupt handler\n",args->a_dev);
#endif	ANYKINDOFDEBUG
}

