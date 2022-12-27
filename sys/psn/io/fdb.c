/*
 * @(#)fdb.c  {Apple version 1.7 90/03/13 12:21:57}
 *
 * Copyright (c) 1987, 1988, 1989 by Apple Computer, Inc.
 * All Rights Reserved.
 *
 * THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF APPLE COMPUTER, INC.
 * The copyright notice above does not evidence any actual or
 * intended publication of such source code.
 */

#if !defined(lint) && !defined(NO_SCCS_IDS)
static char _sccsid[]="@(#)fdb.c  {Apple version 1.7 90/03/13 12:21:57}";
#endif

/*	@(#)fdb.c	UniPlus VVV.2.1.9	*/
/*
 * (C) 1986 UniSoft Corp. of Berkeley CA
 *
 * UniPlus Source Code. This program is proprietary
 * with Unisoft Corporation and is not to be reproduced
 * or used in any manner except as authorized in
 * writing by Unisoft.
 */

#include <sys/types.h>
#include <sys/fdb.h>
#include <sys/via6522.h>
#include <sys/uconfig.h>
#include <sys/reg.h>
#include <sys/debug.h>
#include <sys/iopmgr.h>

#define VIA	((struct via *)VIA1_ADDR)

#define FDB_DELAY(n) asm("	mov.l	(%sp), (%sp)");

/*
 *	This driver provides a medium level front desk bus interrupt handling
 *	mechanism. It processes interrupts at a bus transaction level and knows
 *	nothing about individual devices. At the lowest level the code in fdbs.s
 *	implements a finite state machine (FSM) that processes lower level bus
 *	interrupts (it takes 3 interrupts to complete most bus transactions)
 *	without coming through the (relatively) expensive Unix interrupt
 *	support code for each interrupt.
 *
 *	At higher levels device specific interrupt handlers use this driver
 *	to multiplex their requests onto the bus and to provide support for
 *	device polling and access. (eg keyboard/mouse).
 *
 *	Higher level drivers communicate with this driver by making a variety
 *	of calls to this driver to start a transaction on their behalf. 
 *	Completion of each translation is signalled to the drivers by calling
 *	an interrupt routine that they have provided. This interrupt routine
 *	is also called to signal certain exceptions that require higher level
 *	actions (receipt of an automatic poll, device service request or
 *	cancel service request). There cannot be more than one
 *	outstanding transaction for any particular device.
 *
 *	The device interrupt service routines have 3 parameters. The first
 *	is a 'magic cookie', a number that is passed in along with the 
 *	transaction request that is passed back to the driver to idenify the
 *	device. The second is a command that identifies the request that has
 *	just completed or an exception condition. The final parameter is command
 *	specific (see below).
 *
 *	The following front desk bus (fdb) request routines are supported, they
 *	are shown along with their corresponding interrupt service parameters.
 *
 *	request			result command		parameter
 *	=======			==============		=========
 *
 *	fdb_open()		FDB_EXISTS		0 if the device really
 *								exists
 *							non zero if the device
 *								doesn't exist
 *
 *	fdb_flush()		FDB_FLUSH		(as above)
 *
 *	fdb_listen()		FDB_LISTEN		(as above)
 *
 *	fdb_talk()		FDB_TALK		0 if the device
 *								responded and
 *								valid data was
 *								returned from
 *								the device
 *							non zero if a device
 *								timeout occured
 *								(and no data was
 *								returned)
 *
 *	fdb_close()		(no result)
 *
 *	The fdb driver also responds to two other fdb conditions, one is a
 *	successfull fdb poll (the fdb controller chip keeps on polling the last
 *	device fdb_talk()ed to, if such a poll succeeds with out timeout it will
 *	interrupt the main cpu and the following call will be made to the device
 *	dependent routine
 *
 *				FDB_POLL		the 16-bit data
 *								retrieved from
 *								the device
 *
 *	Also if a device issues a service request (most devices have a bit that
 *	can be set by a fdb_listen() that enables this) occurs the fdb driver
 *	must explicitly poll (issue talk commands) each known device on the
 *	fdb. Since this sort of poll can be rather device dependent the 
 *	following call is made back to the driver. If the driver then requests
 *	a fdb transaction (usually via fdb_talk()) it returns 1 from the 
 *	interrupt service routine to indicate this, otherwise it returns 0.
 *
 *				FDB_INT			(no parameter)
 *
 *	Since there are several devices on the fdb and only one service request
 *	might be active, if the service request goes away before all the polls
 *	have completed (from all the device drivers that returned 1 above) then
 *	the drivers whose transactions have not yet been started have their
 *	transactions canceled and the following call is made to notify them
 *	of this
 *
 *				FDB_UNINT		(no parameter)
 *
 *	fdb_talk() calls made in response to an FDB_INT that complete (either
 *	by timing out or by reading data) before the service request goes away
 *	will complete in the manner described earlier (FDB_TALK)
 *
 *	NOTE: there are two sorts of polling going on here
 *
 *		- polling by the fdb chip automaticly when the fdb is idle
 *			indicated by an FDB_POLL call to a driver
 *
 *		- explicit polling in response to a device service request
 *			(to find the device). This is done by the driver and
 *			results in FDB_INT/FDB_UINT calls.
 *
 *
 *	Most higher level drivers are also FSMs they start by opening the fdb
 *	device (using fdb_open) and then continue using later events
 *	(interrupts) to move them from state to state. What follows is an
 *	example of the state transitions of such a driver.
 *
 *	Device Driver				FDB Driver
 *	=============				==========
 *
 *	fdb_open()	------------------------>
 *						.
 *						.
 *						.
 *	FDB_EXISTS	<------------------------
 *	fdb_flush()	------------------------>
 *						.
 *						.
 *						.
 *	FDB_FLUSH	<------------------------
 *	fdb_listen()	------------------------>	(to set the service
 *						.	 request enable bit)
 *						.
 *						.
 *	FDB_EXISTS	<------------------------
 *	fdb_talk()	------------------------>	(to read the device's
 *						.	 initial state
 *						.
 *						.
 *	FDB_TALK	<------------------------
 *
 *	At this point the device driver does fdb_talk()s until the device
 *	returns a timeout. After which it responds to FDB_POLLs by returning the
 *	data given by the poll. If it receives an FDB_INT it starts an
 *	fdb_talk() (if none are in progress at that time) and if it
 *	receives an FDB_UNINT is notes that no transactions are in progress.
 *	Whenever is receives a successfull FDB_TALK without timeout it
 *	automaticly does another fdb_talk() (until it gets a timeout). The
 *	reason for this is that one can make the assumption that the device
 *	that last was active (ie mouse) will be active next, therefore one wants
 *	to leave the active device the last to use the fdb (and hence be 
 *	automaticly polled by the fdb interface chip).
 */

extern char via1_soft;
extern char via2_soft;
extern int T_fdb;
extern int nulldev();
extern short machineID;

typedef int (*procedure_t)();
static procedure_t fdb_int[] = {
	nulldev, nulldev, nulldev, nulldev, 
	nulldev, nulldev, nulldev, nulldev,
	nulldev, nulldev, nulldev, nulldev,
	nulldev, nulldev, nulldev, nulldev,
};

static unsigned short fdb_mask[] = {
	0x0001, 0x0002, 0x0004, 0x0008,
	0x0010, 0x0020, 0x0040, 0x0080,
	0x0100, 0x0200, 0x0400, 0x0800,
	0x1000, 0x2000, 0x4000, 0x8000,
};

char fdb_timeout;		/* timeout flag from ISR */

char fdb_select;		/* select (interrupt) flag from ISR */

char fdb_state;			/* current state for ISR */

char fdb_command;		/* current command for ISR */

char fdb_cnt;			/* current count for ISR */

char fdb_error;			/* fdb error status */

char fdb_pollflg;		/* a poll occured */

static char fdb_polling;	/* there are 'n' devices trying to poll */

static char fdb_fake;		/* a 'fake' transaction is in progress 
				   this is when there is an attention
				   interrupt from the fdb, but nothing
				   ready to run */

static char fdb_last;		/* the last thing accessed (ie the thing
				   being polled) */

static char fdb_opn[16];	/* the 'port' is open */

static char fdb_poll[16];	/* a poll is in progress for this device */

static char fdb_cmd[16];	/* pending commands */

static char fdb_reg[16];	/* pending command registers */
	
static char *fdb_data[16];	/* pointers to pending data */

static int fdb_count[16];	/* pending listen data lengths */

static char fdb_id[16];		/* magic cookie */

char *fdb_datap;		/* current data pointer */

char fdb_exb[2];		/* buffer used to read R3 when checking for
					device existance, also used for
					the results of polls */

static unsigned int fdb_request;	/* requests out standing */

static unsigned short fdb_current;	/* current request */

static int (*fdb_start)();	/* the proper start routine */

static int via_fdb_start();
static int iop_fdb_start();

unsigned short fdb_result;

static struct iopreq fdb_rcvreq;	/* for incoming unsolicited messages */
static struct adbmsg fdb_automsg;

static struct iopreq fdb_req;		/* for commands */
static struct adbmsg fdb_msg;

/*
 *	Initialize the via .... then run a fdb reset transaction
 */

fdb_init()
{
    int fdb_incoming();

    register struct iopreq *req;

    fdb_request = 0;
    fdb_current = 16;

    if (machineID != MACIIfx) {		/* do it the old way */

	fdb_last = 3;
	VIA->regb |= 0x30;
	VIA->ddrb |= 0x30;
	VIA->ier = VIE_SR | VIE_SET;	/* enable shift register interrupt */

	fdb_start = via_fdb_start;
    
    } else {				/* do it thru the IOP */

	fdb_start = iop_fdb_start;

	/* NB: We don't get control back when the ADB IOP accepts the
	 * request (would be via req->handler), since we get completions
	 * thru fdb_incoming always.
	 */
	req = &fdb_req;			/* generic init of sendmsg req */
	req->req = IOPCMD_SEND;
	req->iop = ADB_IOP;
	req->chan = ADB_CHAN;
	req->msg = (caddr_t)&fdb_msg;
	req->msglen = sizeof(struct adbmsg);

	req = &fdb_rcvreq;
	req->req = IOPCMD_ADDRCVR;	/* install receiver for incomings */
	req->iop = ADB_IOP;
	req->chan = ADB_CHAN;
	req->reply = (caddr_t)&fdb_automsg;
	req->replylen = sizeof(struct adbmsg);
	req->handler = fdb_incoming;
	(void)ioprequest(req);		/* install us */
    }

    fdb_reset();
}

/*
 *	The main purpose of an fdb_open call is to associate an interrupt
 *		service routine and an 'id' (a number passed back to the
 *		interrupt routine to distinguish between devices of the
 *		same type). Finally we run an 'exists' cycle to the device,
 *		this allows us to start the device's state machine and to
 *		tell whether or not the device actually exists.
 *
 *	This routine should be called at spl1.
 */

fdb_open(n, id, intr)
procedure_t intr;
{
	register int s;

	TRACE(T_fdb, ("fdb_open(%d,%d)\n",n,id));
	if (n < 0 || n > 15 || fdb_int[n] != nulldev) {
		return(1);
	}
	s = SPLIOP();			/* prevent incoming IOP interference */
	fdb_request &= ~fdb_mask[n];
	fdb_int[n] = intr;
	fdb_opn[n] = 1;
	fdb_exists(n, id);
	splx(s);
	return(0);
}

/*
 *	Close simply removes the interrupt service routine and marks the
 *		device as not open (so that it will not be polled on
 *		service interrupts). Any pending requests are canceled.
 *		This routine should be called at spl1.
 */

fdb_close(n)
{
	register int s;

	TRACE(T_fdb, ("fdb_close(%d)\n",n));
	if (n < 0 || n > 15) {
		return(1);
	}
	s = SPLIOP();			/* prevent incoming IOP interference */
	fdb_request &= ~fdb_mask[n];
	fdb_int[n] = nulldev;
	fdb_opn[n] = 0;
	splx(s);
	return(0);
}

/*
 *	fdb_listen starts a front desk bus listen transaction (ie send
 *		data to a device)
 *		n		front desk bus address 0-15
 *		id		magic cookie to identify the device
 *				to the interrupt routine (usually the
 *				device's minor number)
 *		reg		the register being written (0-3)
 *		vp		the address of the buffer being written from
 *		count		the number of bytes to be written (usually 2)
 *
 *	This routine should be called at spl1.
 */

fdb_listen(n, id, reg, vp, count)
register int n;
char *vp;
{
	register int s;

	TRACE(T_fdb, ("fdb_listen(%d,%d,%d,%d)\n",n,id,reg,count));
	s = SPLIOP();			/* prevent incoming IOP interference */
	fdb_cmd[n] = FDB_LISTEN;
	fdb_reg[n] = reg;
	fdb_data[n] = vp;
	fdb_count[n] = count;
	fdb_id[n] = id;
	if (fdb_request) {
		fdb_request |= fdb_mask[n];
	} else {
	    fdb_request = fdb_mask[n];
	    (*fdb_start)(n, reg, vp);
	}
	splx(s);
}

/*
 *	fdb_talk starts a front desk bus talk transaction (ie read
 *		data from a device)
 *		n		front desk bus address 0-15
 *		id		magic cookie to identify the device
 *				to the interrupt routine (usually the
 *				device's minor number)
 *		reg		the register being read (0-3)
 *		vp		the address of the buffer being written to
 *
 *	This routine should be called at spl1.
 */


fdb_talk(n, id, reg, vp)
register int n;
char *vp;
{
	register int s;

	TRACE(T_fdb, ("fdb_talk(%d,%d,%d)\n",n,id,reg));
	s = SPLIOP();			/* prevent incoming IOP interference */
	fdb_cmd[n] = FDB_TALK;
	fdb_reg[n] = reg;
	fdb_data[n] = vp;
	fdb_id[n] = id;
	if (fdb_request) {
		fdb_request |= fdb_mask[n];
	} else {
		fdb_request = fdb_mask[n];
		(*fdb_start)(n, reg, vp);
	}
	splx(s);
}

/*
 *	fdb_exists starts a front desk bus talk transaction (ie read
 *		data from a device) to register 3
 *		n		front desk bus address 0-15
 *		id		magic cookie to identify the device
 *				to the interrupt routine (usually the
 *				device's minor number)
 *
 *	This routine should be called at spl1. It is normally called from
 *	fdb_open. If it completes with timeout it means that the device
 *	does not exist.
 */

fdb_exists(n, id)
register int n;
{
	register int s;

	TRACE(T_fdb, ("fdb_exists(%d,%d)\n",n,id));
	s = SPLIOP();			/* prevent incoming IOP interference */
	fdb_cmd[n] = FDB_EXISTS;
	fdb_reg[n] = 3;
	fdb_data[n] = fdb_exb;
	fdb_id[n] = id;
	if (fdb_request) {
		fdb_request |= fdb_mask[n];
	} else {
		fdb_request = fdb_mask[n];
		(*fdb_start)(n, 3, fdb_exb);
	}
	splx(s);
	
}

/*
 *	fdb_flush starts a front desk bus flush transaction
 *		n		front desk bus address 0-15
 *		id		magic cookie to identify the device
 *				to the interrupt routine (usually the
 *				device's minor number)
 *
 *	This routine should be called at spl1.
 */

fdb_flush(n, id)
register int n;
{
	register int s;

	TRACE(T_fdb, ("fdb_flush(%d,%d)\n",n,id));
	s = SPLIOP();			/* prevent incoming IOP interference */
	fdb_cmd[n] = FDB_FLUSH;
	fdb_id[n] = id;
	if (fdb_request) {
		fdb_request |= fdb_mask[n];
	} else {
		fdb_request = fdb_mask[n];
		(*fdb_start)(n, 0, 0);
	}
	splx(s);
}

/*
 *	fdb_reset starts a front desk bus reset transaction
 *	This routine should be called at spl1. It is the only
 *	fdb transaction that is not directed at a particular 
 *	device.
 */

fdb_reset()
{
	register int s;

	TRACE(T_fdb, ("fdb_reset\n"));
	s = SPLIOP();			/* prevent incoming IOP interference */
	fdb_request = 0x10000;
	(*fdb_start)(16, 0, 0);
	splx(s);
}

/*
 *	iop_fdb_start works like via_fdb_start but uses the IOP.
 */

static int
iop_fdb_start(n,reg,vp)		/* issue a command thru the IOP manager */
register int n;
int reg;
char *vp;
{
    register struct adbmsg *msg = &fdb_msg;
    register int cmd;

    fdb_datap = vp;		/* where caller wants the data */

    if (n == 16) {
	msg->cmd = F_RESET;
    } else  {
	switch (fdb_cmd[n]) {

	    case FDB_LISTEN:
		msg->cmd = F_LISTEN | (n<<4) | reg;
		break;

	    case FDB_TALK:
		msg->cmd = F_TALK | (n<<4) | reg;
		break;

	    case FDB_EXISTS:
		msg->cmd = F_TALK | (n<<4) | 3;
		break;

	    case FDB_FLUSH:
		msg->cmd = F_FLUSH | (n<<4);
		break;
	}

	msg->count = fdb_count[n];
	if (msg->count <= 8 && vp) {
	    blt(&msg->data[0],vp,msg->count);	/* message data */
	}
    }

    msg->flags = ADB_EXPLICIT | ADB_AUTOPOLL;

    fdb_current = n;			/* remember for return */
#ifdef notdef
    printf("cmd: %x %x %x\n",msg->flags,msg->count,msg->cmd);
#endif
    (void)ioprequest(&fdb_req);		/* shoot off the request */
}

/*
 *	fdb_incoming handles autopoll/SRQ messages and completions from the IOP.
 */
fdb_incoming(req)
register struct iopreq *req;
{
    static struct iopreq replyreq;
    register struct adbmsg *msg = (struct adbmsg *)req->reply;
    register int n;
    register int comp;

    /* Callback the higher level who did the command: */

    n = fdb_current;			/* the current device */

    /* Figure out what came back: */

    if (msg->cmd == F_RESET || n > 15) { /* nobody gets notified */
	comp = FDB_RESET;
	fdb_request &= 0xffff;
    } else {

	/* If it's an explicit command, copy the reply data to the
	 * requested buffer and call back the sender of the message.
	 * If it's an implicit poll, call the last active driver with a
	 * FDB_POLL and the reply data as a parameter.
	 */

	if (msg->flags & ADB_EXPLICIT) {	/* an explicit command */
	    comp = fdb_cmd[n];		/* recall it */

	    if (msg->count <= 8 && fdb_datap) {	/* copy the reply data */
		blt(fdb_datap,&msg->data[0],msg->count);
	    }

	    fdb_request &= ~fdb_mask[n];	/* allow issuing new cmd */
	    (*(fdb_int[n]))(fdb_id[n],comp,(msg->flags & ADB_TIMEOUT));

	} else {				/* IMPLICIT poll */

	    /* The device is in the command byte: */

	    n = msg->cmd >> 4;
	    (*(fdb_int[n]))(fdb_id[n],FDB_POLL,
					*((unsigned short *)(&msg->data[0])));
	}
#ifdef notdef
	printf("int: %x %x %d %x %x\n",fdb_int[n],fdb_datap,msg->count,comp,
					(msg->flags & ADB_TIMEOUT));
#endif
    }

    /* Tell the IOP we're done with the message: */

    msg->flags = ADB_AUTOPOLL;		/* just re-enable auto-polling */
    replyreq = *req;			/* send back entire incoming msg */
    replyreq.req = IOPCMD_RCVREPLY;
    replyreq.flags = 0;
    (void)ioprequest(&replyreq);

    /* If there are any other requests queued up, send one out now: */

    if (fdb_request && !fdb_req.flags) {
	for (n = 0; n < 16; n++) {
	    if (fdb_request & fdb_mask[n]) {
		iop_fdb_start(n,fdb_reg[n],fdb_data[n]);
		break;
	    }
	}
    }

    return(0);
}

/*
 *	fdb_start starts a fdb transaction. It calculates the command and
 *	sets up the global variables that are accessed by the FSM in fdbs.s
 *	finally it puts the FDB chip into state 0 and writes a command to
 *	the via shift register to start the transaction. The (interrupt driven)
 *	FSM (in fdbs.s) then goes through the handshakes to pass data to/from
 *	the device being addressed. Finally after the last interrupt the device
 *	is put into state 3 and fdb_int is called. NOTE every call of fdb_start
 *	results in a later call of fdb_int (however fdb_int can be called
 *	without a corresponding call of fdb_start - from a device service
 *	request, also the stransaction started by the fdb_start may not have
 *	completed correctly, there may have been a previous poll succeed, and
 *	as a result the transaction may have to be rerun).
 *	
 */

static
via_fdb_start(n, reg, vp)
register int n;
char *vp;
{
	register int cmd;
	register int s;

	TRACE(T_fdb, ("via_fdb_start(%d, %d)\n", n, reg));
	fdb_fake = 0;
	fdb_datap = vp;
	if (n == 16) {
		cmd = F_RESET;
		fdb_command = FDB_RESET;
	} else  {
		switch (fdb_command = fdb_cmd[n]) {
		case FDB_LISTEN:
			cmd = F_LISTEN | (n<<4) | reg;
			fdb_cnt = fdb_count[n];
			break;

		case FDB_TALK:
			cmd = F_TALK | (n<<4) | reg;
			break;

		case FDB_EXISTS:
			cmd = F_TALK | (n<<4) | 3;
			break;

		case FDB_FLUSH:
			cmd = F_FLUSH | (n<<4);
			break;
		}
	}
	fdb_current 	= n;
	fdb_state 	= 1;
	s = spl4();
	VIA->acr 	&= 0xe3;
	FDB_DELAY(1);
	VIA->acr 	|= 0x1c;
	FDB_DELAY(1);
	VIA->sr 	= cmd;
	FDB_DELAY(1);
	VIA->regb 	= (VIA->regb&0xcf)|0x00;
	splx(s);
}

/*
 *
 *	This is the fdb interrupt service routine. It must be able
 *	handle a number of different cases:
 *
 *	Event				Action
 *	=====				======
 *
 *	Completion of a requested	Call the device's completion routine
 *	transaction			passing the transaction type and a
 *					timeout indication. Then start
 *					any pending requests.
 *
 *	Completion of a transaction	Call the previous devices routine
 *	with the poll bit set		with FDB_POLL and passing the
 *					retreived data. Restart the previous
 *					(unsuccessfull) transaction
 *
 *	Completion of a transaction	If polling is not already being done
 *	with the service bit set	call every open device's routine passing
 *					FDB_INT (they return 1 if they started a
 *					talk to poll the device). 
 *
 *	Completion of a transaction 	If any devices that had FDB_INT calls
 *	without the service bit set	made to them have not yet had their
 *					poll transactions run cancel them
 *					and call the driver with FDB_UNINT
 *					to inform them of this.
 *
 *	Receive an interrupt when	run a 'dummy' transaction to get the
 *	no transactions are being run.	current state of the poll and service
 *					bits (ignore the results of this
 *					transaction)
 *
 */

fdb_intr(ap)
struct args *ap;
{
	register int n, m, c, x;

	/*
 	 *	If there are still interrupts pending (quite possible due to
 	 *	timing holes in the via interrupt handlers and not an error
	 *	call back to the FSM in fdbs.s to handle them. If, as a result,
	 *	there are no higher level processing to be done return.
	 */

	while (VIA->ifr&0x04 && !(via1_soft&0x04)) {
		n = spl4();
		fdb_inthand();
		splx(n);
	}
	if ((via1_soft&0x04) == 0) {
		return;
	}

	TRACE(T_fdb, ("fdb_intr: start timeout=%d,poll=%d,select=%d\n",
		fdb_timeout&1, fdb_pollflg&1, fdb_select&1));
	TRACE(T_fdb, ("	fdb_state=0x%x,fdb_command=0x%x,fdb_current=%d\n",
		fdb_state,fdb_command,fdb_current));

	/*
	 *	Clear the soft interrupt
	 */

	via1_soft &= 0xFB;

	/*
	 *	If we are not in the process of running a bus transaction
	 *		(from via_fdb_start) then there is either a poll or
	 *		device service request pending. We now run a dummy
	 *		transaction to find out what to do next
	 */

	if (fdb_state == 0) {
		fdb_default();
		return;
	}

	/*
	 *	If there are no more service requests pending and there are
	 *	outstanding device poll transactions pending cancel the 
	 *	transactions and inform the drivers.
	 */

	fdb_state = 0;
	n = fdb_current;
	if (fdb_select == 0 && fdb_polling) {
		fdb_polling = 0;
		for (m = 0; m < 16; m++)
		if (fdb_poll[m]) {
			fdb_poll[m] = 0;
			fdb_request &= ~fdb_mask[m];
			(*(fdb_int[m])) (fdb_id[m], FDB_UNINT, 1);
		}
	}

	/*
	 *	If the fdb chip just completed a valid poll then call back
	 *		to the driver that ran the last valid bus transaction.
	 *		If the cycle that uncovered the poll was NOT a dummy
	 *		cycle then rerun it.
	 */

	if (fdb_pollflg) {
		TRACE(T_fdb, ("fdb_intr: poll last=%d\n",fdb_last));
		if (fdb_last < 16 && !fdb_timeout) {
			(*(fdb_int[fdb_last]))(fdb_id[fdb_last],
					FDB_POLL,
					*(unsigned short *)fdb_exb);
		}
		if (fdb_fake) {
			fdb_request &= 0xffff;
			fdb_fake = 0;
			fdb_command = 0;
			n = 0;
		} else {
			via_fdb_start(n, fdb_reg[n], fdb_data[n]);
			return;
		}
	} else {

	/*
	 *	If a requested transaction goes to completion call back to
	 *		its driver to inform it of the completion
	 */

		c = fdb_command;
		fdb_command = 0;
		if (fdb_fake) {
			TRACE(T_fdb, ("fdb_intr: fake\n"));
			fdb_request &= 0xffff;
			fdb_fake = 0;
			fdb_last = n;
		} else {
			TRACE(T_fdb, ("fdb_intr: n = %d\n",n));
			if (c != FDB_RESET) {
				fdb_request &= ~fdb_mask[n];
				fdb_last = n;
				TRACE(T_fdb,
				    ("fdb_intr: call(%d,0x%x,%d)\n",
				    n, c, fdb_timeout&1));
				if (fdb_poll[n]) {
					fdb_poll[n] = 0;
					fdb_polling--;
				}
				(*(fdb_int[n]))(fdb_id[n], c, fdb_timeout);
			} else {
				fdb_request &= 0xffff;
				n = 0;
				fdb_last = 16;
			}
		}
	}

	/*
	 *	If there are no currently active bus transactions and there
	 *		are some pending start the next one. (Note this is
	 *		done in a round-robin manner)
	 */

	if (fdb_command == 0 && fdb_request) {
		for (m = n;;m = (m+1)&0xf)
		if (fdb_request&fdb_mask[m]) {
			via_fdb_start(m, fdb_reg[m], fdb_data[m]);
			break;
		}
	}

	/*
	 *	If there is a device service request pending and there are
	 *		no outstanding device poll requests then for each
	 *		open device ask it (using FDB_INT) to create a poll
	 *		request to its device.
	 */

	if (fdb_select && fdb_polling == 0) {
		for (m = 0; m < 15; m++)
		if (fdb_opn[m] && !(fdb_request&fdb_mask[m])) {
			TRACE(T_fdb, ("fdb_intr: int(%d)\n", m));
			if ((*(fdb_int[m]))(fdb_id[m], FDB_INT, 0)) {
				fdb_poll[m] = 1;
				fdb_polling++;
			}
		}
	}
}

/*
 *	Default bus transaction .... to find out the cause
 *		of an interrupt (or as a default at startup time)
 */

fdb_default()
{
	register int i;
	register int s;

	TRACE(T_fdb, ("fdb_default()\n"));
	fdb_pollflg 	= 1;
	fdb_fake 	= 1;
	fdb_request 	|= 0x10000;
	fdb_datap 	= fdb_exb;
	fdb_current 	= fdb_last;
	fdb_state 	= 2;
	fdb_command 	= FDB_TALK;
	s = spl4();
	VIA->acr 	&= 0xe3;
	FDB_DELAY(1);
	VIA->acr 	|= 0x0c;
	FDB_DELAY(1);
	i = VIA->sr;
	FDB_DELAY(1);
	VIA->regb 	= (VIA->regb&0xcf)|0x10;
	splx(s);
}

