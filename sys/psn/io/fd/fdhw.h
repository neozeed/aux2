/*
 * @(#)fdhw.h  {Apple version 1.1 89/08/15 11:41:14}
 *
 * Copyright (c) 1987, 1988, 1989 by Apple Computer, Inc.
 * All Rights Reserved.
 *
 * THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF APPLE COMPUTER, INC.
 * The copyright notice above does not evidence any actual or
 * intended publication of such source code.
 */

/* static char _sccsid[]="@(#)fdhw.h  {Apple version 1.1 89/08/15 11:41:14}"; */

/* Header file for floppy driver. */
#ifndef ASM				/* only for C files! */
#ifndef u_char
#include <sys/types.h>
#endif
#endif ASM

/* Hardware stuff for the floppy driver. */

/*--------------------------------------*/
/* Common to both C and Assembly code:	*/
/*--------------------------------------*/

#define	SELBIT		5		/* head select bit to hit in the reg */
#define	BUSENABLE	0x02		/* PB1: enable NUBUS DMA */

#define	IWMMODE	0x17		/* 8MHz, timer disable, async, latch */

#define	IWM_DAVAIL	0x80

/* offsets for ISM registers, relative to "fd_swim_addr" */

#define	ISM_WDATA	0x0000	/* write a data byte */
#define	ISM_WMARK	0x0200	/* write a mark byte */
#define	ISM_WCRC	0x0400	/* write 2-byte CRC */
#define	ISM_WPARAMS	0x0600	/* write the param regs */
#define	ISM_WPHASE	0x0800	/* write the phase states & dirs */
#define	ISM_WSETUP	0x0a00	/* write the setup register */
#define	ISM_WZEROES	0x0c00	/* write zero bits to mode reg */
#define	ISM_WONES	0x0e00	/* write one  bits to mode reg */
#define	ISM_RDATA	0x1000	/* read a data byte */
#define	ISM_RCORRECTION	ISM_RDATA	/* read error correction reg */
#define	ISM_RMARK	0x1200	/* read a mark byte */
#define	ISM_RERROR	0x1400	/* read the error reg */
#define	ISM_RPARAMS	0x1600	/* read the param regs */
#define	ISM_RPHASE	0x1800	/* read the phase states & dirs */
#define	ISM_RSETUP	0x1a00	/* read the setup reg */
#define	ISM_RMODE	0x1c00	/* read the mode reg */
#define	ISM_RHANDSHAKE	0x1e00	/* read the handshake reg */

/* Swim control stuff: */

#define	ISM_ENABLE	0x80		/* mode reg */
#define	ISM_SETWOZ	0x40
#define	ISM_HDSEL	0x20
#define	ISM_WRITE	0x10
#define	ISM_ACTION	0x08
#define	ISM_DRIVE1	0x04
#define	ISM_DRIVE0	0x02
#define	ISM_CLRFIFO	0x01

#define	ISM_PHASE0	0x01		/* phase register */
#define	ISM_PHASE1	0x02
#define	ISM_PHASE2	0x04
#define	ISM_PHASE3	0x08

#define	ISM_DAVAIL	0x80		/* handshake reg */
#define	ISM_SENSE	0x08
#define	ISM_ERROR	0x20
#define	ISM_ISMARK	0x01
#define	ISM_CRCERR	0x02

#define	ISM_UNDERRUN	0x01		/* error reg */
#define	ISM_OVERRUN	0x04

/*---------------------------------------*/
/*	 	C code only:		 */
/*---------------------------------------*/
#ifndef ASM

struct iwm {
    u_char phase0L;		/* 0000 set phase0 low */
    u_char junk01[0x1ff];
    u_char phase0H;		/* 0200 set phase0 high */
    u_char junk02[0x1ff];
    u_char phase1L;		/* 0400 set phase1 low */
    u_char junk03[0x1ff];
    u_char phase1H;		/* 0600 set phase1 low */
    u_char junk04[0x1ff];
    u_char phase2L;		/* 0800 set phase2 low */
    u_char junk05[0x1ff];
    u_char phase2H;		/* 0a00 set phase2 high */
    u_char junk06[0x1ff];
    u_char phase3L;		/* 0c00 set phase3 low */
    u_char junk07[0x1ff];
    u_char phase3H;		/* 0e00 set phase3 high */
    u_char junk08[0x1ff];
    u_char disable;		/* 1000 disable drive */
    u_char junk09[0x1ff];
    u_char enable;		/* 1200 enable drive */
    u_char junk10[0x1ff];
    u_char drive0;		/* 1400 select drive 0 */
    u_char junk11[0x1ff];
    u_char drive1;		/* 1600 select drive 1 */
    u_char junk13[0x1ff];
    u_char Q6L;			/* 1800 Q6L */
    u_char junk12[0x1ff];
    u_char Q6H;			/* 1a00 Q6H */
    u_char junk14[0x1ff];
    u_char Q7L;			/* 1c00 Q7L */
    u_char junk15[0x1ff];
    u_char Q7H;			/* 1e00 Q7H */
};

struct swim {
    u_char wdata;		/* 0000 write a data byte */
    u_char junk01[0x1ff];
    u_char wmark;		/* 0200 write a mark byte */
    u_char junk02[0x1ff];
    u_char wcrc;		/* 0400 write 2-byte crc to disk */
    u_char junk03[0x1ff];
    u_char wparams;		/* 0600 write the param regs */
    u_char junk04[0x1ff];
    u_char wphase;		/* 0800 write the phase states & dirs */
    u_char junk05[0x1ff];
    u_char wsetup;		/* 0a00 write the setup register */
    u_char junk06[0x1ff];
    u_char wzeroes;		/* 0c00 mode reg: 1's clr bits, 0's are x */
    u_char junk07[0x1ff];
    u_char wones;		/* 0e00 mode reg: 1's set bits, 0's are x */
    u_char junk08[0x1ff];
    u_char rdata;		/* 1000 read a data byte */
    u_char junk09[0x1ff];
    u_char rmark;		/* 1200 read a mark byte */
    u_char junk10[0x1ff];
    u_char rerror;		/* 1400 read the error register */
    u_char junk11[0x1ff];
    u_char rparams;		/* 1600 read the param regs */
    u_char junk12[0x1ff];
    u_char rphase;		/* 1800 read the phase states & dirs */
    u_char junk13[0x1ff];
    u_char rsetup;		/* 1a00 read the setup register */
    u_char junk14[0x1ff];
    u_char rmode;		/* 1c00 read the mode register */
    u_char junk15[0x1ff];
    u_char rhandshake;		/* 1e00 read the handshake register */
};
#define rcorrection rdata		/* read error correction register */
/* This delay is CPU-independent, since the CPU must slowdown to sync
 * with the 16MHz I/O clock used by the VIA.  Each access takes 63.82nsec
 * (except the first, which could go off immediately, but we don't really
 * worry about that).
 *
 * We read the via's A and B registers to minimize the size of the code
 * used by the delay, since there are many of them.  Beware the optimizer!
 *
 * There is a 4-FCLK (256ns) speed limit on accessing the ISM chip.
 */

#define DELAY256NS {						\
		      register u_char junk;			\
		      junk = vp->regb;		/* slowdown */	\
		      junk = vp->rega;		/* slowdown */	\
		      junk = vp->regb;		/* slowdown */	\
		      junk = vp->rega;		/* slowdown */	\
		  }

#endif ASM

#ifdef ASM
/* Assembly code only: */

/* offsets for IWM control stuff, relative to "fd_iwm_addr" */

#define	Q6L	0x1800
#define	Q6H	0x1a00
#define	Q7L	0x1c00
#define	Q7H	0x1e00

#endif ASM
