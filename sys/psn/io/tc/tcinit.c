/*
 * @(#)tcinit.c  {Apple version 1.4 90/01/19 14:03:58}
 *
 * Copyright (c) 1987, 1988, 1989 by Apple Computer, Inc.
 * All Rights Reserved.
 *
 * THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF APPLE COMPUTER, INC.
 * The copyright notice above does not evidence any actual or
 * intended publication of such source code.
 */

#if !defined(lint)
static char _sccsid[]="@(#)tcinit.c  {Apple version 1.4 90/01/19 14:03:58}";
#endif

/*
 *	tc -- Apple Tape Backup 40SC cartridge driver
 *
 *	Copyright Apple Computer Inc. 1987
 *
 */
#include "tc.h"

/*--------------------------------*/
void					/* general initialization */
tc_init()
{
    int tc_ret();
    void tc_hello();
    void tc_pos_init();

    extern struct softc tc_softc[];
    extern struct softc *tcsc;

    static int initted = 0;

    register int i;
    register struct softc *s;

    if (initted) {
	return;
    }

    for (i = 0; i < NTC; i++) {
	s = &tc_softc[i];

	s->id = i;			/* our SCSI ID */

	s->file = 1;			/* initial position */
	s->blk = 0;

	s->req.cmdbuf   = (caddr_t)&s->cmd;
	s->req.faddr    = tc_ret;	/* return from scsi_request() */
	s->req.cmdlen   = 6;
    }

    tc_pos_init();			/* init positioning routines */

    tc_hello();				/* say hello on console */

    initted++;

    tcsc = &tc_softc[3];		/* for adb debugging */
}

/*--------------------------------*/
static void tc_hello()
{
    static char tc_rev[] = "1.4";
    extern short tc_revnum;

    register char *vp;

    vp = &tc_rev[0];

    printf("tc:Apple 40SC tape driver ver %s\n",vp);

    /* Compute numeric rev number. We convert the m.nn string to
     * a number in the form (m * 100) + nn.  A cheap algorithm is
     * used since the kernel doesn't have atoi() or any variant.
     */
    
    tc_revnum  = 100 * (*vp++ & 0x0f);	/* m */
    vp++;				/* bump over '.' */
    tc_revnum += 10  * (*vp++ & 0x0f);	/* n */
    tc_revnum +=       (*vp & 0x0f);	/* n */

}
