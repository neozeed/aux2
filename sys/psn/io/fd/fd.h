/*
 * @(#)fd.h  {Apple version 2.4 90/02/19 14:27:39}
 *
 * Copyright (c) 1987, 1988, 1989 by Apple Computer, Inc.
 * All Rights Reserved.
 *
 * THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF APPLE COMPUTER, INC.
 * The copyright notice above does not evidence any actual or
 * intended publication of such source code.
 */

#if !defined(lint) && !defined(NO_SCCS_IDS)
/* static char _sccsid[]="@(#)fd.h  {Apple version 2.4 90/02/19 14:27:39}"; */
#endif

/* Header file for floppy driver. */

#ifndef ASM				/* only for C files! */
#ifndef __sys_types_h
#include <sys/types.h>
#endif
#ifndef __sys_errno_h
#include <sys/errno.h>
#endif
#ifndef __sys_iopmgr_h
#include <sys/iopmgr.h>
#endif
#endif ASM

/*--------------------------------------*/
/* Common to both C and Assembly code:	*/
/*--------------------------------------*/

/* Minor Dev bits. Options valid only for raw devices! */
#define	FD_CLSEJECT	0x40		/* eject on close */
#define	FD_WAIT		0x20		/* wait for insert on open */
#define	FD_DRIVE	0x10		/* drive number bit */
#define	FD_TUNE		0x08		/* only "special" tune/status ioctls */

#define	FD_FORMAT	0x07		/* format bits *

/* These format type numbers should not be changed. */
#define	FMT_GCR400	0x01		/* GCR  400K */
#define	FMT_GCR800	0x02		/* GCR  800K */
#define	FMT_MFM720	0x03		/* MFM  720K */
#define	FMT_MFM1440	0x04		/* MFM 1440K */
#define	FMT_MAXLEGAL	0x07		/* max legal format number */

#define	FMT_UNFORMATTED	0x10		/* unformatted */
#define	FMT_ILLEGAL	0x11		/* illegal format this media */

#define SPLFD spl6

/* Error codes: */
#define EDMARKS	 	1		/* bad data marks */
#define EDSUM	 	2		/* bad data checksum */
#define EDSLIP	 	3		/* bad data bitslip marks */
#define	EOVERRUN	4		/* overrun on read */
#define EASLIP	 	5		/* bad address bitslip marks */
#define EWTIMEOUT	6		/* write timeout */
#define	EAMTIMEOUT	7		/* no addr marks: timeout */
#define	EPAMARKS 	8		/* only partial address marks found */
#define EASUM	 	9		/* bad address checksum */
#define	EWUNDERRUN	10		/* write underrun */
#define	EUNDERRUN	11		/* CPU not reading fast enough */
#define	EOTHER		12		/* other error */

/* Drive status bits. Properly flipped for POSITIVE logic. */

#define	 S_OUTWARD	0x0001		/* seek direction toward track 0 */
#define	  S_STEPOK	0x0002		/* ok to issue Step command */
#define	S_MOTOROFF	0x0004		/* motor is off */
#define	S_EJECTING	0x0008		/* disk eject in progress */
#define	 S_RDDATA0	0x0010		/* read data from head 0 */
#define	S_FDHD		0x0020		/* drive is an FDHD */
#define	S_TWOSIDED	0x0040		/* drive is doublesided */
#define	 S_NODRIVE	0x0080		/* no drive present */
#define	  S_NODISK	0x0100		/* no disk inserted */
#define	 S_WRTENAB	0x0200		/* write enabled disk inserted */
#define	S_NCYLZERO	0x0400		/* not on cyl zero (recal sensor) */
#define	    S_TACH	0x0800		/* tach pulse in GCR mode */
#define	     S_MFM	0x1000		/* drive is in MFM mode */
#define	 S_RDDATA1	0x2000		/* read data from head 1 */
#define	S_NOTREADY	0x4000		/* drive is NOT ready */
#define	S_1MBMEDIA	0x8000		/* 1MB media inserted */
#define	   S_INDEX	S_TACH		/* index pulse in MFM mode */
#define	  S_NOTPWM	S_1MBMEDIA	/* drive doesn't need PWM signal */

/* Here we assume 1us per loop, probably the minimum useful loop. */
#define	MAXLOOPS	50000		/* max for any spinloop */

/*---------------------------------------*/
/*	 	C code only:		 */
/*---------------------------------------*/
#ifndef ASM

/* Drive commands. */

#define	M_CA0		(1 << 0)	/* bit masks */
#define	M_CA1		(1 << 1)
#define	M_CA2		(1 << 2)
#define	M_SEL		(1 << 3)

#define	C_STEPIN	(0x00				)
#define	C_STEPOUT	(	 M_CA2			)
#define	C_STEP		(			 M_CA0	)
#define	C_MOTORON	(		 M_CA1		)
#define	C_MOTOROFF	(	 M_CA2 + M_CA1		)
#define	C_EJECT		(	 M_CA2 + M_CA1 + M_CA0	)
#define	C_SETMFM	(M_SEL		       + M_CA0	)
#define	C_SETGCR	(M_SEL + M_CA2	       + M_CA0	)
#define	C_INDEX		(		 M_CA1 + M_CA0  )

struct drivestatus {			/* Status of drives */
    struct driveparams	*dp;		/* drive params table */
    struct iopreq	req;		/* IOP manager request packet */
    u_char		msg[32];	/* IOP SWIM message buffer */
    long		timeout;
    short		timertype;
    short		curcyl;		/* current cylinder */
    dev_t		dev;		/* major and current minor dev */
    short		density;	/* density bits if "hasdisk" true */
    int			hasdisk:1;	/* disk in place */
    int			open:1;		/* open */
    int			opening:1;	/* open in-progress */
    int			exclusive:1;	/* opened O_EXCL */
    int			twosided:1;	/* doublesided */
    int			fdhd:1;		/* handles 2mb media */
    int			installed:1;	/* drive installed */
    int			harderr:1;	/* hard error, dead till closed */
    int			event:1;	/* disk-insert event was generated */
    int			wrtenab:1;	/* diskette is write-enabled */
};

typedef int (*pfi)();			/* pointer to func returning int */

struct interface {

    /* Vectors to low-level routines which are different for
     * IOP vs. motherboard IWM/SWIM chip hardware: */

    pfi beginopen;			/* setup drive at begin of open */
    pfi disable;			/* disable */
    pfi eject;				/* eject */
    pfi enable;				/* enable */
    pfi	format;				/* format */
    pfi	getformat;			/* determine format on open */
    pfi init;				/* init */
    pfi loadfloppy;			/* handle newly inserted floppy */
    pfi pollinsert;			/* poll for disk-insertion event */
    pfi rw;				/* do a Read or Write operation */
    pfi seldrive;			/* select and enable a drive */
    pfi setdensity;			/* set density params during open */
    pfi status;				/* status, returned as S_xxx bits */
};

struct driveparams {			/* Info about drives */

    /* Vectors to low-level routines which are different by recording format: */

    pfi erasetrack;			/* erase a track */
    pfi fmttrack;			/* format a track */
    pfi geometry;			/* return #tracks & sectors/track */
    pfi motoron;			/* setup and start motor */
    pfi optseek;			/* suggest optimum seek to A or B */
    pfi rdadr;				/* read address */
    pfi read;				/* read */
    pfi sectortime;			/* return # ms before next sector */
    pfi setmode;			/* set required GCR/MFM mode */
    pfi setparams;			/* set read/write I/O parameters */
    pfi write;				/* write */
    pfi wrt1to1;			/* true if 1:1 write, else false */
};

struct chipparams {

    /* Vectors to low-level routines which are different by chip mode: */

    pfi command;			/* issue a command */
    pfi disable;			/* disable the drive */
    pfi enable;				/* enable the drive */
    pfi init;				/* initialize */
    pfi seltrack;			/* select track */
    pfi setgcr;				/* set GCR mode */
    pfi setmfm;				/* set MFM mode */
    pfi status;				/* get status */
};

struct addr {				/* returned by read-address */
    short head;				/* head, 0 or 1 */
    short cyl;				/* cylinder */
    short sector;			/* sector */
    short interleave;			/* GCR: from format byte */
    short sides;			/* GCR: 1 or 2, from format byte */
    short blksize;			/* MFM: blksize byte */
    long gap;				/* bytes seen before address mark */
};

struct geom {				/* geometry */
    /* Fixed values for this density: */
    short blks;				/* number of blocks */
    short maxtrk;			/* max track */
    short gapsize;			/* intersector gap for formatters */

    /* Calculated based on block number input: */
    short cyl;				/* cyl 0-79 */
    short track;			/* 0 or 1 */
    short sector;			/* 0-11 for GCR, 0-17 for MFM */
    short spt;				/* number of sectors this track */
};

struct seekinfo {
    short start;			/* start cyl of request */
    short end;				/* end cyl + 1 of request */
    short dir;				/* direction, +-1 */
};

struct todo {				/* current request */
    int wanted:1;			/* 1 if sector wanted */
    short errcnt;			/* error count */
    short seen;				/* number times seen during rotation */
    caddr_t buf;			/* data buffer */
};

#define MAXDRIVES	2		/* max drives */
#define MAXDTYPES	4	/* 400K GCR, 800K GCR, 720K MFM, 1440K MFM */

/* Timeout stuff.  We expect fd_timer to run at 250ms resolution.  */

#define	T_250MS		1		/* 250ms is a single tick */
#define	T_500MS		2		/* 500ms is a two tick */
#define	SECONDS		4		/* fd_timer ticks in a second */
#define	MINUTES		(60 * SECONDS)	/* fd_timer ticks in a minute */

#define	T_WAKEUP	1		/* have fd_timer wake me up */
#define	T_MOTOROFF	2		/* turn motor off */

#define	ABS(a) ((a) < 0 ? -(a) : (a))
#define TRACE(level,msg) {					\
			    extern int fd_c_verbose;		\
			    extern int fd_minor; 		\
			    if (fd_c_verbose >= (level)) {	\
				printf("fd d%d: ",fd_minor);	\
				printf msg;			\
			    }					\
			}

/* Logic to get and free the IWM/SWIM chip: */

/* Commands issued in fd_cmdbuf. */
#define	b_cmd		b_resid		/* command goes here */
#define	b_params	b_un.b_addr	/* params passed in here */
#define	CMD_OPEN	1
#define	CMD_EJECT	2
#define	CMD_FORMAT	3
#define	CMD_FMTONLY	4
#define	CMD_VFYONLY	5
#define	CMD_GETSTAT	6

#define GETBUF(bp)	{						\
			    int s = SPLFD();				\
			    while (bp->b_flags & B_BUSY) {		\
				bp->b_flags |= B_WANTED;		\
				(void)sleep((caddr_t)bp,PRIBIO + 1);	\
			    }						\
			    bp->b_flags = B_BUSY;			\
			    splx(s);					\
			}
#define FREEBUF(bp)	{						\
			    int s = SPLFD();				\
			    if (bp->b_flags & B_WANTED) {		\
				wakeup((caddr_t)bp);			\
			    }						\
			    bp->b_flags = 0;				\
			    splx(s);					\
			}

/*---------------------------------------*/
/* 	Assembly code only:		 */
/*---------------------------------------*/

#else CC

/* Register save/restore masks for movem: */

#define	S_D2	0x2000
#define	S_D3	0x1000
#define	S_D4	0x0800
#define	S_D5	0x0400
#define	S_D6	0x0200
#define	S_D7	0x0100
#define	S_ALLD	0x3f00
#define	S_A2	0x0020
#define	S_A3	0x0010
#define	S_A4	0x0008
#define	S_A5	0x0004
#define	S_A6	0x0002
#define	S_ALLA	0x003e
#define	S_ALL	(S_ALLD + S_ALLA)

#define	R_D2	0x0004
#define	R_D3	0x0008
#define	R_D4	0x0010
#define	R_D5	0x0020
#define	R_D6	0x0040
#define	R_D7	0x0080
#define	R_ALLD	0x00fc
#define	R_A2	0x0400
#define	R_A3	0x0800
#define	R_A4	0x1000
#define	R_A5	0x2000
#define	R_A6	0x4000
#define	R_ALLA	0x7c00
#define	R_ALL	(R_ALLD + R_ALLA)

#endif ASM
