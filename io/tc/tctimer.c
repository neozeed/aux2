/*
 * @(#)tctimer.c  {Apple version 1.1 89/08/25 16:55:34}
 *
 * Copyright (c) 1987, 1988, 1989 by Apple Computer, Inc.
 * All Rights Reserved.
 *
 * THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF APPLE COMPUTER, INC.
 * The copyright notice above does not evidence any actual or
 * intended publication of such source code.
 */

#if !defined(lint) && !defined(NO_SCCS_IDS)
static char _sccsid[]="@(#)tctimer.c  {Apple version 1.1 89/08/25 16:55:34}";
#endif

#include "tc.h"

/* Timeout routine.  This routine runs once every 100 milliseconds.
 *  It is used to wait before retries, delay after certain errors,
 *  wait for autoload to complete, and whatever else...
 *
 * We let the timeout go negative so that we get an extra count for
 *  all timeouts, to prevent a one-count timer from hitting instantly.
 */

int tc_timeron = 0;			/* nonzero if we're running */

/*ARGSUSED*/
int
tc_timer(arg)
caddr_t arg;
{
    void wakeup();
    void timeout();

    extern struct softc tc_softc[];
    extern int tc_t_timer;

    register struct buf *bp;
    register struct softc *s;
    register int open = 0;

    /* Note that the s->timeout-- is done like that so we get
     * an extra count out of the timer.
     */

    /* Scan all devices for pending requests. */

    for (s = &tc_softc[0]; s < &tc_softc[NTC]; s++) {
	if (s->timertype != 0) {	/* then one's pending */
	    if (s->timeout == 0) {	/* then it's expired */
		switch (s->timertype) {
		    
		    case T_WAKEUP:
					wakeup((caddr_t)&s->buf);
					break;

		    case T_RESTART:
					bp = (struct buf *)s->req.driver;
					(void)tc_start(s,bp,NOWAIT);
					break;
		    
		    default:		/* none? clean it up! */
					printf("tc: timer error!\n");
					break;
		}
		s->timeout = 0;
		s->timertype = 0;	/* not running anymore */
	    } else {			/* not yet expired, count down */
		s->timeout--;
	    }
	}
	if (s->opening || s->open) {	/* remember open drives */
	    open++;
	}
    }
    
    /* Only reschedule ourselves if there's a drive open. */

    if (open) {
	timeout(tc_timer,(caddr_t)0,tc_t_timer);
	tc_timeron = 1;
    } else {
	tc_timeron = 0;
    }
    return(0);
}
