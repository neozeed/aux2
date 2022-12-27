/*
 * @(#)geometry.c  {Apple version 1.1 89/08/15 11:46:03}
 *
 * Copyright (c) 1987, 1988, 1989 by Apple Computer, Inc.
 * All Rights Reserved.
 *
 * THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF APPLE COMPUTER, INC.
 * The copyright notice above does not evidence any actual or
 * intended publication of such source code.
 */

#if !defined(lint) && !defined(NO_SCCS_IDS)
static char _sccsid[]="@(#)geometry.c  {Apple version 1.1 89/08/15 11:46:03}";
#endif

#include "fd.h"

/* These routines return cylinder/track/sector for a block number. */

    extern struct drivestatus *fd_drive;

    static short spt[]	= {12, 11, 10, 9, 8};	/* sectors/track */

    static void gcr_bncalc();
    static void mfm_bncalc();

/*----------------------------------*/
int
gcr400_geometry(gp,blk)
register struct geom *gp;
register int blk;
{
    if (blk < 0 || blk > 800) {		/* invalid blocknum */
	return(-1);
    } else {
	if (blk == 800) {
	    return(1);			/* exactly at the end */
	}
    }

    gp->blks    = 800;
    gp->maxtrk  = 0;

    gcr_bncalc(gp,blk);			/* fills in the rest */
    return(0);
}

/*----------------------------------*/
int
gcr800_geometry(gp,blk)
register struct geom *gp;
register int blk;
{
    if (blk < 0 || blk > 1600) {	/* invalid blocknum */
	return(-1);
    } else {
	if (blk == 1600) {
	    return(1);			/* exactly at the end */
	}
    }

    gp->blks = 1600;
    gp->maxtrk  = 1;

    gcr_bncalc(gp,blk);			/* fills in the rest */
    return(0);
}

/*----------------------------------*/
static void
gcr_bncalc(gp,blk)
register struct geom *gp;		/* returned struct contents */
register int blk;			/* requested block */
{

    static short grpstart[] = {   0,				/* 00-15 */
			(12*16),				/* 16-31 */
			(12*16) + (11*16),			/* 32-47 */
			(12*16) + (11*16) + (10*16),		/* 48-63 */
			(12*16) + (11*16) + (10*16) + (9*16),	/* 64-79 */
			999999 };				/* end */

    register int group = 0;
    register int c = 0;
    register int s = 0;

    gp->track = 0;			/* assume on track 0 */

    /* Grossly find the proper speedgroup first. */
    do {				/* find next larger group */
	group++;
    } while (blk >= (grpstart[group] << gp->maxtrk));
    group--;				/* it's in this one */

    blk -= grpstart[group] << gp->maxtrk;

    c = (16 * group) + (blk / ((spt[group] << gp->maxtrk)));
    s = blk % (spt[group] << gp->maxtrk);

    if (gp->maxtrk) {			/* only for 2 trks/cyl */
	if (s >= spt[group]) {		/* higher track this cyl */
	    s -= spt[group];
	    gp->track = 1;
	}
    }

    gp->cyl     = c;
    gp->sector  = s;

    gp->spt     = spt[c / 16];		/* 16 tracks per speedgroup */

    gp->gapsize = 0;			/* n/a in GCR */
}

/*----------------------------------*/
int
mfm720_geometry(gp,blk)
register struct geom *gp;
register int blk;
{
    if (blk < 0 || blk > 1440) {	/* invalid blocknum */
	return(-1);
    } else {
	if (blk == 1440) {
	    return(1);			/* exactly at the end */
	}
    }

    gp->gapsize = 80;			/* intersector gap */
    gp->blks    = 1440;
    gp->maxtrk  = 1;
    gp->spt     = 9;

    mfm_bncalc(gp,blk);
    return(0);
}

/*----------------------------------*/
int
mfm1440_geometry(gp,blk)
register struct geom *gp;
register int blk;
{
    if (blk < 0 || blk > 2880) {	/* invalid blocknum */
	return(-1);
    } else {
	if (blk == 2880) {
	    return(1);			/* exactly at the end */
	}
    }

    gp->gapsize = 101;			/* intersector gap */
    gp->blks    = 2880;
    gp->maxtrk  = 1;
    gp->spt     = 18;

    mfm_bncalc(gp,blk);
    return(0);
}

/*----------------------------------*/
static void				/* 0 for OK, -1 invalid, +1 at end */
mfm_bncalc(gp,blk)
register struct geom *gp;		/* returned struct contents */
register int blk;
{

    gp->cyl = blk / (gp->spt << 1);	/* always two tracks in MFM */
    gp->sector = blk % (gp->spt << 1);
    if (gp->sector >= gp->spt) {	/* higher track this cyl */
	gp->sector -= gp->spt;
	gp->track = 1;
    } else {
	gp->track = 0;
    }
}
