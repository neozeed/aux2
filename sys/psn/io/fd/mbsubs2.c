/*
 * @(#)mbsubs2.c  {Apple version 1.1 89/12/12 14:12:56}
 *
 * Copyright (c) 1987, 1988, 1989 by Apple Computer, Inc.
 * All Rights Reserved.
 *
 * THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF APPLE COMPUTER, INC.
 * The copyright notice above does not evidence any actual or
 * intended publication of such source code.
 */

#if !defined(lint) && !defined(NO_SCCS_IDS)
static char _sccsid[]="@(#)mbsubs2.c  {Apple version 1.1 89/12/12 14:12:56}";
#endif

#include "fd.h"
#include "fdhw.h"
#include <sys/var.h>
#include <sys/uconfig.h>
#include <sys/via6522.h>
#include <sys/sysmacros.h>
#include <sys/buf.h>

    extern struct drivestatus fd_status[];
    extern struct drivestatus *fd_drive;
    extern struct chipparams *fd_chip;
    extern struct chipparams fd_c_params[];
    extern struct driveparams fd_d_params[];
    extern int fd_minor;		/* current drive minor number */
    extern int fd_motordelay;
    extern struct buf *fd_buf;

/*--------------------------------*/
int
fd_mb_loadfloppy(ds)			/* MOTHERBOARD */
register struct drivestatus *ds;
{
    register int s;

    /* If this disk is newly inserted, we must recalibrate in GCR mode
     * to get it properly chucked, then determine its density.
     */

    if (fd_mb_recal() < 0) {
	TRACE(3,("fd_mb_open: recal failed\n"));

	s = SPLFD();
	ds->timeout = fd_motordelay;
	ds->timertype = T_MOTOROFF;	/* schedule the motor turnoff */
	splx(s);

	return(EIO);			/* couldn't recal */
    }

    return(0);				/* OK */
}

/*----------------------------------*/
void
fd_mb_motoroff()
{
    /* If we're busy, then forget the motoroff timer, since we're
     * either going to the current drive or have already shut it
     * off to go to the other drive.
     */

    if (fd_buf == 0) {
	TRACE(8,("motoroff\n"));
	(void)(*fd_chip->command)(C_MOTOROFF);	/* drive motor off */
	(void)(*fd_chip->disable)();		/* disable the drive */
    }
}

/*----------------------------------*/
int
fd_mb_pollinsert()			/* MOTHERBOARD */
{
    extern int (*postDIroutine)();
    register struct drivestatus *ds;
    register int i;
    register int stat;

    /* If we're busy, then forget polling, since we're
     * either going to the current drive or would kill a drive
     * by enabling the other drive.  We also don't check for
     * diskinsert if the drive is open or being opened.
     */

    if (fd_buf != 0) {
	return(0);
    }

    for (i = 0; i < MAXDRIVES; i++) {
	ds = &fd_status[i];
	if (!ds->installed || ds->hasdisk || 
	    ds->event || ds->open || ds->opening) {
	    continue;
	}
	(void)(*fd_chip->enable)(i);	/* enable the drive */
	stat = (*fd_chip->status)();	/* get its current status */
	(void)(*fd_chip->disable)();	/* disable the drive */
	if ((stat & S_NODISK) == 0) {	/* now has a floppy */
	    (void)(*postDIroutine)(makedev(major(ds->dev),i << 4),stat);
	    ds->event = 1;		/* he accepted the event */
	}
    }

    return(0);
}

/*----------------------------------*/
/* Returned values from recal and seek:
 *	-4 unusual status from drive
 *	-3 NOT USED
 *	-2 too many loops
 *	-1 bad cyl specification
 *	+n OK, n is expected number of clock ticks for completion
 */
/*----------------------------------*/
int fd_mb_recal()			/* MOTHERBOARD */
{
    void delay_100us();

    register int stat;
    register int loops;

    /* In case last op was a write, delay 700uSec for erase-turnoff. */
    delay_100us(8);			/* 800uSec plus call overhead */

/* We should probably issue a chunk of steps, then sleep for a couple of
 * ticks, then check for CYLZERO, and keep looping around till it gets there.
 * The present implementation sends hundreds of steps, but we think the drive
 * simply suppresses the extras when it hits CYLZERO.
 */

    (void)(*fd_chip->command)(C_STEPOUT);	/* step outward */

    loops = 0;
    do {
	stat = (*fd_chip->status)();
	if (stat & (S_MOTOROFF | S_NODRIVE | S_NODISK)) {
	    TRACE(3,("fd_mb_recal: unusual status %x\n",stat));
	    return(-4);
	}

	/* If the drive is ready to take a step, do it.  Otherwise
	 * we just go back around in the loop until it's ready.
	 */

	if (stat & S_STEPOK) {
	    (void)(*fd_chip->command)(C_STEP);
	}
	if (++loops > MAXLOOPS) {
	    return(-2);
	}
    } while ((*fd_chip->status)() & S_NCYLZERO);

    (void)(*fd_drive->dp->setparams)(0);	/* set I/O params */

    fd_drive->curcyl = 0;

    /* We must delay 150uSec to ensure that RDY goes away. */
    delay_100us(2);				/* 200 is fine */

    return(0);
}

/*----------------------------------*/
/* We return before the seek is done (i.e. not ready) */
int fd_mb_seek(newcyl)			/* MOTHERBOARD: >=0 good, else status */
register short newcyl;
{
    void delay_100us();

    int seektime;
    register struct via *vp = (struct via *)VIA1_ADDR;
    register int count;
    register int stat;
    register int loops;
    register int dir;
    register int i;

    if (newcyl < 0 || newcyl > 79) {		/* invalid request */
	return(-1);
    }

    if (fd_drive->curcyl < 0 || fd_drive->curcyl > 79) {
	TRACE(0,("curcyl=%d,newcyl=%d\n",fd_drive->curcyl,newcyl));
	pre_panic();
	panic("fd_mb_seek:curcyl wrong on entry");
    }

    count = (newcyl - fd_drive->curcyl);
    if (count == 0) {				/* already there! */
	(void)(*fd_drive->dp->setparams)(newcyl);	/* set I/O params */
	return(0);
    }

    /* In case last op was a write, delay 700uSec for erase-turnoff. */
    delay_100us(8);			/* 800uSec plus call overhead */

    if (count < 0) {
	count = -count;
	(void)(*fd_chip->command)(C_STEPOUT);	/* step outward */
	dir = -1;
    } else {
	(void)(*fd_chip->command)(C_STEPIN);	/* step inward */
	dir = 1;
    }

    /* The time calculation ignores speedgroup crossing, which takes longer. */

    seektime = (count * 6) + 30;		/* mSec it should take */
    seektime = ((seektime * 1000) / v.v_clktick) + 1;

    loops = 0;
    while (count > 0) {
	stat = (*fd_chip->status)();
	if (stat & (S_MOTOROFF | S_NODRIVE | S_NODISK)) {
	    return(-4);
	}

	/* If the drive is ready to take a step, do it.  Otherwise we
	 * just go back around in the while-loop until it's ready.
	 *
	 * NOTE: Gary Davidian has observed an undocumented delay between
	 * S_STEPOK status and the driver REALLY being ready. He
	 * feels 18uSec might be about right.  Symptom is occasionally
	 * ending up 1 cylinder short on a seek, necessitating a reseek.
	 */

	if (stat & S_STEPOK) {
	    for (i = 0; i < 18 * 4; i++) {	/* gives 18uSec */
		DELAY256NS;
	    }
	    count--;
	    fd_drive->curcyl += dir;
	    if (fd_drive->curcyl < 0 || fd_drive->curcyl > 79) {
		TRACE(0,("curcyl=%d,newcyl=%d\n",fd_drive->curcyl,newcyl));
		TRACE(0,("count=%d,dir=%d\n",count,dir));
		pre_panic();
		panic("fd_mb_seek:curcyl wrong during seek");
	    }
	    (void)(*fd_chip->command)(C_STEP);
	}
	if (++loops > MAXLOOPS) {
	    return(-2);
	}
    }

    (void)(*fd_drive->dp->setparams)(newcyl);	/* set I/O params */

    /* We must delay 150uSec to ensure that RDY goes away. */
    delay_100us(2);				/* 200 is fine */

    return(seektime);
}

/*----------------------------------*/
/* Select a drive and enable it. */
int					/* status is returned */
fd_mb_seldrive(minordev)		/* MOTHERBOARD */
register int minordev;
{
    register struct drivestatus *ds;
    register struct drivestatus *nds;	/* new drive */
    register struct driveparams *ndp;
    register int s;

    if ((minordev < 0) || (minordev > 1)) {
	panic("fd_mb_seldrive: illegal minor dev");
    }

    if (minordev != fd_minor) {		/* must switch drives */

	ds = fd_drive;			/* current drive pointers */
	nds = &fd_status[minordev];	/* new drive pointers */
	ndp = nds->dp;

	s = SPLFD();
	ds->timertype  = ds->timeout  = 0; /* blow away motoroff timers */
	nds->timertype = nds->timeout = 0;
	splx(s);

	/* Get chip and drive into proper mode. */

	(void)(*ndp->setmode)(minordev); /* ensure proper GCR/MFM mode */

	/* Switch pointers to the new drive. */

	fd_minor = minordev;
	fd_drive = nds;
    }

    (void)(*fd_chip->enable)(minordev); /* select & enable the drive */

    return((*fd_chip->status)());
}

/*--------------------------------*/
int
fd_mb_setdensity(ds)			/* MOTHERBOARD */
register struct drivestatus *ds;
{
    register int stat;
    register int s;

    /* If the disk is unformatted, we pretend it's either GCR800K or
     * MFM1440K, depending on drive & media, to setup our tables correctly.
     * The motor turnoff is not needed since we disable when mode-setting.
     */

    if (ds->density == FMT_UNFORMATTED) {

	stat = (*fd_chip->status)();		/* get the status */

	if (stat & S_FDHD && !(stat & S_1MBMEDIA)) {
	    ds->dp = &fd_d_params[FMT_MFM1440 - 1];	/* set 1440K MFM */
	    (void)fd_setmfmmode();
	} else {
	    ds->dp = &fd_d_params[FMT_GCR800  - 1];	/* set 800K GCR */
	    (void)fd_setgcrmode();
	}

    } else {
	ds->dp = &fd_d_params[ds->density - 1];	/* set format params */

	s = SPLFD();
	ds->timeout = fd_motordelay;
	ds->timertype = T_MOTOROFF;		/* schedule the motor turnoff */
	splx(s);

    }

    return(0);
}

/*--------------------------------*/
int
fd_mb_status()
{
    return((*fd_chip->status)());
}

static char fd_verbuf[512];
/*----------------------------------*/
int
fd_mb_verify(ds,s,firstcyl,lastcyl)	/* MOTHERBOARD */
register struct drivestatus *ds;
register int s;
int firstcyl;
int lastcyl;
{
    void fd_cleartodo();

    extern struct todo fd_todo[];
    extern int fd_ntodo;

    static struct geom geometry;

    register struct geom *gp		= &geometry;
    register struct driveparams *dp	= ds->dp;
    register struct todo *tp;
    register int rc;
    register int cyl;
    register int track;
    register int blk;

    splx(s);

    (void)(*dp->geometry)(gp,0);	/* get blks and largest spt */

    for (tp = &fd_todo[0]; tp < &fd_todo[gp->spt]; tp++) {
	tp->buf = fd_verbuf;		/* all data goes into verify buffer */
    }

    blk = gp->blks - 1;			/* last block of last track */

    for (cyl = lastcyl; cyl >= firstcyl; cyl--) {	/* do all cylinders */

	if ((rc = fd_mb_seek(cyl)) < 0) {	/* start the seek */
	    TRACE(3,("fd_verify seek cyl %d failed\n",cyl));
	    return(EIO);
	}

	/* Setup all sectors this track for reading. */

	if (rc = (*dp->geometry)(gp,blk)) {	/* get spt this track */
	    TRACE(0,("fd_verify:geometry error, blk=%d\n",blk));
	    return(EIO);
	}

	if ((rc = fd_mb_waitready()) & S_NOTREADY) {	/* wait for seek done */
	    TRACE(3,("fd_verify:not ready after seek\n"));
	    return(EIO);
	}

	/* Do all tracks this cylinder. */

	for (track = gp->maxtrk; track >= 0; track--) {

	    (void)(*fd_chip->seltrack)(track);

	    fd_cleartodo();		/* clear seen, errcnt, wanted*/
	    for (tp = &fd_todo[0]; tp < &fd_todo[gp->spt]; tp++) {
		tp->wanted = 1;
	    }
	    fd_ntodo = gp->spt;

	    (void)SPLFD();
	    rc = fd_dotrack(s,cyl,1);	/* read the track */
	    splx(s);			/* make sure spl'ed back nice */

	    if (rc) {			/* quit if verify error */
		TRACE(3,("ver dotrack failed c%d t%d\n",cyl,track));
		return(EIO);
	    }
	}
	blk -= gp->spt;			/* gets us next track info */
    }

    return(0);
}

/*-----------------------*/
int
fd_mb_waitready(i)
{
    register int loops;
    register int stat;

    loops = 0;

    do {
	stat = (*fd_chip->status)();
    } while ( stat & S_NOTREADY && ++loops < MAXLOOPS);

    if (stat & S_NOTREADY) {
	TRACE(2,("fd_mb_waitready: drive not ready after %d loops\n",loops));
    }

    return(stat);
}
