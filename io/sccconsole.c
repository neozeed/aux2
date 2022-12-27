/*
 * @(#)sccconsole.c  {Apple version 1.1 90/02/02 11:04:30}
 *
 * Copyright (c) 1987, 1988, 1989 by Apple Computer, Inc.
 * All Rights Reserved.
 *
 * THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF APPLE COMPUTER, INC.
 * The copyright notice above does not evidence any actual or
 * intended publication of such source code.
 */

#if !defined(lint)
static char _sccsid[]="@(#)sccconsole.c  {Apple version 1.1 90/02/02 11:04:30}";
#endif

#define MODEM_PORT	0
#define	PRINTER_PORT	1

/*
 *	SCC device driver for debugging purposes only, removed from sccio.c.
 */

#include "sys/types.h"
#include "sys/param.h"
#include "sys/mmu.h"
#include "sys/page.h"
#include "sys/region.h"
#include "sys/time.h"
#include "sys/proc.h"
#include "sys/termio.h"
#include "sys/tty.h"
#include <sys/scc.h>
#include <sys/uconfig.h>

#define	isAside(a)  (((int)(a) & 0x2) == 2)	/* check address for A port */

/* The "scc_console" flag informs the "normal" scc driver (sccio.c) that
 * we've been running the console on the serial port, and that it should
 * compensate appropriately.  This replaces its original #ifdef.
 */
short scc_console = 0;			/* set if we get called */

extern int scc_addr;
extern unsigned char	sc_d5[];	/* saved register 5 contents */
extern struct tty	sc_tty[];	/* the per-device tty structures */
extern struct ttyptr	sc_ttptr[];	/* tty lookup structures */

static short		isinit;

#define	W5ON	(W5TXENABLE | W5RTS | W5DTR)	/* turn on to talk */

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

/*
 *	table to initialize a port to 9600 baud
 *	ports are initialized OFF, to avoid asserting appletalk type signals.
 */

static char scitable[] = {
	9, 0,
#define	SCCIRESET scitable[1]
	1, 0,
	15, 0x80,
	4, W4CLK16 | W42STOP,
	11, W11RBR | W11TBR,
	10, 0,
	12, S(9600) & 0xFF,		/* 12/13 are baud rate */
	13, (S(9600) >> 8) & 0xFF,	/* speed should be set to sspeed */
	14, W14BRGE,
	3, W38BIT | W3RXENABLE,
	5, W58BIT | W5TXENABLE,
	1, W1RXIALL | W1TXIEN | W1EXTIEN,
	2, 0,				/* auto vector */
	0, W0RXINT,
	9, W9MIE | W9DLC,
};

scputchar(c)
register int c;
{
	register struct device *addr ;
	register int s;
	register int i;

	if (!isinit) {
	    scc_console = 1;
	    sc_addr_init (scc_addr);
	}

	addr = (struct device *)sc_ttptr[CONSOLE].tt_addr;

	s = spl7();
	if ((sc_d5[CONSOLE] & W5ON) != W5ON) {
		if(!isinit) {
			if (isAside(addr)) {
				SCCIRESET = W9NV | W9ARESET;
			} else {
				SCCIRESET = W9NV | W9BRESET;
			}
			for (i = 0; i < sizeof(scitable); i++) {
				addr->csr = scitable[i];
			}
			isinit = 1;
			for (i = 500000; i; i--) {
				addr->csr = 1;
				if (addr->csr & R1ALLSENT)
					break;
			}
			for(i = 10000; i ; --i) {	/* Wait some more */
				;
			}
		}
		sc_d5[CONSOLE] |= W58BIT | W5ON;
		addr->csr = 5;
		addr->csr = sc_d5[CONSOLE];
	}
	if (c == '\n')
		scputchar('\r');
	i = 100000;
	while ((addr->csr & R0TXRDY) == 0 && --i)
			;
	addr->data = c;
	splx(s);
}

scgetchar()
{
	register struct device *addr ;
	register int c;
	register int s;
	register int i;

	addr = (struct device *)sc_ttptr[CONSOLE].tt_addr;
	s = spl7();
	while ((addr->csr & R0RXRDY) == 0)
		;
	c = addr->data & 0x7F;	/* read data BEFORE reset error */
	addr->csr = W0RERROR;	/* reset error condition */
	addr->csr = W0RXINT;	/* reinable receiver interrupt */
	addr->csr = W0REXT;	/* reset external status interrupts */
	addr->csr = W0RIUS;	/* reset interrupt under service */
	splx(s);
	return(c);
}

scwaitchar()
{
	register struct device *addr ;
	register int c;
	register int s;
	register int i;

	addr = (struct device *)sc_ttptr[CONSOLE].tt_addr;
	return ( ((addr->csr & R0RXRDY) != 0));
}

sc_addr_init (scc_addr)
int scc_addr;
{
    sc_ttptr[MODEM_PORT].tt_addr = scc_addr + 2;
    sc_ttptr[PRINTER_PORT].tt_addr = scc_addr;

    sc_ttptr[MODEM_PORT].tt_tty = &sc_tty[MODEM_PORT];
    sc_ttptr[PRINTER_PORT].tt_tty = &sc_tty[PRINTER_PORT];
}

/* This routine is installed by the debugger when you press the NMI
 * key and we're running IOP serial.  Although the IOP is now dead
 * (in bypass mode), we need a routine to handle the spurious IRQ
 * that gets left pending after resuming from the debugger.
 *
 * Also, consider the following scenario:
 *	- boot the Mac OS in "Compatibility" serial mode
 *	- set "Faster" serial with Compatibility CDEV
 *	- launch A/UX without rebooting
 *
 * This scenario leaves the SCC activated, from Compatibility mode,
 * causing it to generate interrupts.  Unfortunately, with A/UX in
 * "faster" IOP mode, no A/UX code handles the SCC interrupt directly.
 * This can either cause a never-ending stream of "spurious bypass-mode"
 * printfs from iopmgr, or a hang if scintr() gets called before scinit().
 *
 * We just blast the SCC interrupts and hope for the best.
 */
/*ARGSUSED*/
int scbypassint(iop,a)
register int iop;
register struct iop *a;
{
	register struct device *addr ;

	addr = (struct device *)scc_addr;
	addr->csr = W0REXT;
	addr->csr = W0RTXPND;
	addr->csr = W0RERROR;
	addr->csr = W0RIUS;
	addr->csr = 1;
	addr->csr = 0;			/* disable all interrupts */

	addr = (struct device *)(scc_addr + 2);
	addr->csr = W0REXT;
	addr->csr = W0RTXPND;
	addr->csr = W0RERROR;
	addr->csr = W0RIUS;
	addr->csr = 1;
	addr->csr = 0;			/* disable all interrupts */
	return(0);
}
