/*
 * @(#)format.c  {Apple version 1.4 89/12/12 14:02:56}
 *
 * Copyright (c) 1987, 1988, 1989 by Apple Computer, Inc.
 * All Rights Reserved.
 *
 * THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF APPLE COMPUTER, INC.
 * The copyright notice above does not evidence any actual or
 * intended publication of such source code.
 */

#if !defined(lint) && !defined(NO_SCCS_IDS)
static char _sccsid[]="@(#)format.c  {Apple version 1.4 89/12/12 14:02:56}";
#endif

#include "fd.h"
#include <sys/buf.h>
#include <sys/diskformat.h>

    extern struct drivestatus	*fd_drive;
    extern struct chipparams	*fd_chip;
    extern struct driveparams	 fd_d_params[];
    extern int fd_motordelay;
    extern struct interface	*fd_int;

/*--------------------------------*/
int
fd_cmd_format(bp)			/* GENERIC: format a diskette */
register struct buf *bp;
{
    register struct drivestatus *ds	= fd_drive;
    register struct diskformat *pp	= (struct diskformat *)bp->b_params;
    register int stat;

    stat = (*fd_int->status)();

    if (stat & S_NODISK) {
	return(ENXIO);			/* no disk inserted */
    }
    if (!(stat & S_WRTENAB)) {
	return(EROFS);			/* write-protected */
    }

    /* Density selection occurs in this order:
     *
     *        1) d_lhead and d_dens fields in the diskformat structure;
     *   else 2) Bits in the minor dev number;
     *   else 3) A local decision based on media and drive types.
     */

    if (pp->d_lhead == 0) {		/* only one head means 400K GCR */
	pp->d_dens = 400;
    }

    if (pp->d_dens == DISKDEFAULT) {	/* we get to plug it */

	switch (bp->b_dev & FD_FORMAT) {

	    case FMT_GCR400:
		    pp->d_dens = 400;	/*  400K GCR */
		    break;

	    case FMT_GCR800:
		    pp->d_dens = 800;	/*  800K GCR */
		    break;

	    case FMT_MFM720:
		    pp->d_dens = 720;	/*  720K MFM */
		    break;

	    case FMT_MFM1440:
		    pp->d_dens = 1440;	/* 1440K MFM */
		    break;

	    case 0 :			/* we decide */
		    if (stat & S_FDHD && !(stat & S_1MBMEDIA)) {
			pp->d_dens = 1440;	/* 1440K always on HD media */
		    } else {
			pp->d_dens = 800;	/*  800K GCR on 1mb media */
		    }
		    break;
	}
    }

    /* Now we get ready to format the bloody disk. We know that the drive
     * and chip are either in the proper mode for the existing density on
     * the diskette, or else they were forced to GCR800 if it is unformatted.
     */

    return((*fd_int->format)(stat,pp,bp->b_cmd));	/* try to format it */
}

/*----------------------------------*/
int					/* -1 = gcr, -2 = mfm, else error */
fd_val_format(stat,pp)			/* GENERIC: validate density request */
register int stat;
register struct diskformat *pp;
{
    register struct drivestatus *ds = fd_drive;
    register int ret;

    if (pp->d_lcyl == DISKDEFAULT) {	/* allow cylinder range */
	pp->d_lcyl = 79;
    }

    if (pp->d_fcyl == DISKDEFAULT) {
	pp->d_fcyl = 0;
    }

    switch (pp->d_dens) {
	
	case 400:			/*  400K GCR */
		if (stat & S_FDHD && !(stat & S_1MBMEDIA)) {
		    return(EINVAL);	/* wrong media */
		}
		ds->dp = &fd_d_params[FMT_GCR400 - 1];
		ds->density = FMT_GCR400;
		return(-1);		/* GCR */

	case 800:			/*  800K GCR */
		if (stat & S_FDHD && !(stat & S_1MBMEDIA)) {
		    return(EINVAL);	/* wrong media */
		}
		ds->dp = &fd_d_params[FMT_GCR800 - 1];
		ds->density = FMT_GCR800;
		return(-1);		/* GCR */

	case 720:			/*  720K MFM */
		if (!(stat & S_FDHD)) {
		    return(EINVAL);	/* wrong drive type */
		}
		if (!(stat & S_1MBMEDIA)) {
		    return(EINVAL);	/* wrong media */
		}
		ds->dp = &fd_d_params[FMT_MFM720 - 1];
		ds->density = FMT_MFM720;
		return(-2);		/* MFM */

	case 1440:			/* 1440K MFM */
		if (!(stat & S_FDHD)) {	/* wrong drive type */
		    return(EINVAL);
		}
		if (stat & S_1MBMEDIA) {
		    return(EINVAL);	/* wrong media */
		}
		ds->dp = &fd_d_params[FMT_MFM1440 - 1];
		ds->density = FMT_MFM1440;
		return(-2);		/* MFM */
    }
}
