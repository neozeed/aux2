/*
 * @(#)gcrsubs.c  {Apple version 1.1 89/08/15 11:44:58}
 *
 * Copyright (c) 1987, 1988, 1989 by Apple Computer, Inc.
 * All Rights Reserved.
 *
 * THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF APPLE COMPUTER, INC.
 * The copyright notice above does not evidence any actual or
 * intended publication of such source code.
 */

#if !defined(lint) && !defined(NO_SCCS_IDS)
static char _sccsid[]="@(#)gcrsubs.c  {Apple version 1.1 89/08/15 11:44:58}";
#endif

#include "fd.h"
#include "fdhw.h"
#include <sys/uconfig.h>
#include <sys/via6522.h>

    extern struct chipparams *fd_chip;

/*----------------------------------*/
int
fd_setgcrmode()
{
    (void)(*fd_chip->setgcr)();		/* switch to GCR mode */
    return(0);
}

/*----------------------------------*/
int
fd_gcr_motoron()
{
    (void)(*fd_chip->command)(C_SETGCR);	/* resets after disable */
    (void)(*fd_chip->command)(C_MOTORON);
    return(0);
}

/* Seek direction optimizers return +-1 for direction to seek. GCR drives
 * have motor-speed settling time when changing to a new speedgroup, hence
 * they care.  MFM doesn't change speed, so it's not an issue.  We may wish
 * to revise the logic if we find that accelerate/decelerate times differ.
 * Inputs are assumed: lo <= curcyl <= hi
 */

 extern struct drivestatus *fd_drive;

/*----------------------------------*/
int gcr_optseek(lo,hi)
register short lo;
register short hi;
{
    static short grp[] = { 0, 1, 2, 3, 4};	/* groups */

    register short logroup;
    register short curgroup;
    register short higroup;
    register short lodist;
    register short hidist;

    lodist = fd_drive->curcyl - lo;
    hidist = hi - fd_drive->curcyl;
    
    logroup  = grp[lo >> 4];		/* quick! what groups? */
    curgroup = grp[fd_drive->curcyl >> 4];
    higroup  = grp[hi >> 4];

    /* If both LO and HI are within the current group, go to the closer one */

    if (curgroup == logroup && curgroup == higroup) {	/* no group change */
	return((lodist < hidist) ? 1 : -1);		/* so pick closer */
    }

    /* If neither is within the current group, go to the closer one */

    if (curgroup != logroup && curgroup != higroup) {	/* group'll change */
	return((lodist < hidist) ? 1 : -1);		/* so pick closer */
    }

    /* Otherwise, we know that the group will change in only one direction. */

    return((curgroup == logroup) ? 1 : -1);		/* pick nochange way */
}

/*----------------------------------*/
#define RD_NEXTNIBL(x)	{						\
			    do {					\
				(x) = vp->regb;	/* slowdown */		\
			    } while (!(((x) = iwm->Q6L) & ISM_DAVAIL));	\
			}

/* We're called at high spl to prevent interrupts. */

int gcr_rdadr(ap)
register struct addr *ap;
{
    extern long iwm_addr;
    extern u_char gcr_dnibt[];

    register struct iwm *iwm	= (struct iwm *)iwm_addr;
    register u_char *dnib	= &gcr_dnibt[0] - 0x80;
    register struct via *vp	= (struct via *)VIA1_ADDR;
    register int found		= 0;
    register int loops		= 40000;
    register u_char sum		= 0;
    register u_char byte;

    ap->gap = 0;

    /* Chip setup: */

    byte = iwm->Q7L;			/* get into read mode */
    byte = iwm->Q6L;

    do {

	while (!((byte = iwm->Q6L) & IWM_DAVAIL) && --loops)
	    ;

	if (loops == 0) {			/* timed out */
	    return(EAMTIMEOUT);
	}

	ap->gap++;			/* another byte found */
	if (byte != 0xd5) {
	    continue;
	}

	RD_NEXTNIBL(byte);
	ap->gap++;			/* another byte found */
	if (byte != 0xaa) {
	    continue;
	}

	RD_NEXTNIBL(byte);
	ap->gap++;			/* another byte found */
	if (byte != 0x96) {
	    continue;
	}
	found = 1;

    }  while (!found && --loops);

    if (!found) {
	return(EPAMARKS);
    }

    /* Track (cylinder). The seventh bit will be in the "side" byte. */

    RD_NEXTNIBL(byte);
    byte = dnib[byte];			/* denibblize */
    sum ^= byte;

    ap->cyl = byte;

    /* Sector */
    RD_NEXTNIBL(byte);
    byte = dnib[byte];			/* denibblize */
    sum ^= byte;

    ap->sector = byte;

    /* Side (head). This byte really contains the side in bit 6,
     * with seventh cylinder (track) in bit 0.
     */
    RD_NEXTNIBL(byte);
    byte = dnib[byte];			/* denibblize */
    sum ^= byte;

    ap->head = byte >> 5;		/* head as 0 or 1 */
    ap->cyl |= (byte & 0x01) << 6;	/* merge in seventh bit of cyl */

    /* Format byte. */
    RD_NEXTNIBL(byte);
    byte = dnib[byte];			/* denibblize */
    sum ^= byte;

    ap->interleave = byte & 0x1f;
    ap->sides = (byte & 0x20) ? 2 : 1;
	
    /* Get the checksum, but don't validate it till we have good bitslips. */

    RD_NEXTNIBL(byte);
    byte = dnib[byte];			/* denibblize */
    sum ^= byte;

    /* Get bit slip marks: */

    RD_NEXTNIBL(byte);
    if (byte != 0xde) {
	return(EASLIP);
    }

    RD_NEXTNIBL(byte);
    if (byte != 0xaa) {
	return(EASLIP);
    }

    TRACE(4,("c%d t%d s%d\n",ap->cyl,ap->head,ap->sector));
    return(sum ? EASUM : 0);
}

/*----------------------------------*/
int gcr_sectortime()
{
    /* Enough time for a reasonable sector delay, but we can
     * expect to get back in time.
     */

    return(9);
}
