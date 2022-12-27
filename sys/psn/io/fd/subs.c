/*
 * @(#)subs.c  {Apple version 1.3 89/12/12 14:03:59}
 *
 * Copyright (c) 1987, 1988, 1989 by Apple Computer, Inc.
 * All Rights Reserved.
 *
 * THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF APPLE COMPUTER, INC.
 * The copyright notice above does not evidence any actual or
 * intended publication of such source code.
 */

#if !defined(lint) && !defined(NO_SCCS_IDS)
static char _sccsid[]="@(#)subs.c  {Apple version 1.3 89/12/12 14:03:59}";
#endif

#include "fd.h"
#include <sys/param.h>
#include <sys/fdioctl.h>

    void wakeup();
    void timeout();

/*----------------------------------*/
int fd_trstat(stat)			/* GENERIC */
register int stat;
{
    register int result = 0;

    if (stat & S_FDHD)
	result |= STAT_FDHD;

    if (stat & S_TWOSIDED)
	result |= STAT_2SIDED;

    if (stat & S_NODRIVE)
	result |= STAT_NODRIVE;

    if (stat & S_NODISK)
	result |= STAT_NODISK;

    if (stat & S_WRTENAB)
	result |= STAT_WRTENAB;

    if (stat & S_1MBMEDIA)
	result |= STAT_1MBMEDIA;

    return(result);
}

/*----------------------------------*/
void fd_print(dev,str)			/* GENERIC */
dev_t dev;
char str[];
{
    printf("%s on fd drive %d\n",str,(dev & FD_DRIVE) >> 4);
}

/*--------------------------------*/
void					/* copied from clock.c */
fd_sleep(ticks)
register int ticks;
{
    extern struct drivestatus fd_status[];
    extern int fd_minor;

    register int s;

    if (ticks <= 0) {
	return;
    }

    s = spl7();
    timeout(wakeup, (caddr_t)&fd_status[fd_minor], ticks);
    (void) sleep((caddr_t)&fd_status[fd_minor], PRIBIO);
    (void) splx(s);
}

/*
 *	This stub routine is called to post a disk insert
 *	event when the ui driver has not been configured
 *	into the kernel. This routine does not eat the DI
 *	event so the floppy driver will keep trying until
 *	the UI device is opened.
 */
/*ARGSUSED*/
int noUIdriver(dev,status)
dev_t dev;
int status;
{
    return(0);
}

int (*postDIroutine)() = noUIdriver;

extern struct drivestatus fd_status[];

int fd_t_timer = 16;			/* ticks per run */

/* Timeout routine.  This routine runs once every 250 milliseconds.
 *  It is used to turn off the drive motor, monitor insert, etc.
 */

/*ARGSUSED*/
int
fd_timer(arg)				/* GENERIC */
caddr_t arg;
{
    void fd_mb_motoroff();

    extern struct interface *fd_int;

    register struct drivestatus *ds;
    register int running = 0;

    /* Scan all devices for pending requests. */

    for (ds = &fd_status[0]; ds < &fd_status[MAXDRIVES]; ds++) {
	if (ds->timertype != 0) {	/* then one's pending */
	    if (--ds->timeout == 0) {	/* then it's expired */
		switch (ds->timertype) {
		    
		    case T_WAKEUP:
					wakeup((caddr_t)ds);
					break;

		    case T_MOTOROFF:
					fd_mb_motoroff();
					break;

		    default:		/* none? clean it up! */
					printf("fd: timer error!\n");
					break;
		}
		ds->timeout = 0;
		ds->timertype = 0;	/* not running anymore */
	    } else {			/* not yet expired, count down */
		running = 1;
	    }
	}
    }

    if (!running) {
	(void)(*fd_int->pollinsert)();	/* check for disk insertion */
    }
    
    /* Always reschedule ourselves.*/

    timeout(fd_timer,(caddr_t)0,fd_t_timer);	/* roughly 250msec */

    return(0);
}
