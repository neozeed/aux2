/*
 * @(#)deviop.c  {Apple version 1.3 90/03/28 16:09:36}
 *
 * Copyright (c) 1987, 1988, 1989 by Apple Computer, Inc.
 * All Rights Reserved.
 *
 * THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF APPLE COMPUTER, INC.
 * The copyright notice above does not evidence any actual or
 * intended publication of such source code.
 */

#if !defined(lint)
static char _sccsid[]="@(#)deviop.c  {Apple version 1.3 90/03/28 16:09:36}";
#endif

/*
 * IOP device driver for A/UX.  This driver buffers a code resource
 * for later downloading into an iop.
 *
 * Copyright 1989, Apple Computer Inc.  All rights reserved.
 */

#include <sys/types.h>
#include <sys/uconfig.h>
#include <sys/param.h>
#include <sys/sysmacros.h>
#include <sys/errno.h>
#include <sys/file.h>
#include <sys/uio.h>
#include <sys/iopmgr.h>
#include <sys/scciop.h>

#define TRACE(x)  if (deviop_trace) { printf x; }

#define NIOP 1				/* number of IOPs, starting at 0 */
#define	NDRIVERS 8			/* drivers per IOP, incl. kernel */

static struct context {
    struct iopreq	req;		/* iopmgr request */
    u_short		flags;
    u_short		iop;		/* which IOP this is */
    u_char		msg[32];	/* generic IOP message/reply buffer */
    struct dvrinfo {
	caddr_t		buf;		/* driver buffer */
	char		res_type[4];	/* resource type, 4 chars */
	u_short		res_id;		/* resource id */
	u_short		len;		/* driver length */
    } dvr[NDRIVERS];
} diopcontext[NIOP];

#define	OPENED		0x0001		/* we're open */
#define	HAVE_KERNEL	0x0002		/* IOP kernel is up and running */
#define	HAVE_A		0x0004		/* Driver A   is up and running */
#define	HAVE_B		0x0008		/* Driver B   is up and running */
#define	A_ALLOC		0x0010		/* dvr A  channel is owned by someone */
#define	B_ALLOC		0x0020		/* dvr B  channel is owned by someone */
#define	K_ALLOC		0x0040		/* kernel channel is owned by someone */

static short initted = 0;

static u_short allocflags[]	= { A_ALLOC, B_ALLOC, K_ALLOC };
static u_short haveflags[]	= { HAVE_A, HAVE_B };

u_short deviop_trace = 0;


caddr_t kmem_alloc();

/*-------------------------------*/
int
iop_open(dev,flags)
dev_t dev;
int flags;
{
    extern short machineID;
    register struct context *cx;

    if (machineID != MACIIfx) {
	return(ENXIO);
    }

    if (!(flags & FWRITE)) {
	return(ENXIO);
    }

    if (!initted) {			/* get initted */
	if (doinit()) {
	    return(EIO);
	}
    }

    if (minor(dev) >= NIOP) {
	return(ENXIO);
    }

    cx = &diopcontext[minor(dev)];

    if (cx->flags & OPENED) {		/* only one at a time, guys */
	return(EBUSY);
    }

    cx->flags |= OPENED;

    return(0);
}

/*-------------------------------*/
int
iop_write(dev,uio)
dev_t dev;
register struct uio *uio;
{
    char res_type[4];			/* driver info, temp while deciding */
    u_short res_id;
    u_short len;

    register struct context *cx;
    register caddr_t base;
    register struct dvrinfo *dp;

    if (minor(dev) >= NIOP) {
	return(ENXIO);
    }
    cx = &diopcontext[minor(dev)];

    if  (!(cx->flags & OPENED)) {
	return(EIO);
    }

    /* Get the Resource Type, ID and length and decide what to do. */

    base = uio->uio_iov->iov_base;		/* user's buffer */

    copyin(base,&res_type[0],4);
    base += 4;
    copyin(base,&res_id,sizeof(u_short));
    base += sizeof(u_short);
    copyin(base,&len,sizeof(u_short));
    base += sizeof(u_short);

    if (len == 0 || len > 32767) {		/* max is 32K */
	return(EINVAL);
    }

    /* Find an empty slot for the driver info. */

    for (dp = &cx->dvr[0]; dp < &cx->dvr[NDRIVERS]; dp++) {
	if (dp->res_type[0] == '\0') {
	    break;
	}
    }
    if (dp == &cx->dvr[NDRIVERS]) {		/* table overflow */
	return(ENFILE);
    }

    /* Get a buffer for the driver code.  We round up to 512-byte boundary. */

    dp->buf = kmem_alloc((len + 511) & ~511);
    if (dp->buf == 0) {
	return(ENOMEM);				/* no memory */
    }

    /* Now save the info and copy the code. */

    strncpy(dp->res_type,res_type,4);
    dp->res_id = res_id;
    dp->len = len;

    copyin(base,dp->buf,len);

    uio->uio_resid = 0;
    return(0);
}

/*-------------------------------*/
void
iop_close(dev,flag)
dev_t dev;
int flag;
{
    register struct context *cx;

    if (minor(dev) >= NIOP) {
	return;
    }
    cx = &diopcontext[minor(dev)];

    if  (!(cx->flags & OPENED)) {
	return;
    }

    cx->flags &= ~OPENED;
}

/*-------------------------------*/
/* Allocate "ownership" of an IOP driver (A,B, or kernel). */
int
iop_alloc(iop,driver,wait)
register int iop;
register int driver;
register int wait;			/* nonzero to sleep for it */
{
    register struct context *cx;
    register int s;

    if (iop < 0 || iop > NIOP) {
	return(EINVAL);
    }
    if (driver < DVR_A || driver > DVR_KERNEL) {
	return(EINVAL);
    }

    cx = &diopcontext[iop];

    if (cx->flags & allocflags[driver]) {
	if (wait) {
	    s = SPLIOP();
	    while (cx->flags & allocflags[driver]) {
		TRACE(("sleeping for dvr %d\n",driver));
		sleep((caddr_t)(&cx->flags),PRIBIO);
	    }
	    splx(s);
	} else {
	    return(EBUSY);
	}
    }

    cx->flags |= allocflags[driver];

    return(0);
}

/*-------------------------------*/
/* Download a driver, getting the IOP kernel there too.  This routine
 * assumes the caller is able to sleep, and it grabs ownership of the
 * appropriate driver (via iop_alloc).  The caller must call iop_free()
 * after closing the driver.
 */
int
iopdriver_load(iop,eventreq,driver,code,len)
register int iop;
register struct iopreq *eventreq;	/* (optional) for incoming event msgs */
register int driver;			/* driver: 00=A, 01=B */
register caddr_t code;
register u_short len;
{
    register int rc = 0;
    int kcode;
    int klen;

    /* Grab "ownership" of the IOP kernel. If it needs to be loaded,
     * we do it.  Then get ownership of the appropriate driver and
     * always load it.
     */

    (void)iop_alloc(iop,DVR_KERNEL,1);	/* wait for ownership */

    if (!(diopcontext[iop].flags & HAVE_KERNEL)) {
	rc = iop_getdvr(iop,"iopc",0,&kcode,&klen);
	if (rc)	{
	    TRACE(("iopdvr_load: getdvr err %d\n",rc));
	}
	if (rc == 0) {
	    rc = iop_download(iop,DVR_KERNEL,kcode,klen);
	}
    }
    (void)iop_free(iop,DVR_KERNEL,(struct iopreq *)0,0); /* free it again */

    /* Install the receiver for incoming events. */

    if (rc) {
	TRACE(("iopdvr_load: kernel load err %d\n",rc));
	return(rc);
    }

    if (eventreq) {
	eventreq->req = IOPCMD_ADDRCVR;		/* add a receiver */
	rc = ioprequest(eventreq);
    }

    /* Grab ownership of the driver and keep it till we close. */

    if (rc == 0) {
	rc = iop_alloc(iop,driver,0);	/* don't wait for ownership */
	if (rc == 0) {
	    rc = iop_download(iop,driver,code,len);
	    if (rc) {
		TRACE(("iopdvr_load: driver load err %d\n",rc));
	    }
	} else {
	    TRACE(("iopdvr_load: driver iop_alloc err %d\n",rc));
	}
    }

    /* If something went wrong, we have to remove the incoming
     * receiver (if it was installed) and give up ownership of the driver.
     */

    if (rc == 0) {
	return(0);
    } else {
	if (eventreq && (eventreq->flags & REQ_RCVR)) {
	    (void)iop_free(iop,driver,eventreq,1);
	} else {
	    (void)iop_free(iop,driver,(struct iopreq *)0,0);
	}
	return(rc);
    }
}

/*-------------------------------*/
/* Free "ownership" of an IOP driver. No check is made for permission. */
int
iop_free(iop,driver,eventreq,dealloc)
register int iop;
register int driver;			/* 0=A, 1=B, 2=kernel */
register struct iopreq *eventreq;	/* optional to do IOP_DELRCVR */
int dealloc;				/* nonzero to dealloc the driver */
{
    register struct context *cx;
    register int rc = 0;

    if (iop < 0 || iop > NIOP) {
	return(EINVAL);
    }
    if (driver < DVR_A || driver > DVR_KERNEL) {
	return(EINVAL);
    }

    cx = &diopcontext[iop];

    TRACE(("free driver %d\n",driver));
    if (!(cx->flags & allocflags[driver])) {	/* nobody owns it */
	return(ENOENT);
    }

    if (dealloc) {
	rc = dealloc_dvr(cx,driver);
    }

    if (eventreq && (eventreq->flags & REQ_RCVR)) {
	eventreq->req = IOPCMD_DELRCVR;		/* change the command */
	(void)ioprequest(eventreq);		/* remove it */
    }

    if (rc == 0) {
	cx->flags &= ~allocflags[driver];
	wakeup((caddr_t)(&cx->flags));
    }

    return(rc);
}

/*-------------------------------*/
/* This routine is called by a serial driver to get an IOP driver
 * code resource.
 */
int
iop_getdvr(iop,res_type,res_id,code,len)
register int iop;
register char res_type[];
register u_short res_id;
register caddr_t *code;			/* code address returned */
u_short *len;				/* length returned */
{
    register struct context *cx;
    register struct dvrinfo *dp;

    if (iop < 0 || iop > NIOP) {
	return(EINVAL);
    }
    cx = &diopcontext[iop];

    for (dp = &cx->dvr[0]; dp < &cx->dvr[NDRIVERS]; dp++) {
	if (dp->res_id == res_id && !strncmp(dp->res_type,res_type,4)) {
	    *code = dp->buf;
	    *len = dp->len;
	    return(0);
	}
    }
    return(ESRCH);			/* no such */
}

/*-------------------------------*/
/* This routine is called by a serial driver to get an IOP driver
 * downloaded.  The caller must be able to sleep() when calling here.
 */
int
iop_download(iop,driver,code,len)
register int iop;
register u_short driver;		/* 0=A, 1=B, 2=kernel */
register caddr_t code;
register u_short len;
{
    register struct context *cx;

    if (iop < 0 || iop > NIOP) {
	return(EINVAL);
    }
    cx = &diopcontext[iop];

    switch(driver) {

	case DVR_A:			/* port-A driver */
	case DVR_B:			/* port-B driver */
		if (!(cx->flags & HAVE_KERNEL)) {
		    return(EACCES);
		}
		return(download_driver(cx,driver,code,len));

	case DVR_KERNEL:		/* IOP kernel */

		cx->flags &= ~(HAVE_KERNEL | HAVE_A | HAVE_B);
		return(download_kernel(cx,code));
	
	default:
		return(ENXIO);
    }
}

/*-------------------------------*/
static int
download_kernel(cx,code,len)
register struct context *cx;
register caddr_t code;
{
    register struct iopreq *req;
    register struct kmsg_bypass *bm;
    register struct kreply_bypass *br;
    register u_short count;
    register u_short offset;
    register int rc = 0;

    req = &cx->req;

    /* Tell the IOP manager we want it out of bypass mode to do
     * the other things we want done.  It might not be in bypass
     * so we ignore any error.
     */
    req->req = IOPCMD_NOBYPASS;
    if (rc = iopsend(req)) {
	TRACE(("dnld_knl iopnobypass err %d\n",rc));
    }

    req->req = IOPCMD_STOPIOP;
    if (rc = iopsend(req)) {
	TRACE(("dnld_knl iopstop err %d, ignoring\n",rc));
    }

    req->req = IOPCMD_CLRMEM;
    if (rc = iopsend(req)) {
	TRACE(("dnld_knl iopclrmem err %d\n",rc));
	return(EIO);
    }

    /* The kernel is arranged as a series of code-segments, each
     * of which has a 3-byte header:
     *		char  bytes;		count
     *		u_short offset;		where in IOP memory
     *
     * The end is marked by a zero length byte.
     */
    
    while (count = (*code++) & 0xff) {		/* count */
	offset = *((u_short *)code)++;	/* offset */
	if (rc = download_code(cx,offset,code,count)) {
	    TRACE(("dnld_knl dnld err %d\n",rc));
	    return(EIO);
	}
	code += count;
    }

    /* Start the IOP. */

    req->req = IOPCMD_STARTIOP;
    if (rc = iopsend(req)) {
	TRACE(("dnld_knl iopstart err %d\n",rc));
	return(EIO);
    }

    /* The kernel starts up in bypass mode, so take it out.
     * We must use ClientID of 0xff to do this.
     */

    bm = (struct kmsg_bypass *)req->msg;
    br = (struct kreply_bypass *)bm;

    bm->code = kern_bypass;
    bm->onoff = 0;			/* off */
    bm->clientid = 0xff;
    req->req = IOPCMD_SEND;
    req->msglen = sizeof(struct kmsg_bypass);
    req->replylen = sizeof(struct kreply_bypass);
    TRACE(("dnld_knl sending outofbypass\n"));
    if (iopsend(req)) {
	TRACE(("dnld_knl outofbypass iopsend err %d\n",rc));
	return(EIO);
    }
    if (br->result) {			/* aw, well... */
	TRACE(("dnld_knl outofbypass result %d\n",rc));
	return(EIO);
    }

    cx->flags |= HAVE_KERNEL;		/* kernel is there */

    return(0);
}
	
/*-------------------------------*/
static int
alloc_dvr(cx,driver)			/* allocate a driver */
register struct context *cx;
register int driver;
{
    register struct iopreq *req;
    register struct kmsg_alloc *am;
    register struct kreply_alloc *ar;

    req = &cx->req;
    req->req = IOPCMD_SEND;

    am = (struct kmsg_alloc *)req->msg;
    ar = (struct kreply_alloc *)am;

    am->code = kern_alloc;
    am->driver = driver;
    am->clientid = 1;		/* what the heck... */

    req->msglen = sizeof(struct kmsg_alloc);
    req->replylen = sizeof(struct kreply_alloc);
    TRACE(("alloc_dvr: sending allocate\n"));

    if (iopsend(req)) {
	return(EIO);
    }
    if (ar->result) {		/* couldn't allocate */
	TRACE(("alloc_dvr:allocate err %d\n",ar->result));
	TRACE(("client=%d,addr=%x\n",ar->clientid,ar->addr));
	return(ar->result);
    } else {
	cx->flags |= haveflags[driver];
	return(0);
    }
}
	
/*-------------------------------*/
static int
dealloc_dvr(cx,driver)
register struct context *cx;
register int driver;
{
    register struct iopreq *req;
    register struct kmsg_dealloc *dm;
    register int rc = 0;

    TRACE(("dealloc_dvr\n"));

    req = &cx->req;
    req->req = IOPCMD_SEND;

    dm = (struct kmsg_dealloc *)req->msg;

    dm->code = kern_dealloc;
    dm->driver = driver;
    req->msglen = sizeof(struct kmsg_dealloc);
    req->replylen = sizeof(struct kreply_dealloc);
    if (rc = iopsend(req)) {
	TRACE(("dealloc_dvr: iopsend err %d\n",rc));
	return(EIO);
    }

    rc = ((struct kreply_dealloc *)req->reply)->result;
    if (rc == 0) {
	cx->flags &= ~haveflags[driver];
    }
    return(rc);
}

/*-------------------------------*/
static int
download_driver(cx,driver,code,len)
register struct context *cx;
register u_short driver;
register caddr_t code;
register u_short len;
{
    static u_short dvrbase[] = { DVRABASE, DVRBBASE };

    register struct iopreq *req;
    register int rc = 0;

    req = &cx->req;
    req->req = IOPCMD_SEND;

    /* Allocate the driver. If necessary, deallocate
     * whatever driver is there, then try again.
     */

    if (cx->flags & haveflags[driver]) {
	if (rc = dealloc_dvr(cx,driver)) {
	    TRACE(("dnld_dvr:dvr dealloc err %d\n",rc));
	    return(EIO);
	}
    }

    if (rc = alloc_dvr(cx,driver)) {
	TRACE(("dnld_dvr:dvr alloc err %d\n",rc));
	return(EIO);
    }

    if (rc = download_code(cx,dvrbase[driver],code,len)) {
	TRACE(("dnld_dvr:dvr load err %d\n",rc));
	return(EIO);
    }

    /* Send the init driver message. */

    {
	register struct kmsg_initdvr *im;
	register struct kreply_initdvr *ir;

	im = (struct kmsg_initdvr *)req->msg;
	ir = (struct kreply_initdvr *)im;

	im->code = kern_initdvr;
	im->driver = driver;
	req->req = IOPCMD_SEND;
	req->msglen = sizeof(struct kmsg_initdvr);
	req->replylen = sizeof(struct kreply_initdvr);
	TRACE(("dld_dvr: sending initdvr\n"));
	if (iopsend(req)) {
	    return(EIO);
	}
	if (ir->result) {		/* didn't init */
	    TRACE(("dnld_dvr:dvr init err %d\n",ir->result));
	    TRACE(("ir=%x,im=%x\n",ir,im));
	    return(EIO);
	}
    }

    TRACE(("dld_dvr: done\n"));
    return(0);
}

/*-------------------------------*/
static int
download_code(cx,base,code,len)
register struct context *cx;
register u_short base;			/* where in IOP memory */
register caddr_t code;			/* the code */
register u_short len;
{
    register struct iopreq *req;

    req = &cx->req;

    req->move.type = MV_TO_IOP;
    req->move.iop = req->iop = cx->iop;	/* which IOP */
    req->move.b.buf = code;
    req->move.offset = base;
    req->move.count = len;
    req->req = IOPCMD_MOVE;

    /* Now download it into the IOP: */

    {
	register int i;
	register int c;
	TRACE(("download: %x->%x:%d bytes/",req->move.b.buf,req->move.offset,
					    req->move.count));
	for (i = 0; i < 8; i++) {
	    if (i >= req->move.count) {
		break;
	    }
	    c = req->move.b.buf[i] & 0xff;
	    if (c < 16) {
		TRACE(("0"));
	    }
	    TRACE(("%x ",c));
	}
	TRACE(("\n"));
    }
    if (iopsend(req)) {
	return(-1);
    }

    return(0);
}

/*-------------------------------*/
static int
iopsend(req)
register struct iopreq *req;
{
    register int rc = 0;
    register int s;

    if (rc = ioprequest(req)) {		/* err if it fails */
	return(rc);
    }

    s = SPLIOP();
    while (req->flags & REQ_BUSY) {	/* sleep till done */
	sleep((caddr_t)req,PRIBIO);
    }
    splx(s);

    return(0);
}

/*-------------------------------*/
static int
iopret(req)
register struct iopreq *req;
{
    wakeup((caddr_t)req);

    return(0);
}

/*-------------------------------*/
static int
doinit()
{
    register struct context *cx;
    register struct iopreq *req;
    register int i;

    for (i = 0; i < NIOP; i++) {

	cx = &diopcontext[i];
	cx->iop = i;

	req = &cx->req;
	req->iop = SCC_IOP;
	req->move.iop = SCC_IOP;
	req->chan = SCC_KERN_MSG;
	req->msg = (caddr_t)&cx->msg[0];
	req->reply = req->msg;
	req->handler = iopret;
    }

    return(0);
}
