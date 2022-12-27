/*
 * @(#)start.c  {Apple version 1.3 89/12/12 14:03:50}
 *
 * Copyright (c) 1987, 1988, 1989 by Apple Computer, Inc.
 * All Rights Reserved.
 *
 * THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF APPLE COMPUTER, INC.
 * The copyright notice above does not evidence any actual or
 * intended publication of such source code.
 */

#if !defined(lint) && !defined(NO_SCCS_IDS)
static char _sccsid[]="@(#)start.c  {Apple version 1.3 89/12/12 14:03:50}";
#endif

#include "fd.h"
#include <sys/buf.h>
#include <sys/fdioctl.h>

extern	struct drivestatus	 fd_status[];;
extern	struct drivestatus	*fd_drive;
extern	struct buf		 fd_cmdbuf;

struct buf *fd_buf;			/* current active buffer */

/*----------------------------------*/
void
fd_strategy(bp)				/* GENERIC */
register struct buf *bp;
{
    void fd_start();

    register struct buf *sbp;

    /* If we're busy, just queue the buffer to the end of the
     * chain.  We make no attempt at optimizing at all.  We only
     * need to queue buffers when someone is sleeping with the
     * chip busy.
     */

     /* Even on an IOP system, there can only be one active message
      * being processed by the SWIM driver.  If we try to send
      * another, the IOP manager will simply queue it and we'll
      * sleep in fd_iopsend() waiting for it to complete.  So we
      * avoid complicating our life by simply serializing every
      * request here in fd_strategy for both Motherboard and IOP.
      */

    bp->av_forw = (struct buf *)0;		/* none after us */

    if (fd_buf) {				/* queue to end */
	sbp = fd_buf;
	TRACE(8,("enq:head @ %x\n",sbp));
	while (sbp->av_forw) {
	    sbp = sbp->av_forw;			/* scan to end of queue */
	    TRACE(8,("enq:next @ %x\n",sbp));
	}
	TRACE(8,("enq:tail @ %x, adding %x\n",sbp,bp));
	sbp->av_forw = bp;
	bp->av_back = sbp;
	return;
    }

    /* No active buffer. */

    fd_buf = bp;

    TRACE(8,("enq:mt-q @ %x\n",bp));
    bp->av_back = (struct buf *)0;

    sbp = bp;

    do {
	TRACE(8,("deq:svc  @ %x\n",sbp));
	fd_start(sbp);			/* fire this one off */

	fd_buf = sbp->av_forw;		/* possible next buf */
	if (fd_buf) {
	    sbp->av_forw->av_back = (struct buf *)0;
	    sbp->av_forw = (struct buf *)0;
	}

	iodone(sbp);

	sbp = fd_buf;
    } while (sbp);
    TRACE(8,("deq:empty\n"));

}

/*----------------------------------*/
void
fd_start(bp)				/* GENERIC */
register struct buf *bp;
{
    extern struct interface *fd_int;
    static void docommand();

    register struct drivestatus *ds;
    register int minor;
    register int rc;

    minor = (bp->b_dev & FD_DRIVE) ? 1 : 0;
    ds = &fd_status[minor];		/* drive pointers */

    if (ds->harderr) {			/* hard error? Forget it! */
	bp->b_error = EIO;
	bp->b_flags |= B_ERROR;
	TRACE(3,("fd_start:harderr\n"));
	return;				/* return w/o motoroff schedule */
    }

    /* Select and enable the drive: */

    rc = (*fd_int->seldrive)((bp->b_dev & FD_DRIVE) ? 1 : 0);
    if (rc & S_NODRIVE) {		/* couldn't select */
	TRACE(3,("fd_start: couldn't select\n"));
	bp->b_error = EIO;
	bp->b_flags |= B_ERROR;
	return;
    }

    if (bp == &fd_cmdbuf) {		/* (cheap decision here) */
	docommand(bp);			/* do a command */
    } else {				/* do a read or write */
	(void)(*fd_int->rw)(bp);
    }
}

/*--------------------------------*/
static void
docommand(bp)				/* GENERIC */
register struct buf *bp;
{
    register int rc;
    register struct drivestatus *ds = fd_drive;
    register int s;

    bp->b_error = 0;
    bp->b_flags &= ~B_ERROR;

    switch (bp->b_cmd) {

	case CMD_OPEN   :			/* open */
			    rc = fd_cmd_open(bp);
			    break;

	case CMD_EJECT  :			/* eject */
			    (void)(*fd_int->eject)();	/* eject it */
			    rc = 0;
			    break;

	case CMD_FORMAT :			/* format */
	case CMD_FMTONLY :			/* format only */
	case CMD_VFYONLY :			/* verify only */
			    rc = fd_cmd_format(bp);
			    break;

	case CMD_GETSTAT:			/* get status */
			    *((int *)bp->b_params) = (*fd_int->status)();
			    rc = 0;
			    break;
    }

    if (rc) {
	bp->b_error = rc;
	bp->b_flags |= B_ERROR;
    }
}

