/*
 * @(#)func.c  {Apple version 1.1 89/08/15 11:41:26}
 *
 * Copyright (c) 1987, 1988, 1989 by Apple Computer, Inc.
 * All Rights Reserved.
 *
 * THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF APPLE COMPUTER, INC.
 * The copyright notice above does not evidence any actual or
 * intended publication of such source code.
 */

#if !defined(lint) && !defined(NO_SCCS_IDS)
static char _sccsid[]="@(#)func.c  {Apple version 1.1 89/08/15 11:41:26}";
#endif

#include "fd.h"
#include <sys/file.h>
#include <sys/buf.h>
#include <sys/uio.h>
#include <sys/param.h>
#include <sys/ioctl.h>
#include <sys/ssioctl.h>
#include <sys/sysmacros.h>
#include <sys/diskformat.h>
#include <sys/fdioctl.h>

/* all these needed to return GD_PARTSIZE. Sigh... */
#include <sys/mmu.h>
#include <sys/param.h>
#include <sys/signal.h>
#include <sys/time.h>
#include <sys/user.h>

    extern struct fd_meter fd_meter;
    extern struct fd_tune fd_tune;
    extern struct interface *fd_int;

    extern struct drivestatus fd_status[];
    extern struct buf fd_cmdbuf;

/*--------------------------------*/
/*ARGSUSED*/
void
fd_close(dev,flag)
dev_t dev;
int flag;
{

    void fd_strategy();

    register struct drivestatus *ds;
    register struct buf *bp = &fd_cmdbuf;
    register int drivenum;

    drivenum = (dev & FD_DRIVE) ? 1 : 0;

    ds = &fd_status[drivenum];

    if (ds->open == 0) {		/* not open */
	return;
    }

    if (dev & FD_CLSEJECT) {	/* wants to eject on close */
	GETBUF(bp);
	bp->b_cmd = CMD_EJECT;
	bp->b_dev = dev;
	fd_strategy(bp);
	iowait(bp);
	FREEBUF(bp);
    }

    ds->harderr = 0;
    ds->exclusive = 0;
    ds->open = 0;
}

/*--------------------------------*/
int
fd_read(dev,uio)
dev_t dev;
struct uio *uio;
{
    void fd_strategy();

    register struct drivestatus *ds;
    register int drivenum;

    if (dev & FD_TUNE) {		/* special-IOCTL device is NG */
	return(ENXIO);
    }

    drivenum = (dev & FD_DRIVE) ? 1 : 0;

    ds = &fd_status[drivenum];

    if (ds->open == 0) {		/* not opened */
	return(ENXIO);
    }
    if (ds->harderr) {			/* no I/O after hard error */
	return(EIO);
    }

    return(physio(fd_strategy,(struct buf *)0,dev,B_READ,uio));
}

/*--------------------------------*/
int
fd_write(dev,uio)
dev_t dev;
struct uio *uio;
{
    void fd_strategy();

    register struct drivestatus *ds;
    register int drivenum;

    if (dev & FD_TUNE) {		/* special-IOCTL device is NG */
	return(ENXIO);
    }

    drivenum = (dev & FD_DRIVE) ? 1 : 0;

    ds = &fd_status[drivenum];

    if (ds->open == 0) {		/* not opened */
	return(ENXIO);
    }
    if (ds->harderr) {			/* no I/O after hard error */
	return(EIO);
    }
    
    return(physio(fd_strategy,(struct buf *)0,dev,B_WRITE,uio));
}

/*--------------------------------*/
int
fd_ioctl(dev,cmd,addr,flags)		/* GENERIC */
dev_t dev;
int cmd;
caddr_t addr;
int flags;
{
    void fd_strategy();

    static struct geom g;

    register struct drivestatus *ds;
    register struct buf *bp = &fd_cmdbuf;
    register int m;
    register int rc;
    int stat;

    m = (dev & FD_DRIVE) ? 1 : 0;

    ds = &fd_status[m];

    if (!ds->open) {
	return(ENXIO);			/* not open! hah! */
    }

    switch (cmd) {

	case FD_GETMETER:		/* get current metering values */
	    if (!(dev & FD_TUNE)) {	/* not the special-IOCTL device? */
		return(ENXIO);
	    }
	    blt(addr,&fd_meter,sizeof(struct fd_meter));
	    return(0);

	case FD_SETMETER:		/* set metering values */
	    if (!(dev & FD_TUNE)) {	/* not the special-IOCTL device? */
		return(ENXIO);
	    }
	    if (!(flags & FWRITE)) {	/* not open for write? fooey! */
		return(EROFS);
	    }
	    blt(&fd_meter,addr,sizeof(struct fd_meter));
	    return(0);

	case FD_GETTUNE:		/* get current tuning values */
	    if (!(dev & FD_TUNE)) {	/* not the special-IOCTL device? */
		return(ENXIO);
	    }
	    blt(addr,&fd_tune,sizeof(struct fd_tune));
	    return(0);

	case FD_SETTUNE:		/* set tuning values */
	    if (!(dev & FD_TUNE)) {	/* not the special-IOCTL device? */
		return(ENXIO);
	    }
	    if (!(flags & FWRITE)) {	/* not open for write? fooey! */
		return(EROFS);
	    }
	    blt(&fd_tune,addr,sizeof(struct fd_tune));
	    return(0);
	
	    /* The special FD_TUNE dev is allowed to eject, just to
	     * handle emergencies (better than a paper-clip!)
	     */
	case AL_EJECT:			/* eject */
	    GETBUF(bp);
	    bp->b_cmd = CMD_EJECT;	/* command */
	    bp->b_dev = dev;
	    fd_strategy(bp);
	    iowait(bp);
	    ds->harderr = 1;		/* no further I/O allowed */
	    FREEBUF(bp);
	    return(0);

	case GD_PARTSIZE:
	    if (ds->hasdisk && ds->density != FMT_UNFORMATTED) {
		(*ds->dp->geometry)(&g,0);
		u.u_rval1 = g.blks;	/* give total blocks on media */
		rc = 0;
	    } else {
		if (ds->hasdisk) {
		    u.u_rval1 = 0;	/* unformatted disks have zero */
		    rc = 0;
		} else {
		    rc = EIO;		/* no disk? must've ejected it */
		}
	    }
	    return(rc);

	case UIOCFORMAT:		/* format */
	    if (dev & FD_TUNE) {	/* special-IOCTL device is NG */
		return(ENXIO);
	    }
	    if (!(flags & FWRITE)) {	/* not open for write? fooey! */
		return(EROFS);
	    }

	    if (ds->exclusive == 0) {	/* must be opened exclusively */
		return(ENXIO);
	    }

	    GETBUF(bp);
	    bp->b_cmd = CMD_FORMAT;	/* command */
	    bp->b_params = addr;	/* formatting params */
	    bp->b_dev = dev;
	    fd_strategy(bp);
	    iowait(bp);
	    if (bp->b_flags & B_ERROR) {
		rc = bp->b_error;
		ds->harderr = 1;
	    } else {
		rc = 0;
		ds->harderr = 0;	/* OK to use */
	    }

	    FREEBUF(bp);
	    return(rc);

	case FD_FMTONLY:		/* format only - no verify */
	    if (dev & FD_TUNE) {	/* special-IOCTL device is NG */
		return(ENXIO);
	    }
	    if (!(flags & FWRITE)) {	/* not open for write? fooey! */
		return(EROFS);
	    }

	    if (ds->exclusive == 0) {	/* must be opened exclusively */
		return(ENXIO);
	    }

	    GETBUF(bp);
	    bp->b_cmd = CMD_FMTONLY;	/* command */
	    bp->b_params = addr;	/* formatting params */
	    bp->b_dev = dev;
	    fd_strategy(bp);
	    iowait(bp);
	    if (bp->b_flags & B_ERROR) {
		rc = bp->b_error;
		ds->harderr = 1;
	    } else {
		rc = 0;
		ds->harderr = 0;	/* OK to use */
	    }

	    FREEBUF(bp);
	    return(rc);

	case FD_VFYONLY:		/* verify only - no format */
	    if (dev & FD_TUNE) {	/* special-IOCTL device is NG */
		return(ENXIO);
	    }
	    if (!(flags & FWRITE)) {	/* not open for write? fooey! */
		return(EROFS);
	    }

	    if (ds->exclusive == 0) {	/* must be opened exclusively */
		return(ENXIO);
	    }

	    GETBUF(bp);
	    bp->b_cmd = CMD_VFYONLY;	/* command */
	    bp->b_params = addr;	/* formatting params */
	    bp->b_dev = dev;
	    fd_strategy(bp);
	    iowait(bp);
	    if (bp->b_flags & B_ERROR) {
		rc = bp->b_error;
		ds->harderr = 1;
	    } else {
		rc = 0;
		ds->harderr = 0;	/* OK to use */
	    }

	    FREEBUF(bp);
	    return(rc);

	case FD_GETSTAT:
	    if (dev & FD_TUNE) {	/* special-IOCTL device is NG */
		return(ENXIO);
	    }

	    GETBUF(bp);
	    bp->b_cmd = CMD_GETSTAT;	/* command */
	    bp->b_params = (caddr_t)&stat;	/* result goes here */
	    bp->b_dev = dev;
	    fd_strategy(bp);
	    iowait(bp);

	    u.u_rval1 = fd_trstat(stat);	/* give status */
	    FREEBUF(bp);
	    return(0);


	default:
	    return(ENXIO);
    }
}
