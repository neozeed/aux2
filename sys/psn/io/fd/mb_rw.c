/*
 * @(#)mb_rw.c  {Apple version 1.2 90/01/19 14:22:40}
 *
 * Copyright (c) 1987, 1988, 1989 by Apple Computer, Inc.
 * All Rights Reserved.
 *
 * THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF APPLE COMPUTER, INC.
 * The copyright notice above does not evidence any actual or
 * intended publication of such source code.
 */

#if !defined(lint) && !defined(NO_SCCS_IDS)
static char _sccsid[]="@(#)mb_rw.c  {Apple version 1.2 90/01/19 14:22:40}";
#endif

#include "fd.h"
#include "fdhw.h"
#include <sys/param.h>		/* for MAX/MIN macros */
#include <sys/fdioctl.h>
#include <sys/via6522.h>
#include <sys/buf.h>

    extern struct drivestatus fd_status[];
    extern struct drivestatus *fd_drive;
    extern struct chipparams *fd_chip;
    extern struct chipparams fd_c_params[];
    extern struct driveparams fd_d_params[];
    extern struct fd_meter fd_meter;
    extern struct fd_tune fd_tune;
    extern int fd_minor;		/* current drive minor number */
    extern struct via *via2_addr;

    void fd_sleep();
    int errretry();
    int filltodo();
    int startcyl();

#define MAXSECT	19			/* max in any density */

struct todo	fd_todo[MAXSECT];	/* current request, indexed by sector */
int		fd_ntodo;		/* num active todo entries */

/* Error recovery flags/values: */
static short fd_reseeks;		/* reseeks this track */
static short fd_recals;			/* recals this track */
static short fd_stayspl;		/* stay SPL'ed in fd_dotrack waits */
static short fd_shortskip;		/* shortening value for sector-skips */

/*----------------------------------*/
int
fd_mb_rw(bp)
register struct buf *bp;
{

    extern int fd_blkrange[];
    extern int fd_motordelay;

    static struct seekinfo infobuf;
    static struct geom geom;

    register struct drivestatus *ds;
    register struct driveparams *dp;
    register struct seekinfo *info	= &infobuf;
    register int rc;
    register int loops;
    register int i;
    register int ntodosave;

    struct via *vp			= via2_addr;
    int s;
    int block;				/* current block of request */
    int endblock;			/* stop BEFORE this one */
    int track;
    int starting = 0;
    int stat;

    ds = fd_drive;
    dp = ds->dp;

    if (bp->b_bcount % 512) {		/* r/w must be mult of 512 */
	bp->b_error = ENXIO;
	bp->b_flags |= B_ERROR;
	return(0);
    }

    bp->b_resid = bp->b_bcount;

    /* Start the motor if it isn't already running. */

    if ((stat = (*fd_chip->status)()) & S_MOTOROFF) {
	starting = 1;
	(void)(*dp->motoron)();		/* setup and start motor */

	/* Wait an initial time to ensure that READY is valid, and to
	 * let motor inrush current go away before seeking.
	 */

	fd_sleep(5);			/* wait 64-80 ms */
    }

    ++fd_meter.reqcount;		/* count total R/W requests */

    (void)(*dp->geometry)(&geom,0);	/* get tracks/cylinder */

    /* Get starting cylinder and seek direction. */

    rc = startcyl(bp,info);

    switch (rc) {
	case -1 :
		    TRACE(2,("startcyl err,bn=%d\n",bp->b_blkno));
		    bp->b_error = EIO;		/* invalid */
		    goto error;
	
	case  1 :
		    if (bp->b_flags & B_READ) {
			goto nomore;		/* read gets EOF */
		    } else {
			bp->b_error = ENXIO;	/* exactly at end */
			goto error;
		    }
    }

    /* Compute starting/ending blocks too: */
    if (info->dir > 0) {
	block = bp->b_blkno;
	endblock = block + (bp->b_bcount >> 9);
    } else {
	endblock = bp->b_blkno - 1;
	block = endblock + (bp->b_bcount >> 9);
    }

    /* Do blockcount metering: */
    loops = 0;
    for (;;) {
	if (ABS(endblock - block) <= fd_blkrange[loops]) {
	    break;
	}
	loops++;
    }
    ++fd_meter.blkcount[loops];

    if (ds->curcyl != info->start) {	/* meter off-cyl request */
	++fd_meter.offcyl;
    }

    /* Now do the entire request: */

    do {

	/* Decide what's to be done on the current track. */

	fd_ntodo = filltodo(bp,block,endblock,&track,info->dir);
	switch (fd_ntodo) {
	    
	    case -1 :			/* an error */
			TRACE(2,("filltodo err; bn=%d,",bp->b_blkno));
			TRACE(2,("blk=%d,endblk=%d\n",block,endblock));
			bp->b_error = EIO;
			goto error;

	    case  0 :			/* all done */
			goto nomore;

	    default :			/* some to do */
			block += fd_ntodo * info->dir;
			ntodosave = fd_ntodo;
	}

	/* Seek. */

	if ((rc = fd_mb_seek(info->start)) < 0) {	/* start the seek */
	    TRACE(2,("seek failed: cyl %d, err %d\n",info->start,rc));
	    bp->b_error = EIO;
	    goto error;
	}

	/* We now wait for the motor to come up to speed and for the
	 * seek to near completion. If starting the motor for a write,
	 * we wait at least 300ms for +-1.5% speed stability.
	 */
	if (starting) {
	    starting = 0;

	    if (!(bp->b_flags & B_READ)) {
		fd_sleep(MAX(20,rc));		/* write:at least 300 ms */
	    } else {				/* a read */
		if (rc != 0) {
		    fd_sleep(rc);		/* read:just wait for seek */
		}
	    }

	} else {				/* not starting, just seek */
	    if (rc != 0) {
		fd_sleep(rc);			/* wait expected seektime */
	    }
	}

	(void)fd_mb_waitready();

	/* Do this track until we succeed or fail hard. */

	fd_recals = fd_reseeks = fd_stayspl = fd_shortskip = 0;

	do {

	    (void)(*fd_chip->seltrack)(track); /* select proper head */

	    s = SPLFD();

	    rc = fd_dotrack(s,info->start,bp->b_flags & B_READ); /* do track */

	    /* In case dotrack didn't clean up, we will. */
	    vp->regb |= BUSENABLE;	/* allow NUBUS DMA */
	    splx(s);			/* allow interrupts */

	    i = ntodosave - fd_ntodo;	/* blocks just done */
	    bp->b_resid -= 512 * i;	/* bytes just done */
	    ntodosave -= i;

	    rc = errretry(rc,info->start);

	} while (rc > 0);		/* retry till hard failure */

	if (rc < 0) {			/* unrecoverable */
	    bp->b_error = EIO;
	    goto error;
	}

	/* Bump to next cylinder if necessary. */

	TRACE(6,("did cyl=%d,trk=%d,dir=%d\n",info->start,track,info->dir));
	if ((info->dir == -1 && track == 0) || 
	    (info->dir ==  1 && track == geom.maxtrk)) {
	    TRACE(6,("cyl++; stop cyl=%d\n",info->end));
	    info->start += info->dir;
	}

    } while (info->start != info->end);

nomore:;
    bp->b_error = 0;			/* good completion */
    bp->b_flags &= ~B_ERROR;
    goto done;

error:;
    bp->b_resid = bp->b_bcount;		/* entire request always fails */
    bp->b_flags |= B_ERROR;

done:;
    s = SPLFD();
    ds->timeout = fd_motordelay;
    ds->timertype = T_MOTOROFF;		/* schedule the motor turnoff */
    splx(s);

    return(0);
}

/*----------------------------------*/
void
fd_cleartodo()
{
    register struct todo *tp;

    for (tp = &fd_todo[0]; tp < &fd_todo[MAXSECT]; tp++) {
	tp->wanted = 0;
	tp->seen   = 0;
	tp->errcnt = 0;
    }
}

#define MAXHIST 200

short showhist = 0;			/* nonzero to run it */

    struct {
	short sect;
	short skip;
    } hist[MAXHIST];

/*----------------------------------*/
/* Read or write sectors this track.
 *
 * We are called at high SPL so we can return at that SPL.  This makes
 * the code structure easier to read, allowing a return() anywhere.
 *
 * Return values:
 *	 0 for OK,
 *	+1 for bad sector
 *	-1 suggest reseek
 *	-2 suggest recal
 *	-3 couldn't find a sector
 *
 * Assert: drive already selected, running, on cyl, track selected.
 */
int
fd_dotrack(s,cyl,read)
int s;					/* entry spl level */
int cyl;				/* desired cylinder for checking */
int read;
{

    void delay_100us();
    void delay_ms();
    void reset_todo();
    static void printhist();

    static struct addr a;

    register struct via *vp = via2_addr;
    register struct driveparams *dp = fd_drive->dp;
    register struct todo *tp;
    register int rc;
    register int nh;

    int consec_bad = 0;

    reset_todo();			/* clear error & seen values */

    if (showhist) {
	nh = 0;
    } else {
	nh = MAXHIST + 1;		/* shuts it off */
    }

    do {

	vp->regb |= BUSENABLE;		/* allow NUBUS DMA */
	if (!fd_stayspl) {
	    splx(s);			/* allow interupt if pending */
	}

	(void)SPLFD();			/* prevent interrupts */
	vp->regb &= ~BUSENABLE;		/* lock out NUBUS DMA */

	/* Note: After return from rdadr, we don't have all day to
	 * decide if we want to read the data sector that follows
	 * the header.  On GCR, we have ~100uSec. MFM is ~350uS.
	 */

	rc = (*dp->rdadr)(&a);		/* get rotational position */

	if (rc) {
	    TRACE(3,("rdadr error %d\n",rc));
	    fd_meter.addrerrors++;	/* meter them */
	    if (++consec_bad > fd_tune.addrretry) {
		TRACE(2,("> %d consec rdadr errors\n",fd_tune.addrretry));
		rc = -2;		/* address error: recalibrate */
		goto done;
	    } else {
		continue;		/* keep trying */
	    }
	}

	if (a.cyl != cyl) {
	    TRACE(2,("seek error: want %d, got %d\n",cyl,a.cyl));
	    if (a.cyl > 0 && a.cyl < 80) {
		fd_drive->curcyl = a.cyl;	/* nasty side-effect, but... */
		rc = -1;		/* seek error: reseek */
		goto done;
	    } else {
		rc = -2;		/* bogus cyl? recal */
		goto done;
	    }
	}

	consec_bad = 0;
    
	tp = &fd_todo[a.sector];

	if (++tp->seen > fd_tune.maxrevs) {
	    TRACE(2,("too many revs\n"));
	    rc = -3;			/* inform higher authority */
	    goto done;
	}

	if (tp->wanted && tp->errcnt <= fd_tune.sectretry) {
	    if (read) {
		rc = (*dp->read)(tp->buf);
	    } else {
		rc = (*dp->write)(tp->buf,a.sector);
	    }

	    if (rc == 0) {
		tp->wanted = 0;		/* this one's done */
		fd_ntodo--;
	       TRACE(6,("%c c%d t%d s%d\n",(read?'R':'W'),cyl,a.head,a.sector));

		if (nh < MAXHIST) {
		    hist[nh].sect = a.sector;
		    hist[nh].skip = 0;
		    nh++;
		}

	    } else {			/* other error */
		TRACE(3,("I/O err %d:c%d t%d s%d\n",rc,
					    a.cyl,a.head,a.sector));
		fd_meter.ioerrors++;
		if (++tp->errcnt > fd_tune.sectretry) {
		    TRACE(2,(">%d I/O errs\n",fd_tune.sectretry));
		    rc = 1;
		    goto done;
		}
	    }
	    /* After we've written a sector, we may not be able to
	     * do the next one (e.g. 1-to-1), so we may delay until
	     * we get two sectors away (2-to-1).
	     */
	    if (!read && fd_ntodo > 0) {	/* gonna write more */
		if (!(*dp->wrt1to1)()) {	/* can't write 1:1 */
		    vp->regb |= BUSENABLE;	/* allow NUBUS DMA */
		    splx(s);			/* allow interupt if pending */
		    TRACE(6,("wrtskip aft c%d t%d s%d\n",cyl,a.head,a.sector));
		    delay_ms((*dp->sectortime)() - fd_shortskip);
		}
	    }

	} else {				/* not wanted...skip sector */

	    /* If we don't want this sector, we open things up while it
	     * goes by, then lock down again to look at the next one.
	     * This delay should work for all formats.
	     */
	    vp->regb |= BUSENABLE;		/* allow NUBUS DMA */
	    if (!fd_stayspl) {
		splx(s);			/* allow interupt if pending */
	    }
	    TRACE(6,("skip c%d t%d s%d\n",cyl,a.head,a.sector));
	    delay_ms((*dp->sectortime)() - fd_shortskip);

	    if (nh < MAXHIST) {
		hist[nh].sect = a.sector;
		hist[nh].skip = 1;
		nh++;
	    }
	}

    } while (fd_ntodo > 0);

    vp->regb |= BUSENABLE;		/* allow NUBUS DMA */

done:;

    if (nh <= MAXHIST) {
	printhist(nh);
    }

    return(rc);				/* all done, return results */
}

/*----------------------------------*/
static void
printhist(nh)
register int nh;
{
    register int max = 0;
    register int nl = 0;
    register struct todo *tp;

    for (tp = &fd_todo[0]; tp < &fd_todo[12]; tp++) {
	if (tp->seen > max) {
	    max = tp->seen;
	}
    }

    printf("maxrevs=%d\n",max);

    for (max = 0; max < nh; max++) {
	printf("%s ",hist[max].skip ? "sk" : "rw");
	if (hist[max].sect < 10) {
	    printf(" ");
	}
	printf("%d  ",hist[max].sect);
	if (++nl > 11) {
	    nl = 0;
	    printf("\n");
	}
    }
    printf("\n");
}

/*----------------------------------*/
static
int errretry(rc,cyl)
register int rc;			/* what fd_dotrack() returned */
register short cyl;
{

    void fd_sleep();

    register int time = 0;		/* seek time */

    switch (rc) {

	case  0 :				/* no error */
		    return(0);

	case  1 :				/* bad sector */
		    return(-1);

	case -1 :				/* reseek suggested */

		    /* Reseek is only useful if we didn't recal. */

		    if (fd_recals == 0 && ++fd_reseeks <= fd_tune.maxreseek) {

			TRACE(4,("reseek\n"));

			if ((time = fd_mb_seek(cyl)) < 0) { /* do the reseek */
			    TRACE(2,("reseek failed:cyl %d err %d\n",cyl,time));
			    return(-1);		/* it failed */
			}
			break;			/* it succeeded */
		    }

		    /* fallthrough to recal if already tried reseek */

recal:;
	case -2 :				/* recal suggested */

		    if (++fd_recals > fd_tune.maxrecal) {
			return(-1);		/* too many recals tried */
		    }

		    TRACE(4,("recal\n"));

		    if ((time = fd_mb_recal()) < 0) {	/* recalibrate */
			TRACE(2,("recal failed:cyl %d err %d\n",cyl,time));
			return(-1);		/* recal failed */
		    }

		    if (fd_mb_waitready() & S_NOTREADY) {
			TRACE(2,("dropped ready\n"));
			return(-1);		/* drive not ready anymore */
		    }

		    if ((time = fd_mb_seek(cyl)) < 0) {	/* do a reseek */
			TRACE(2,("reseek failed: cyl %d, err %d\n",cyl,time));
			return(-1);		/* it failed */
		    }
		    fd_reseeks = 0;
		    break;

	/* Recovery for "can't find sector" errors goes like this:
	 *	1. First, set fd_shortskip to limit delay time on sector-skips;
	 *	2. Next time, try staying SPL'ed in fd_dotrack;
	 *	3. 3rd time, just do a recalibrate.
	 *
	 * This handles CPU speed variations (e.g. floppy root mount before
	 * cache is enabled, and heavily video-loaded IIci) as well as high
	 * interrupt loading.
	 */

	case -3 :				/* couldn't find sector(s) */
	    
		    if (fd_shortskip == 0) {	/* first time, shorten skip */
			fd_shortskip = 3;	/* shorten by 3 ms */
			TRACE(3,("trying shortskip %d\n",fd_shortskip));
		    } else {
			if (!fd_stayspl) {	/* stay SPL'ed in fd_dotrack */
			    fd_stayspl = 1;
			    TRACE(3,("trying stayspl\n"));
			} else {
			    fd_shortskip = fd_stayspl = 0;
			    TRACE(3,("trying recal\n"));
			    goto recal;		/* just recalibrate now */
			}
		    }
		    break;
    }

    if (time > 0) {
	fd_sleep(time);				/* wait for seek */
    }

    if (fd_mb_waitready() & S_NOTREADY) {	/* wait for ready */
	TRACE(2,("dropped ready\n"));
	return(-1);				/* drive not ready anymore */
    }

    return(1);					/* advise retrying now */
}

/*----------------------------------*/
/* Fill in the list of sectors to do on this track. */
static int				/* returns num sectors to do */
filltodo(bp,startblock,endblock,track,dir)
register struct buf *bp;
register int startblock;		/* starting block */
register int endblock;			/* stop BEFORE this one */
register int *track;			/* returned track we're doing */
register int dir;			/* +- 1 */
{

    static struct geom geometry;

    register struct todo *tp;
    register struct geom *gp = &geometry;
    register int block = startblock;

    if ((*fd_drive->dp->geometry)(gp,block)) {;	/* do first block */
	return(-1);			/* error */
    }

    fd_cleartodo();			/* clear the list */

    *track = gp->track;			/* the track we're doing */

    while (block != endblock) {		/* start grinding blocks */
	
	/* Try the next sector and see if we stay on the same track. */

	/* Put current sector into the todo list: */
	tp = &fd_todo[gp->sector];

	tp->wanted = 1;
	tp->buf = bp->b_un.b_addr + ((block - bp->b_blkno) << 9);

	gp->sector += dir;
	block += dir;
	if (gp->sector < 0 || gp->sector >= gp->spt) {
	    return(ABS(block - startblock));
	}
    }

    return(ABS(block - startblock));		/* sector count this track */
}

/*----------------------------------*/
static void
reset_todo()
{
    register struct todo *tp;

    for (tp = &fd_todo[0]; tp < &fd_todo[MAXSECT]; tp++) {
	tp->seen   = 0;
	tp->errcnt = 0;
    }
}

/*----------------------------------*/
/* Determine cylinder range and direction for this request. */
static int				/* 0 = OK, -1 = error */
startcyl(bp,info)
register struct buf *bp;
register struct seekinfo *info;
{
    /* The initial cylinder selection algorithm works as follows:
     *
     * - If the head is at a lower cylinder than the lowest cylinder in the
     *  request, just seek "up" to the lowest requested cylinder.
     * - If the head is at a higher cylinder than the highest cylinder in the
     *  request, just seek "down" to the highest requested cylinder.
     * - If the head is within the requested area, we seek to the lowest or
     *  highest requested cylinder, depending on time required. This is usually
     *  the shortest seek distance. We will, however, prefer to seek in the
     *  direction which avoids crossing a speedgroup (since motor settling
     *  time is expensive).
     */

    static struct geom geometry;

    register struct geom *gp = &geometry;
    register struct driveparams *dp = fd_drive->dp;
    register short start;
    register int rc;

    rc = (*dp->geometry)(gp,bp->b_blkno);	/* for lo block */
    if (rc) {
	return(rc);			/* bncalc was unimpressed */
    }
    start = gp->cyl;

    /* If the end of the request is off the end of the disk,
     * we truncate the request until it fits.
     */

    do {
	rc = (*dp->geometry)(gp,(bp->b_blkno + (bp->b_bcount >> 9) - 1));
	if (rc) {
	    bp->b_bcount -= 512;	/* truncate it back */
	}
    } while (rc);

    /* If head is outside requested range, just seek to either end. */

    if (fd_drive->curcyl <= start) {	/* go up to lowest cyl of req */
	info->dir = 1;

    } else {
	if (fd_drive->curcyl >= gp->cyl) { /* go down to highest cyl of req */
	    info->dir = -1;

	} else {			/* get best way to either end */
	    info->dir = (*dp->optseek)(start,gp->cyl);
	}
    }
    switch (info->dir) {

	case -1 :				/* down from highest of req */
		    info->start = gp->cyl;
		    info->end   = start - 1;
		    return(0);
	case  1 :				/* up from lowest of req */
		    info->start = start;
		    info->end   = gp->cyl + 1;
		    return(0);
	case  0 :				/* invalid */
		    return(-1);
    }
#ifdef LINT
    return(0);
#endif
}
