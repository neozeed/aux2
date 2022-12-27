/*
 * @(#)init.c  {Apple version 1.9 90/03/13 12:21:38}
 *
 * Copyright (c) 1987, 1988, 1989 by Apple Computer, Inc.
 * All Rights Reserved.
 *
 * THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF APPLE COMPUTER, INC.
 * The copyright notice above does not evidence any actual or
 * intended publication of such source code.
 */

#if !defined(lint)
static char _sccsid[]="@(#)init.c  {Apple version 1.9 90/03/13 12:21:38}";
#endif

#include "fd.h"
#include <sys/fdioctl.h>
#include <sys/conf.h>
#include <sys/uconfig.h>

    extern int fd_minor;
    extern struct drivestatus fd_status[];

/*--------------------------------*/
int
fd_init()				/* GENERIC */
{
    int fd_strategy();
    void finddrives();
    void fd_hello();
    int fd_timer();

    extern short machineID;
    extern struct interface fd_interface[];
    extern struct interface *fd_int;
    extern int fd_t_timer;
    register int i;

    /* Stuff the major dev into ds->dev. */

    for (i = 0; i < bdevcnt; i++) {
	if(bdevsw[i].d_strategy == fd_strategy)
	    break;
    }
    fd_status[0].dev = i << 8;
    fd_status[1].dev = i << 8;

    /* Assume that the initial drive is drive zero, GCR800 */

    fd_minor = 0;

    if (machineID == MACIIfx) {
	fd_int = &fd_interface[0];	/* IOP-controlled SWIM */
    } else {
	fd_int = &fd_interface[1];	/* motherboard-controlled IWM/SWIM */
    }

    fd_hello();				/* say hello on console */

    if((*fd_int->init)()) {		/* init the interface */
	printf("fd: init failed, driver unusable.\n");
	return(-1);
    }

    finddrives();			/* find and announce drives */

    /* Start the timer to poll for disk insert and do sleep delays: */

    timeout(fd_timer,(caddr_t)0,fd_t_timer);	/* kick it off */

    return(0);

}

/*--------------------------------*/
static void fd_hello()			/* GENERIC */
{
    static char fd_rev[] = "1.9";
    extern struct fd_meter fd_meter;
    extern int fd_revnum;

    register char *vp;

    vp = fd_rev;

    printf("fd:floppy driver ver %s; ",vp);

    /* Compute numeric rev number. We convert the m.nn string to
     * a number in the form (m * 100) + nn.  A cheap algorithm is
     * used since the kernel doesn't have atoi() or any variant.
     */
    
    fd_revnum  = 100 * (*++vp & 0x0f);	/* m */
    vp++;				/* bump over '.' */
    fd_revnum += 10  * (*++vp & 0x0f);	/* n */
    if (*vp) {
	fd_revnum +=       (*++vp & 0x0f);	/* n */
    }

    fd_meter.rev = fd_revnum;
}

/*----------------------------------*/
static void
finddrives()				/* GENERIC */
{
    extern struct driveparams fd_d_params[];

    register struct drivestatus *ds;
    register int dn;			/* drive number */
    register int n = 0;			/* number of drives */
    register int stat;

    /* We go downward to prevent leaving the
     * LED lit on an external drive.
     */
    for (dn = MAXDRIVES - 1; dn >= 0; dn--) {

	ds = &fd_status[dn];
	ds->installed = ds->twosided = ds->fdhd = 0;

	(void)(*fd_int->enable)(dn);	/* enable the drive */
	stat = (*fd_int->status)();	/* get its status */
	(void)(*fd_int->disable)();	/* now disable it again */

	if (stat & S_NODRIVE) {		/* no drive--skip it */
	    continue;
	}

	ds->installed = 1;
	if (stat & S_TWOSIDED) {	/* set default params */
	    ds->twosided = 1;
	    ds->dp = &fd_d_params[FMT_GCR800 - 1];
	} else {
	    ds->dp = &fd_d_params[FMT_GCR400 - 1];
	}

	if (n++ > 0) {
	    printf(",");
	}

	if (stat & S_FDHD) {
	    ds->fdhd = 1;
	    printf(" d%d is an FDHD Drive",dn);
	} else {
	    ds->fdhd = 0;
	    if (ds->twosided) {
		printf(" d%d is 800K (2 head)",dn);
	    } else {
		printf(" d%d is 400K (1 head)",dn);
	    }
	}
    }
    printf("\n");

    if (fd_status[0].installed && !fd_status[0].twosided) {
	printf("fd: d0 400K drive unusable.\n");
    }
    if (fd_status[1].installed && !fd_status[1].twosided) {
	printf("fd: d1 400K drive unusable.\n");
    }
}
