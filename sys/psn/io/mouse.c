#ifndef lint	/* .../sys/psn/io/mouse.c */
#define _AC_NAME mouse_c
#define _AC_NO_MAIN "@(#) Copyright (c) 1987 Apple Computer, Inc., All Rights Reserved.  {Apple version 2.2 90/05/01 01:19:37}"
#include <apple_notice.h>

#ifdef _AC_HISTORY
  static char *sccsid = "@(#)Copyright Apple Computer 1987\tVersion 2.2 of mouse.c on 90/05/01 01:19:37";
#endif		/* _AC_HISTORY */
#endif		/* lint */

#define _AC_MODS

/*
 *	Mouse driver
 *
 *	Copyright 1986 Unisoft Corporation of Berkeley CA
 *
 *
 *	UniPlus Source Code. This program is proprietary
 *	with Unisoft Corporation and is not to be reproduced
 *	or used in any manner except as authorized in
 *	writing by Unisoft.
 *
 */

#include <mac/types.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/param.h>
#include <sys/sysmacros.h>
#include <sys/mmu.h>
#include <sys/page.h>
#include <sys/region.h>
#include <sys/time.h>
#include <sys/proc.h>
#include <sys/dir.h>
#include <sys/signal.h>
#include <sys/user.h>
#include <sys/ioctl.h>
#include <sys/tty.h>
#include <sys/termio.h>
#include <sys/conf.h>
#include <sys/errno.h>
#include <sys/sysinfo.h>
#include <sys/buf.h>
#include <sys/fdb.h>
#include <sys/mouse.h>
#include <sys/debug.h>

extern int T_fdb;
extern long lbolt;

/*
 *	This is the number of mouse type devices supported (currently must
 *	be one untill someone solves the problem of how to identify a particular
 *	mouse with a particular screen)
 */
 
#define NDEVICES 1

/*
 *	This is the mouse FDB handler number we initialise it to (which
 *	determines the number of points/inch our mouse responds to)
 */

#define HANDLER	1

extern int nulldev();

typedef int (*procedure_t)();

static procedure_t mouse_call[NDEVICES];	/* the driver service routine */
static int mouse_state[NDEVICES];		/* current state */
static int mouse_opened[NDEVICES];		/* are we already open? */
static int mouse_present[NDEVICES];		/* true if there really is a
						   mouse out there */
static int mouse_mode[NDEVICES];		/* do we have to call back to
						   the higher level if something
						   changes */
static short mouse_buff[NDEVICES];		/* where the fdb data is read
						   into */

static int mouse_intr();

short	mouse_x[NDEVICES];			/* absolute mouse position */
short	mouse_y[NDEVICES];			/* 	since booting */
char	mouse_button[NDEVICES];			/* current mouse position */
caddr_t ui_lowaddr = (caddr_t)-1;

long mouse_op();
int mouse_open();
int mouse_close();

struct mouse_data mouse_data = {
	mouse_open,
	mouse_close,
	mouse_op,
};

/*
 *	Values for mouse_state
 */

#define	STATE_INIT	0		/* not yet initialised */
#define	STATE_REG0	1		/* device is in inactive state */
#define	STATE_REG3	2		/* register 3 listen in progress */
#define	STATE_ACTIVE	5		/* register 0 talk in progress */

/*
 *	called at spl7
 *		for each device
 *			initialise its global variables
 *			call fdb_open to declare the ISR and
 *				start the FSMs events
 */

mouse_init()
{
	register int i;

	for (i = 0; i < NDEVICES;i++) {
		mouse_x[i] = 0;
		mouse_y[i] = 0;
		mouse_call[i] =	nulldev;
		mouse_state[i] =	STATE_INIT;
		mouse_mode[i] =	0;
		mouse_opened[i] =	0;
		mouse_present[i] =	0;
		fdb_open(FDB_MOUSE, i, mouse_intr);
	}
}

/*
 *	Open succeeds if
 *
 *		The device id is valid
 *		The device exists
 *		Device initialisation has completed
 *
 *	It marks the device as open, and sets its mode and interrupt service
 *		routine.
 */

mouse_open(id, intr, mode)
procedure_t intr;
{
	register int s;

	if (id < 0 || id >= NDEVICES || !mouse_present[id] ||
	    mouse_state[id] == STATE_INIT)
		return(0);
	mouse_call[id] =	intr;
	mouse_mode[id] =	mode;
	mouse_opened[id] =	1;
	return(1);
}

/*
 *	close simply marks the device closed
 */

mouse_close(id)
{
	register int s;

	mouse_call[id] = nulldev;
	mouse_opened[id] = 0;
}

/*
 *	mouse_op sets and returns mouse globals (context). It is most
 *		usefull when one wishes to take controll of an already
 *		open device (from the kernel), one uses this to save the
 *		device's state and set up the new one.
 */

long
mouse_op(id, op, x)
long x;
{
	int s;
	long t;

	switch(op) {
	case MOUSE_OP_MODE:
		t = mouse_mode[id];
		mouse_mode[id] = op;
		return(t);

	case MOUSE_OP_INTR:
		s = splhi();
		t = (long)mouse_call[id];
		mouse_call[id] = (procedure_t)x;
		splx(s);
		return(t);

	case MOUSE_OP_OPEN:
		return(mouse_opened[id]);

	case MOUSE_OP_X:
		return(mouse_x[id]);

	case MOUSE_OP_Y:
		return(mouse_y[id]);

	case MOUSE_OP_BUTTON:
		return(mouse_button[id]);
	}
}

/*
 *	The interrupt service routine is called from the fdb driver (refer
 *	to the comment in fdb.c for details of the finite state machine used
 *	and the meanings of the various calls and replys from the fdb driver)
 */

#define CRSRBUSY 0x8cd
#define CRSRNEW  0x8ce
#define MTEMP    0x828

static
mouse_intr(id, cmd, tim)
{
	register short i, x, y;
	register int change;
	register caddr_t lowaddr;

	lowaddr = ui_lowaddr;

	switch(cmd) {
	case FDB_UNINT:

		/*
		 *	A poll was canceled .... mark the device as inactive
		 */

		if (mouse_state[id] == STATE_ACTIVE)
			mouse_state[id] = STATE_REG0;
		break;

	case FDB_INT:

		/*
		 *	A poll is requested. If we are doing nothing then
		 *	do a fdb_talk to do the poll.
		 */

		if (mouse_state[id] == STATE_REG0) {
			fdb_talk(FDB_MOUSE, id, 0, &mouse_buff[id]);
			mouse_state[id] = STATE_ACTIVE;
			return(1);
		}
		return(0);

	case FDB_POLL:

		/*
		 *	A hardware poll succeeded ..... fake the timeout
		 *		parameter and the mouse buffer to look as
		 *		if a fdb_talk() succeded without timeout
		 *		and fall through into the FDB_TALK handler
		 */

		if (mouse_state[id] != STATE_REG0 && 
		    mouse_state[id] != STATE_ACTIVE)
			break;
		mouse_buff[id] = tim;
		tim = 0;
	case FDB_TALK:

		/*
		 *	A fdb talk transaction completed. If it timed out
		 *	mark the device as inactive and return. If is didn't
		 *	set up the global variables that describe the current
		 *	mouse location and button state. If required call a
		 *	higher level action routine (only if something changed).
		 *	If it wasn't a hardware poll start another transaction.
		 */

		if (tim == 0) {		/* there is a message */
			change = 0;
			if (!mouse_button[id]) {
				if (!(mouse_buff[id]&0x8000)) {
					change = 1;
					mouse_button[id] = 1;
				}
			} else {
				if (mouse_buff[id]&0x8000) {
					change = 1;
					mouse_button[id] = 0;
				}
			}
			if (mouse_buff[id]&0x3f) {
				change |= 2;
				x = mouse_buff[id]&0x7f;
				if (x&0x40) {
					x |= 0xffc0;
				}
				if (lowaddr != (caddr_t)-1) {
				    if (*(char *)(lowaddr + CRSRBUSY) == 0) {
				        ((Point *)(lowaddr + MTEMP))->h += x;
					*(char *)(lowaddr + CRSRNEW) = 0xff;
				    }
				}
				mouse_x[id] += x;
			}
			if (mouse_buff[id]&0x3f00) {
				change |= 2;
				y = (mouse_buff[id]>>8)&0x7f;
				if (y&0x40) {
					y |= 0xffc0;
				}
				if (lowaddr != (caddr_t)-1) {
				    if (*(char *)(lowaddr + CRSRBUSY) == 0) {
				        ((Point *)(lowaddr + MTEMP))->v += y;
					*(char *)(lowaddr + CRSRNEW) = 0xff;
				    }
				}
				mouse_y[id] += y;
			}
			if (change && mouse_mode[id])
				(*mouse_call[id])(id,MOUSE_CHANGE,mouse_buff[id],change);
			if (cmd != FDB_POLL){
				fdb_talk(FDB_MOUSE, id, 0, &mouse_buff[id]);
				mouse_state[id] = STATE_ACTIVE;
			}
		} else {
			mouse_state[id] = STATE_REG0;
		}
		break;

	case FDB_LISTEN:

		/*
		 *	The listen to set the handler id and service request
		 *		enable has completed, now start a talk to
		 *		register 0 to start the first device read
		 *		transaction and to put the driver into 
		 *		the normal state
		 */

		mouse_state[id] = STATE_ACTIVE;
		fdb_talk(FDB_MOUSE, id, 0, &mouse_buff[id]);
		break;
		
	case FDB_EXISTS:

		/*
	  	 *	This is as a result from the fdb_open() in mouse_init()
		 *	above. If tim is non zero then the device does not
		 *	exist. Tell any higher level drivers. If it does then
		 *	start a flush transaction to clean out the device.
		 */

		if (tim) {
			(*mouse_call[id])(id, MOUSE_OPEN, 1);
			mouse_state[id] = STATE_INIT;
		} else {
			mouse_present[id] = 1;
			(*mouse_call[id])(id, MOUSE_OPEN, 0);
			fdb_flush(FDB_MOUSE, id);
		}
		break;

	case FDB_FLUSH:

		/*
		 *	After the flush completes start a listen to set the
		 *	devices handler number and turn on the service request
		 *	interrupts
		 */

		mouse_state[id] = STATE_REG3;
		mouse_buff[id] = 0x2000 | (FDB_MOUSE<<8) | HANDLER;
		fdb_listen(FDB_MOUSE, id, 3, &mouse_buff[id], 2);
		break;

	case FDB_RESET:
		return;
	}
}


/*
 *	Below here is the driver for "/dev/mouse". It is very simple and
 *	simply returns the current state of the mouse.
 */

static mouseintr();

/*
 *	Open checks to make sure the mouse exists and is not already being used.
 *	It opens it and gives it a (dummy) interrupt routine.
 */

mouseopen(dev)
{
	register int s;

	dev = minor(dev);
	if (dev >= NDEVICES) {
		return(EINVAL);
	}
	s = spl1();
	if (mouse_op(dev, MOUSE_OP_OPEN, 0) ||
	    mouse_open(dev, mouseintr, 0) == 0) {
		splx(s);
		return(EINVAL);
	}
	splx(s);
	return(0);
}

/*
 *	All this does is mark it as closed
 */

mouseclose(dev)
{
	register int s;

	dev = minor(dev);
	s = spl1();
	mouse_close(dev);
	splx(s);
}

static
mouseintr(dev, code, value)
{
}

/*
 *	return the current position (4 bytes)
 */

mouseread(dev,uio)
struct uio *uio;
{
	short mb[2];

	mb[0] = mouse_x[minor(dev)];
	mb[1] = mouse_y[minor(dev)];
	return(uiomove((char *)mb, 4, UIO_READ, uio));
}

/*
 *	you can't write to a mouse!!
 */

mousewrite(dev, uio)
struct uio *uio;
{
	return(EINVAL);
}

/*
 *	ioctl only supports getting the mouse's current button state
 */

mouseioctl(dev, cmd, addr, arg)
caddr_t addr;
{
	if (cmd == MOUSE_BUTTON) {
		*addr = mouse_button[minor(dev)];
		return(0);
	} else {
		return(EINVAL);
	}
}
