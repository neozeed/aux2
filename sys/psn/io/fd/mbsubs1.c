/*
 * @(#)mbsubs1.c  {Apple version 1.2 90/03/26 20:43:38}
 *
 * Copyright (c) 1987, 1988, 1989 by Apple Computer, Inc.
 * All Rights Reserved.
 *
 * THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF APPLE COMPUTER, INC.
 * The copyright notice above does not evidence any actual or
 * intended publication of such source code.
 */

#if !defined(lint) && !defined(NO_SCCS_IDS)
static char _sccsid[]="@(#)mbsubs1.c  {Apple version 1.2 90/03/26 20:43:38}";
#endif

#include "fd.h"
#include "fdhw.h"
#include <sys/var.h>
#include <sys/fdioctl.h>
#include <sys/uconfig.h>
#include <sys/via6522.h>
#include <sys/diskformat.h>

    extern struct drivestatus fd_status[];
    extern struct drivestatus *fd_drive;
    extern struct chipparams *fd_chip;
    extern struct chipparams fd_c_params[];
    extern struct driveparams fd_d_params[];
    extern int fd_minor;		/* current drive minor number */
    extern struct via *via2_addr;

    void fd_sleep();

/*--------------------------------*/
int
fd_mb_beginopen(ds)			/* MOTHERBOARD */
register struct drivestatus *ds;
{
    extern int fd_motordelay;
    register struct driveparams *dp = ds->dp;
    register int s;

    /* Start the motor if it isn't already running. */

    if ((*fd_chip->status)() & S_MOTOROFF) {
	(void)(*dp->motoron)();		/* setup and start motor */

	/* Wait an initial time to ensure that READY is valid, and to
	 * let motor inrush current go away before seeking.
	 */

	fd_sleep(5);			/* wait 64-80 ms */
    }

    if (fd_mb_waitready() & S_NOTREADY) {	/* make sure drive's ready */
	TRACE(3,("fd_mb_open: drive not ready\n"));

	s = SPLFD();
	ds->timeout = fd_motordelay;
	ds->timertype = T_MOTOROFF;	/* schedule the motor turnoff */
	splx(s);

	return(EIO);
    }

    return(0);
}

/*--------------------------------*/
int
fd_mb_disable()
{
    (void)(*fd_chip->disable)();

    return(0);
}

/*--------------------------------*/
int
fd_mb_eject()				/* MOTHERBOARD */
{
    register struct drivestatus *ds = fd_drive;
    register struct driveparams *dp = ds->dp;
    register int loops;
    register int s;
    register int seektime;

    /* Start the motor if it isn't already running. */

    if ((*fd_chip->status)() & S_MOTOROFF) {
	(void)(*dp->motoron)();		/* setup and start motor */

	/* Wait an initial time to ensure that READY is valid, and to
	 * let motor inrush current go away before seeking.
	 */

	fd_sleep(5);			/* wait 64-80 ms */
    }

    /* Check status to make sure a disk to eject. */

    if (((*fd_chip->status)() & S_NODISK) == 0) {

	/* The peripherals group wants us to seek to track 79
	 * so that if any dirt gets left behind it's gonna be
	 * left in a "safer" place than wherever we might be.
	 */

	seektime = fd_mb_seek(79);		/* out to the end */
	if (seektime >= 0) {		/* seek started OK */
	    ds->harderr = 1;		/* prevent further I/O */
	    if (seektime > 0) {
		fd_sleep(seektime);	/* sleep till seek nears completion */
	    }
	    (void)fd_mb_waitready();		/* Wait for completion. */
	}

	(void)(*fd_chip->command)(C_EJECT);

	/* Wait for the disk to pop out. The spec says a max of 1.5
	 * seconds, so we'll give it up to 5.  We sleep for 1/4 sec
	 * intervals between checks.
	 */

	for (loops = 0; loops < 20; loops++) {	/* max 5 seconds */
	    fd_sleep(v.v_hz / 4);		/* sleep for 1/4 second */
	    if ((*fd_chip->status)() & S_NODISK) {
		break;
	    }
	}
    }

    s = SPLFD();

    /* We clear hasdisk and event SPL'ed to prevent pollinsert() from
     * coming in while they're not set consistently.  That would cause
     * pollinsert() to think the event was already posted.
     */

    ds->hasdisk = 0;
    ds->event = 0;

    ds->timertype = 0;				/* no need for motor timer */
    ds->timeout = 0;
    splx(s);

    (void)(*fd_chip->command)(C_MOTOROFF);	/* drive motor off */
    (void)(*fd_chip->disable)();		/* disable the drive */

    return(0);
}

/*--------------------------------*/
int
fd_mb_enable(drive)
register int drive;
{
    (void)(*fd_chip->enable)(drive);

    return(0);
}

/*----------------------------------*/
/* Formatting is noninterruptible while formatting,
 * but is interruptible while verifying.
 */
int
fd_mb_format(stat,pp,cmd)		/* MOTHERBOARD */
register int stat;
struct diskformat *pp;
int cmd;		/* CMD_FORMAT, CMD_FMTONLY, CMD_VFYONLY */
{

    void fd_sleep();
    void delay_ms();

    extern int fd_minor;

    static struct geom geom;

    struct drivestatus *ds		= fd_drive;
    register struct via *vp		= via2_addr;
    register struct geom *gp		= &geom;
    register struct driveparams *dp;
    register int blk = 0;
    register int rc;
    register int s;

    int firstcyl;
    int lastcyl;

    rc = fd_val_format(stat,pp);	/* validate the requested density */
    if (rc > 0) {			/* wrongo */
	return(rc);
    }

    dp = ds->dp;			/* was setup by fd_val_format() */

    if (rc == -1) {			/* set GCR */
	(void)fd_setgcrmode();
    } else {				/* set MFM */
	(void)fd_setmfmmode();
    }

    fd_mb_enable(fd_minor);		/* ensure drive is enabled again */

    firstcyl = pp->d_fcyl;
    lastcyl  = pp->d_lcyl;

    /* Start the motor if it isn't already running. */

    if ((*fd_chip->status)() & S_MOTOROFF) {
	(void)(*dp->motoron)();		/* setup and start motor */

	/* Wait an initial time to ensure that READY is valid, and to
	 * let motor inrush current go away before seeking.
	 */

	fd_sleep(5);			/* wait 64-80 ms */
    }

    if ((rc = fd_mb_recal()) < 0) {	/* start the recal */
	TRACE(3,("fd_format:recal failed %d\n",rc));
	return(EIO);
    }

    /* Delay an additional 300ms to ensure +-1.5% speed. *?

    fd_sleep(20);			/* 304-320 ms */

    if ((rc = fd_mb_waitready()) & S_NOTREADY) { /* wait for recal */
	TRACE(3,("fd_format:not ready %x\n,rc"));
	return(EIO);
    }

    s = SPLFD();

    rc = 0;

    if (cmd == CMD_FORMAT || cmd == CMD_FMTONLY) {

	for (gp->cyl = firstcyl; gp->cyl <= lastcyl; gp->cyl++) {

	    vp->regb |= BUSENABLE;		/* allow NUBUS DMA */
	    splx(s);			/* lower spl while seeking */

	    if ((rc = fd_mb_seek(gp->cyl)) < 0) {	/* start the seek */
		TRACE(3,("fd_format:seek failed cyl %d rc %d\n",gp->cyl,rc));
		goto done;
	    }

	    TRACE(4,("fmt cyl %d\n",gp->cyl));

	    (void)(*dp->geometry)(gp,blk);	/* get maxtrk, gapsize, spt */

	    if ((rc = fd_mb_waitready()) & S_NOTREADY) { /* wait for seek */
		TRACE(3,("fd_format:not ready after seek %x\n",rc));
		goto done;
	    }

	    (void)SPLFD();			/* SPL while writing tracks */
	    vp->regb &= ~BUSENABLE;		/* inhibit NUBUS DMA */

	    for (gp->track = 0; gp->track <= gp->maxtrk; gp->track++) {

		rc = (*dp->fmttrack)(gp);	/* appropriate track-writer */
		if (rc) {			/* quit now if writing failed */
		    TRACE(3,("fmttrack failed c%d t%d\n",gp->cyl,gp->track));
		    goto done;
		}

		blk += gp->spt;
	    }
	}
    }
done:;

    vp->regb |= BUSENABLE;		/* allow NUBUS DMA */

    if (rc == 0 && (cmd == CMD_FORMAT || cmd == CMD_VFYONLY)) {
	rc = fd_mb_verify(ds,s,firstcyl,lastcyl);	/* verify it */
    }

    /* If the format or verify fails, we deliberately destroy track zero. */

    if (rc) {

	splx(s);			/* open up while seeking */

	if (fd_mb_recal() < 0) {		/* start the recal */
	    TRACE(3,("fd_verfail:recal failed %d\n",rc));
	    rc = EIO;
	} else {			/* good recal started */
	    if (fd_mb_waitready() & S_NOTREADY) {
		TRACE(3,("fd_verfail:not ready %x\n,rc"));
		rc = EIO;
	    } else {			/* recal completed successfully */
		(void)SPLFD();
		vp->regb &= ~BUSENABLE;		/* inhibit NUBUS DMA */
		(void)(*dp->erasetrack)();
		vp->regb |= BUSENABLE;		/* allow NUBUS DMA */
	    }
	}
    }

    ds->timeout = fd_motordelay;
    ds->timertype = T_MOTOROFF;		/* schedule the motor turnoff */

    splx(s);

    return(rc);
}

/*--------------------------------*/
/* Validate that the proper media is inserted.
 * We are called with the drive selected, running, in IWM mode.
 */
int					/* 0 = OK, else error code */
fd_mb_getformat(fmt)			/* MOTHERBOARD */
register short *fmt;
{
    extern struct fd_tune fd_tune;
    extern struct fd_meter fd_meter;

    static struct addr a;

    register struct via *vp		= via2_addr;
    register int rc;
    register int stat;
    register int tries;
    register int s;

    if ((stat = fd_mb_waitready()) & S_NOTREADY) {	/* wait for ready */
	TRACE(3,("fd_getformat: drive not ready\n"));
	return(EIO);
    }

    /* Try GCR first. */
    
    (void)(*fd_chip->seltrack)(0);		/* select head 0 */

    for (tries = fd_tune.gf_tries; tries > 0; tries--) {

	s = SPLFD();
	vp->regb &= ~BUSENABLE;		/* lock out NUBUS DMA */

	rc = gcr_rdadr(&a);

	vp->regb |= BUSENABLE;		/* allow NUBUS DMA */
	splx(s);

	if (rc != 0) {
	    continue;				/* no header found yet */
	} else {				/* it's GCR. 400 or 800? */
	    if (a.sides == 1) {
		*fmt = FMT_GCR400;
		fd_meter.fmt_gcr400++;		/* meter it */
	    } else {
		if (stat & S_TWOSIDED) {	/* 800K ok on 2sided drive */
		    *fmt = FMT_GCR800;
		    fd_meter.fmt_gcr800++;	/* meter it */
		} else {
		    TRACE(3,("fd_getformat:800K on single-sided drive!\n"));
		}
	    }

	    /* Don't allow 400K/800K on 2mb media. */

	    if ((stat & S_FDHD) && !(stat & S_1MBMEDIA)) {
		fd_meter.fmt_illegal++;		/* meter it */
		return(EINVAL);
	    } else {
		return(0);			/* OK */
	    }
	}
    }

    /* Wasn't GCR. Maybe MFM? */

    if (!(stat & S_FDHD)) {			/* drive is GCR only */
	*fmt = FMT_UNFORMATTED;			/* so it must be unformatted */
	fd_meter.fmt_unformatted++;		/* meter it */
	return(0);
    }

    /* Try MFM. */

    (void)fd_setmfmmode();			/* switch to MFM mode */
    (void)(*fd_chip->enable)(fd_minor);		/* re-enable the drive */
    (void)fd_mfm_motoron();			/*  and restart the motor */
	
    fd_drive->dp = &fd_d_params[FMT_MFM1440 - 1];	/* force temp MFM */

    /* Wait an initial time to ensure that READY is valid, and to
     * let motor inrush current go away before seeking.
     */

    fd_sleep(5);			/* wait 64-80 ms */

    (void)fd_mb_recal();		/* gets speed and params set */

    if ((stat = fd_mb_waitready()) & S_NOTREADY) {	/* wait for ready */
	TRACE(3,("fd_getformat: drive not ready after recal\n"));
	return(EIO);
    }

    /* Again, we only try enough for good measure. */

    (void)(*fd_chip->seltrack)(0);	/* select head 0 */

    for (tries = fd_tune.gf_tries; tries > 0; tries--) {

	s = SPLFD();
	vp->regb &= ~BUSENABLE;		/* lock out NUBUS DMA */

	rc = mfm_rdadr(&a);

	vp->regb |= BUSENABLE;		/* allow NUBUS DMA */
	splx(s);

	if (rc != 0) {
	    continue;				/* no header found yet */
	} else {				/* it's MFM. 720 or 1440? */
	    /* No way to be sure about this except to try to find
	     * a sector >9, then we know it's 1440.
	     */
	    if (stat & S_1MBMEDIA) {
		*fmt = FMT_MFM720;		/* guess it's 720 */
		fd_meter.fmt_mfm720++;		/* meter it */
		return(0);
	    } else {
		*fmt = FMT_MFM1440;		/* must be 1440 on hd media */
		fd_meter.fmt_mfm1440++;		/* meter it */
		return(0);
	    }
	}
    }
    *fmt = FMT_UNFORMATTED;		/* must be unformatted if it fails */
    fd_meter.fmt_unformatted++;		/* meter it */

    return(0);
}

/*--------------------------------*/
int
fd_mb_init()				/* MOTHERBOARD */
{
    extern long iwm_addr;
    extern long timedbra;
    extern long timedbra_us;
    extern int cpuspeed;

    int fd_haveswim;
    register struct swim *sp;

    /* Setup timedbra values.  An approximation, but good enough. */
    timedbra = timedbra * cpuspeed / 16;	/* adjust for faster CPUs */
    timedbra_us = timedbra/10;

    /* Make sure we're in woz mode. */

    if (fd_getchipmode() != 0) {
	(void)ism_setwoz();		/* get out of ISM mode */
	(void)woz_init();		/* init the IWM */
	if (fd_getchipmode() != 0) {
	    TRACE(0,("fd: can't set iwm chip mode; driver unusable."));
	    return(-1);
	}
    }

    fd_drive = &fd_status[0];

    fd_chip = &fd_c_params[0];		/* woz mode params */
    (void)woz_init();			/* init the IWM */

    fd_haveswim = 0;			/* assume the worst */

    /* Check for a swim chip. */
    (void)woz_setism();			/* try to set ism mode */
    (void)ism_init();

    /* We do like the rom and try some harmless phase states. */

    sp = (struct swim *)iwm_addr;
    sp->wphase = 0xf5;
    if (sp->rphase == 0xf5) {
	sp->wphase = 0xf6;
	if (sp->rphase == 0xf6) {
	    sp->wphase = 0xf7;
	    if (sp->rphase == 0xf7) {
		fd_haveswim = 1;	/* we have a swim */
		(void)ism_setwoz();	/* go back to woz mode */
	    }
	}
    }

    (void)woz_init();			/* if no swim, rsetup hosed IWM mode */

    /* Show what's plugged in. */

    if (fd_haveswim) {
	printf("SWIM chip,");
    } else {
	printf(" IWM chip,");
    }

    return(0);
}
