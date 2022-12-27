/*
 * @(#)iopmgr.c  {Apple version 1.5 90/03/13 12:22:36}
 *
 * Copyright (c) 1987, 1988, 1989 by Apple Computer, Inc.
 * All Rights Reserved.
 *
 * THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF APPLE COMPUTER, INC.
 * The copyright notice above does not evidence any actual or
 * intended publication of such source code.
 */

#if !defined(lint)
static char _sccsid[]="@(#)iopmgr.c  {Apple version 1.5 90/03/13 12:22:36}";
#endif

/*
 * IOP Manager for A/UX.  This driver handles information (data and
 * message) transfers to/from the IOP chips.
 *
 * Copyright 1989, Apple Computer Inc.  All rights reserved.
 *
 * Notes:
 *
 *  1. Within this program, the following definitions apply:
 *	a) "CPU" is our main processor and/or its memory;
 *	b) "send" refers to an information transfer from CPU to IOP;
 *	c) "receive" refers to an information transfer from IOP to CPU;
 */

#include <sys/types.h>
#include <sys/uconfig.h>
#include <sys/iop.h>
#include <sys/iopkernel.h>
#include <sys/iopmgr.h>
#include <sys/var.h>

#define NIOP 2		/* IOPs */

struct chaninfo {			/* per-channel info */
    struct iopreq	*qhead;		/* send-message head */
    struct iopreq	*qtail;		/* send-message tail */
    struct iopreq	*receiver;	/* receive-message handler */
};

struct iopstat {
    struct iop		*addr;		/* ptr to IOP hardware regs */
    struct chaninfo	chaninfo[NCH + 1]; /* channel info */
    struct movemsg	moveinfo;	/* info for move msg */
    u_short		maxsendchan;	/* max send channel */
    u_short		maxrcvchan;	/* max receive channel */

    /* FLAGS */
    u_short		initted:1;	/* IOP initted */
    u_short		bypass:1;	/* bypass mode */
    u_short		dead:1;		/* dead */
    u_short		stopped:1;	/* stopped by IOPREQ_STOPIOP */
} iopstat[NIOP];

static struct iopreq msg1req[NIOP];	/* move-message receiver */

static struct iop *iopaddr[NIOP] = {(struct iop *)IOP0_ADDR,
				    (struct iop *)IOP1_ADDR };

static int	iop_alive();		/* check if IOPs are alive */
static int	iop_add_rcvr();		/* install a receive-message handler */
static int	iop_clrmem();		/* clear IOP memory */
static int	iop_del_rcvr();		/*  delete a receive-message handler */
static int	cmd_into_bypass();	/* put IOP into bypass mode (iopcmd) */
       int	iop_into_bypass();	/* put IOP into bypass mode <call) */
static int	iop_move();		/* move data to/from an IOP */
static int	iop_no_bypass();	/* take IOP out of bypass mode */
static int	iop_patch();		/* patch an IOP */
static int	iop_rcvreply();		/* send receive-message reply to IOP */
static int	iop_send();		/* send a message to an IOP */
static int	iop_startiop();		/* start an IOP after download */
static int	iop_stopiop();		/* stop  an IOP for   download */
       int	iop_init();		/* init the IOP manager */

static u_char	get1();
static void	getn();
static void	putn();
static void	put1();
static void	sendmsg();

       void	timeout();

/*----------------------------*/
int ioprequest(req)
register struct iopreq *req;	/* the request */
{
    register int result = 0;

    if (req == 0) {
	return(EIOPCMD);
    }

    if (req->link) {
	return(EIOPREQBUSY);	/* req is already on a queue ?!? */
    }

    switch (req->req) {
	
	case IOPCMD_ADDRCVR:
			result = iop_add_rcvr(req);
			break;

	case IOPCMD_CLRMEM:
			result = iop_clrmem(req);
			break;

	case IOPCMD_DELRCVR:
			result = iop_del_rcvr(req);
			break;
	
	case IOPCMD_INTOBYPASS:
			result = cmd_into_bypass(req);
			break;
	
	case IOPCMD_MOVE:
			result = iop_move(req);
			break;

	case IOPCMD_NOBYPASS:
			result = iop_no_bypass(req);
			break;

	case IOPCMD_RCVREPLY:
			result = iop_rcvreply(req);
			break;

	case IOPCMD_SEND:
			result = iop_send(req);
			break;

	case IOPCMD_STARTIOP:
			result = iop_startiop(req);
			break;

	case IOPCMD_STOPIOP:
			result = iop_stopiop(req);
			break;

	default:
			result = EIOPCMD;	/* invalid request */
    }

    return(result);
}

/*----------------------------*/
int
iop_init()
{
    extern short machineID;

    static int iop_rcv_move();

    register struct iopreq *req;
    register struct iopstat *ip;
    register struct chaninfo *chp;
    register int chan;
    register int iop;

    if (machineID != MACIIfx) {		/* no IOPs on other CPUs */
	return(0);
    }

    for (iop = 0,ip = &iopstat[0]; ip < &iopstat[NIOP]; iop++,ip++) {

	ip->addr = iopaddr[iop];
	ip->bypass = 0;
	ip->dead = 0;

	for (chan = 1; chan <= NCH; chan++) {
	    chp = &ip->chaninfo[chan];
	    chp->receiver = (struct iopreq *)0;
	    chp->qhead    = (struct iopreq *)0;
	    chp->qtail    = (struct iopreq *)0;
	}

	ip->maxsendchan = get1(ip,IOP_MAXSEND);
	ip->maxrcvchan  = get1(ip,IOP_MAXRCV);

	ip->initted = 1;

	/* Install a receive-message handler for channel 1, to handle
	 * IOP-initiated "move" requests.
	 */

	req = &msg1req[iop];
	req->req = IOPCMD_ADDRCVR;
	req->iop = iop;
	req->chan = 1;			/* incoming channel 1 is for moves */
	req->replylen = sizeof(struct movemsg);
	req->reply = (caddr_t)&req->move;
	req->handler = iop_rcv_move;		/* handle normally */
	(void)iop_add_rcvr(req);
    }

    /* We halt IOP 0 (the serial one) because we can't trust its
     * kernel or the drivers loaded there.  It will be downloaded
     * by deviop.c later in system startup via an ioctl.
     */
    
    iop_into_bypass();
    iopstat[0].maxsendchan = iopstat[0].maxrcvchan = 0;

    timeout(iop_alive,(caddr_t)0,10 * v.v_hz); /* to check for IOPs alive */

    return(0);
}

/*----------------------------*/
/* Install a receive-message handler (IOP to CPU) */
static int iop_add_rcvr(req)
register struct iopreq *req;	/* the request */
{
    register struct iopstat *ip;
    register struct chaninfo *chp;
    register int s;
    register int chan;
    register int result = 0;

    chan = req->chan;
    ip = &iopstat[req->iop];
    chp = &ip->chaninfo[chan];

    s = SPLIOP();

    if (req->flags) {
	result = EIOPREQBUSY;		/* req is already in use ?!? */
	goto out;
    }

    if (!ip->initted) {			/* not initialized */
	result = EIOPNOTINIT;		
	goto out;
    }
    if (ip->dead) {			/* is it alive? */
	result = EIOPDEAD;		
	goto out;
    }

    if (ip->bypass) {			/* not allowed in bypass mode */
	result = EIOPBYPASS;		
	goto out;
    }

    if (chan < 1 || chan > ip->maxrcvchan) {
	result = EIOPCHANRANGE;
	goto out;
    }

    if (chp->receiver) {		/* already has one installed */
	result = EIOPHASRCVR;
	goto out;
    }

    chp->receiver = req;		/* remember receiver request */
    req->flags = REQ_BUSY | REQ_RCVR;	/* set it busy */

out:;
    splx(s);
    return(result);
}

/*----------------------------*/
/* Clear the IOP memory.  Some downloaded code assumes this, ugh. */
static int iop_clrmem(req)
register struct iopreq *req;		/* the request */
{
    register struct iopstat *ip;
    register int result = 0;
    register int i;
    register int s;

    ip = &iopstat[req->iop];

    s = SPLIOP();

    if (req->flags) {
	result = EIOPREQBUSY;		/* req is already in use ?!? */
	goto out;
    }

    if (!ip->initted) {			/* not initialized */
	result = EIOPNOTINIT;		
	goto out;
    }

    if (!(ip->stopped | ip->bypass)) {	/* can't clear unless stopped first */
	result = EIOPINUSE;		
	goto out;
    }

    ip->addr->ramaddrL = 0;		/* start at location zero */
    ip->addr->ramaddrH = 0;

    for (i = 0; i < (32768/sizeof(long)); i++) {
	*((long *)(&ip->addr->ramdata)) = 0L;
    }

out:;
    splx(s);
    return(result);
}
/*----------------------------*/
/* Delete a receive-message handler. */
static int iop_del_rcvr(req)
register struct iopreq *req;	/* the request */
{
    register struct iopstat *ip;
    register struct chaninfo *chp;
    register int s;
    register int chan;
    register int result = 0;

    chan = req->chan;
    ip = &iopstat[req->iop];
    chp = &ip->chaninfo[chan];

    s = SPLIOP();

    if (req->flags != (REQ_BUSY | REQ_RCVR)) {
	result = EIOPREQBUSY;		/* req is already in use ?!? */
	goto out;
    }

    if (!ip->initted) {			/* not initialized */
	result = EIOPNOTINIT;		
	goto out;
    }
    if (ip->dead) {			/* is it alive? */
	result = EIOPDEAD;		
	goto out;
    }

    if (ip->bypass) {			/* not allowed in bypass mode */
	result = EIOPBYPASS;		
	goto out;
    }

    if (!chp->receiver) {		/* none installed */
	result = EIOPNORCVR;
	goto out;
    }

    if (chp->receiver != req) {	/* not this one */
	result = EIOPWRONGRCVR;
	goto out;
    }

    chp->receiver->flags = 0;
    chp->receiver = (struct iopreq *)0;	/* it's gone */

out:;
    splx(s);
    return(result);
}

/*----------------------------*/
static int cmd_into_bypass(req)
register struct iopreq *req;	/* the request */
{
    register struct iopstat *ip;
    register struct chaninfo *chp;
    register int s;
    register int i;
    register int result = 0;

    ip = &iopstat[req->iop];

    s = SPLIOP();

    if (req->flags) {
	result = EIOPREQBUSY;		/* req is already in use ?!? */
	goto out;
    }

    if (!ip->initted) {			/* not initialized */
	result = EIOPNOTINIT;		
	goto out;
    }

    if (ip->bypass) {			/* already in bypass mode */
	result = EIOPBYPASS;		
	goto out;
    }

    if (!req->handler) {		/* must provide an interrupt handler */
	result = EIOPNORCVR;
	goto out;
    }

    /* You can't put it into bypass mode if there are outstanding
     * requests or someone has installed a message receiver.
     */
    
    for (chp = &ip->chaninfo[2]; chp < &ip->chaninfo[NCH]; chp++) {
	if (chp->qhead || chp->receiver) {
	    result = EIOPINUSE;
	    goto out;
	}
    }
    if (ip->chaninfo[1].qhead) {	/* check send channel 1 */
	result = EIOPINUSE;
	goto out;
    }

    /* Remove our move-message handler (channel 1). */

    (void)iop_del_rcvr(&msg1req[req->iop]);

    (void)iop_do_bypass(ip);

out:;
    splx(s);
    return(result);
}

typedef int (*pfi)();		/* pointer to func returning int */

/*----------------------------*/
int iop_into_bypass()
{
    register int s;

    s = spl7();
    (void)iop_do_bypass(&iopstat[0]);
    splx(s);
    return(0);
}

/*----------------------------*/
int iop_do_bypass(ip)
register struct iopstat *ip;
{
    static struct {
	u_short addr;			/* where it loads */
	u_short size;			/* size */
	u_char code[64];		/* the 65C02 code */
    } bypasscode = {	0x7feb,
			21,
	    {
	    /* 7feb */ 0xad,0x30,0xf0,	/* LDA 0xf030 (SCC ctl reg)	*/
	    /* 7fee */ 0x09,0x81,	/* ORA #BYPASS + GPOUTHIGH	*/
	    /* 7ff0 */ 0x8d,0x30,0xf0,	/* STA 0xf030 (SCC ctl reg)	*/
	    /* 7ff3 */ 0xa9,0x44,	/* LDA #0x44  (I/O clock/holdoff) */
	    /* 7ff5 */ 0x8d,0x31,0xf0,	/* STA 0xf031 (IOCtlReg)	*/
	    /* 7ff8 */ 0x80,0xfe,	/* BRA .			*/
	    /* 7ffa */ 0xf8,0x7f,	/* NMI   vector			*/
	    /* 7ffc */ 0xeb,0x7f,	/* RESET vector			*/
	    /* 7ffe */ 0xf8,0x7f	/* IRQ   vector			*/
	    }
    };

    register int i;
    
    /* Stuff the IOP into a bypass-mode "halt": */

    ip->addr->statctl = IOP_STOPIOP;		/* stop the IOP */
    ip->addr->ramaddrL = bypasscode.addr & 0xff;
    ip->addr->ramaddrH = bypasscode.addr >> 8;	/* where it goes */
    for (i = 0; i < bypasscode.size; i++) {
	ip->addr->ramdata = bypasscode.code[i];
    }
    ip->addr->statctl = IOP_START | IOP_INT0 | IOP_INT1; /* start it up */

    for (i = 0; i < 1000; i++)			/* let it finish */
	;

    ip->bypass = 1;

    return(0);
}

/*----------------------------*/
static int iop_rcv_move(req)		/* handle IOP-requested move */
register struct iopreq *req;		/* the request */
{
    static struct iopreq localreq;
    register int rc;
    
    localreq = *req;			/* copy to the move request */
    localreq.flags = 0;
    localreq.move.iop = req->iop;	/* IOP doesn't fill in this field */
    rc = iop_move(&localreq);		/* do the move */
    if (rc) {
	printf("iopmgr: err %d moving data for IOP %d.\n",rc,localreq.iop);
    }

    localreq.req = IOPCMD_RCVREPLY;

    rc = iop_rcvreply(&localreq);	/* acknowledge it */
    if (rc) {
	printf("iopmgr: err %d ack'ing IOP %d move req.\n",rc,localreq.iop);
    }

    return(0);
}

/*----------------------------*/
static int iop_move(req)
register struct iopreq *req;		/* the request */
{
    register struct movemsg *mp;
    register struct iopstat *ip;
    register int s;
    register int result = 0;

    mp = &req->move;
    ip = &iopstat[mp->iop];

    if (req->flags) {
	return(EIOPREQBUSY);		/* req is already in use ?!? */
    }

    if (ip->dead) {
	return(EIOPDEAD);
    }

    s = SPLIOP();

    switch (mp->type) {

	case MV_TO_CPU:
			getn(ip,mp->offset,mp->b.buf,mp->count);
			break;

	case MV_TO_IOP:
			putn(ip,mp->offset,mp->b.buf,mp->count);
			break;

	case MV_COMPARE:
			mp->compresult = 0;
			result = EIOPMVCMD;	/* NOT IMPLEMENTED YET */
			break;

	case MV_PATCH:
			result = iop_patch(ip,mp->b.pkt);
			break;

	default:
			result = EIOPMVCMD;	/* invalid request */
    }

    splx(s);
    return(result);
}
/*----------------------------*/
static int iop_no_bypass(req)
register struct iopreq *req;		/* the request */
{
    register struct iopstat *ip;
    register int s;
    register int chan;
    register int result = 0;

    ip = &iopstat[req->iop];

    s = SPLIOP();

    if (req->flags) {
	result = EIOPREQBUSY;		/* req is already in use ?!? */
	goto out;
    }

    if (!ip->initted) {			/* not initialized */
	result = EIOPNOTINIT;		
	goto out;
    }

    if (!ip->bypass) {			/* not allowed in bypass mode */
	result = EIOPINUSE;		
	goto out;
    }

    ip->bypass = 0;

out:;
    splx(s);
    return(result);
}

/*----------------------------*/
static int iop_patch(ip,p)
register struct iopstat *ip;
struct codepacket *p;		/* ptr to null-terminated list */
{
    register int s;
    register int result = 0;

    s = SPLIOP();

    if (!ip->initted) {			/* not initialized */
	result = EIOPNOTINIT;		
	goto out;
    }
    if (ip->dead) {			/* is it alive? */
	result = EIOPDEAD;		
	goto out;
    }

    if (ip->bypass) {			/* not allowed in bypass mode */
	result = EIOPBYPASS;		
	goto out;
    }

    put1(ip,IOP_PATCHFLAG,MSG_NEWSENT);	/* tell the IOP to stop */
    while (get1(ip,IOP_PATCHFLAG) != MSG_COMPLETE) /* wait for it to stop */
	;

    while (p->count) {
	putn(ip,p->offset,p->code,p->count);	/* stuff the bytes */
	p = (struct codepacket *)(((char *)p + p->count + 3));
    }

    put1(ip,IOP_ALIVE,0xff);			/* make sure it shows alive */
    put1(ip,IOP_PATCHFLAG,MSG_IDLE);		/* tell the IOP to go */

out:;
    splx(s);
    return(result);
}

/*----------------------------*/
/* Send the receive-reply result message back to the IOP. */
static int iop_rcvreply(req)	/* send a reply */
register struct iopreq *req;	/* the request */
{
    register struct iopstat *ip;
    register struct chaninfo *chp;
    register int s;
    register int chan;
    register int result = 0;

    chan = req->chan;
    ip = &iopstat[req->iop];
    chp = &ip->chaninfo[chan];

    s = SPLIOP();

    if (req->flags) {
	result = EIOPREQBUSY;		/* req is already in use ?!? */
	goto out;
    }

    if (!ip->initted) {			/* not initialized */
	result = EIOPNOTINIT;		
	goto out;
    }
    if (ip->dead) {			/* is it alive? */
	result = EIOPDEAD;		
	goto out;
    }

    if (ip->bypass) {			/* not allowed in bypass mode */
	result = EIOPBYPASS;		
	goto out;
    }

    if (chan < 1 || chan > ip->maxsendchan) {
	result = EIOPCHANRANGE;		/* channel out of range */
	goto out;
    }

    if (req->reply && req->replylen) {
	putn(ip,IOP_RCV + (MSGLEN * chan),req->reply,req->replylen);
    }
    put1(ip,(IOP_RCVSTATE + chan), MSG_COMPLETE); /* tell it "done" */
    ip->addr->statctl = IOP_INTERRUPT;		/* interrupt the IOP */

out:;
    splx(s);
    return(result);
}

/*----------------------------*/
/* Send a message to an IOP */
static int iop_send(req)		/* send a msg */
register struct iopreq *req;		/* the request */
{
    register struct iopstat *ip;
    register struct chaninfo *chp;
    register int s;
    register int chan;
    register int result = 0;

    chan = req->chan;
    ip = &iopstat[req->iop];
    chp = &ip->chaninfo[chan];

    s = SPLIOP();

    if (req->flags) {
	result = EIOPREQBUSY;		/* req is already in use ?!? */
	goto out;
    }

    if (!ip->initted) {			/* not initialized */
	result = EIOPNOTINIT;		
	goto out;
    }
    if (ip->dead) {			/* is it alive? */
	result = EIOPDEAD;		
	goto out;
    }

    if (ip->bypass) {			/* not allowed in bypass mode */
	result = EIOPBYPASS;		
	goto out;
    }

    if (chan < 1 || chan > ip->maxsendchan) {
	result = EIOPCHANRANGE;		/* channel out of range */
	goto out;
    }

    req->flags = REQ_BUSY;		/* request is busy */

    if (chp->qhead) {			/* >=one in queue, so queue this one */
	chp->qtail->link = req;
	chp->qtail = req;
	req->flags |= REQ_QUEUED;
    } else {
	chp->qhead = req;
	chp->qtail = req;
	sendmsg(ip,req);		/* OK, send the message */
    }

out:;
    splx(s);
    return(result);
}

/*----------------------------*/
static int iop_startiop(req)		/* start an IOP */
register struct iopreq *req;		/* the request */
{
    register struct iopstat *ip;
    register int result = 0;
    register int i;
    register int s;

    ip = &iopstat[req->iop];

    s = SPLIOP();

    if (req->flags) {
	result = EIOPREQBUSY;		/* req is already in use ?!? */
	goto out;
    }

    if (!ip->initted) {			/* not initialized */
	result = EIOPNOTINIT;		
	goto out;
    }

    if (ip->bypass) {			/* not allowed in bypass mode */
	result = EIOPBYPASS;		
	goto out;
    }

    /* Just start it, clearing interrupts,  and set it alive. */

    put1(ip,IOP_ALIVE,0);		/* make sure it comes alive */
    ip->dead = 0;
    ip->stopped = 0;
    ip->addr->statctl = IOP_START | IOP_CLRINT0 | IOP_CLRINT1;

    /* Wait till it comes alive. */

    i = 100000;
    while (--i && (get1(ip,IOP_ALIVE) != 0xff))
	;

    if (i == 0) {			/* never came to life. */
	result = EIOPDEAD;
	ip->dead = 1;			/* really dead */
    }

    ip->maxsendchan = get1(ip,IOP_MAXSEND);	/* get msg limits */
    ip->maxrcvchan  = get1(ip,IOP_MAXRCV);
out:;
    splx(s);
    return(result);
}

/*----------------------------*/
static int iop_stopiop(req)		/* stop an IOP */
register struct iopreq *req;		/* the request */
{
    register struct iopstat *ip;
    register int result = 0;
    register int s;

    ip = &iopstat[req->iop];

    s = SPLIOP();

    if (req->flags) {
	result = EIOPREQBUSY;		/* req is already in use ?!? */
	goto out;
    }

    if (!ip->initted) {			/* not initialized */
	result = EIOPNOTINIT;		
	goto out;
    }

    if (ip->bypass) {			/* not allowed in bypass mode */
	result = EIOPBYPASS;		
	goto out;
    }

    /* Just stop it, clearing interrupts, and set it dead. */

    ip->dead = 0;			/* not really dead, just stopped */
    ip->stopped = 1;
    ip->addr->statctl = IOP_STOPIOP | IOP_INT0 | IOP_INT1;

out:;
    splx(s);
    return(result);
}

/*----------------------------*/
/* Check if IOPs are alive. Called every 10 seconds via timeout. We assume
 * that our entry SPL is LESS than SPLIOP.
 */
static int iop_alive(arg)		/* returns 1 if alive, 0 if dead */ 
caddr_t arg;
{
    register struct iopstat *ip;
    register int s;
    register int result = 1;		/* assume alive */
    register int iop;

    s = SPLIOP();

    for (iop = 0; iop < NIOP; iop++) {
	ip = &iopstat[iop];

	/* If it's stopped or in bypass mode, don't check it. */

	if (!ip->stopped && !ip->bypass) {
	    if (get1(ip,IOP_ALIVE) != 0xff) {
		result = 0;		/* dead */
		ip->dead = 1;		/* prevent further accesses */
	    } else {
		put1(ip,IOP_ALIVE,0);	/* alive */
		ip->dead = 0;		/* allow further access again */
	    }
	}
    }

    timeout(iop_alive,(caddr_t)0,10 * v.v_hz);	/* reschedule ourselves */

    splx(s);
    return(result);
}

/*============================*/
int
iop_intr_scc()
{
    register struct iopstat *ip;

    ip = &iopstat[0];
    if (ip->bypass) {
	return(0);			/* skip it if in bypass mode */
    } else {
	return(iop_intr(ip));
    }
}

/*----------------------------*/
int
iop_intr_swim()
{
    register struct iopstat *ip;

    ip = &iopstat[1];
    if (ip->bypass) {
	return(0);			/* skip it if in bypass mode */
    } else {
	return(iop_intr(ip));
    }
}

/*----------------------------*/
static int
iop_intr(ip)
register struct iopstat *ip;
{
    static void do_msgcompletes();
    static void do_rcvmsgs();

    register int status;
    register int s;

    /* Since each IOP may interrupt at a different SPL,
     * we ensure that we run at the best one we need.
     */
    s = SPLIOP();

    status = ip->addr->statctl;	/* get the ctl register */

    /* If we've got IOP-kernel-generated interrupts, handle 'em. */

    if (status & (IOP_INT0 | IOP_INT1)) {
	if (status & IOP_INT0) {	/* send-msg completed by IOP */
	    do_msgcompletes(ip);
	}
	if (status & IOP_INT1) {	/* rcv-msg arrived from IOP */
	    do_rcvmsgs(ip);
	}
    }

    splx(s);
    return(0);
}

/*----------------------------*/
static void do_msgcompletes(ip)			/* called at high enough SPL */
register struct iopstat *ip;
{
    register struct iopreq *req;
    register struct chaninfo *chp;
    register int chan;

    /* We clear the interrupt before scanning the status
     * bytes, so that if the IOP sets up another one while
     * we're scanning, it'll interrupt us again.
     */
    ip->addr->statctl = IOP_CLRINT0;

    /* Scan all channels this IOP for completed messages: */

    for (chan = 1; chan <= ip->maxsendchan; chan++) {
	chp = &ip->chaninfo[chan];
	if (get1(ip,(chan + IOP_SENDSTATE)) == MSG_COMPLETE) {
	    req = chp->qhead;

	    /* Get the reply first, then dequeue this request (so the
	     * handler can reuse the request structure), then call
	     * the handler.
	     */

	    if (req->reply && req->replylen) {	/* a buffer exists */
		getn(ip,IOP_SEND + (MSGLEN * chan),req->reply,req->replylen);
	    }

	    if (req->link) {	/* another in the queue */
		chp->qhead = req->link;
		sendmsg(ip,chp->qhead);	/* send the next one */
	    } else {				/* only this one in the queue */
		chp->qhead = chp->qtail  = (struct iopreq *)0;
		put1(ip,(IOP_SENDSTATE + chan), MSG_IDLE);
		ip->addr->statctl = IOP_INTERRUPT;	/* interrupt the IOP */
	    }
	    req->link = (struct iopreq *)0;
	    req->flags = 0;

	    if (req->handler) {			/* a handler exists */
		(*req->handler)(req);		/* call him with req & reply */
	    }
	}
    }
}

/*----------------------------*/
static void do_rcvmsgs(ip)
register struct iopstat *ip;
{
    register struct iopreq *req;
    register struct chaninfo *chp;
    register int chan;

    /* We clear the interrupt before scanning the status
     * bytes, so that if the IOP sets up another one while
     * we're scanning, it'll interrupt us again.
     */
    ip->addr->statctl = IOP_CLRINT1;

    for (chan = 1; chan <= ip->maxsendchan; chan++) {
	chp = &ip->chaninfo[chan];
	if (get1(ip,(chan + IOP_RCVSTATE)) == MSG_NEWSENT) {
	    req = chp->receiver;
	    if (req && req->handler) {
		if (req->reply && req->replylen) {
		    getn(ip,IOP_RCV + (MSGLEN * chan),req->reply,req->replylen);
		}
		(*req->handler)(req);		/* call him with req */

		/* The receiving handler must call to send
		 * the "complete" message.
		 */

	    } else {				/* no receive-handler? */
		printf("spurious received message: IOP %d chan %d\n",
				    ip - &iopstat[0],chan);
		dumpmsg(ip,chan);
		put1(ip,(IOP_RCVSTATE + chan), MSG_COMPLETE); /* say "done" */
		ip->addr->statctl = IOP_INTERRUPT;	/* interrupt the IOP */
	    }
	}
    }
}

/*----------------------------*/
static int dumpmsg(ip,chan)
register struct iopstat *ip;
register int chan;
{
    register int i;
    register int mp;

    mp = IOP_RCV + (MSGLEN * chan);

    for (i = 0; i < MSGLEN; i++) {
	printf("%x ",get1(ip,mp));
	mp++;
    }

    printf("\n");

    return(0);
}

/*============================*/
static u_char get1(ip,offset)		/* MUST be called at high enough SPL */
register struct iopstat *ip;
register u_short offset;	/* IOP memory address */
{
    register u_char data;

    ip->addr->ramaddrL = offset & 0xff;
    ip->addr->ramaddrH = offset >> 8;
    data = ip->addr->ramdata;

    return(data);
}

/*----------------------------*/
static void put1(ip,offset,byte)	/* MUST be called at high enough SPL */
register struct iopstat *ip;
register u_short offset;	/* IOP memory address */
register u_char byte;		/* byte to put into IOP memory */
{
    ip->addr->ramaddrL = offset & 0xff;
    ip->addr->ramaddrH = offset >> 8;
    ip->addr->ramdata = byte;
}

/*----------------------------*/
static void getn(ip,offset,buf,count)	/* MUST be called at high enough SPL */
register struct iopstat *ip;
register u_short offset;	/* IOP memory address */
register caddr_t buf;		/* buffer to put IOP bytes into */
register int count;		/* bytecount */
{
    ip->addr->ramaddrL = offset & 0xff;
    ip->addr->ramaddrH = offset >> 8;

    while (((long)buf & 0x03) && count) {	/* move till long aligned */
	*buf++ = ip->addr->ramdata;
	--count;				/* only dec if done */
    }

    while (count >= sizeof(long)) {	/* move longwords */
	*((long *)buf)++ = *((long *)(&ip->addr->ramdata));
	count -= 4;
    }

    while (count--) {				/* move last odd bytes */
	*buf++ = ip->addr->ramdata;
    }
	
}

/*----------------------------*/
static void putn(ip,offset,buf,count)	/* MUST be called at high enough SPL */
register struct iopstat *ip;
register u_short offset;	/* IOP memory address */
register caddr_t buf;		/* bytes to put into IOP memory */
register int count;		/* bytecount */
{
    ip->addr->ramaddrL = offset & 0xff;
    ip->addr->ramaddrH = offset >> 8;

    while (((long)buf & 0x03) && count) {	/* move till long aligned */
	ip->addr->ramdata = *buf++;
	--count;				/* only dec if done */
    }

    while (count >= sizeof(long)) {	/* move longwords */
	*((long *)(&ip->addr->ramdata)) = *((long *)buf)++;
	count -= 4;
    }

    while (count--) {				/* move last odd bytes */
	ip->addr->ramdata = *buf++;
    }
	
}

/*----------------------------*/
static void sendmsg(ip,req)		/* MUST be called at high enough SPL */
register struct iopstat *ip;
register struct iopreq *req;
{
    register u_short mp;
    register int iop;
    register int chan;

    iop = req->iop;
    chan = req->chan;

    mp = IOP_SEND + (MSGLEN * chan);

    putn(ip,mp,req->msg,req->msglen);		/* stuff the message */

    put1(ip,(IOP_SENDSTATE + chan), MSG_NEWSENT);	/* tell it to go */

    ip->addr->statctl = IOP_INTERRUPT;		/* interrupt the IOP */
}
