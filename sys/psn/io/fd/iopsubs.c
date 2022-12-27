/*
 * @(#)iopsubs.c  {Apple version 1.2 90/01/12 15:12:39}
 *
 * Copyright (c) 1987, 1988, 1989 by Apple Computer, Inc.
 * All Rights Reserved.
 *
 * THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF APPLE COMPUTER, INC.
 * The copyright notice above does not evidence any actual or
 * intended publication of such source code.
 */

#if !defined(lint) && !defined(NO_SCCS_IDS)
static char _sccsid[]="@(#)iopsubs.c  {Apple version 1.2 90/01/12 15:12:39}";
#endif

#include "fd.h"
#include <sys/fdioctl.h>
#include <sys/swimiop.h>
#include <sys/sysmacros.h>
#include <sys/buf.h>
#include <sys/param.h>

    extern struct drivestatus *fd_drive;
    extern struct drivestatus fd_status[];
    extern int fd_minor;

/*--------------------------------*/
int
fd_iop_eject()				/* IOP */
{
    register struct drivestatus *ds;
    register struct iopreq *req;
    register struct cmd_eject *msg;
    register int s;

    ds = fd_drive;

    /* Get the status: */

    req = &ds->req;
    msg = (struct cmd_eject *)&ds->msg[0];
    msg->code = cc_eject;
    msg->drive = fd_minor + 1;		/* IOP wants drives 1 & 2 */
    req->msg = (caddr_t)msg;
    req->msglen = sizeof(struct cmd_eject);
    req->reply = req->msg;		/* the reply message buffer */
    req->replylen = sizeof(struct cmd_eject);
    if (fd_iopsend(req)) {		/* send the message, wait for reply */
	return(1);			/* IOP manager complaint */
    }

    /* We clear hasdisk and event SPL'ed to prevent pollinsert() from
     * coming in while they're not set consistently.  That would cause
     * pollinsert() to think the event was already posted.
     */

    s = spl7();
    ds->hasdisk = 0;
    ds->event = 0;
    splx(s);

    return(0);
}

/*--------------------------------*/
int
fd_iop_enable(drive)			/* IOP */
int drive;
{
    fd_minor = drive;			/* simply remember it for later use */
    fd_drive = &fd_status[drive];

    return(0);
}

/*--------------------------------*/
int
fd_iop_format(stat,pp,cmd)		/* IOP */
register int stat;
register struct diskformat *pp;
register int cmd;		/* CMD_FORMAT, CMD_FMTONLY, CMD_VFYONLY */
{
    register struct drivestatus *ds;
    register struct iopreq *req;
    register struct cmd_format *msg;
    register int rc;
    register int fmt;
    register int i;

    rc = fd_val_format(stat,pp);	/* validate the requested density */
    if (rc > 0) {			/* wrongo */
	return(rc);
    }

    ds = fd_drive;
    req = &ds->req;

    /* Format it. */
    msg = (struct cmd_format *)&ds->msg[0];
    msg->drive = fd_minor + 1;		/* IOP wants drives 1 & 2 */
    msg->p.f.hdrbyte = msg->p.f.interleave = 0;
    msg->p.f.databuf = msg->p.f.tagbuf = (caddr_t)0;

    if (ds->density == FMT_GCR400) {	/* convert our fmt code to IOP code */
	fmt = fmtkind_400K;
    } else {
	if (ds->density == FMT_GCR800) {
	    fmt = fmtkind_800K;
	} else {
	    if (ds->density == FMT_MFM720) {
		fmt = fmtkind_720K;
	    } else {
		fmt = fmtkind_1440K;
	    }
	}
    }
    msg->p.f.fmt = fmt;

    req->msg = (caddr_t)msg;
    req->reply = req->msg;		/* the reply message buffer */

    if (cmd == CMD_FORMAT || cmd == CMD_FMTONLY) {

	msg->code = cc_format;
	req->msglen = sizeof(struct cmd_format);
	req->replylen = sizeof(struct cmd_format);

	if (fd_iopsend(req)) {		/* send the msg, wait for reply */
	    return(EIO);		/* IOP manager complaint */
	}
	if (msg->error) {
	    return(EIO);
	}
    }

    /* Now verify it. */

    if (cmd == CMD_FORMAT || cmd == CMD_VFYONLY) {
	msg->code = cc_fmtverify;
	req->msglen = sizeof(struct cmd_fmtverify);
	req->replylen = sizeof(struct cmd_fmtverify);
	if (fd_iopsend(req)) {		/* send the msg, wait for reply */
	    return(EIO);		/* IOP manager complaint */
	}
	if (msg->error) {
	    return(EIO);
	}
    }

    return(0);
}

/*--------------------------------*/
int
fd_iop_getformat(fmt)			/* IOP */
register short *fmt;
{
    extern struct fd_meter fd_meter;
    register struct drivestatus *ds;
    register struct iopreq *req;
    register struct cmd_status *msg;
    register struct drvstatus *sp;

    ds = fd_drive;

    req = &ds->req;

    /**************************
     * A workaround here, because the IOP driver doesn't determine the
     * format of the diskette until we do read/write type operations.
     * We issue a deliberate bad readverify, which causes the IOP driver
     * to do its internal "SetUpDrive" function, determining the format
     * before it gives us the paramErr error without reading any data.  We
     * give it a deliberate wrong block to get it done instantly.
     *
     * NB: This entire block can be yanked if/when it is no longer needed.
     */

    {
	register struct cmd_rdverify *rvmsg;

	rvmsg = (struct cmd_rdverify *)&ds->msg[0];
	rvmsg->code = cc_rdverify;
	rvmsg->drive = fd_minor + 1;	/* IOP wants drives 1 & 2 */
	rvmsg->block = 0x7fffffff;	/* ridiculous to cause error */
	rvmsg->buf = 0;			/* never written on rdverify anyway */
	rvmsg->nblks = 0;		/* don't read any, you fool */
	req->msg = (caddr_t)rvmsg;
	req->msglen = sizeof(struct cmd_rw);
	req->reply = req->msg;		/* the reply message buffer */
	req->replylen = sizeof(struct cmd_rw);
	if (fd_iopsend(req)) {		/* send the msg, wait for reply */
	    return(EIO);		/* IOP manager complaint */
	}
    }
    /**************** end of workaround ***********/

    msg = (struct cmd_status *)&ds->msg[0];
    msg->code = cc_status;
    msg->drive = fd_minor + 1;		/* IOP wants drives 1 & 2 */
    req->msg = (caddr_t)msg;
    req->msglen = sizeof(struct cmd_status);
    req->reply = req->msg;		/* the reply message buffer */
    req->replylen = sizeof(struct cmd_status);
    if (fd_iopsend(req)) {		/* send the msg, wait for reply */
	return(EIO);			/* IOP manager complaint */
    }

    if (msg->error) {
	if (msg->error == gcrOnMFMErr) {
	    fd_meter.fmt_illegal++;
	    return(EINVAL);
	} else {
	    return(EIO);
	}
    }

    sp = (struct drvstatus *)(&msg->status);
    switch (sp->curformat) {

	case fmt_400K:
			*fmt = FMT_GCR400;
			fd_meter.fmt_gcr400++;
			break;

	case fmt_800K:
			*fmt = FMT_GCR800;
			fd_meter.fmt_gcr800++;
			break;

	case fmt_720K:
			*fmt = FMT_MFM720;
			fd_meter.fmt_mfm720++;
			break;

	case fmt_1440K:
			*fmt = FMT_MFM1440;
			fd_meter.fmt_mfm1440++;
			break;

	default:
			*fmt = FMT_UNFORMATTED;
			fd_meter.fmt_unformatted++;
			break;
    }

    return(0);
}

/*--------------------------------*/
int
fd_iop_init()				/* IOP */
{
    extern struct msg_insert fd_swimmsg;

    int fd_iop_incoming();
    int fd_iopreturn();

    static struct iopreq rcvreq;

    register struct drivestatus *ds;
    register struct iopreq *req;
    register struct cmd_init *cmdi;
    register struct cmd_cachectl *cmdp;
    register int i;

    printf(" IOP SWIM,");

    /* Generic initialization of the IOP request packets: */

    for (i = 0; i < MAXDRIVES; i++) {
	req = &fd_status[i].req;
	req->iop = SWIM_IOP;		/* the SWIM IOP */
	req->chan = SWIM_CHAN;		/* SWIM driver channel */
	req->req = IOPCMD_SEND;		/* usually sending a message */
	req->handler = fd_iopreturn;	/* the handler--returns there */
    }

    /* Initialize the SWIM IOP driver: */

    ds = &fd_status[0];			/* use the req for drive 0 */

    req = &ds->req;
    cmdi = (struct cmd_init *)&ds->msg[0];
    cmdi->code = cc_init;
    req->msg = (caddr_t)cmdi;
    req->msglen = sizeof(struct cmd_init);
    req->reply = req->msg;		/* the incoming message buffer */
    req->replylen = sizeof(struct cmd_init);
    if (fd_iopsend(req)) {		/* send the msg, wait for reply */
	return(1);			/* IOP manager complaint */
    }

    /* Install a receiver for the Insert, Eject and Statuschange messages: */

    req = &rcvreq;
    req->req = IOPCMD_ADDRCVR;		/* install a receiver */
    req->iop = SWIM_IOP;		/* the SWIM IOP */
    req->chan = SWIM_CHAN;		/* SWIM driver channel */
    req->msglen = sizeof(struct msg_insert);
    req->reply = (caddr_t)&fd_swimmsg;	/* the incoming message buffer */
    req->replylen = sizeof(struct msg_insert);
    req->handler = fd_iop_incoming;	/* the handler */

    if (fd_iopsend(req)) {		/* send the msg, wait for reply */
	return(1);			/* IOP manager complaint */
    }

    /* Enable cacheing. */

    req = &ds->req;
    cmdp = (struct cmd_cachectl *)&ds->msg[0];
    cmdp->code = cc_cachectl;
    cmdp->enable = cmdp->install = 1;
    req->msg = (caddr_t)cmdp;
    req->msglen = sizeof(struct cmd_cachectl);
    req->reply = req->msg;		/* the incoming message buffer */
    req->replylen = sizeof(struct cmd_startpoll);
    if (fd_iopsend(req)) {		/* send the msg, wait for reply */
	return(1);			/* IOP manager complaint */
    }
    if (cmdp->error) {
	printf("\nfd: error %d on cachectl msg.\n",cmdp->error);
	return(1);
    }

    return(0);
}

/*----------------------------------*/
int
fd_iop_rw(bp)
register struct buf *bp;
{
    extern struct fd_meter fd_meter;
    extern int fd_blkrange[];

    static struct geom g;

    register struct drivestatus *ds;
    register struct iopreq *req;
    register struct cmd_rw *msg;
    register int nblocks;
    register int loops;

    bp->b_error = 0;
    bp->b_flags &= ~B_ERROR;

    if (bp->b_bcount % 512) {		/* r/w must be mult of 512 */
	bp->b_error = ENXIO;
	bp->b_flags |= B_ERROR;
	return(0);
    }

    bp->b_resid = bp->b_bcount;

    ++fd_meter.reqcount;		/* count total R/W requests */

    (void)(*ds->dp->geometry)(&g,0);	/* get max blocks this format */

    /* Read gets EOF when running off the end, write gets ENXIO. */

    if (bp->b_blkno == g.blks) {	/* exactly off the end */
	if (bp->b_flags & B_READ) {
	    return(0);
	} else {			/* write gets ENXIO at end */
	    bp->b_error = ENXIO;
	    bp->b_flags |= B_ERROR;
	    return(0);
	}
    }

    nblocks = bp->b_bcount >> 9;	/* blockcount */

    /* Do blockcount metering: */
    loops = 0;
    for (;;) {
	if (nblocks <= fd_blkrange[loops]) {
	    break;
	}
	loops++;
    }
    ++fd_meter.blkcount[loops];

    ds = fd_drive;

    req = &ds->req;
    msg = (struct cmd_rw *)&ds->msg[0];

    if (bp->b_flags & B_READ) {
	msg->code = cc_read;
    } else {
	msg->code = cc_write;
    }
    msg->drive = fd_minor + 1;		/* IOP wants drives 1 & 2 */
    msg->buf   = bp->b_un.b_addr;	/* buffer */
    msg->block = bp->b_blkno;		/* block */
    msg->nblks = nblocks;		/* blockcount */

    req->msg = (caddr_t)msg;
    req->msglen = sizeof(struct cmd_rw);
    req->reply = req->msg;		/* the reply message buffer */
    req->replylen = sizeof(struct cmd_rw);

    if (fd_iopsend(req)) {		/* send the msg, wait for reply */
	bp->b_error = EIO;		/* IOP manager complaint */
	bp->b_resid = bp->b_bcount;	/* entire request always fails */
	bp->b_flags |= B_ERROR;
	return(0);
    }

    bp->b_flags &= ~B_ERROR;		/* assume good completion */
    bp->b_error = 0;
    bp->b_resid = 0;

    if (msg->error) {			/* driver complained */
	bp->b_resid = bp->b_bcount;	/* entire request fails */
	bp->b_flags |= B_ERROR;

	if (msg->error == paramErr) {	/* bad block number */
	    bp->b_error = ENXIO;	/* bad block number */
	} else {			/* all other errors become EIO */
	    bp->b_error = EIO;
	}
    }

    return(0);
}

/*--------------------------------*/
int					/* status is returned */
fd_iop_seldrive(minordev)		/* IOP */
register int minordev;
{
    register struct drivestatus *nds;		/* new drive */

    if ((minordev < 0) || (minordev > 1)) {
	panic("fd_iop_seldrive: illegal minor dev");
    }

    if (minordev != fd_minor) {			/* must switch drives */
	fd_minor = minordev;
	fd_drive = &fd_status[minordev];	/* new drive pointers */
    }

    (void)fd_iop_enable(minordev);		/* select & enable the drive */

    return(fd_iop_status());
}

/*--------------------------------*/
int
fd_iop_setdensity(ds)			/* IOP */
register struct drivestatus *ds;
{
    extern struct driveparams fd_d_params[];
    register int stat;

    /* If the disk is unformatted, we pretend it's either GCR800K or
     * MFM1440K, depending on drive & media, to setup our tables correctly.
     * The motor turnoff is not needed since we disable when mode-setting.
     */

    if (ds->density == FMT_UNFORMATTED) {

	stat = fd_iop_status();		/* get the status */

	if (stat & S_FDHD && !(stat & S_1MBMEDIA)) {
	    ds->dp = &fd_d_params[FMT_MFM1440 - 1];	/* set 1440K MFM */
	} else {
	    ds->dp = &fd_d_params[FMT_GCR800  - 1];	/* set 800K GCR */
	}

    } else {
	ds->dp = &fd_d_params[ds->density - 1];	/* set format params */
    }

    return(0);
}

/*--------------------------------*/
static int
decode_stat(sp)
register struct drvstatus *sp;
{

    /* Convert the IOP SWIM status to S_xxxx bit format.  Most
     * bits are not applicable, but routines examining them won't
     * be running when we're operating with an IOP anyway.
     */

    register int status = 0;

    if (sp->installed == 0xff) {
	status = S_NODRIVE;
    } else {
	if (sp->mfmdrive == 0xff) {
	    status |= S_FDHD;
	    status |= S_TWOSIDED;
	}
	if (sp->sides == 0x80) {
	    status |= S_TWOSIDED;
	}
	if (sp->hasdisk < 1 || sp->hasdisk > 3) {
	    status |= S_NODISK;
	} else {
	    if (sp->wprot != 0xff) {
		status |= S_WRTENAB;
	    }
	    if ((sp->allowfmt & fmt_1440K) == 0) {
		status |= S_1MBMEDIA;
	    }
	}
    }

    return(status);
}

/*--------------------------------*/
int
fd_iop_status()				/* IOP: get & convert to S_xxx form */
{

    register struct drivestatus *ds;
    register struct iopreq *req;
    register struct cmd_status *msg;
    register int status;

    ds = fd_drive;			/* current drive */
    status = 0;

    /* Get the status: */

    req = &ds->req;
    msg = (struct cmd_status *)&ds->msg[0];
    msg->code = cc_status;
    msg->drive = fd_minor + 1;		/* IOP wants drives 1 & 2 */
    req->msg = (caddr_t)msg;
    req->msglen = sizeof(struct cmd_status);
    req->reply = req->msg;		/* the reply message buffer */
    req->replylen = sizeof(struct cmd_status);
    if (fd_iopsend(req)) {		/* send the msg, wait for reply */
	status = S_NODRIVE;		/* oh, well... */
	return(status);			/* IOP manager complaint */
    }
    if (msg->error) {
	status = S_NODRIVE;		/* oh, well... */
	return(status);
    }

    return(decode_stat((struct drvstatus *)(&msg->status)));
}

/*--------------------------------*/
int
fd_iopsend(req)
register struct iopreq *req;
{
    register int ret;
    register int s;

    s = SPLIOP();
    ret = ioprequest(req);		/* send the message */

    if (ret) {
	printf("\nfd: IOP manager request @ %x failed, err %d).\n",req,ret);
	splx(s);
	return(1);
    }
    
    while (req->flags == REQ_BUSY) {	/* sleep if busy being sent */
	sleep((caddr_t)req,PRIBIO);
    }

    splx(s);

    return(0);
}

/*--------------------------------*/
int
fd_iopreturn(req)			/* called at IOP manager's SPL */
register struct iopreq *req;
{
    wakeup((caddr_t)req);

    return(0);
}
/*--------------------------------*/
/* We're called here when an unsolicited message comes in from the SWIM
 * IOP driver.  We're at the IOP manager's SPL here, so we use care.
 *
 * We ignore the eject button and status-change messages.
 */
int
fd_iop_incoming(req)
register struct iopreq *req;
{
    extern int (*postDIroutine)();
    static struct iopreq replyreq;

    register struct drivestatus *ds;
    register struct msg_insert *msg;
    register int stat;
    register int dn;

    msg = (struct msg_insert *)req->reply;	/* incoming message */
    dn = msg->drive - 1;
    ds = &fd_status[dn];

    msg->error = 0;			/* assume we accepted it */

    if (msg->code == 1) {		/* disk insertion. Ignore others */

	/* Simply hand control to the disk-insert-event generating routine: */


	if (!ds->event) {
	    stat = fd_trstat(decode_stat((struct drvstatus *)(&msg->status)));
	    (void)(*postDIroutine)(makedev(major(ds->dev),dn << 4),stat);
	    ds->event = 1;
	}
    }

    /* Reply to the incoming message. */

    replyreq = *req;			/* copy the entire incoming request */
    replyreq.req = IOPCMD_RCVREPLY;
    replyreq.flags = 0;

    (void)ioprequest(&replyreq);
    
    return(0);
}
