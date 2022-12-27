/*
 * @(#)mfm_fmttrk.c  {Apple version 1.2 90/03/15 19:50:26}
 *
 * Copyright (c) 1987, 1988, 1989 by Apple Computer, Inc.
 * All Rights Reserved.
 *
 * THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF APPLE COMPUTER, INC.
 * The copyright notice above does not evidence any actual or
 * intended publication of such source code.
 */

#if !defined(lint) && !defined(NO_SCCS_IDS)
static char _sccsid[]="@(#)mfm_fmttrk.c  {Apple version 1.2 90/03/15 19:50:26}";
#endif

#include "fd.h"
#include "fdhw.h"
#include <sys/uconfig.h>
#include <sys/via6522.h>

    extern long iwm_addr;
    extern struct chipparams *fd_chip;

#define WRT_NEXTNIBL(x)	{						\
			    while (!(sp->rhandshake & ISM_DAVAIL)) {	\
				DELAY256NS;				\
			    }						\
			    sp->wdata = (x);				\
			}

/*----------------------------------*/
int
mfm_erasetrack()
{
    void delay_100us();

    register struct swim *sp	= (struct swim *)iwm_addr;
    register struct via *vp	= (struct via *)VIA1_ADDR;
    register u_char byte;
    register int i;
    register int j;

    (void)(*fd_chip->seltrack)(0);	/* select track zero */

    /* Chip setup: */

    byte        = sp->rerror;		/* clear error reg */
    DELAY256NS;
    sp->wzeroes = (ISM_WRITE | ISM_ACTION); /* turn off ACTION */
    DELAY256NS;
    sp->wones   = ISM_WRITE;		/* indicate WRITE */
    DELAY256NS;
    sp->wones   = ISM_CLRFIFO;		/* clr the fifo by toggling */
    DELAY256NS;
    sp->wzeroes = ISM_CLRFIFO;
    DELAY256NS;
    byte        = sp->rerror;		/* clear error reg again */

    sp->wdata = 0x4e;			/* prime the fifo with gaps */
    DELAY256NS;
    sp->wdata = 0x4e;
    DELAY256NS;
    sp->wones = ISM_ACTION;		/* now turn on ACTION */

    /* We write partial address-marks for a sure failure. */

    for (i = 0; i < 1000; i++) {	/* well more than a trackfull */

	byte = 0x00;			/* sync value */
	for (j = 0; j < 12; j++) {
	    WRT_NEXTNIBL(byte);
	}

	do {
	    DELAY256NS;
	} while (!(sp->rhandshake & ISM_DAVAIL));

	DELAY256NS;
	sp->wmark = 0xa1;

	do {
	    DELAY256NS;
	} while (!(sp->rhandshake & ISM_DAVAIL));

	DELAY256NS;
	sp->wmark = 0xfe;
    }

out:;
    DELAY256NS;
    sp->wzeroes = (ISM_WRITE | ISM_ACTION); /* turn off WRITE and ACTION */

    delay_100us(8);			/* must wait 750us before headswitch */

#ifdef LINT
    byte = byte;
#endif

    return(0);
}


/*----------------------------------*/
/* We write the following on MFM diskettes:
 *
 *	 80	gap bytes after index pulse
 *	 12	sync bytes (0x00)
 *	  4	index mark (0xc2 0xc2 0xc2 0xfc)
 *	 50	gap bytes
 *
 *	EACH SECTOR CONSISTS OF:
 *	 12	sync bytes (0x00)
 *	 10	address field (0xa1 0xa1 0xa1 0xfe cyl side sect size CRC1 CRC2)
 *	 22	gap bytes (0x4e)
 *	 12	sync bytes (0x00)
 *	  4	data marks (0xa1 0xa1 0xa1 0xfb)
 *	514	data field (<512-0xf6's> CRC1 CRC2)
 *	nnn	gap bytes (0x4e)  (80 for 720K, 101 for 1440K)
 *
 * We write gap bytes while waiting for S_INDEX to go high, which we will
 * use as the signal to start writing the track.  After we finish the last
 * sector, we write gap bytes while watching for S_INDEX (to ensure that
 * we haven't plowed right over sector zero.  We write a maximum of 1000
 * gap bytes...if we don't find S_INDEX by then, we know we're past it and
 * simply report an I/O error (probably drive too fast).
 *
 * On track 0, however, we don't bother doing this...we take the quick way
 * out so we'll have plenty of time to switch heads before S_INDEX shows up
 * on track 1.
 *
 * A side effect of this "not checking for S_INDEX" on track 0 is that on
 * cyl0/track0 we could have an unformatted area after the last sector, but
 * that area will either be 1) never formatted, 2) the tail of an old sector,
 * or 3) gap bytes from a prior format.  In any event it can't be something
 * dangerous like an address mark since S_INDEX is always in a fixed place
 * with respect to the media.  There is never any problem on cylinders past
 * zero since the seek time gets us into the track about 4 sectors from the
 * starting index pulse, thus causing us to fill the rest of the track until
 * we see the next occurrence of the index pulse.
 */

int
mfm_fmttrack(gp)
register struct geom *gp;
{
    void delay_100us();

    extern u_char mfm_amarks[];
    extern u_char mfm_dmarks[];
    extern u_char mfm_index[];

    register struct swim *sp	= (struct swim *)iwm_addr;
    register struct via *vp	= (struct via *)VIA1_ADDR;
    register u_char *cp;
    register u_char savedphase;
    register u_char byte;
    register int spt;
    register int i;

    spt = gp->spt;
    gp->sector = 1;

    /* Chip setup: */

    (void)(*fd_chip->seltrack)(gp->track);	/* select the head */
    savedphase = sp->rphase;

    DELAY256NS;
    byte        = sp->rerror;		/* clear error reg */
    DELAY256NS;
    sp->wzeroes = (ISM_WRITE | ISM_ACTION); /* turn off ACTION */
    DELAY256NS;
    sp->wones   = ISM_WRITE;		/* indicate WRITE */
    DELAY256NS;
    sp->wones   = ISM_CLRFIFO;		/* clr the fifo by toggling */
    DELAY256NS;
    sp->wzeroes = ISM_CLRFIFO;
    DELAY256NS;
    byte        = sp->rerror;		/* clear error reg again */

    /* Write a bunch of gap bytes while waiting for S_INDEX. We wait
     * for it to be low, then watch for it going high. 
     */
    DELAY256NS;
    sp->wdata = 0x4e;			/* prime the fifo */
    DELAY256NS;
    sp->wdata = 0x4e;
    DELAY256NS;
    sp->wones = ISM_ACTION;		/* now turn on ACTION */

    while (sp->rhandshake & 0x08) {	/* pad till index goes low */
	WRT_NEXTNIBL(0x4e);		/* write a pad */
	DELAY256NS;
    }

    i = 13500;				/* more than full track of 12500 */

    while (!(sp->rhandshake & 0x08) && --i) { /* pad till index goes high */
	WRT_NEXTNIBL(0x4e);		/* write a pad */
	DELAY256NS;
    }

    if (i == 0) {
	TRACE(3,("no index pulse to start\n"));
	goto out;
    }

    /* Select a "constant" input from the drive to avoid crosstalk
     * from the index pulse.  An undocumented feature here is that
     * while writing, "index" always appears on read and sense lines,
     * even though one would expect the HEADSEL line to be required
     * in order to select "index".  Hitting HEADSEL to select "index"
     * would, naturally, select a different head...
     */

    sp->wphase	= 0xf5;			/* S_1MBDRIVE is constant */

    byte = 0x4e;
    for (i = 0; i < 80; i++) {		/* starting gap after index */
	WRT_NEXTNIBL(byte);
    }

    byte = 0x00;			/* sync value */
    for (i = 0; i < 12; i++) {
	WRT_NEXTNIBL(byte);
    }
    cp = &mfm_index[0];			/* index marks */
    do {				/* a fast loop */
	while (!(sp->rhandshake & ISM_DAVAIL)) {
	    DELAY256NS;
	}
	sp->wmark = *cp++;
	DELAY256NS;
    } while (*cp);

    byte = 0x4e;
    for (i = 0; i < 50; i++) {		/* gap after index mark */
	WRT_NEXTNIBL(byte);
    }

    do {

	/* Sync field before address marks: */

	byte = 0x00;			/* sync value */
	for (i = 0; i < 12; i++) {
	    WRT_NEXTNIBL(byte);
	}

	/* The address field: */

	cp = &mfm_amarks[0];		/* address marks */
	do {				/* a fast loop */
	    while (!(sp->rhandshake & ISM_DAVAIL)) {
		DELAY256NS;
	    }
	    sp->wmark = *cp++;
	    DELAY256NS;
	} while (*cp);

	if (sp->rhandshake & ISM_ERROR) {	/* XXX */
	    TRACE(3,("before cyl\n"));
	    i = fd_ism_error();
	    goto out;
	}
	WRT_NEXTNIBL(gp->cyl);		/* track (cylinder) */

	DELAY256NS;
	if (sp->rhandshake & ISM_ERROR) {	/* XXX */
	    TRACE(3,("before side\n"));
	    i = fd_ism_error();
	    goto out;
	}
	WRT_NEXTNIBL(gp->track);	/* side */

	DELAY256NS;
	if (sp->rhandshake & ISM_ERROR) {	/* XXX */
	    TRACE(3,("before sector\n"));
	    i = fd_ism_error();
	    goto out;
	}
	WRT_NEXTNIBL(gp->sector);	/* sector 1..n */

	DELAY256NS;
	if (sp->rhandshake & ISM_ERROR) {	/* XXX */
	    TRACE(3,("before size\n"));
	    i = fd_ism_error();
	    goto out;
	}
	WRT_NEXTNIBL(0x02);		/* size byte */

	DELAY256NS;
	if (sp->rhandshake & ISM_ERROR) {	/* XXX */
	    TRACE(3,("before header CRC\n"));
	    i = fd_ism_error();
	    goto out;
	}

	DELAY256NS;
	while (!(sp->rhandshake & ISM_DAVAIL)) {
	    DELAY256NS;
	}
	sp->wcrc = byte;		/* force write of 2-byte CRC */

	/* The intrasector gap: */

	DELAY256NS;
	if (sp->rhandshake & ISM_ERROR) {	/* XXX */
	    TRACE(3,("before intrasector gap\n"));
	    i = fd_ism_error();
	    goto out;
	}

	byte = 0x4e;
	for (i = 0; i < 22; i++) {	/* gap after address mark */
	    WRT_NEXTNIBL(byte);
	}

	/* Sync field before data marks: */

	byte = 0x00;			/* sync value */
	for (i = 0; i < 12; i++) {
	    WRT_NEXTNIBL(byte);
	}

	/* The data mark bytes: */

	cp = &mfm_dmarks[0];
	do {				/* fast loop */
	    while (!(sp->rhandshake & ISM_DAVAIL)) {
		DELAY256NS;
	    }
	    sp->wmark = *cp++;
	} while (*cp);

	DELAY256NS;
	if (sp->rhandshake & ISM_ERROR) {	/* XXX */
	    TRACE(3,("before data bytes\n"));
	    i = fd_ism_error();
	    goto out;
	}
	/* Write the data. */

	byte = 0xf6;			/* fill value */
	for (i = 0; i < 512; i++) {
	    WRT_NEXTNIBL(byte);
	}

	/* Write the CRC. */

	while (!(sp->rhandshake & ISM_DAVAIL)) {
	    DELAY256NS;
	}
	sp->wcrc = byte;		/* force write of 2-byte CRC */

	/* The intersector gap. After the last sector, the gap is only
	 * five (5) bytes, enough to flush out the checksum.
	 */

	if (gp->sector < spt) {
	    i = gp->gapsize;		/* intersector gap size */
	} else {
	    i = 5;
	}
	byte = 0x4e;
	do {
	    WRT_NEXTNIBL(byte);
	} while (--i);

    } while (++gp->sector <= spt);

    /* Now we keep writing gap bytes while watching for index. We
     * don't really return an error if index isn't found in time,
     * since the verify pass will fail if we destroyed sector zero.
     */
    i = 1000;				/* max before seeing index */
    if (gp->track != 0) {
	DELAY256NS;
	sp->wphase = savedphase;	/* turn index back on */
	DELAY256NS;
	while (!(sp->rhandshake & 0x08) && --i) { /* pad till index high */
	    WRT_NEXTNIBL(byte);		/* write more gap bytes */
	    DELAY256NS;
	}
	if (i == 0) {
	    TRACE(3,("no index pulse in seam\n"));
	    goto out;
	}
    }

out:;
    sp->wzeroes = (ISM_WRITE | ISM_ACTION); /* turn off WRITE and ACTION */

    delay_100us(8);			/* must wait 750us before headswitch */

    return(i ? 0 : EIO);		/* OK if we saw index */
}
