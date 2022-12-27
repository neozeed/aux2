#ifndef lint	/* .../sys/PAGING/os/prf.c */
#define _AC_NAME prf_c
#define _AC_NO_MAIN "@(#) Copyright (c) 1985-87 UniSoft Corporation, All Rights Reserved.  {Apple version 2.1 89/10/13 12:06:19}"
#include <apple_notice.h>

#ifdef _AC_HISTORY
  static char *sccsid = "@(#)Copyright Apple Computer 1987\tVersion 2.1 of prf.c on 89/10/13 12:06:19";
#endif		/* _AC_HISTORY */
#endif		/* lint */

#define _AC_MODS
/*	@(#)prf.c	UniPlus VVV.2.1.5	*/

#ifdef lint
#include "sys/sysinclude.h"
#else lint
#include "sys/types.h"
#include "sys/mmu.h"
#include "sys/sysmacros.h"
#include "sys/param.h"
#include "sys/page.h"
#include "sys/systm.h"
#include "sys/buf.h"
#include "sys/elog.h"
#include "sys/iobuf.h"
#include "sys/region.h"
#include "sys/signal.h"
#include "sys/dir.h"
#include "sys/time.h"
#include "sys/proc.h"
#include "sys/user.h"
#include "sys/debug.h"
#include "sys/conf.h"
#endif lint

/*
 * printf - scaled down C library printf, only
 * %s, %u, %d, %o, and %x.
 */

int printfstall = 
#ifdef	PRINTFSTALL
	PRINTFSTALL;
#else	PRINTFSTALL
	0;
#endif	PRINTFSTALL

/* VARARGS1 */

printf(fmt, x1)
register char *fmt;
unsigned x1;
{
	register c;
	register unsigned int *adx;
	register char *s;
	int sps;
	extern char	putbuf[];

	for (c = 0; c < printfstall; c++)
		;
	sps = splhi();

	adx = &x1;

	for(;;) {
		while((c = *fmt++) != '%') {
			if(c == '\0') {
				osmwakeup();
				splx(sps);
				return;
			}
			outchar(c);
		}
		switch( c = *fmt++ ) {
		case 'd':
		case 'u':
		case 'o':
		case 'x':
			printn((unsigned int)*adx, c);
			break;
		case 'b':
			{
			char fcode;
			int b, any, i;

			b = *adx++;
			s = (char *)*adx;
			switch ((int) *s++) {
				case 8:
					fcode = 'o';
					break;

				case 10:
					fcode = 'u';
					break;

				case 16:
				default:
					fcode = 'x';
					break;
			}
			printn((long)b, fcode);
			any = 0;
			if (b) {
				outchar('<');
				while (i = *s++) {
					if (b & (1 << (i - 1))) {
						if (any)
							outchar(',');
						any = 1;
						for (; (c = *s) > 32; s++)
							outchar(c);
					}
					else
						for (; *s > 32; s++)
							;
				}
				outchar('>');
			}
			}
			break;
		case 'c':
			outchar(*adx);
			break;
		case 's':
			s = (char *)*adx;
			while(c = *s++) {
				outchar(c);
			}
		}
		adx++;
	}
}

static
printn(val, fcode)
unsigned int val;
int fcode;
{
	register int hradix, lowbit, plmax, i;
	char d[12];

	switch(fcode) {
	case 'd':
		if((int)val < 0) {
			outchar('-');
			val = -val;
		}
	case 'u':
		hradix = 5;
		plmax = 10;
		break;
	case 'o':
		hradix = 4;
		plmax = 11;
		break;
	case 'x':
		hradix = 8;
		plmax = 8;
		break;
	}
	for(i=0; i < plmax; i++) {
		lowbit = val & 1;
		val = val >> 1;
		d[i] = "0123456789ABCDEF"[val % hradix * 2 + lowbit];
		val /= hradix;
		if(val == 0)
			break;
	}
	if(i == plmax)
		i--;
	for(; i >= 0; i--) {
		outchar(d[i]);
	}
}

/*
 *	putchar - single character write to memory circular buffer -
 */

char putbuf[10000];		/* system putchar circular buffer */
int  putbsz = sizeof putbuf;	/* size of above */
int  putindx = 0;		/* next position for system putchar */
int  putread = 0;		/* next position to read for dev 2 */
int  putwrap = 0;		/* has the buffer wrapped yet ? */
extern	int	kputc_on;	/* whether kputchar actually does anything */

putchar(c)
char c;
{
	register int pind = putindx;

	putbuf[pind++] = c;
	if( pind < sizeof putbuf ) {
		putindx = pind;
	} else {
		putwrap = 1;
		putindx = 0;
	}
	if (!kputc_on && putread != putindx) {
		return;
	}
	putread++;
	if( putread >= sizeof putbuf )
		putread = 0;
}

/*
 *	prdev - prints a warning message of the
 *	form "mesg on dev maj/min".
 */
prdev(str, dev)
char *str;
dev_t dev;
{
	register maj;

	maj = major(dev);
	if (maj >= bdevcnt) {
		printf("%s on bad dev %o(8)\n", str, dev);
		return;
	}
	(*bdevsw[maj].d_print)(minor(dev), str);
}

static	char	*nam[] = {
	"DFC",
	"MT",
} ;

/*
 *	deverr - print block I/O error diagnostics. It
 *	prints the device, block number, and two arguments.
 */
deverr(dp, o1, o2)
register struct iobuf *dp;
{
	register struct buf *bp;

	bp = dp->b_actf;
	printf("err on %s, minor %o\n",nam[major(dp->b_dev)],minor(bp->b_dev));
	printf("bn=%D er=%o,%o\n", bp->b_blkno, o1, o2);
}

char	*panicstr;

int	mpanics;		/* count master panics */
proc_t		*panicproc;	/* Proc which panic'ed. Used by resched. */
int	pre_paniced;

/*
 *  prepare to panic.
 *	Insure that all printfs related to a panic will come out
 *  somewhere that the user can read them.  Called by panic in case
 *  not already called.
 */
pre_panic()
{
    if (!pre_paniced) {
	pre_paniced = 1;
	kputc_on = 1;

	disp_onebit();
    }
}

/*
 *	panic - called on unresolvable fatal errors.
 *		the fix.sed script ensures that calls
 *		to saveregs are inserted before the calls
 *		to panic, ensuring all general and cpu
 *		registers reflect status at panic.
 *	It syncs, prints "panic: mesg" and then loops.
 */

panic(s)
char *s;
{
	pre_panic();
	if(s && panicstr) {
		printf("!Double panic: %s\n", s);
	} else {
		register int sxl = splhi();

		if (s)
			panicstr = s;
		printf("panic: %s\n", panicstr ?  panicstr : "???");
#ifdef	DEBUG
		printf("Warning:  entering debugger.  You must exit from debug mode to sync the disks.\n");
		debug(u.u_ar0);
#endif
		panicproc = u.u_procp;
		mpanics += 0x10000;
		splx(sxl);
		sync();
		printf("panic: %s\n", panicstr);
	}
	for(;;)
		idle();
}


/*

/*#if OSDEBUG == YES*/
#ifdef HOWFAR

assfail(a, f, l)
register char *a, *f;
{
	/*	Save all registers for the dump since crash isn't
	 *	very smart at the moment.
	 */
	
#ifndef lint
	register int	r6, r5, r4, r3;
#endif lint

	pre_panic();
	printf("assertion failed: %s, file: %s, line: %d\n", a, f, l);
	panic("assertion error");
}

#endif

/* <@(#)prf.c	6.3> */
