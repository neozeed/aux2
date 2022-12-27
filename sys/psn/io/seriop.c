/*
 * @(#)seriop.c  {Apple version 1.8 90/05/02 11:25:05}
 *
 * Copyright (c) 1987, 1988, 1989 by Apple Computer, Inc.
 * All Rights Reserved.
 *
 * THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF APPLE COMPUTER, INC.
 * The copyright notice above does not evidence any actual or
 * intended publication of such source code.
 */

#if !defined(lint)
static char _sccsid[]="@(#)seriop.c  {Apple version 1.8 90/05/02 11:25:05}";
#endif

/*
** These are the minor devices of the printer, and modem ports.
** This information is used to make sure that the correct flow control
** is always present by default on these lines.
*/
#define	MODEM_PORT	0
#define	PRINTER_PORT	1

/*
 *	IOP serial device driver.
 */

#ifdef lint
#include "sys/sysinclude.h"
#else lint
#include "sys/param.h"
#include "sys/types.h"
#include "sys/mmu.h"
#include "sys/page.h"
#include "sys/region.h"
#include "sys/systm.h"
#include "sys/signal.h"
#include "sys/time.h"
#include "sys/user.h"
#include "sys/errno.h"
#include "sys/file.h"
#include "sys/ioctl.h"
#include "sys/tty.h"
#include "sys/termio.h"
#include "sys/var.h"
#include "sys/proc.h"
#include "sys/conf.h"
#include "sys/sysmacros.h"
#endif lint
#include "sys/scciop.h"
#include "sys/iopmgr.h"

#define STATIC /* static */		/* easy testing with ADB */

	int	siopinit();
	int	siopioctl();
	int	siopopen();
	int	siopclose();
	int	siopread();
	int	siopwrite();

STATIC	void	ctl_output();
STATIC	void	devparms();
STATIC	void	freereq();
STATIC	void	getreq();
STATIC	int	hsoptions();
STATIC	int	iop_event();
STATIC	int	iopreq_wait();
STATIC	int	iopret_ctl();
STATIC	int	iopret_read();
STATIC	int	iopret_wake();
STATIC	int	iopret_write();
STATIC	void	getstatus();
STATIC	int	nextread();
STATIC	int	siopproc();
STATIC	int	siopproc();
STATIC	void	t_output();

#define NDEV 2

/* We send certain Mac-compatibility IOCTLs thru the output proc,
 * so we define a few arbitrary new T_??? values.
 */
#define	T_CLRBRKMODE	30
#define	T_SETBRKMODE	29
#define	T_RESET		28
#define	T_CLRDTR	27
#define	T_SETDTR	26

#ifdef TRACE
#undef TRACE
#endif

#define DEBUG

#ifdef DEBUG
#define TRACE(level,msg) {		\
    int s = spl7();			\
    if ((level) & siop_trace) {		\
	printf("si%d: ",cx - context);	\
	printf msg;			\
    }					\
    splx(s);				\
}
#else	/* !DEBUG */
#define TRACE
#endif	/* DEBUG */

u_short	siop_ticks = 5;			/* delay before reads, 16ms each */

/* Trace levels, as bits to allow selective tracing. The "flow"
 * traces, useful for initial debugging, are kept separate from
 * the "error reporting" traces for ease of testing.
 */
#define	TR_IOP		0x00000001	/* report iopreq errors */
#define	TR_DERRS	0x00000002	/* report data errors */
#define	TR_ERRS		0x00000004	/* report errors of any kind */

#define	TR_TCSET	0x00000200	/* report TCSET? flags */
#define	TR_OPEN		0x00000400	/* report opens */
#define	TR_CLOSE	0x00000800	/* report closes */
#define	TR_READ		0x00001000	/* report reads */
#define	TR_WRITE	0x00002000	/* report writes */
#define	TR_IOCTL	0x00004000	/* report ioctls */
#define	TR_GETREQ	0x00008000	/* report getreq/freereq */
#define	TR_QCTL		0x00010000	/* queueing  an output control */
#define	TR_DQCTL	0x00020000	/* dequeued  an output control */
#define	TR_XCTL		0x00040000	/* executing an output control */
#define	TR_RCOPY	0x00080000	/* show read copy len & source */
#define	TR_OPENPROG	0x00100000	/* show open progress */
#define	TR_FUNC		0x00200000	/* function entries */
#define	TR_SLEEP	0x00400000	/* sleeps */
#define	TR_OUT		0x00800000	/* t_output traces */
#define	TR_INPUT	0x01000000	/* input traces */
#define	TR_STAT		0x02000000	/* show getstatus() results */
#define	TR_EVENT	0x04000000	/* show incoming events */
#define	TR_PARMS	0x08000000	/* show devparms,hsoptions */

u_long siop_trace = 	0		|	
			0
		    ;

#define OP_READ		0		/* read */
#define OP_WRITE	1		/* write */
#define	OP_EVENT	2		/* event (incoming receiver) */
#define	OP_IOCTL	3		/* ioctl and other top-half use */
#define	OP_CTL		4		/* controls, used by t_proc */

#define	OP_MAX		5		/* last + 1 */


static u_short chantab[][OP_MAX] = {	/* MUST MATCH #define order! */
    {
	SCCA_READ,	/* OP_READ */
	SCCA_WRITE,	/* OP_WRITE */
	SCCA_EVENT,	/* OP_EVENT */
	SCCA_CTL,	/* OP_IOCTL */
	SCCA_CTL,	/* OP_CTL */
    },
    {
	SCCB_READ,	/* OP_READ */
	SCCB_WRITE,	/* OP_WRITE */
	SCCB_EVENT,	/* OP_EVENT */
	SCCB_CTL,	/* OP_IOCTL */
	SCCB_CTL,	/* OP_CTL */
    }
};

STATIC struct context {
    struct tty		*tp;		/* tty struct */
    u_short		flags;
    u_short		wbuf;		/* write buffer offset in IOP */
    u_short		wbuflen;	/* write buffer len in IOP */
    u_short		statbuf;	/* stats buffer offset in IOP */
    u_long		defer;		/* queued T_whatever commands */
    struct iop_ops {
	struct iopreq	req;		/* for data transfer & cmd */
	char		msg[32];	/* msg buffer */
	u_short		flags;
    } iop_ops[OP_MAX];			/* READ, WRITE, CTL, STATUS, EVENT */
    struct iopreq	copy;		/* for read/write/status copies */
    struct serstatus	status;		/* status buffer */
    u_long		framerrs;	/* error counts for Mac compat */
    u_long		overruns;
    u_long		parityerrs;
} context[NDEV];

/* Struct iop_ops flags: */
#define	INUSE		0x0001		/* req is in use */

/* General flags: */
#define DTRFLOW		0x0001		/* DTR flow control on/off */
#define	OPENING		0x0002		/* open in progress */
#define	OPEN		0x0004		/* open */
#define	MODEMCTL	0x0008		/* modem control on/off */
#define	EXCL		0x0010		/* opened O_EXCL */
#define	CLOSING		0x0020		/* close in progress */
#define	TINPUT		0x0040		/* T_INPUT received */
#define	NOHUP		0x0080		/* don't drop DTR on interrupted open */
#define	BREAKWAIT	0x0100		/* wait for setbreak to take effect */

#define S(b) ((115200 / b) - 2)
static int siopspeeds[] = {
	S(1),	S(50),	S(75),	S(110),	S(134),	S(150),	S(200),	S(300),
	S(600),	S(1200), S(1800), S(2400), S(4800), S(9600), S(19200), S(38400)
};

/*-------------------------------*/
/*
 * Initialize the driver.
 */
int
siopinit()
{
    extern struct cdevsw cdevsw[];
    extern int cdevcnt;
    extern struct tty sc_tty[];
    extern int (*sccirq)();
    int scopen();
    int iop_intr_scc();

    register struct context *cx;
    register struct iop_ops *ip;
    register struct iopreq *req;
    register int i;
    register int j;
    int s;

    s = spl7();

    for (i = 0; i < NDEV; i++) {
	cx = &context[i];
	cx->tp = &sc_tty[i];

	/* The modem port and printer port get different default
	 * options when initted, for ease of plug & play use.
	 */
	if (i == MODEM_PORT) {
	    cx->flags |= MODEMCTL;
	} else {
	    cx->flags |= DTRFLOW;
	}

	cx->copy.req = IOPCMD_MOVE;
	cx->copy.handler = (int (*)()) 0;
	cx->copy.driver = (u_long)cx;
	cx->copy.iop = SCC_IOP;
	cx->copy.move.iop = SCC_IOP;

	for (j = 0; j < OP_MAX; j++) {
	    ip = &cx->iop_ops[j];
	    req = &ip->req;
	    req->req = IOPCMD_SEND;
	    req->iop = SCC_IOP;
	    req->move.iop = SCC_IOP;
	    req->chan = chantab[i][j];		/* where msg gets sent */
	    req->msg = &ip->msg[0];
	    req->reply = &ip->msg[0];
	    req->driver = (u_long)cx;

	    /* Special initialization for some: */

	    switch (j) {
		
		case OP_CTL:			/* controls from t_proc */
		    req->handler = iopret_ctl;
		    break;

		case OP_WRITE:			/* writes from t_proc */
		    req->handler = iopret_write;
		    req->msglen   = sizeof(struct cmd_write);
		    req->replylen = sizeof(struct cmd_write);
		    break;

		case OP_READ:			/* stuff for read */
		    req->handler = iopret_read;
		    req->msglen   = sizeof(struct cmd_read);
		    req->replylen = sizeof(struct cmd_read);
		    break;
		
		case OP_EVENT:			/* stuff for event rcvr */
		    req->replylen = sizeof(struct sccevent);
		    req->handler = iop_event;	/* incoming event */
		    break;
		
		case OP_IOCTL:
		    req->handler = iopret_wake;
		    break;
	    }
	}
    }

    /* Now replace the entries in cdevsw[] so they come to us. */

    for (i = 0; i < cdevcnt; i++) {		/* find our major dev */
	if(cdevsw[i].d_open == scopen)
	    break;
    }

    cdevsw[i].d_open  = siopopen;
    cdevsw[i].d_close = siopclose;
    cdevsw[i].d_read  = siopread;
    cdevsw[i].d_write = siopwrite;
    cdevsw[i].d_ioctl = siopioctl;

    sccirq = iop_intr_scc;		/* SCC irq comes from IOP now */

    printf("IOP-based serial driver.\n");

    splx(s);

    return(0);
}

/*-------------------------------*/
/* ARGSUSED */
int
siopopen(dev, flag)
register dev_t dev;
register int flag;
{
    static struct cnfg opencnfg = {
	cnf_2stop, cnf_noparity, cnf_8bits, S(9600),
    };

    static struct resinfo {
	u_long	type;		/* really char[4] */
	u_short	id;
    } ri[] =	{ { 'SERD', 60 }, { 'SERD', 61 } };


    register struct tty *tp;
    register struct context *cx;
    register struct iopreq *req;
    register int rc;
    caddr_t code;
    u_short len;

    dev = minor(dev);
    if (dev >= NDEV) {
	return(ENXIO);
    }
    cx = &context[dev];
    tp = cx->tp;

    TRACE(TR_OPEN,("open\n"));

    if ((cx->flags & (OPEN | OPENING | EXCL)) && (flag & FLOCKOUT)) {
	return(EBUSY);
    }

    if (flag & FLOCKOUT) {
	cx->flags |= EXCL;
    }

    while (cx->flags & (CLOSING | OPENING)) {	/* wait for open/close */
	TRACE(TR_SLEEP,("open:sleeping on cx %x for open/close\n",cx));
	sleep((caddr_t)cx,PRIBIO);
    }

    /* If we're interrupted during the carrier wait, the caller can
     * prevent the DTR drop.  Needed for Mac support.
     */
    if (flag & FNOHUP) {
	cx->flags |= NOHUP;
    }


    /*	If the device is not already open then:
     *		- ensure the IOP kernel and driver are downloaded;
     *		- sense carrier (for modem control)
     */
    
    if (!(cx->flags & OPEN)) {

	cx->flags |= OPENING;

	getreq(cx,"open");
	req = &cx->iop_ops[OP_IOCTL].req;

	/* Get the driver downloaded and ready to go: */

	if (rc = iop_getdvr(SCC_IOP,&ri[dev].type,ri[dev].id,&code,&len)) {
	    TRACE(TR_IOP,("iop_getdvr err %d\n",rc));
	    rc = EIO;
	    goto out;
	}
	code += 2; len -= 2;		/* eliminate extra length word */
	if (rc = iopdriver_load(SCC_IOP,&cx->iop_ops[OP_EVENT].req,
							dev,code,len)) {
	    TRACE(TR_IOP,("iopdriver_load err %d\n",rc));
	    rc = EIO;
	    goto out;
	}

	cx->framerrs = cx->overruns = cx->parityerrs = 0;

	tp->t_index = dev;
	tp->t_proc = siopproc;
	ttinit(tp);		

	/* Open the IOP driver. */

	{
	    register struct cmd_open *op;

	    req->msglen   = sizeof(struct cmd_open);
	    req->replylen = sizeof(struct cmd_open);
	    op = (struct cmd_open *)req->msg;
	    op->code = cc_open;
	    op->config = opencnfg;
	    op->divisor = S(9600);
	    TRACE(TR_OPENPROG,("sending open\n"));
	    rc = iopreq_wait(cx,req);
	    if (rc) {
		TRACE(TR_ERRS,("open err %d\n",rc));
		goto out;
	    }

	    /* Stash what we're told from the open: */

	    cx->statbuf = op->statbuf;
	    cx->wbuf = op->writebuf;
	    cx->wbuflen = op->wbuflen;
	    TRACE(TR_OPENPROG,("open:statbuf=%x,wbuf=%x,wbuflen=%d\n",
				cx->statbuf,cx->wbuf,cx->wbuflen));
	}

	/* Now set the desired handshake options. We have the
	 * IOP driver handle output XON/XOFF.
	 */

	rc = hsoptions(cx,req);
	if (rc) {
	    TRACE(TR_ERRS,("open: hsoptions err %d\n",rc));
	    goto out;
	}

	/*
	 *	If we are the printer port, make sure that
	 *	the tabs get done correctly
	 */

	if (dev == PRINTER_PORT && (cx->flags & DTRFLOW)) {
	    tp->t_oflag |= (TAB3 | OPOST | ONLCR);
	}

	tp->t_state = WOPEN;

	if (!(cx->flags & MODEMCTL) || (tp->t_cflag & CLOCAL)) {
	    tp->t_state |= CARR_ON;
	}

	tp->t_cflag = B9600 | CS8 | CSTOPB;
	TRACE(TR_OPENPROG,("open, calling devparms\n"));
	devparms(cx,req);
	rc = iopreq_wait(cx,req);
	if (rc) {
	    TRACE(TR_ERRS,("devparms err %d\n",rc));
	    goto out;
	}

	/* Assert DTR. */

	{
	    register struct cmd_assertdtr *ap;

	    req->msglen   = sizeof(struct cmd_assertdtr);
	    req->replylen = sizeof(struct cmd_assertdtr);
	    ap = (struct cmd_assertdtr *)req->msg;
	    ap->code = cc_control;
	    ap->op = ctl_assertdtr;
	    TRACE(TR_OPENPROG,("sending assertdtr\n"));
	    rc = iopreq_wait(cx,req);
	    if (rc) {
		TRACE(TR_ERRS,("assertdtr err %d\n",rc));
		goto out;
	    }
	}
    }

    getstatus(cx);			/* get current status */
    if (!cx->status.ctsHold) {		/* CTS is present */
	tp->t_state |= CARR_ON;
    }

    if (!(flag & (FNDELAY | FNONBLOCK))) {
	while (!(tp->t_state & CARR_ON)) {
	    tp->t_state |= WOPEN;
	    TRACE(TR_SLEEP,("open:sleeping on rawq %x\n",&tp->t_rawq));
	    freereq(cx,"CARR_ON sleep");
	    (void) sleep((caddr_t)&tp->t_rawq, TTOPRI);
	    getreq(cx,"CARR_ON sleep");
	}
    }

    /*	Call the line discipline open routine to set things going.  */

    TRACE(TR_OPENPROG,("open: calling l_open\n"));
    (*linesw[tp->t_line].l_open)(tp, flag);

out:;
    cx->flags &= ~OPENING;

    freereq(cx,"open");

    wakeup((caddr_t)cx);	/* wakeup possible next opener */

    if (rc) {			/* something failed */
	tp->t_state = 0;
	TRACE(TR_ERRS,("open fails with %d\n",rc));
	return(rc);
    }

    cx->flags |= OPEN;
    cx->flags &= ~NOHUP;	/* open succeeded */
    TRACE(TR_OPEN,("open succeeded\n"));
    return(0);
}

/*-------------------------------*/
/* ARGSUSED */
int
siopclose(dev, flag)
register dev_t dev;
{
    register struct iopreq *req;
    register struct context *cx;
    register struct tty *tp;
    register int rc;

    /*
     *	Call the line discipline routine to let output drain and to
     *		shut things down gracefully. If required drop DTR
     *		to hang up the line.
     */

    dev = minor(dev);
    cx = &context[dev];
    cx->flags |= CLOSING;

    TRACE(TR_CLOSE,("close\n"));

    untimeout(nextread,(caddr_t)cx);	/* cancel pending next read */

    /* By getting the IOCTL req, we force any new opens
     * to block until we free the req packet.
     */

    getreq(cx,"close");
    req = &cx->iop_ops[OP_IOCTL].req;

    tp = cx->tp;
    (*linesw[tp->t_line].l_close)(tp);

    /* Time to close the IOP driver. */

    /* Possibly drop DTR on close: */
    {
	register struct cmd_options *op;

	op = (struct cmd_options *)req->msg;
	op->code = cc_control;
	op->op = ctl_options;
	if (!(cx->flags & NOHUP) && (tp->t_cflag & HUPCL)) {
	    op->options = 0;
	    TRACE(TR_STAT,("dropping DTR\n"));
	} else {
	    op->options = CTL_LEAVEDTR;
	    TRACE(TR_STAT,("leaving  DTR\n"));
	}
	req->msglen = sizeof(struct cmd_options);
	req->replylen = req->msglen;
	rc = iopreq_wait(cx,req);
	if (rc) {
	    TRACE(TR_IOP,("ctl_options err %d\n",rc));
	}
    }

    /* Do the close: */

    {
	register struct cmd_close *cp;

	cp = (struct cmd_close *)req->msg;
	cp->code = cc_close;
	if (rc = iopreq_wait(cx,req)) {
	    TRACE(TR_IOP,("close err %d\n",rc));
	}
    }

    freereq(cx,"close");

    /* Deallocate driver, remove incoming receiver and free up IOP ownership. */

    if (rc = iop_free(SCC_IOP,dev,&cx->iop_ops[OP_EVENT].req,1)) {
	TRACE(TR_IOP,("close iopfree err %d\n",rc));
    }

    cx->flags &= (DTRFLOW | MODEMCTL);	/* only retain modem/dtr modes */
    TRACE(TR_CLOSE,("close done\n"));
    wakeup((caddr_t)cx);		/* wakeup possible pending open */
}
/*-------------------------------*/
/*
 *	Read simply calls the line discipline to do all the work
 */
int
siopread(dev, uio)
dev_t dev;
struct uio *uio;
{
    register struct tty *tp;
    register struct context *cx;

    cx = &context[minor(dev)];

    TRACE(TR_READ,("read\n"));
    tp = cx->tp;
    return((*linesw[tp->t_line].l_read)(tp, uio));
}

/*-------------------------------*/
/*
 *	Write simply calls the line discipline to do all the work
 */
int
siopwrite(dev, uio)
dev_t dev;
struct uio *uio;
{
    register struct tty *tp;
    register struct context *cx;

    cx = &context[minor(dev)];

    TRACE(TR_WRITE,("write\n"));
    tp = cx->tp;
    return((*linesw[tp->t_line].l_write)(tp, uio));
}

/*-------------------------------*/
/*
 *	The ioctl routine mostly handles the MODEM ioctls, all others are
 *		passed to ttiocom to be done.
 */
int
siopioctl(dev, cmd, arg, mode)
register dev_t dev;
register int cmd;
char *arg;
int mode;
{
    register struct context *cx;
    register struct iopreq *req;
    register struct tty *tp;
    register int rc = 0;

    cx = &context[minor(dev)];
    tp = cx->tp;
    TRACE(TR_IOCTL,("ioctl %x\n",cmd));

    req = &cx->iop_ops[OP_IOCTL].req;

    switch(cmd) {

	/*
	 * UIOCMODEM: turns on modem control, turns off DTR flow control.
	 */

	case UIOCMODEM:
	    cx->flags &= ~DTRFLOW;
	    cx->flags |= MODEMCTL;

	    getreq(cx,"UIOCMODEM");
	    rc = hsoptions(cx,req);		/* tell the IOP */
	    freereq(cx,"UIOCMODEM");

	    rc = 0;
	    break;

	/*
	 * UIOCNOMODEM: turns off modem control and DTR flow control.
	 * We also wake up those waiting for carrier on open.
	 */

	case UIOCNOMODEM:
	    cx->flags &= ~(DTRFLOW | MODEMCTL);

	    getreq(cx,"UIOCNOMODEM");
	    rc = hsoptions(cx,req);		/* tell the IOP */
	    freereq(cx,"UIOCNOMODEM");

	    if (tp->t_state & WOPEN) {
		tp->t_state |= CARR_ON;
		wakeup((caddr_t)&tp->t_rawq);
	    }
	    rc = 0;
	    break;

	/*
	 * UIOCDTRFLOW: turns on DTR flow control, turns off
	 * modem control.  Wakes up processes waiting to open.
	 */

	case UIOCDTRFLOW:
	    cx->flags |= DTRFLOW;
	    cx->flags &= ~MODEMCTL;

	    getreq(cx,"UIOCDTRFLOW");
	    rc = hsoptions(cx,req);			/* tell the IOP */
	    freereq(cx,"UIOCDTRFLOW");

	    if (tp->t_state & WOPEN) {
		tp->t_state |= CARR_ON;
		wakeup((caddr_t)&tp->t_rawq);
	    }
	    break;

	/*
	 *	UIOCTTSTAT: return the modem control/flow control state
	 */

	case UIOCTTSTAT:
	    if (cx->flags & MODEMCTL) {
		*arg++ = 1;
	    } else {
		*arg++ = 0;
	    }
	    if (cx->flags & DTRFLOW) {
		*arg++ = 1;
	    } else {
		*arg++ = 0;
	    } *arg = 0;
	    rc = 0;
	    break;

	case TCSETSTP:			/* Mac compat: set stop (XOFF) char */
	    tp->t_chars.tc_stopc  = *arg;
	    getreq(cx,"TCSETSTP");
	    rc = hsoptions(cx,req);	/* set XON/XOFF chars */
	    freereq(cx,"TCSETSTP");
	    break;

	case TCSETSTA:			/* Mac compat: set start (XON) char */
	    tp->t_chars.tc_startc = *arg;
	    getreq(cx,"TCSETSTA");
	    rc = hsoptions(cx,req);	/* set XON/XOFF chars */
	    freereq(cx,"TCSETSTA");
	    break;
	
	case TCSBRKM:			/* Mac compat: set break mode */
	    (*tp->t_proc)(tp,T_SETBRKMODE);
	    break;

	case TCCBRKM:			/* Mac compat: clr break mode */
	    (*tp->t_proc)(tp,T_CLRBRKMODE);
	    break;

	case TCSETDTR:			/* Mac compat:   assert DTR */
	    (*tp->t_proc)(tp,T_SETDTR);
	    break;

	case TCCLRDTR:			/* Mac compat: deassert DTR */
	    (*tp->t_proc)(tp,T_CLRDTR);
	    break;

	case TCRESET:			/* Mac compat: reset serial port */
	    (*tp->t_proc)(tp,T_RESET);
	    break;

	case TCGETSTAT:			/* Mac compat: get status */
	    {
		register struct serstat *st;

		st = (struct serstat *)arg;

		st->ser_frame = cx->framerrs;
		st->ser_ovrun = cx->overruns;
		st->ser_parity = cx->parityerrs;
		cx->framerrs = 0;
		cx->overruns = 0;
		cx->parityerrs = 0;

		getstatus(cx);		/* get current status */
		st->ser_cts = ~cx->status.ctsHold;
		st->ser_inflow = (cx->status.xOffSent & 0xc0) ? 0xff: 0;
		st->ser_outflow = (cx->status.xOffHold) ? 0xff: 0;
		break;
	    }

	/*	Otherwise, just do a generic ioctl.
	 *	If params are changed, advise the IOP.
	 */

    default:
	    if (ttiocom(tp, cmd, arg, mode)) {	/* a change was made */

		switch (cmd) {

		    case TCSETAW:
			getreq(cx,"TCSETAW");
			goto foo;
		    case TCSETAF:
			getreq(cx,"TCSETAF");
			goto foo;
		    case TCSETA:
			getreq(cx,"TCSETA");
foo:;
			TRACE(TR_TCSET,("TCSET:c=0%o, i=0%o, l=0%o, o=0%o\n",
			    tp->t_cflag,tp->t_iflag,tp->t_lflag,tp->t_oflag));

			devparms(cx,req);	/* setup for params */
			rc = iopreq_wait(cx,req);
			if (rc == 0) {
			    rc = hsoptions(cx,req);	/* set handshaking */
			}
			freereq(cx,"TCSET*");
			break;
		}

	    } else {
		rc = u.u_error;
	    }
    }

    TRACE(TR_IOCTL,("ioctl done, rc=%d\n",rc));
    return(rc);
}

/*-------------------------------*/
STATIC void
getreq(cx,st)
register struct context *cx;
register char st[];
{
    register struct iop_ops *iop_ops;
    register int s;

    s = spl7();
    iop_ops = &cx->iop_ops[OP_IOCTL];
    while (iop_ops->flags & INUSE) {
	TRACE(TR_GETREQ,("%s:sleeping on iop_ops %x\n",st,iop_ops));
	sleep((caddr_t)iop_ops,PRIBIO);
    }
    iop_ops->flags |= INUSE;
    TRACE(TR_GETREQ,("%s:got iop_ops %x\n",st,iop_ops));
    splx(s);
}

/*-------------------------------*/
STATIC void
freereq(cx,st)
register struct context *cx;
register char st[];
{
    register struct iop_ops *iop_ops;
    register int s;
    
    s = spl7();
    iop_ops = &cx->iop_ops[OP_IOCTL];
    iop_ops->flags &= ~INUSE;
    TRACE(TR_GETREQ,("%s:freeing iop_ops %x\n",st,iop_ops));
    wakeup((caddr_t)iop_ops);
    splx(s);
}

/*-------------------------------*/
/*
 *	The proc routine does all the work. It takes care of requests from
 *		the line discipline.
 */
STATIC int
siopproc(tp, cmd)
register struct tty *tp;
int cmd;
{
    register struct ccblock *tbuf;
    register struct context *cx;
    register struct cmd_read *rp;
    register struct iop_ops *iop_ops;
    register int s;
    register int rc;

    /*
     *	disable interrupts in order to synchronise with the device
     */

    s = SPLIOP();
    cx = &context[minor(tp->t_index)];
    TRACE(TR_OUT,("proc: cmd %d\n",cmd));

    /* We have 2 kinds of commands.  One doesn't need to talk to the
     * IOP driver.  It sets status bits, and may then try to start
     * normal output.
     *
     * The others must immediately talk to the IOP driver, or else
     * they have to queue themselves for later execution.
     */

    switch (cmd) {

	case T_WFLUSH:				/* clear the output buffer */
	    tbuf = &tp->t_tbuf;
	    tbuf->c_size -= tbuf->c_count;
	    tbuf->c_count = 0;
	    ctl_output(tp,T_RESUME,cx);		/* tell IOP to resume */
	    t_output(tp,cx);
	    break;

	case T_RFLUSH:				/* flush pending input */
	    ctl_output(tp,T_UNBLOCK,cx);	/* tell IOP to resume */
	    t_output(tp,cx);
	    break;

	case T_OUTPUT:
	    t_output(tp,cx);
	    break;

	case T_INPUT:				/* line discipline open */

	    /* Start the first read. */

	    TRACE(TR_FUNC,("T_INPUT\n"));
	    if (cx->flags & TINPUT) {
		TRACE(TR_ERRS,("initial read already done!\n"));
	    } else {
		struct iopreq *req;
		int i;

		iop_ops = &cx->iop_ops[OP_READ];
		req = &iop_ops->req;
		if (req->link || req->flags) {
		    TRACE(TR_ERRS,("initial read req busy!\n"));
		    TRACE(TR_ERRS,("link=%x,req=%d,flags=%x,chan=%d\n",
			req->link,req->req,req->flags,req->chan));
		    for (i = 0; i < 32; i++) {
			TRACE(TR_ERRS,("msg=%x ",req->msg[i]));
		    }
		    TRACE(TR_ERRS,("\n"));
		}
		rp = (struct cmd_read *)iop_ops->req.msg;
		rp->taken = 0;
		rp->u.abortack = 1;
		TRACE(TR_OPEN,("open:starting first read\n"));
		if (rc = ioprequest(&iop_ops->req)) {
		    TRACE(TR_IOP,("initial rd err %d\n",rc));
		}
		cx->flags |= TINPUT;
	    }
	    break;

	default:				/* all others */
	    ctl_output(tp,cmd,cx);		/* do it or queue it */
	    break;
    }

    splx(s);
    return(0);
}

/*-------------------------------*/
STATIC void
t_output(tp,cx)
register struct tty *tp;
register struct context *cx;
{
    static struct cmdprio {
	u_long	bit;			/* which bit it is */
	int	cmd;			/* command value */
    } cmdprio[] = {
	{ 1 << T_RESET, T_RESET },		/* reset channel */
	{ 1 << T_BREAK, T_BREAK },		/* start a break */
	{ 1 << T_TIME, T_TIME },		/* stop  a break */
	{ 1 << T_SETBRKMODE, T_SETBRKMODE },	/* set break mode */
	{ 1 << T_CLRBRKMODE, T_CLRBRKMODE },	/* clear break mode */
	{ 1 << T_SETDTR, T_SETDTR },		/* assert DTR */
	{ 1 << T_CLRDTR, T_CLRDTR },		/* clear  DTR */
	{ 1 << T_PARM, T_PARM },		/* set parameters */
        { 1 << T_SUSPEND, T_SUSPEND },		/* stop all output */
	{ 1 << T_RESUME, T_RESUME },		/* start output again */
	{ 1 << T_UNBLOCK, T_UNBLOCK },		/* send XON to restart input */
	{ 1 << T_BLOCK, T_BLOCK },		/* send XOFF to block input*/
	{ 0, 0},
    };

    struct cmdprio *cp;
    struct cmd_write *wp;

    register struct ccblock *tbuf;
    register struct iopreq *req;
    register int count;
    register int rc;

    TRACE(TR_OUT,("t_output\n"));

    /* If something's queued, do it first. We've arranged the
     * commands in priority order in the table above.  If the
     * control went off, then we're happy. If
     */

    if (cx->defer) {
	for (cp = &cmdprio[0]; cp->bit; cp++) {
	    if (cx->defer & cp->bit) {
		cx->defer &= ~cp->bit;
		TRACE(TR_DQCTL,("dq ctl %d\n",cp->cmd));
		ctl_output(tp,cp->cmd,cx);	/* do it or (re)queue it */
	    }
	}
    }

    if (tp->t_state & (TTSTOP | TIMEOUT | BUSY)) {
	TRACE(TR_OUT,("t_output:busy\n"));
	return;
    }

    /*
     *	If there is no data in the buffer, get some more.
     *	If no more is available then return.
     */

    tbuf = &tp->t_tbuf;
    if ((tbuf->c_ptr == 0) || (tbuf->c_count == 0)) {
	if (tbuf->c_ptr) {
	    tbuf->c_ptr -= tbuf->c_size;
	}
	TRACE(TR_OUT,("t_output:more chars? "));
	if (!(CPRES & (*linesw[tp->t_line].l_output)(tp))) {
	    TRACE(TR_OUT,("no\n"));
	    return;
	}
    }

    TRACE(TR_OUT,("yes\n"));

    /* Normal character transmission. We copy all characters
     * (up to wbuflen) for one-shot transmission.
     */

    req = &cx->copy;
    req->move.b.buf = tbuf->c_ptr;
    count =  min(tbuf->c_count,cx->wbuflen);
    tbuf->c_count -= count;
    tbuf->c_ptr   += count;

    /* Copy characters to the IOP.  We *know* that IOPCMD_MOVE
     * returns immediately, and then we can start the write.
     */

    req->move.count =  count;
    req->move.type = MV_TO_IOP;
    req->move.offset = cx->wbuf;
    TRACE(TR_OUT,("t_output:copy chars\n"));
    if (rc = ioprequest(req)) {		/* copy the characters */
	TRACE(TR_IOP,("wcopy err %d\n",rc));
    }

    /* Now start the write (transmit) operation. Don't wait for it. */

    tp->t_state |= BUSY;

    req = &cx->iop_ops[OP_WRITE].req;
    wp = (struct cmd_write *)req->msg;
    wp->count = count;			/* count to send */
    TRACE(TR_OUT,("t_output:write %d\n",wp->count));
    if (rc = ioprequest(req)) {		/* start transmitting */
	TRACE(TR_IOP,("xmit err %d\n",rc));
    }
}

/*-------------------------------*/
STATIC void
ctl_output(tp,cmd,cx)			/* issue a simple control */
register struct tty *tp;
register int cmd;
register struct context *cx;
{
    struct iop_ops *iop_ops;
    register struct cmd_generic *mp;
    register struct iopreq *req;
    register int len;
    register int rc;

    iop_ops = &cx->iop_ops[OP_CTL];
    req = &iop_ops->req;

    if (iop_ops->flags & INUSE) {
	cx->defer |= 1 << cmd;		/* queue for later */
	TRACE(TR_QCTL,("ctl_output queing ctl %d\n",cmd));
	return;
    }
    iop_ops->flags |= INUSE;

    TRACE(TR_XCTL,("ctl_output execing cmd %d\n",cmd));

    mp = (struct cmd_generic *)&iop_ops->msg[0];
    mp->code = cc_control;

    switch(cmd) {

	case T_BREAK:				/* start transmitting a break */
	    mp->op = ctl_setbreak;
	    len = sizeof(struct cmd_setbreak);
	    tp->t_state |= TIMEOUT;
	    cx->flags |= BREAKWAIT;		/* must wait for it to take */
	    break;

	case T_TIME:				/* done with T_BREAK */
	    mp->op = ctl_clearbreak;
	    len = sizeof(struct cmd_clearbreak);
	    tp->t_state &= ~TIMEOUT;
	    break;

        case T_SUSPEND:				/* stop all output */
	    mp->op = ctl_setxoff;
	    len = sizeof(struct cmd_setxoff);
	    tp->t_state |= TTSTOP;
	    break;
	
	case T_RESUME:				/* start output again */
	    mp->op = ctl_clearxoff;
	    len = sizeof(struct cmd_clearxoff);
	    tp->t_state &= ~TTSTOP;
	    break;

	case T_UNBLOCK:				/* send XON to restart input */
	    mp->op = ctl_sendxon;
	    len = sizeof(struct cmd_sendxon);
	    tp->t_state &= ~TBLOCK;
	    break;

	case T_BLOCK:				/* send XOFF to block input*/
	    mp->op = ctl_sendxoff;
	    len = sizeof(struct cmd_sendxoff);
	    tp->t_state |= TBLOCK;
	    break;

	case T_SETBRKMODE:	/* set break mode (Mac compatibility ) */
	    mp->op = ctl_setbreak;
	    len = sizeof(struct cmd_setbreak);
	    break;

	case T_CLRBRKMODE:	/* clear break mode (Mac compatibility ) */
	    mp->op = ctl_clearbreak;
	    len = sizeof(struct cmd_clearbreak);
	    break;

	case T_SETDTR:			/* set DTR (Mac compatibility ) */
	    mp->op = ctl_assertdtr;
	    len = sizeof(struct cmd_assertdtr);
	    break;

	case T_CLRDTR:			/* clear DTR (Mac compatibility ) */
	    mp->op = ctl_cleardtr;
	    len = sizeof(struct cmd_cleardtr);
	    break;

	case T_RESET:		/* reset channel (Mac compatibility ) */
	    mp->op = ctl_resetSCC;
	    len = sizeof(struct cmd_resetSCC);

	    /* Reset the printer/modem port default handshaking. */

	    cx->flags &= ~(MODEMCTL | DTRFLOW);
	    if (cx == &context[MODEM_PORT]) {
		cx->flags |= MODEMCTL;
	    } else {
		cx->flags |= DTRFLOW;
	    }
	    len = 0;
	    break;

	case T_PARM:				/* call the param routine */
	    TRACE(TR_FUNC,("T_PARM, calling devparms\n"));
	    devparms(cx,req);
	    len = 0;
	    break;
	
	default:
	    TRACE(TR_FUNC,("ctl_output ignoring cmd %d\n",cmd));
	    return;
    }

    if (len) {
	req->msglen = len;
    }

    if (rc = ioprequest(req)) {		/* issue the command to the IOP */
	TRACE(TR_IOP,("ctl_output err %d\n",rc));
    }
    TRACE(TR_FUNC,("ctl_output sent %x\n",req));
}

/*-------------------------------*/
STATIC int
hsoptions(cx,req)			/* issue handshake options */
register struct context *cx;
register struct iopreq *req;
{
    register struct cmd_extshake *ep;
    register struct tty *tp;
    register int iflag;

    TRACE(TR_FUNC,("hsoptions\n"));

    tp = cx->tp;
    req->msglen   = sizeof(struct cmd_extshake);
    req->replylen = sizeof(struct cmd_extshake);

    ep = (struct cmd_extshake *)req->msg;
    ep->code = cc_control;
    ep->op = ctl_extshake;

    ep->indtr = 0;					/* never input DTR */
    ep->outcts = (cx->flags & DTRFLOW) ? 1 : 0;		/* output DTR */

    iflag = tp->t_iflag;
    ep->inxon = (iflag & IXOFF) ? 1 : 0;		/* input  XON/XOFF */
    if (iflag & IXON) {
	ep->outxon = 1;
	ep->xoffchar = tp->t_chars.tc_stopc;
	ep->xonchar = tp->t_chars.tc_startc;
    } else {
	ep->outxon = 0;
    }
    ep->errabort = (ABT_FRAMING |  ABT_OVERRUN);
    if (iflag & INPCK) {
	ep->errabort |= ABT_PARITY;
    }

    ep->statevent = POST_BREAK | POST_CTS;

    TRACE(TR_PARMS,("hsopt:outxon=%d,outcts=%d,xon=%x,xoff=%x,",
	ep->outxon,ep->outcts,ep->xonchar,ep->xoffchar));
    TRACE(TR_PARMS,("abt=%x,event=%x,inxon=%d,indtr=%d\n",
	ep->errabort,ep->statevent,ep->inxon,ep->indtr));

    return(iopreq_wait(cx,req));
}

/*-------------------------------*/
/*
 *	Setup the message request to set device dependent physical parameters
 */
STATIC void
devparms(cx,req)
register struct context *cx;
register struct iopreq *req;
{
    register struct cnfg *cnfg;
    register struct cmd_reset *rp;
    register struct cmd_cleardtr *cp;
    register int flag;

    TRACE(TR_FUNC,("devparms\n"));
    TRACE(TR_PARMS,("cx=%x,tp=%x,cflag=%x,iflag=%x,oflag=%x,lflag=%x\n",
		    cx,cx->tp, cx->tp->t_cflag, cx->tp->t_iflag,
		    cx->tp->t_oflag, cx->tp->t_lflag));

    flag = cx->tp->t_cflag;

    if ((flag & CBAUD) == B0) {			/* just drop DTR */
	cp = (struct cmd_cleardtr *)(req->msg);
	cp->code = cc_control;
	cp->op = ctl_cleardtr;
	req->msglen   = sizeof(struct cmd_cleardtr);
	req->replylen = sizeof(struct cmd_cleardtr);
	TRACE(TR_PARMS,("devparms: B0, dropping DTR\n"));
	return;
    }

    if ((flag & CLOCAL) && (cx->flags & MODEMCTL) && 
	(cx->tp->t_state & (ISOPEN|WOPEN))) {
	if ((cx->tp->t_state & CARR_ON) == 0) {
	    cx->tp->t_state |= CARR_ON;
	    if (cx->tp->t_state & WOPEN) {
		wakeup((caddr_t)&cx->tp->t_rawq);
	    }
	}
    }

    rp = (struct cmd_reset *)(req->msg);
    cnfg = (struct cnfg *)(&rp->config);

    switch (flag & CSIZE) {
	case CS5:
	    cnfg->charbits = cnf_5bits;
	    break;
	case CS6:
	    cnfg->charbits = cnf_6bits;
	    break;
	case CS7:
	    cnfg->charbits = cnf_7bits;
	    break;
	case CS8:
	    cnfg->charbits = cnf_8bits;
	    break;
    }

    if (flag & CSTOPB) {
	cnfg->stopbits = cnf_2stop;
    } else {
	if (flag & CSTOPB15) {
	    cnfg->stopbits = cnf_15stop;
	} else {
	    cnfg->stopbits = cnf_1stop;
	}
    }

    cnfg->parity = cnf_noparity;
    if (flag & PARENB) {
	if (flag & PARODD) {
	    cnfg->parity = cnf_oddparity;
	} else {
	    cnfg->parity = cnf_evenparity;
	}
    }
    rp->code = cc_control;
    rp->op = ctl_reset;
    rp->divisor = siopspeeds[flag & CBAUD];

    req->msglen   = sizeof(struct cmd_reset);
    req->replylen = sizeof(struct cmd_reset);

    TRACE(TR_PARMS,
	("cnfg:stopbits=%d,parity=%d,charbits=%d,divisor=%d,cnfg wd=%x\n",
	    cnfg->stopbits,cnfg->parity,cnfg->charbits,rp->divisor,
	    (*((unsigned short *)(&rp->config))) & 0xffff));
}

/*-------------------------------*/
/* We're called here from the iopmgr on completion of a read.
 * NB: We are running at SPLIOP().
 */
STATIC int
iopret_read(req)
struct iopreq *req;
{
    register struct cmd_read *rp;
    register struct tty *tp;
    register int flag;
    register int count;
    register int rc;
    struct context *cx;

    cx = (struct context *)req->driver;

    TRACE(TR_INPUT,("iopret_read.."));

    /* If we aren't open, forget it. */

    if (!(cx->flags & OPEN) || cx->flags & CLOSING) {
	TRACE(TR_INPUT,("ignoring\n"));
	return(0);
    }

    tp = cx->tp;
    rp = (struct cmd_read *)req->msg;

    TRACE(TR_INPUT,("processing\n"));
    TRACE(TR_INPUT,("result=%d, taken=%d\n",rp->result,rp->taken));
    TRACE(TR_INPUT,("ringbuf=%x, ringend=%x, first=%x\n",
	rp->u.ringbuf,rp->ringend,rp->first));
    TRACE(TR_INPUT,("buflen=%x, abortcnt=%d\n",
	rp->buflen,rp->abortcnt));

    getstatus(cx);			/* get updated status */

    /* If we're observing XON/XOFF and IXANY is set and we get
     * any character, we have to inform the IOP to begin output again.
     *
     * The IOP updates its XON/XOFF status after each character
     * read, so we can tell if the IOP has stopped sending due to an XOFF.
     *
     * The better solution, of course, would be for the IOP to do IXANY!
     *
     * NB: Normal "dumb chip" drivers have to "eat" incoming
     *     XON or XOFF characters, but we know we'll never
     *     get such a character from the IOP.  Small favors...
     */

    flag = tp->t_iflag;

    if ((flag & IXON) && (flag & IXANY)) {
	if (cx->status.xOffHold) {		/* output was XOFFed */
	    (*tp->t_proc)(tp,T_RESUME);		/*  so resume it now */
	    TRACE(TR_INPUT,("IXANY resume\n"));
	}
    }

    /* We have four cases of completion of the input:
     *
     * RESULT	BUFLEN	ABORTCNT
     *
     * 0	>0	0	buflen bytes of normal, good data
     * 255	0	0	abort alone
     * 255	>0	>0	buflen data, abort
     * 255	>0	<buflen	abortcnt data, abort, (buflen - abortcnt) data
     */

    rp->taken = 0;				/* no good bytes taken yet */

    if (rp->result == 0) {			/* no abort */
	TRACE(TR_INPUT,("%d data only\n",rp->buflen));
	dodata(cx,rp,rp->buflen);
	rp->u.abortack = 0;			/* no abort to take */

    } else {					/* an abort occurred */

	if (rp->buflen > 0) {			/* some data */
	    if (rp->abortcnt > 0) {		/* data + abort */
		TRACE(TR_INPUT,("%d data + abort at %d\n",
		    rp->buflen,rp->abortcnt));
		dodata(cx,rp,rp->abortcnt);
		doabort(cx);
		if (rp->buflen > 0) {		/* more data after abort */
		    TRACE(TR_INPUT,("doing last %d data\n",rp->buflen));
		    dodata(cx,rp,rp->buflen);
		}
	    } else {
		TRACE(TR_INPUT,("oops! abort + data ?\n"));
		doabort(cx);
	    }
	} else {				/* abort only */
	    TRACE(TR_INPUT,("abort only\n"));
	    doabort(cx);
	}
	rp->u.abortack = 1;			/* ack after u.ringbuf used */
    }

    /* Schedule the next read request. */

    if ((cx->flags & OPEN) && !(cx->flags & CLOSING)) {
	timeout(nextread,(caddr_t)cx,siop_ticks); /* schedule next read */
    }
}
    
/*-------------------------------*/
STATIC int
doabort(cx)
register struct context *cx;
{
    register struct tty *tp;
    register struct ccblock *cbp;
    register char *op;
    register int flag;

    tp = cx->tp;
    flag = tp->t_iflag;
    cbp = &tp->t_rbuf;
    op = cbp->c_ptr;

    if (!(flag & IGNPAR)) {
	if (flag & PARMRK) {		/* if marking, add 2 char marker */
	    *op++ = 0xff;
	    *op++ = 0x00;
	    cbp->c_count -= 2;		/* the marker characters */
	}
	*op = 0x00;			/* NUL since IOP doesn't return it */
	cbp->c_count -= 1;

	(*linesw[tp->t_line].l_input)(tp, L_BUF);
    }

    /* Count the error statistics: */

    if (cx->status.cumErrs & Eframing) {
	cx->framerrs++;
    }
    if (cx->status.cumErrs & (Eoverrun | Eoverflow)) {
	cx->overruns++;
    }
    if (cx->status.cumErrs & Eparity) {
	cx->parityerrs++;
    }
    return(0);
}

/*-------------------------------*/
STATIC char siopbuf[CLSIZE];		/* never need more than this */

STATIC int
dodata(cx,rp,resid)			/* handles circ. buffer for n bytes */
struct context *cx;
register struct cmd_read *rp;
register int resid;
{
    struct tty *tp;
    register struct ccblock *cbp;
    register char *ip;
    register char *op;
    register int count;
    register u_char c;
    register int flag;

    tp = cx->tp;
    flag = tp->t_iflag;

    while (resid > 0) {

	cbp = &tp->t_rbuf;

	if (cbp->c_ptr) {			/* take the data */

	    /* Copy incoming data to our temp buffer.  We copy the
	     * smallest of the following sizes:
	     *
	     *  resid			(what's left of the requested amount)
	     *  CLSIZE			(size of a clist buffer)
	     *	rp->ringend - rp->first	(bytes to end of ring buffer)
	     */

	    count = min(resid,CLSIZE);
	    /* count = min(resid,cbp->c_count); */
	    count = min(count,(rp->ringend - rp->first));

	    copydata(cx,rp->first,siopbuf,count);	/* get a chunk */

	    /* Now move the data from our temp buffer to the clist.
	     * We have to do this step anyway to handle the case of
	     * PARMRK && !ISTRIP, where 0xff characters must get
	     * sent up as 0xff 0xff.  Plus we'd scan the data anyway
	     * if ISTRIP is set.
	     *
	     * Copying from the temp buf to the clist allows us to
	     * expand the incoming data for the PARMRK !ISTRIP case.
	     * After we move the data to the clist, we recalculate
	     * count, which compensates for the case where we can't
	     * fit the entire siopbuf into the clist (due to expansion
	     * of 0xff characters).  This recalculation causes the outer
	     * loop to re-copy the bytes not yet moved to the clist.
	     */

	    ip = siopbuf;
	    op = cbp->c_ptr;
	    do {
		c = *ip;
		if (flag & ISTRIP) {
		    c &= 0x7f;
		} else {
		    if (flag & PARMRK) {
			if (c == 0xff) {
			    if (cbp->c_count < 2) {	/* no room this clist */
				break;
			    }
			    *op++ = c;		/* convert to 0xff 0xff */
			    cbp->c_count--;
			}
		    }
		}
		*op++ = c;			/* char to clist */
		cbp->c_count--;
		ip++;				/* char got moved to clist */
	    } while ((cbp->c_count > 0) && (ip < &siopbuf[count]));

	    TRACE(TR_INPUT,("%d byte clist done. buf = %d bytes, %d left\n",
		(cbp->c_size - cbp->c_count),count,(count - (ip - siopbuf))));

	    (*linesw[tp->t_line].l_input)(tp, L_BUF);

	    count = ip - siopbuf;		/* how many actually done */

	} else {				/* no clist? toss it all */
	    count = resid;
	}

	TRACE(TR_INPUT,("actual bytecount done = %d\n",count));

	rp->buflen -= count;
	rp->taken  += count;		/* how much we took from IOP */
	rp->first  += count;		/* bump buf start */

	if (rp->first >= rp->ringend) {	/* wrap to beginning */
	    rp->first = rp->u.ringbuf;
	    TRACE(TR_INPUT,("ringbuf wraparound,rp->first now %x\n",rp->first));
	}

	resid -= count;

    } /* end while */

    return(0);
}

/*-------------------------------*/
STATIC int
copydata(cx,from,to,count)		/* copies contiguous good data */
register struct context *cx;
register u_short from;
register caddr_t to;
register int count;
{
    register struct iopreq *copyreq;
    register caddr_t cp;
    register int rc;

    TRACE(TR_RCOPY,("copying %d chars from %x\n",count,from));

    if (count == 0) {
	return(0);
    }

    /* The copy completes immediately. */

    copyreq = &cx->copy;
    copyreq->move.type = MV_TO_CPU;
    copyreq->move.count = count;
    copyreq->move.offset = from;
    copyreq->move.b.buf = to;
    if (rc = ioprequest(copyreq)) {
	TRACE(TR_IOP,("rcopy err %d\n",rc));
    }

    if (siop_trace & TR_RCOPY) {	/* show 'em all */
	TRACE(TR_RCOPY,("got "));
	cp = to;
	while (cp < (to + count)) {
	    printf("%x ",(*cp++ & 0xff));
	}
	printf("\n");
    }

    return(0);
}

/*-------------------------------*/
STATIC int
nextread(cx)
register struct context *cx;
{
    register int rc;

    if ((cx->flags & OPEN) && !(cx->flags & CLOSING)) {
	TRACE(TR_INPUT,("issuing next read\n"));
	if (rc = ioprequest(&cx->iop_ops[OP_READ].req)) {
	    TRACE(TR_IOP,("err %d issuing read\n",rc));
	}
    }

    return(0);
}

/*-------------------------------*/
STATIC void
getstatus(cx)
register struct context *cx;
{
    static u_char ffbuf = 0xff;

    register struct iopreq *req;
    register int rc;
    register int s;

    TRACE(TR_FUNC,("getstatus\n"));
    s = SPLIOP();			/* while using cx->move */

    req = &cx->copy;
    req->move.count = sizeof(struct serstatus);
    req->move.b.buf = (caddr_t)&cx->status;
    req->move.offset = cx->statbuf + 1;
    req->move.type = MV_TO_CPU;

    if (rc = ioprequest(req)) {
	TRACE(TR_IOP,("getstatus err %d\n",rc));
    }

    TRACE(TR_STAT,("status: cumErrs=%x xOffSent=%x ctsHold=%x xOffHold=%x\n",
			cx->status.cumErrs,cx->status.xOffSent,
			cx->status.ctsHold,cx->status.xOffHold));
    
    /* Tell the IOP to reset the error flags. */

    req->move.count = 1;
    req->move.b.buf = (caddr_t)&ffbuf;
    req->move.offset -= 1;		/* hit the "reset counts" flag */
    req->move.type = MV_TO_IOP;
    if (rc = ioprequest(req)) {
	TRACE(TR_IOP,("getstatus reset err %d\n",rc));
    }

    splx(s);
}

/*-------------------------------*/
/* We're called here from the iopmgr on an incoming status event.
 * NB: We are running at SPLIOP().
 */
STATIC int
iop_event(req)
register struct iopreq *req;
{
    /* We can use a static structure to acknowledge the incoming
     * event, because we're SPLIOP()'ed in here.
     */

    static struct iopreq ack = {    (struct iopreq *)0,	/* link */
				    {0}, 		/* no move msg */
				    IOPCMD_RCVREPLY,	/* acknowledge it */
				    0,			/* flags */
				    SCC_IOP,		/* which IOP */
				    -1,			/* chan filled in */
				    0,			/* no msglen */
				    0,			/* no replylen */
				    (caddr_t)0,		/* no msg */
				    (caddr_t)0,		/* no reply */
				    (int (*)())0,	/* no handler */
				    (u_long)0,		/* no driver info */
				};
    register struct context *cx;
    register struct tty *tp;
    register int flag;
    register int rc;
    register u_char RR0;
    register u_char changed;

    cx = (struct context *)req->driver;

    RR0     = ((struct sccevent *)req->reply)->reg0;
    changed = ((struct sccevent *)req->reply)->changed;

    /* Acknowledge the message quickly, to avoid missing a quick
     * status change while we're busy in here.  This completes
     * immediately.
     */

    ack.chan = req->chan;		/* acknowledge current channel */
    if (rc = ioprequest(&ack)) {
	TRACE(TR_IOP,("eventcmp err %d\n",rc));
    }

    if (cx->flags & (OPEN | OPENING)) {

	TRACE(TR_EVENT,("iop_event: RR0=%x, changed=%x --",RR0,changed));

	tp = cx->tp;

	/* Handle DTR (really the CTS line) changes.  We always remember
	 *  the current state of DTR, and then do the following IF we
	 *  have MODEMCTL set:
	 *
	 * DTR change:
	 *    UP	Wakeup waiting opens
	 *
	 *   DOWN	Give SIGHUP
	 */

	if (changed & POST_CTS) {		/* then DTR changed */

	    if ((RR0 & POST_CTS) == 0) {	/* DTR is up */

		TRACE(TR_EVENT,("DTR up. "));

		if (!(tp->t_cflag & CLOCAL) && (cx->flags & MODEMCTL)) {
		    if ((tp->t_state & CARR_ON) == 0) {
			if (tp->t_state & WOPEN) {
			    wakeup((caddr_t)&tp->t_rawq);
			    TRACE(TR_EVENT,("waking opens"));
			}
		    }
		}
		tp->t_state |= CARR_ON;
		TRACE(TR_EVENT,("\n"));
		
	    } else {				/* DTR is down */

		TRACE(TR_EVENT,("DTR down. "));

		if (!(tp->t_cflag & CLOCAL) && (cx->flags & MODEMCTL)) {
		    if (tp->t_state & CARR_ON) {
			tp->t_state &= ~CARR_ON;
			if (tp->t_state & ISOPEN) {
			    ttyflush(tp,(FREAD | FWRITE));
			    signal(tp->t_pgrp, SIGHUP);
			    TRACE(TR_EVENT,("SIGHUP"));
			}
		    }
		}
		tp->t_state &= ~CARR_ON;
		TRACE(TR_EVENT,("\n"));
	    }
	}

	/* Handle incoming break. If there's a break, and the user
	 * isn't ignoring break, and we should generate the signal,
	 * we do so.  We act only when the break signal terminates.
	 */

	flag = tp->t_iflag;

	if ((changed & POST_BREAK) && !(RR0 & POST_BREAK)) {
	    if (!(flag & IGNBRK)) {		/* the user isn't ignoring */
		if (flag & BRKINT) {		/* we should signal */
		    (*linesw[tp->t_line].l_input)(tp,L_BREAK);
		} else {			/* maybe PARMRK it */
		    if (flag & PARMRK) {

			register struct ccblock *cbp;
			register char *op;

			cbp = &tp->t_rbuf;
			op = cbp->c_ptr;
			*op++ = 0xff;
			*op++ = 0x00;
			*op++ = 0x00;
			cbp->c_count -= 3;
			(*linesw[tp->t_line].l_input)(tp, L_BUF);
		    }
		}
	    }
	    TRACE(TR_EVENT,("\n"));
	}
    }

    return(0);
}

/*-------------------------------*/
/* Issue an IOP request, wait for completion, return the result. */
STATIC int
iopreq_wait(cx,req)
register struct context *cx;
register struct iopreq *req;
{
    register int rc;
    register int s;

    if (rc = ioprequest(req)) {		/* err if it fails */
	TRACE(TR_IOP,("iopreq_wait err %d\n",rc));
	return(EIO);
    }

    s = SPLIOP();
    while (req->flags & REQ_BUSY) {	/* sleep till done */
	TRACE(TR_SLEEP,("iopreq_wait:sleeping on req %x\n",req));
	sleep((caddr_t)req,PRIBIO);
    }
    splx(s);

    /* An assumption is made here that "result" is always
     * in the same place for ALL requests.
     */

    TRACE(TR_SLEEP,("iopreq_wait:awakened on req %x\n",req));
    return(((struct cmd_generic *)req->msg)->result);
}

/*-------------------------------*/
/* We're called here from the iopmgr on completion of an iop_ops[OP_WRITE]
 * request.  Simply set the tty un-busy, mark the iop_ops free, and call
 * for more output.
 *
 * NB: We are running at SPLIOP().
 */
STATIC int
iopret_write(req)
register struct iopreq *req;
{
    register struct context *cx;
    register struct tty *tp;

    cx = (struct context *)req->driver;
    tp = cx->tp;
    TRACE(TR_OUT,("iopret_write\n"));

    cx->iop_ops[OP_WRITE].flags &= ~INUSE;

    tp->t_state &= ~BUSY;			/* output no longer busy */
    (*tp->t_proc)(tp,T_OUTPUT);			/* restart output */

    return(0);
}


/*-------------------------------*/
/* We're called here from the iopmgr on completion of an iop_ops[OP_CTL]
 * request.  Simply set the tty un-busy, mark the iop_ops free, and call
 * for more output.
 *
 * NB: We are running at SPLIOP().
 */
STATIC int
iopret_ctl(req)
register struct iopreq *req;
{
    extern int ttrstrt();
    register struct context *cx;
    register struct tty *tp;

    cx = (struct context *)req->driver;
    tp = cx->tp;
    TRACE(TR_XCTL,("iopret_ctl\n"));

    cx->iop_ops[OP_CTL].flags &= ~INUSE;

    /* If we've been waiting for a ctl_setbreak to take effect, we
     * can NOW start the ttrstrt() timer for the 1/4 second break.
     * This foolishness is necessary because there's an indeterminate
     * amount of time before the break begins (because the IOP will
     * wait for character bits to drain).  If we start the timeout
     * when issuing the setbreak (and not here, when we *know* it's
     * active), we'd drop the break too soon at low baud rates.
     */
    if (cx->flags & BREAKWAIT) {
	cx->flags &= ~BREAKWAIT;
	timeout(ttrstrt, (caddr_t)tp, v.v_hz>>2);
    }

    (*tp->t_proc)(tp,T_OUTPUT);			/* restart output */

    return(0);
}

/*-------------------------------*/
/* Wake up iopreq_wait on completion of his request.
 * NB: We are running at SPLIOP().
 */
STATIC int
iopret_wake(req)
register struct iopreq *req;
{
    register struct context *cx;

    cx = (struct context *)req->driver;
    /* TRACE(TR_SLEEP,("iopret_wake, wakeup req %x\n",req)); */
    wakeup((caddr_t)req);

    return(0);
}
