/*
 * @(#)sccio.c  {Apple version 2.19 90/05/03 17:20:51}
 *
 * Copyright (c) 1987, 1988, 1989 by Apple Computer, Inc.
 * All Rights Reserved.
 *
 * THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF APPLE COMPUTER, INC.
 * The copyright notice above does not evidence any actual or
 * intended publication of such source code.
 */

#if !defined(lint) && !defined(NO_SCCS_IDS)
static char _sccsid[]="@(#)sccio.c  {Apple version 2.19 90/05/03 17:20:51}";
#endif

/*	@(#)sccio.c	UniPlus VVV.2.1.13	*/
#ifdef HOWFAR
extern int T_sccio;
#endif	HOWFAR
/*
** These are the minor devices of the printer, and modem ports.
** This information is used to make sure that the correct flow control
** is always present by default on these lines.
*/
#define	MODEM_PORT	0
#define	PRINTER_PORT	1

/*
 *	SCC device driver
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

#ifdef lint
#include "sys/sysinclude.h"
#else lint
#include "sys/param.h"
#include "sys/types.h"
#include "sys/mmu.h"
#include "sys/page.h"
#include "sys/region.h"
#include "sys/systm.h"
#include "sys/dir.h"
#include "sys/signal.h"
#include "sys/time.h"
#include "sys/user.h"
#include "sys/errno.h"
#include "sys/file.h"
#include "sys/ioctl.h"
#include "sys/tty.h"
#include "sys/termio.h"
#include "sys/conf.h"
#include "sys/sysinfo.h"
#include "sys/var.h"
#include "sys/reg.h"
#include <sys/debug.h>
#include "sys/proc.h"
#include "setjmp.h"
#endif lint
#include <sys/scc.h>
#include <sys/uconfig.h>
#include <sys/sysmacros.h>


#define NSCC 2

/*
 *	Local static routines
 */

static int scproc();
static int scparam();
static int scscan();
static int scw5();
static int schup();
static int scrintr();
static int scxintr();
static int scsintr();


/*
 * 	Note: 	support is provided below for other drivers which might wish to
 *		make use of the SCC chip. Two variables are declared:
 *
 *		sc_open		a character array (one per channel) which is
 *				used to mark a device in use so that two drivers
 *				do not access the same device at the same time
 *				(this driver checks this on open and clears it
 *				on close)
 *
 *		sc_intaddr	an array of adddresses of interrupt service
 *				routines (one per channel) which are used
 *				instead of the built in ISRs if they are non
 *				zero.
 *
 *		Between these two hooks it is possible to have multiple 
 *		drivers for the same device coexist without problems
 */

typedef int 		(*procedure_t)();
int 			sc_cnt = NSCC;	/* the number of scc channels */
static unsigned char	sc_modem[NSCC];	/* current modem control state */
static unsigned char	sc_dtr[NSCC];	/* current state of dtr flow control */
static unsigned char	sc_wdtr[NSCC];	/* we are waiting for dtr flow cntrl */
       unsigned char	sc_d5[NSCC];	/* saved register 5 contents */
static unsigned char	sc_brk[NSCC];	/* flag to mark input break in
					   progress */
procedure_t 		sc_intaddr[NSCC];/* indirect isr (hooks) */
char 			sc_open[NSCC];	/* open status (hooks) */
char 			sc_excl[NSCC];	/* exclusive use status */
char 			sc_nohup[NSCC];	/* don't hang up on close flag */
struct tty		sc_tty[NSCC];	/* the per-device tty structures */
struct ttyptr		sc_ttptr[NSCC];	/* tty lookup structures */
struct serstat		sc_serstat[NSCC]; /* statistics recording struct */

static int		sc_scanner_on; 	/*  Any devices need scanning? */
static int		isinit;

extern short		scc_console;	/* set if running serial console */

#define	W5ON	(W5TXENABLE | W5RTS | W5DTR)	/* turn on to talk */

#define SCTIME (v.v_hz>>4)				/* scscan interval */

#define	isAside(a)  (((int)(a) & 0x2) == 2)	/* check address for A port */

/*
 * Note: to select baud rate
 *	k = chip_input_frequency/(2 * baud * factor) - 2
 *	put factor in register 9 and k in registers D & C
 *	NOTE:
 *		normally, factor = 16
 *		for this driver, chip_input_frequency = 3684600 Hz
 * scspeeds is a table of these numbers by UNIX baud rate
 */

#define	S(b) (((3686400 / 16) / (b * 2)) - 2)
static int scspeeds[] = {
	S(1),	S(50),	S(75),	S(110),	S(134),	S(150),	S(200),	S(300),
	S(600),	S(1200), S(1800), S(2400), S(4800), S(9600), S(19200), S(38400)
};

/*
 *	table to initialize a port to 9600 baud
 *	ports are initialized OFF, to avoid asserting appletalk type signals.
 */

static char scINITtable[] = {
	9,	0,
#define	SCCIRESET scINITtable[1]
	1,	0,
	15,	0x80,
	4,	W4CLK16|W42STOP,
	11,	W11RBR|W11TBR,
	10,	0,
	12,	S(9600) & 0xFF,		/* 12/13 are baud rate */
	13,	(S(9600) >> 8) & 0xFF,	/* speed should be set to sspeed */
	14,	W14BRGE,
	3,	W38BIT,
	5,	W58BIT|W5RTS,
	1,	W1EXTIEN,
	2,	0,				/* auto vector */
	9,	W9MIE|W9DLC
};

static char scOPENtable[] = {
	9,	0x2,
	4,	W4CLK16|W42STOP,
	3,	W38BIT|W3RXENABLE,
	5,	W58BIT|W5TXENABLE|W5RTS,
	2,	0,
	10,	W10NRZ,
	11,	W11RBR|W11TBR,
	12,	S(9600) & 0xFF,
	13,	(S(9600) >> 8) & 0xFF,
	14,	W14BRGE,
	15,	0x80,
	0,	W0REXT,
	0,	W0REXT,
	1,	W1RXIALL|W1TXIEN|W1EXTIEN,
	0,	W0RXINT,
	9,	W9MIE|W9DLC
};

static char scCLOSEtable[] = {
	9,	0x2,
	4,	W4CLK16|W42STOP,
	3,	W38BIT,
	5,	W58BIT|W5RTS,
#define	SCCDTRRESET	scCLOSEtable[7]
	15,	0x80,
	1,	W1EXTIEN,
	9,	W9MIE
};

#define	CSIZEMASKOFF(x)		(((x) & CSIZE) / CS6)	/* CS5 == 0 */

static int csizemasklist[] = { 0x1f, 0x3f, 0x7f, 0xff };
static int csizemask[NSCC] = { 0xff, 0xff };

u_short scc_iop_on = 1;			/* used by sysdebug */

/*
 * we call this to preinitialize all the ports
 */

scinit()
{
	extern int scc_addr;
	extern short machineID;
	extern int (*sccirq)();
	int siopinit();
	int scintr();

	register struct device *addr;
	register unsigned i;
	register unsigned nsc;
	u_char pram;

	sc_ttptr[MODEM_PORT].tt_tty = &sc_tty[MODEM_PORT];
	sc_ttptr[PRINTER_PORT].tt_tty = &sc_tty[PRINTER_PORT];

	/* If the Mac is in Enhanced Communications mode, we also
	 * start the IOPs.  If not, stay in "Compatibility" mode,
	 * running the SCC in the non-IOP way.
	 */

	ReadXPRam(&pram,((1 << 16) | 0x89));

	if ((pram == 0) && machineID == MACIIfx) {
	    (void)siopinit();
	    return(0);
	}

	scc_iop_on = 0;

	sc_ttptr[MODEM_PORT].tt_addr = scc_addr + 2;
	sc_ttptr[PRINTER_PORT].tt_addr = scc_addr;

	if (scc_console) {
	    addr = (struct device *)sc_ttptr[CONSOLE].tt_addr;
	    for (i = 100000; i; i--) {		/* wait for output to drain */
		addr->csr = 1;
		if (addr->csr & R1ALLSENT) {
		    break;
		}
	    }
	}

	for (nsc = 0; nsc < sc_cnt; nsc++) {	/* init each channel */
		addr = (struct device *)sc_ttptr[nsc].tt_addr;
		if (isAside(addr)) {
                        SCCIRESET = W9MIE|W9ARESET;
                } else {
                        SCCIRESET = W9MIE|W9BRESET;
                }
                for (i = 0; i < sizeof(scINITtable); i++) {
                        addr->csr = scINITtable[i];
                }
		sc_d5[nsc] = W58BIT | W5TXENABLE | W5RTS;
		sc_dtr[nsc] = 0;
		sc_modem[nsc] = 0;
		addr->csr = W0REXT;		/* clear pending status */
	}

	if (scc_console) {
	    addr = (struct device *)sc_ttptr[CONSOLE].tt_addr;
	    for (i = 100000; i; i--) {		/* wait for channel to settle */
		addr->csr = 1;
	 	if (addr->csr & R1ALLSENT) {
		    break;
		}
	    }
	}

	sc_dtr[PRINTER_PORT] = 1;
	sc_modem[MODEM_PORT] = 1;
	sccirq = scintr;		/* we receive SCC interrupts now */
	isinit = 1;
	printf("Onboard SCC serial driver.\n");
}

/* ARGSUSED */
scopen(dev, flag)
register dev_t dev;
{
	register struct tty *tp;
	register struct device *addr;
	int	orig_dev;

	/*
	 *	Check the device's minor number for validity
	 */

	orig_dev = dev;
	dev = minor(dev);
	if (dev >= sc_cnt) {
		return(ENXIO);
	}
	if ((sc_open[dev]) && (flag & FLOCKOUT))
		return(EBUSY);	/* want excl. use but already open */
	if (sc_excl[dev])
		return(EBUSY);
	if (flag & FNOHUP)
		sc_nohup[dev] = 1;
	else
		sc_nohup[dev] = 0;
	tp = sc_ttptr[dev].tt_tty;

	/*
	 *	Disable interrupts while initialising shared data structures
	 *		and starting the chip.
	 */

	SPL6();

	/*
	 *	If the device is not already open then:
	 *		- initialise the device data structures
	 *		- setup the device parameters
	 *		- sense carrier (for modem control)
	 */

	if ((tp->t_state&(ISOPEN|WOPEN)) == 0) {
		if (sc_open[dev]) {	/* make sure the device is not being */
			SPL0();		/*	used for other purposes */
			return(ENXIO);
		}
		sc_open[dev] = 1;
		if (flag & FLOCKOUT)
			sc_excl[dev] = 1;
		tp->t_index = dev;
		tp->t_proc = scproc;
		ttinit(tp);		

		/*
		 *	If we are the printer port, make sure that
		 *	the tabs get done correctly
		 */

		sc_wdtr[dev] = 0;
		addr = (struct device *)sc_ttptr[dev].tt_addr;
		if (dev == PRINTER_PORT && sc_dtr[dev])
			tp->t_oflag |= (TAB3|OPOST|ONLCR);

                {
                        register i;


                        for (i = 0; i < sizeof(scOPENtable); i++)
                                addr->csr = scOPENtable[i];
                        addr->csr = W0REXT;             /* clear pending status */
                }


#ifdef POSIX
		if ((tp->t_cflag & CLOCAL) || !sc_modem[dev] ||
		    !(addr->csr & R0CTS)) {
#else
		if (!sc_modem[dev] || !(addr->csr & R0CTS)) {
#endif
			tp->t_state = WOPEN | CARR_ON;
		} else {
			tp->t_state = WOPEN;
		}

		if (scc_console && dev == CONSOLE) {
		    tp->t_iflag = ICRNL | ISTRIP;
		    tp->t_oflag = OPOST | ONLCR | TAB3;
		    tp->t_lflag = ISIG | ICANON | ECHO | ECHOK;
		    tp->t_cflag = sspeed | CS8 | CREAD;
		}

		scparam(dev);
	}

	/*
	 *	Wait until carrier is present before proceeding
	 */
	/*
	 *	If necessary start the scan routine to look for DCD 
	 *		(really CTS) changes
	 */

	if (sc_scanner_on == 0) {
	        sc_scanner_on++;
		scscan();
	}

#ifdef POSIX
	if (!(flag & (FNDELAY|FNONBLOCK))) {
#else
	if (!(flag & FNDELAY)) {
#endif POSIX
		while (!(tp->t_state&CARR_ON)) {
			tp->t_state |= WOPEN;
			(void) sleep((caddr_t)&tp->t_rawq, TTOPRI);
		}
	}

	/*
	 *	Renable interrupts and call the line discipline open
	 *		routine to set things going
	 */

	SPL0();
	(*linesw[tp->t_line].l_open)(tp, flag);
	sc_nohup[dev] = 0;
	return(0);
}

/* ARGSUSED */
scclose(dev, flag)
register dev_t dev;
{
	register struct tty *tp;
        register struct device *addr;
        register i;

	/*
	 *	Call the line discipline routine to let output drain and to
	 *		shut things down gracefully. If required drop DTR
	 *		to hang up the line.
	 */

	dev = minor(dev);
	sc_open[dev] = 0;
	sc_excl[dev] = 0;
	tp = sc_ttptr[dev].tt_tty;
	(*linesw[tp->t_line].l_close)(tp);

	if (sc_nohup[dev] || !(tp->t_cflag & HUPCL))   /* preserve state of DTR */
		SCCDTRRESET |= (sc_d5[dev] & W5DTR);

        addr = (struct device *)sc_ttptr[dev].tt_addr;
	SPL6();

        for (i = 0; i < sizeof(scCLOSEtable); i++) {
                addr->csr = scCLOSEtable[i];
        }
        addr->csr = W0REXT;             /* clear pending status */

	SCCDTRRESET &= ~W5DTR;		/* clear the bit for future use */

	csizemask[dev] = csizemasklist[CSIZEMASKOFF(CS8)];

	SPL0();

	/* Used to remove scanner timeout here, but the scanner now has 
	 * the smarts to turn itself off when no longer needed.
	 */
	return(0);
}

/*
 *	Read simply calls the line discipline to do all the work
 */

scread(dev, uio)
dev_t dev;
struct uio *uio;
{
	register struct tty *tp;

	tp = sc_ttptr[minor(dev)].tt_tty;
	return((*linesw[tp->t_line].l_read)(tp, uio));
}

/*
 *	Write simply calls the line discipline to do all the work
 */

scwrite(dev, uio)
dev_t dev;
struct uio *uio;
{
	register struct tty *tp;

	tp = sc_ttptr[minor(dev)].tt_tty;
	return((*linesw[tp->t_line].l_write)(tp, uio));
}

/*
 *	The proc routine does all the work. It takes care of requests from
 *		the line discipline.
 */

static
scproc(tp, cmd)
register struct tty *tp;
int cmd;
{
	register struct ccblock *tbuf;
	register struct device *addr;
	register int dev;
	register int s;
	register int i;
	extern ttrstrt();

	/*
	 *	disable interrupts in order to synchronise with the device
	 */

	s = spl6();
	dev = tp->t_index;
	addr = (struct device *)sc_ttptr[dev].tt_addr;
	switch (cmd) {

	case T_TIME:
		scw5(dev, addr, 0);		/* clear break */
		tp->t_state &= ~TIMEOUT;
		goto start;

	case T_WFLUSH:
		tbuf = &tp->t_tbuf;		/* clear the output buffer */
		tbuf->c_size -= tbuf->c_count;
		tbuf->c_count = 0;
		if (sc_wdtr[dev]) {
			tp->t_state &= ~BUSY;
			sc_wdtr[dev] = 0;
		}
		/* fall through */

	case T_RESUME:				
		tp->t_state &= ~TTSTOP;		/* start output again */
		goto start;

	case T_OUTPUT:
start:						/* output data */
		if (tp->t_state & (TTSTOP|TIMEOUT|BUSY))
			break;
		if (tp->t_state & TTXOFF) {	/* send an XOFF */ 
			tp->t_state &= ~TTXOFF;
			tp->t_state |= BUSY;
			addr->data = tp->t_chars.tc_stopc;
			break;
		}
		if (tp->t_state & TTXON) {	/* send an XON */
			tp->t_state &= ~TTXON;
			tp->t_state |= BUSY;
			addr->data = tp->t_chars.tc_startc;
			break;
		}

		/*
		 *	If there is no data in the buffer, get some more. If no
		 *		more is available then return
		 */

		tbuf = &tp->t_tbuf;
		if ((tbuf->c_ptr == 0) || (tbuf->c_count == 0)) {
			if (tbuf->c_ptr)
				tbuf->c_ptr -= tbuf->c_size;
			if (!(CPRES & (*linesw[tp->t_line].l_output)(tp)))
				break;
		}

		/*
		 *	If DTR flow control is enabled and DTR is not asserted
		 *		then wait until it is
		 */

		if (sc_dtr[dev] && addr->csr&R0CTS) { 
			tp->t_state |= BUSY;
			sc_wdtr[dev] = 1;
			break;
		}

		/*
		 *	Output a character, set the busy bit until it is done
		 */

		tp->t_state |= BUSY;
		addr->data = *tbuf->c_ptr++ & csizemask[dev];
		tbuf->c_count--;
		break;

	case T_SUSPEND:				/* stop all output */
		tp->t_state |= TTSTOP;
		break;

	case T_BLOCK:				/* send XOFF to block input*/
		tp->t_state &= ~TTXON;
		tp->t_state |= TBLOCK;
		tp->t_state |= TTXOFF;
		goto start;

	case T_RFLUSH:				/* flush pending input */
		if (!(tp->t_state&TBLOCK))
			break;
		/* fall through */

	case T_UNBLOCK:				/* send XON to restart input */
		tp->t_state &= ~(TTXOFF|TBLOCK);
		tp->t_state |= TTXON;
		goto start;

	case T_BREAK:				/* start transmitting a break */
		scw5(dev, addr, W5BREAK);
		tp->t_state |= TIMEOUT;
		timeout(ttrstrt, (caddr_t)tp, v.v_hz>>2);
		break;

	case T_PARM:				/* call the param routine */
		scparam((dev_t)dev);
		break;
	}
	splx(s);
}

/*
 *	scw5 -- write register d5.
 *	   Used to send a break, the intent is to set or clear only those 
 *	bits permitted by the mask.
 */

#define	scw5MASK  W5BREAK

static
scw5(dev, addr, d)
register dev;
register struct device *addr;
int d;
{
	register int s;

	s = spl6();
	sc_d5[dev] &= ~scw5MASK;
	sc_d5[dev] |= d & scw5MASK;
	addr->csr = 5;
	addr->csr = sc_d5[dev];
	splx(s);
}

/*
 *	The ioctl routine mostly handles the MODEM ioctls, all others are
 *		passed to ttiocom to be done.
 */

scioctl(dev, cmd, arg, mode)
register dev_t dev;
register int cmd;
char *arg;
int mode;
{
	register struct tty *tp;
	register int i;
	register struct device *addr;
	struct serstat *ss;

	dev = minor(dev);
	tp = sc_ttptr[dev].tt_tty;
	addr = (struct device *)sc_ttptr[dev].tt_addr;
	switch(cmd) {

		/*
		 *	UIOCMODEM: turns on modem control. If DTR flow control
		 *		   was on turn it off and start output.
		 */

	case UIOCMODEM:
		if (sc_dtr[dev]) {
			sc_dtr[dev] = 0;
			if (sc_wdtr[dev]) {
				sc_wdtr[dev] = 0;
				tp->t_state &= ~BUSY;
				(*tp->t_proc)(tp, T_OUTPUT);
			}
		}
		sc_modem[dev] = 1;
		if (sc_scanner_on == 0) {
			sc_scanner_on++;
			scscan();
		}
		return(0);

		/*
		 *	UIOCNOMODEM: turns of modem control and DTR flow control
		 *		   If DTR flow control was on turn it off and
		 *		   start output. If modem control is on allow
		 *		   processes waiting to open to do so.
		 */

	case UIOCNOMODEM:
		/*
		 * This used to check to see if dtr or modem devices exist,
		 * and turned scscan off if necessary.  scscan is smarter
		 * now.
		 */
		if (sc_dtr[dev]) {
			sc_dtr[dev] = 0;
			if (sc_wdtr[dev]) {
				sc_wdtr[dev] = 0;
				tp->t_state &= ~BUSY;
				(*tp->t_proc)(tp, T_OUTPUT);
			}
		}
		sc_modem[dev] = 0;
		if (tp->t_state&WOPEN) {
			tp->t_state |= CARR_ON;
			wakeup((caddr_t)&tp->t_rawq);
		}
		return(0);

		/*
		 *	UIOCDTRFLOW: turns on DTR flow control. If modem control
		 *		is on allow processes waiting to open to do so.
		 */

	case UIOCDTRFLOW:
		if (sc_modem[dev]) {
			sc_modem[dev] = 0;
			if (tp->t_state&WOPEN) {
				tp->t_state |= CARR_ON;
				wakeup((caddr_t)&tp->t_rawq);
			}
		}
		sc_dtr[dev] = 1;
		if (sc_scanner_on == 0) {
			sc_scanner_on++;
			scscan();
		}
		return(0);

		/*
		 *	UIOCTTSTAT: return the modem control/flow control state
		 */

	case UIOCTTSTAT:
		if (sc_modem[dev]) {
			*arg++ = 1;
		} else {
			*arg++ = 0;
		}
		if (sc_dtr[dev]) {
			*arg++ = 1;
		} else {
			*arg++ = 0;
		}
		*arg = 0;
		return(0);

	/* 	For Mac Serial Manager Compatability	*/

	case TCSBRKM:		/* set break mode (Mac compatability ) */
		scw5(dev, addr, W5BREAK);
		return(0);

	case TCCBRKM:		/* clear break mode (Mac compatability ) */
		scw5(dev, addr, 0);
		return(0);

	case TCRESET:		/* reset channel (Mac compatability ) */
		SPL6();

		if (dev == 0) {
		        addr->csr = 9;
			addr->csr = W9NV | W9ARESET;
		} else {
		        addr->csr = 9;
			addr->csr = W9NV | W9BRESET;
		}

		for (i = 0; i < sizeof(scOPENtable); i++)
			addr->csr = scOPENtable[i];
		addr->csr = W0REXT;		/* clear pending status */

		sc_d5[dev] = W58BIT | W5TXENABLE | W5RTS;
		sc_dtr[dev] = 0;
		sc_modem[dev] = 0;
		sc_dtr[PRINTER_PORT] = 1;
		sc_modem[MODEM_PORT] = 1;

		SPL0();
		return(0);

	case TCSETDTR:		/* set DTR (Mac compatability ) */
		SPL6();
		sc_d5[dev] |= W5DTR;
		addr->csr = 5;
		addr->csr = sc_d5[dev];
		SPL0();
		return(0);

	case TCCLRDTR:		/* clear DTR (Mac compatability ) */
		SPL6();
		sc_d5[dev] &= ~W5DTR;
		addr->csr = 5;
		addr->csr = sc_d5[dev];
		SPL0();
		return(0);

	case TCSETSTP:		/* set stop character */
		tp->t_chars.tc_stopc = *arg;
		return(0);

	case TCSETSTA:		/* set start character */
		tp->t_chars.tc_startc = *arg;
		return(0);

	case TCGETSTAT:		/* get device stats */
		*(struct serstat *)arg = sc_serstat[dev];
		sc_serstat[dev].ser_frame = 0;
		sc_serstat[dev].ser_ovrun = 0;
		sc_serstat[dev].ser_parity = 0;
		ss = (struct serstat *)arg;
		if (addr->csr&R0CTS)  /* denotes signal is off */
			ss->ser_cts = 0;
		else
			ss->ser_cts = 1;
		if (tp->t_state & TBLOCK)
			ss->ser_inflow =1;
		else
			ss->ser_inflow =0;
		if (tp->t_state & TTSTOP)
			ss->ser_outflow =1;
		else
			ss->ser_outflow =0;
		return(0);

	default:
		if (ttiocom(tp, cmd, arg, mode)) {
			return(scparam(dev));
		} else {
			return(u.u_error);
		}
	}
}

/*
 *	setup device dependant physical parameters
 */

static
scparam(dev)
register dev_t dev;
{
	register struct tty *tp;
	register struct device *addr;
	register int flag;
	register int s;
	register int w4;
	register int w5;
	register int speed;
	int w3;

	dev = minor(dev);
	tp = sc_ttptr[dev].tt_tty;
	flag = tp->t_cflag;
	addr = (struct device *)sc_ttptr[dev].tt_addr;
	if (((flag&CBAUD) == B0)) {
		schup(dev, addr);
		return(0);
	}

	if ((flag & CLOCAL) && sc_modem[dev] && (tp->t_state&(ISOPEN|WOPEN))
		&& ((tp->t_state&CARR_ON) == 0)) {
		tp->t_state |= CARR_ON;
		if (tp->t_state&WOPEN)
			wakeup((caddr_t)&tp->t_rawq);
	}
	else if (((flag & CLOCAL) == 0) && sc_modem[dev]
		&& (tp->t_state&(ISOPEN|WOPEN)) && (sc_scanner_on == 0)) {
		sc_scanner_on++;
		scscan();
	}

	csizemask[dev] = csizemasklist[CSIZEMASKOFF(flag)];

	w3 = (flag & CREAD) ? W3RXENABLE : 0;
	switch(flag & CSIZE) {
	case CS5:
		w3 |= W35BIT;
		break;
	case CS6:
		w3 |= W36BIT;
		break;
	case CS7:
		w3 |= W37BIT;
		break;
	case CS8:
		w3 |= W38BIT;
		break;
	}
	w4 = W4CLK16;
	if (flag & CSTOPB) {
		w4 |= W42STOP;
	} else {
		if (flag & CSTOPB15)
			w4 |= W415STOP;
		else
			w4 |= W41STOP;
	}
	w5 = W5TXENABLE | W5RTS | W5DTR;
	switch(flag & CSIZE) {
	case CS5:
		w5 |= W55BIT;
		break;
	case CS6:
		w5 |= W56BIT;
		break;
	case CS7:
		w5 |= W57BIT;
		break;
	case CS8:
		w5 |= W58BIT;
		break;
	}
	if (flag & PARENB) {
		if (flag & PARODD) {
			w4 |= W4PARENABLE;
		} else {
			w4 |= W4PARENABLE | W4PAREVEN;
		}
	}
	speed = scspeeds[flag&CBAUD];
	s = spl6();
	addr->csr = 3;
	addr->csr = w3;
	addr->csr = 4;
	addr->csr = w4;
	addr->csr = 12;
	addr->csr = speed;
	addr->csr = 13;
	addr->csr = speed >> 8;
	addr->csr = 5;
	addr->csr = w5;
	sc_d5[dev] = w5;
	splx(s);
	return(0);
}

/*
 *	This routine hangs up a modem by removing DTR
 */

static
schup(dev, addr)
register dev_t dev;
register struct device *addr;
{
	register int s;

	dev = minor(dev);
	s = spl6();
	addr->csr = 5;
	sc_d5[dev] = addr->csr = W5TXENABLE | W58BIT | W5RTS;	/* turn off DTR */
	splx(s);
}

/*
 *	This routines gives performance timing/debug access to DTR RTS
 */
scdr(dev, dtr, rts)
register dev_t dev;
{
	register struct device *addr;
	register int s;

	dev = minor(dev);
	addr = (struct device *)sc_ttptr[dev].tt_addr;
	s = spl6();
	addr->csr = 5;
	sc_d5[dev] = addr->csr = W5TXENABLE | W58BIT |
		( dtr ? W5DTR : 0) | ( rts ? W5RTS : 0 );
	splx(s);
}
/*
 *	The interrupt service routine is called from ivec.s and determines which
 *	service routines should be called
 */

scintr(ap)
struct args *ap;
{
	register struct device *addr;
	register int csr;
	register int dev;

	for (dev = 0; dev < sc_cnt; dev++) {
		addr = (struct device *)sc_ttptr[dev].tt_addr;
		if (sc_intaddr[dev]) {			/* hook for other */
			(*sc_intaddr[dev])(dev, addr);	/*  protocols */
			continue;
		}
		for (;;) {
			csr = addr->csr;
			if (!(csr&(R0RXRDY|R0BREAK)))
				break;
			if (csr&R0BREAK) {
				if (sc_brk[dev]) {
				        if (csr & R0RXRDY)
					        csr = addr->data;
				        break;
			        }
				sc_brk[dev] = 1;
			} else if (sc_brk[dev]) {
				csr = addr->data;	/* read 0 char */
				sc_brk[dev] = 0;
			        addr->csr = W0REXT;	/* reset external status */
				continue;
			}
			addr->csr = 1;
			if ((addr->csr & (R1PARERR|R1OVRERR|R1FRMERR)) ||
			    (csr & R0BREAK)) {
				scsintr(dev, addr);
			} else {
#ifdef DEBUG
				scrintr(dev, addr, ap);
#else DEBUG
				scrintr(dev, addr);
#endif DEBUG
			}
		}
		csr = addr->csr;

		if (csr & R0UNDERRUN) {
		        addr->csr = W0TXURUN;  /* reset transmitter underrun */
			addr->csr = W0REXT;    /* reset external status */
		}
		if (csr & R0TXRDY)
			scxintr(dev, addr);
	}
}

static
#ifdef DEBUG
scrintr(dev, addr, ap)
struct args *ap;
#else DEBUG
scrintr(dev, addr)
#endif DEBUG
register int dev;
register struct device *addr;
{
	register struct ccblock *cbp;
	register struct tty *tp;
	register int c;
	register int lcnt;
	register int flg;
	register char ctmp;
	char lbuf[3];

	addr->csr = W0RXINT;	/* reenable receiver interrupt */
	addr->csr = W0RIUS;	/* reset interrupt */
	c = addr->data & csizemask[dev];

#if defined(DEBUG)
	if (scc_console && dev == CONSOLE && ((c & 0x7F) == 1)) {
	    debug(ap);
	    return;
	}
#endif	/* DEBUG */

	sysinfo.rcvint++;
	tp = sc_ttptr[dev].tt_tty;
	flg = tp->t_iflag;
	if (flg&ISTRIP)
		c &= 0x7f;
	if (flg & IXON) {
		if (tp->t_state & TTSTOP) {
			if (c == tp->t_chars.tc_startc || tp->t_iflag & IXANY)
				(*tp->t_proc)(tp, T_RESUME);
		} else {
			if (c == tp->t_chars.tc_stopc)
				(*tp->t_proc)(tp, T_SUSPEND);
		}
		if (c == tp->t_chars.tc_startc || c == tp->t_chars.tc_stopc)
			return;
	}
	if (tp->t_rbuf.c_ptr == NULL) {
		return;
	}
	lcnt = 1;
	if ((flg&ISTRIP) == 0) {
		if (c == 0xff && flg&PARMRK) {
			lbuf[1] = 0xff;
			lcnt = 2;
		}
	}

	/*
	 * Stash character in r_buf
	 */

	cbp = &tp->t_rbuf;
	if (lcnt != 1) {
		lbuf[0] = c;
		while (lcnt) {
			*cbp->c_ptr++ = lbuf[--lcnt];
			if (--cbp->c_count == 0) {
				cbp->c_ptr -= cbp->c_size;
				(*linesw[tp->t_line].l_input)(tp, L_BUF);
			}
		}
		if (cbp->c_size != cbp->c_count) {
			cbp->c_ptr -= cbp->c_size - cbp->c_count;
			(*linesw[tp->t_line].l_input)(tp, L_BUF);
		}
	} else {
		*cbp->c_ptr = c;
		cbp->c_count--;
		(*linesw[tp->t_line].l_input)(tp, L_BUF);
	}
}

static
scxintr(dev, addr)
register int dev;
register struct device *addr;
{
	register struct tty *tp;

	sysinfo.xmtint++;
	addr->csr = W0RTXPND;	/* reset transmitter interrupt */
	addr->csr = W0RIUS;	/* reset interrupt */
	tp = sc_ttptr[dev].tt_tty;
	if(!sc_wdtr[dev]) {
		tp->t_state &= ~BUSY;
		scproc(tp, T_OUTPUT);
	}
}

static
scsintr(dev, addr)
register int dev;
register struct device *addr;
{
	register struct ccblock *cbp;
	register struct tty *tp;
	register int c;
	register int lcnt;
	register int flg;
	register char ctmp;
	register int csr;
	register unsigned char stat;
	char lbuf[3];

	sysinfo.rcvint++;

	csr = addr->csr;	/* read register zero */
	addr->csr = 0x1;	/* cmd to read register 1 */
	stat = addr->csr;	/* read the status */
	c = addr->data & 0xFF;	/* read data BEFORE reset error */

	addr->csr = W0RERROR;	/* reset error condition */
	addr->csr = W0REXT;	/* reset external status interrupts */
	addr->csr = W0RXINT;	/* reinable receiver interrupt */
	addr->csr = W0RIUS;	/* reset interrupt under service */

	tp = sc_ttptr[dev].tt_tty;

	if (tp->t_rbuf.c_ptr == NULL)
		return;
	if (tp->t_iflag & IXON) {
	    ctmp = c & 0x7f;
	    if (tp->t_state & TTSTOP) {
		if (ctmp == tp->t_chars.tc_startc || tp->t_iflag & IXANY)
			(*tp->t_proc)(tp, T_RESUME);
	    } else {
		if (ctmp == tp->t_chars.tc_stopc)
		    (*tp->t_proc)(tp, T_SUSPEND);
	    }
	    if (ctmp == tp->t_chars.tc_startc || ctmp == tp->t_chars.tc_stopc)
		return;
	}
	/*
	 * Check for errors
	 */

	if (csr & R0BREAK) {
		sc_brk[dev] = 1;
		c = FRERROR;
	}
	lcnt = 1;
	flg = tp->t_iflag;
	if (stat & (R1PARERR |R1OVRERR|R1FRMERR)) {
		if ((stat & R1PARERR ) && (flg & INPCK)) {
			c |= PERROR;
			sc_serstat[dev].ser_parity++;
		}
		if (stat & R1OVRERR) {
			c |= OVERRUN;
			sc_serstat[dev].ser_ovrun++;
		}
		if (stat & R1FRMERR) {
			c |= FRERROR;
			sc_serstat[dev].ser_frame++;
		}
	}
	if (c&(FRERROR|PERROR|OVERRUN)) {
		if ((c&0xff) == 0) {
			if (flg&IGNBRK)
				return;
			if (flg&BRKINT) {
				(*linesw[tp->t_line].l_input)(tp, L_BREAK);
				return;
			}
		} else {
			if (flg&IGNPAR)
				return;
		}
		if (flg&PARMRK) {
			lbuf[2] = 0xff;
			lbuf[1] = 0;
			lcnt = 3;
			sysinfo.rawch += 2;
		} else {
			c = 0;
		}
	} else {
		if (flg&ISTRIP) {
			c &= 0x7f;
		} else {
			if (c == 0xff && flg&PARMRK) {
				lbuf[1] = 0xff;
				lcnt = 2;
			}
		}
	}

	/*
	 * Stash character in r_buf
	 */

	cbp = &tp->t_rbuf;
	if (lcnt != 1) {
		lbuf[0] = c;
		while (lcnt) {
			*cbp->c_ptr++ = lbuf[--lcnt];
			if (--cbp->c_count == 0) {
				cbp->c_ptr -= cbp->c_size;
				(*linesw[tp->t_line].l_input)(tp, L_BUF);
			}
		}
		if (cbp->c_size != cbp->c_count) {
			cbp->c_ptr -= cbp->c_size - cbp->c_count;
			(*linesw[tp->t_line].l_input)(tp, L_BUF);
		}
	} else {
		*cbp->c_ptr = c;
		cbp->c_count--;
		(*linesw[tp->t_line].l_input)(tp, L_BUF);
	}
}

static
scscan()
{
	register int i;
	register struct tty *tp;
	register struct device *addr;
	register int need_timer = 0;

	for (i = 0; i < sc_cnt; i++) {
		addr = (struct device *)sc_ttptr[i].tt_addr;
		addr->csr = W0REXT;	/* update CTS */
		tp = sc_ttptr[i].tt_tty;
		if (!(addr->csr & R0CTS)) {
			if (sc_dtr[i]) {
				if (tp->t_state&(ISOPEN|BUSY))
					need_timer++;
				if (sc_wdtr[i]) {
					sc_wdtr[i] = 0;
					tp->t_state &= ~BUSY;
					(*tp->t_proc)(tp, T_OUTPUT);
				}
			} else
#ifdef POSIX
			if (!(tp->t_cflag & CLOCAL) && sc_modem[i]) {
#else
			if (sc_modem[i]) {
#endif
			 	if (tp->t_state&(ISOPEN|WOPEN))
					need_timer++;
				if ((tp->t_state&CARR_ON) == 0) {
					tp->t_state |= CARR_ON;
					if (tp->t_state&WOPEN) 
						wakeup((caddr_t)&tp->t_rawq);
				}
			}
		} else {
#ifdef POSIX
			if (!(tp->t_cflag & CLOCAL) && sc_modem[i]) {
#else
			if (sc_modem[i]) {
#endif
			 	if (tp->t_state&(ISOPEN|WOPEN))
					need_timer++;
				if (tp->t_state&CARR_ON) {
					tp->t_state &= ~CARR_ON;
					if (tp->t_state&ISOPEN) {
						ttyflush(tp, FREAD|FWRITE);
						signal(tp->t_pgrp, SIGHUP);
					}
				}
			} else
			if (sc_dtr[i] && tp->t_state&(ISOPEN|BUSY))
				need_timer++;
		}
	}
	if (need_timer)
		timeout(scscan, (caddr_t)0, SCTIME);
	else
		sc_scanner_on = 0;

}
