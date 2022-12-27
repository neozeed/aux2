/*
 * @(#)open.c  {Apple version 1.5 90/02/19 14:27:47}
 *
 * Copyright (c) 1987, 1988, 1989 by Apple Computer, Inc.
 * All Rights Reserved.
 *
 * THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF APPLE COMPUTER, INC.
 * The copyright notice above does not evidence any actual or
 * intended publication of such source code.
 */

#if !defined(lint) && !defined(NO_SCCS_IDS)
static char _sccsid[]="@(#)open.c  {Apple version 1.5 90/02/19 14:27:47}";
#endif

#include "fd.h"
#include <sys/file.h>
#include <sys/param.h>
#include <sys/sysmacros.h>
#include <sys/fdioctl.h>
#include <sys/buf.h>

    extern struct interface *fd_int;

/*--------------------------------*/
int
fd_open(dev,flags)			/* GENERIC */
register dev_t dev;
int flags;
{
    void fd_strategy();
    void wakeup();

    extern struct drivestatus fd_status[];
    extern struct driveparams fd_d_params[];
    extern struct fd_meter fd_meter;
    extern struct buf fd_cmdbuf;

    register struct drivestatus *ds;
    register struct buf *bp;
    register int i;
    register int error;
    int drivenum;
    int s;

    drivenum = (dev & FD_DRIVE) ? 1 : 0;
    ds = &fd_status[drivenum];

    /* If this is the "special ioctl" device, we open instantly,
     * since he can't do any "real" I/O anyway.  No need to check
     * the device, get the chip, or any of that stuff.
     */

    if (dev & FD_TUNE) {
	ds->open = 1;
	fd_meter.opens++;		/* meter successful opens */
	return(0);			/* instant "open" */
    }

    /* The floppy is a limited-exclusive-open device to avoid
     * conflicts between conflicting-density opens. We only allow
     * multiple opens if the minor bits match exactly.
     *
     * Formatting requires opening O_EXCL.
     */

    if (ds->open || ds->opening) {	/* someone else has it open */

	if (ds->exclusive) {
	    return(EBUSY);		/* prior has it exclusive */
	}

	if (minor(dev) != minor(ds->dev)) {
	    return(EBUSY);		/* prior using different minor dev */
	}

	if (flags & O_EXCL) {
	    return(EBUSY);		/* already open, can't have excl */
	}

    }

    /* If he wants MFM and we don't have the hardware,
     * then we don't waste a moment of our time...
     */

    if (((dev & FD_FORMAT) == FMT_MFM720) ||
        ((dev & FD_FORMAT) == FMT_MFM1440)) {	/* he wants MFM density */

	if (!ds->fdhd) {
	    return(ENXIO);			/* no chance of getting it */
	}
    }

    ds->dev = makedev(major(ds->dev),minor(dev));

    if (!ds->installed) {
	return(ENODEV);			/* no drive plugged in */
    }

    /* If there is someone "ahead" of us in open, we sleep
     * till he finishes, when he'll wake us up.
     */

    if (ds->opening) {			/* then we're number two... */
	while (ds->opening) {
	    i = sleep((caddr_t)(&ds->dev),((PZERO + 1) | PCATCH));
	    if (i == 1) {
		return(EINTR);
	    }
	}
    }

    /* If it's already successfully opened, we're all set. */

    if (ds->open) {			/* already open */
	if ((flags & FWRITE) && !ds->wrtenab) {
	    return(EROFS);
	} else {
	    fd_meter.opens++;		/* meter successful opens */
	    return(0);			/* instant "open" */
	}

    } else {
	ds->opening = 1;		/* open in progress */
	if (flags & O_EXCL) {
	    ds->exclusive = 1;
	}
    }

    bp = &fd_cmdbuf;

    GETBUF(bp);
    bp->b_cmd = CMD_OPEN;
    bp->b_dev = dev;
    fd_strategy(bp);				/* try to open it */
    iowait(bp);

    if (bp->b_flags & B_ERROR) {
	error = bp->b_error;
	FREEBUF(bp);

	if (error == ENOENT) {			/* no diskette */

	    ds->hasdisk = 0;

	    if (dev & FD_WAIT) {		/* then wait for insert */

		for (;;) {

		    GETBUF(bp);
		    bp->b_cmd = CMD_OPEN;
		    bp->b_dev = dev;
		    fd_strategy(bp);			/* try to open it */
		    iowait(bp);

		    if (!(bp->b_flags & B_ERROR)) {	/* got a disk */
			FREEBUF(bp);
			break;
		    }

		    FREEBUF(bp);

		    /* If a wrong format is inserted, we terminate the
		     * open operation right now.  Harderr was set by
		     * the eject operation.
		     */
		    if (ds->harderr) {			/* wrong format */
			ds->opening = 0;
			ds->exclusive = 0;
			ds->harderr = 0;
			wakeup((caddr_t)&ds->dev);	/* wake other(s) */
			return(bp->b_error);
		    }

		    s = SPLFD();
		    ds->timeout = T_500MS;
		    ds->timertype = T_WAKEUP;
		    i = sleep((caddr_t)(ds),((PZERO + 1) | PCATCH));
		    splx(s);
		    if (i == 1) {
			ds->opening = 0;
			ds->exclusive = 0;
			wakeup((caddr_t)&ds->dev);	/* wake other(s) */
			return(EINTR);
		    }
		}

	    } else {				/* not waiting...you lose */

		fd_meter.fmt_none++;
		ds->opening = 0;
		ds->exclusive = 0;
		TRACE(3,("open: no diskette\n"));
		wakeup((caddr_t)&ds->dev);	/* wake possible other(s) */
		return(ENOENT);			/* no disk inserted */
	    }

	} else {				/* some other error */
	    ds->opening = 0;
	    ds->exclusive = 0;
	    ds->harderr = 0;
	    wakeup((caddr_t)&ds->dev);		/* wake possible other(s) */
	    return(error);
	}
    }

    /* Okay, we know there's a diskette in the drive. */

    ds->wrtenab = ((*fd_int->status)() & S_WRTENAB) ? 1 : 0;

    if ((flags & FWRITE) && !ds->wrtenab) {
	FREEBUF(bp);
	ds->opening = 0;
	ds->exclusive = 0;
	wakeup((caddr_t)&ds->dev);	/* wake possible other(s) */
	return(EROFS);			/* write protected */
    }

    FREEBUF(bp);
    ds->open = 1;			/* it's open */
    ds->opening = 0;

    wakeup((caddr_t)&ds->dev);		/* wake possible other(s) */

    fd_meter.opens++;			/* meter successful opens */
    return(0);
}

/*--------------------------------*/
int
fd_cmd_open(bp)				/* GENERIC */
register struct buf *bp;
{
    extern struct drivestatus *fd_drive;
    register struct drivestatus *ds = fd_drive;
    register struct driveparams *dp = ds->dp;
    register int stat;
    register int rc;
    register int s;

    stat = (*fd_int->status)();		/* get the status */

    if (stat & S_NODISK) {		/* you lose if no diskette */
	return(ENOENT);			/* this means no disk inserted */
    }

    if ((*fd_int->beginopen)(ds)) {	/* do what's needed to start up */
	return(EIO);
    }

    if (!ds->hasdisk) {			/* then it's newly inserted */

	if ((*fd_int->loadfloppy)(ds)) {	/* do whatever's needed */
	    return(EIO);
	}

	rc = (*fd_int->getformat)(&ds->density); /* determine disk format */

	if (rc == EINVAL && !ds->exclusive) {	/* something wrong with it */

	    /* We automatically eject invalid formats since the user
	     * can't open to do the ioctl himself.  If the open is
	     * exclusive (O_EXCL), we assume he doesn't care and will
	     * probably be formatting anyway.
	     */

	    (void)(*fd_int->eject)();

	    TRACE(0,("illegal density for this media type\n"));
	    return(rc);
	}

	ds->hasdisk = 1;
    }

    /* Decide whether the disk's format is what the user wants. We only
     * worry about this if he doesn't open O_EXCL.  If he opens exclusive,
     * he's probably going to format it.
     */

    if (!ds->exclusive) {
	if (chkformat(ds->density,bp->b_dev & FD_FORMAT) < 0) {

	    /* We automatically eject invalid formats since the user
	     * can't open to do the ioctl himself.
	     */
	    (void)(*fd_int->eject)();

	    TRACE(0,("density doesn't match request\n"));
	    return(ENXIO);		/* format isn't desired one */
	}
    }

    (void)(*fd_int->setdensity)(ds);	/* setup density params for floppy */

    return(0);
}

/*--------------------------------*/
static int
chkformat(have,want)			/* GENERIC */
register int have;			/* actual format */
register int want;			/* desired format */
{

    register int stat;

    stat = (*fd_int->status)();		/* get drive status */

    switch (want) {
	
	case 0 :				/* no validation */
		    return(0);

	case FMT_GCR400:
		    if (!(stat & S_1MBMEDIA)) {
			return(-1);		/* wrong disk inserted */
		    }
		    if (have == FMT_GCR400 || have == FMT_UNFORMATTED) {
			return(0);
		    } else {
			return(-1);
		    }

	case FMT_GCR800:
		    if (!(stat & S_TWOSIDED)) {
			return(-1);		/* wrong drive */
		    }
		    if (!(stat & S_1MBMEDIA)) {
			return(-1);		/* wrong disk inserted */
		    }
		    if (have == FMT_GCR800 || have == FMT_UNFORMATTED) {
			return(0);
		    } else {
			return(-1);
		    }
	case FMT_MFM720:
		    if (!(stat & S_FDHD)) {
			return(-1);		/* wrong drive */
		    }
		    if (!(stat & S_1MBMEDIA)) {
			return(-1);		/* wrong disk inserted */
		    }
		    if (have == FMT_MFM720 || have == FMT_UNFORMATTED) {
			return(0);
		    } else {
			return(-1);
		    }

	case FMT_MFM1440:
		    if (!(stat & S_FDHD)) {
			return(-1);		/* wrong drive */
		    }
		    if (stat & S_1MBMEDIA) {
			return(-1);		/* wrong disk inserted */
		    }
		    if (have == FMT_MFM1440 || have == FMT_UNFORMATTED) {
			return(0);
		    } else {
			return(-1);
		    }

    }
#ifdef LINT
    return(0);
#endif
}
