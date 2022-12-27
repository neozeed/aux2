/*
 * @(#)gcr_fmttrk.c  {Apple version 1.1 89/08/15 11:44:40}
 *
 * Copyright (c) 1987, 1988, 1989 by Apple Computer, Inc.
 * All Rights Reserved.
 *
 * THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF APPLE COMPUTER, INC.
 * The copyright notice above does not evidence any actual or
 * intended publication of such source code.
 */

#if !defined(lint) && !defined(NO_SCCS_IDS)
static char _sccsid[]="@(#)gcr_fmttrk.c  {Apple version 1.1 89/08/15 11:44:40}";
#endif

#include "fd.h"
#include "fdhw.h"

    extern long iwm_addr;
    extern struct chipparams *fd_chip;

    static u_char sync[]	= { 0xff, 0x3f, 0xcf, 0xf3, 0xfc, 0 };

#define WRT_NEXTNIBL(x)	{ 					\
			    while (!(iwm->Q6L & IWM_DAVAIL))	\
				;				\
			    iwm->Q6H = (x);			\
			}

/*----------------------------------*/
int
gcr_erasetrack()
{
    void delay_100us();

    register struct iwm *iwm	= (struct iwm *)iwm_addr;
    register u_char *cp;
    register int i;
    register u_char byte;

    (void)(*fd_chip->seltrack)(0);	/* select track zero */

    /* Chip setup: */
    byte = iwm->Q6H;

    iwm->Q7H = 0xaa;			/* blast first byte immediately */

    /* By writing partial address marks, we cause instant read-address
     * errors so we spend very little time when we next open this diskette!
     */
    for (i = 0; i < 1200; i++) {	/* more than enough for a track */
	cp = &sync[0];
	do {
	    WRT_NEXTNIBL(*cp++);
	} while (*cp);
	WRT_NEXTNIBL(0xff);
	WRT_NEXTNIBL(0xd5);
	WRT_NEXTNIBL(0xaa);
    }

    byte = iwm->Q7L;			/* out of write mode */

    delay_100us(8);			/* must wait 750us before headswitch */

#ifdef LINT
    byte = byte;
#endif

    return(0);
}

/*----------------------------------*/
/* Note: Drive speed tolerance is +- 2.5%.  Thus we must assume the worst
 * case intersector gap (7 sync groups or 42 nibbles).  We don't bother to
 * update the intersector gap for slow drives.
 */

int
gcr_fmttrack(gp)
register struct geom *gp;
{
    extern u_char gcr_nibl[];

    static int gapsync		= 7;
    static u_char datasync[]	= { 0xff, 0x3f, 0xcf, 0xf3, 0xfc, 0xff,
				    0xd5, 0xaa, 0xad, 0 };

    static u_char addrsync[]	= { 0xff, 0xd5, 0xaa, 0x96, 0 };

    static u_char bitslip[]	= { 0xde, 0xaa, 0xff, 0 };

    /* Sector interleave tables precomputed for simplicity. */
    static u_char stab12[]	= {  0, 6, 1, 7, 2, 8, 3, 9, 4, 10, 5, 11 };
    static u_char stab11[]	= {  0, 6, 1, 7, 2, 8, 3, 9, 4, 10, 5 };
    static u_char stab10[]	= {  0, 5, 1, 6, 2, 7, 3, 8, 4, 9 };
    static u_char stab9[]	= {  0, 5, 1, 6, 2, 7, 3, 8, 4 };
    static u_char stab8[]	= {  0, 4, 1, 5, 2, 6, 3, 7 };
    static u_char *stabaddr[]	= { stab8, stab9, stab10, stab11, stab12 };

    static struct addr a;

    register struct iwm *iwm	= (struct iwm *)iwm_addr;
    register u_char *nibl	= &gcr_nibl[0];
    register u_char *cp;
    register u_char *sp;
    register int sect;
    register u_char sum;
    register u_char byte;
    register int rc;
    register int i;

    (void)(*fd_chip->seltrack)(gp->track);

    sp = stabaddr[gp->spt - 8];

    /* Chip setup: */
    byte = iwm->Q6H;

    iwm->Q7H = 0xff;			/* blast first byte immediately */

    /* Blast out 200 sync groups to make sure we've got a large enough
     * portion of the track recorded that we'll have a clean seam.
     */
    for (i = 0; i < (200 - gapsync); i++) {
	cp = &sync[0];
	do {
	    WRT_NEXTNIBL(*cp++);
	} while (*cp);
    }

    for (sect = 0; sect < gp->spt; sect++) {

	/* The intersector gap. */

	for (i = 0; i < gapsync; i++) {
	    cp = &sync[0];
	    do {
		WRT_NEXTNIBL(*cp++);
	    } while (*cp);
	}

	/* The address marks and header. */

	cp = &addrsync[0];
	do {
	    WRT_NEXTNIBL(*cp++);
	} while (*cp);

	sum = 0;

	byte = gp->cyl & 0x3f;		/* track (cylinder) low 6 bits */
	WRT_NEXTNIBL(nibl[byte]);
	sum ^= byte;

	byte = sp[sect];		/* sector */
	WRT_NEXTNIBL(nibl[byte]);
	sum ^= byte;

	byte = (gp->track) << 5 | (gp->cyl >> 6);	/* side & high cyl */
	WRT_NEXTNIBL(nibl[byte]);
	sum ^= byte;

	byte = 2 | (gp->maxtrk << 5);	/* format: 2:1 interleave, N sides */
	WRT_NEXTNIBL(nibl[byte]);
	sum ^= byte;

	WRT_NEXTNIBL(nibl[sum]);	/* checksum */

	cp = &bitslip[0];		/* bitslip marks */
	do {
	    WRT_NEXTNIBL(*cp++);
	} while (*cp);

	/* Intrasector sync field, data marks, sector number. */

	cp = &datasync[0];
	do {
	    WRT_NEXTNIBL(*cp++);
	} while (*cp);

	byte = sp[sect];		/* sector */
	WRT_NEXTNIBL(nibl[byte]);

	/* Data field, including zero checksums. */

	byte = nibl[0x00];		/* zero data, pre-nibblized */
	for (i = 0; i < (699 + 4); i++) {	/* (12+512) * 4 / 3 */
	    WRT_NEXTNIBL(byte);
	}

	cp = &bitslip[0];		/* bitslip marks */
	do {
	    WRT_NEXTNIBL(*cp++);
	} while (*cp);
    }

    rc = iwm->Q6L;			/* save overrun flag */

    /* Deliberately allow the 0xff to underrun to ensure that it writes. */

    while (iwm->Q6L & 0x40)		/* wait for the overrun bit */
	;

    if (!(rc & 0x40)) {			/* an unexpected overrun before? */
	TRACE(3,("gcr_fmttrack: underrun\n"));
    }

    byte = iwm->Q7L;			/* out of write mode */

    /* Now we make sure we can see sector zero...else we're way too fast. */

     rc = gcr_rdadr(&a);
     if (rc || (a.sector != 0)) {
	TRACE(3,("gcr_fmttrack: rdadr rc=%d, sect=%d\n",rc,a.sector));
	return(EIO);			/* woopsie... */
     }

    return(0);

}
