/*
 * @(#)mfmsubs.c  {Apple version 1.1 89/08/15 11:51:03}
 *
 * Copyright (c) 1987, 1988, 1989 by Apple Computer, Inc.
 * All Rights Reserved.
 *
 * THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF APPLE COMPUTER, INC.
 * The copyright notice above does not evidence any actual or
 * intended publication of such source code.
 */

#if !defined(lint) && !defined(NO_SCCS_IDS)
static char _sccsid[]="@(#)mfmsubs.c  {Apple version 1.1 89/08/15 11:51:03}";
#endif

#include "fd.h"
#include "fdhw.h"
#include <sys/uconfig.h>
#include <sys/via6522.h>

#define MIN(a,b)	(((a) < (b))?(a):(b))

    extern struct chipparams *fd_chip;
    extern struct drivestatus *fd_drive;
    extern long iwm_addr;

    u_char mfm_amarks[] = { 0xa1,0xa1,0xa1,0xfe,0x00 };
    u_char mfm_dmarks[]	= { 0xa1,0xa1,0xa1,0xfb,0x00 };
    u_char mfm_index[]  = { 0xc2,0xc2,0xc2,0xfc,0x00 };

/*----------------------------------*/
int
fd_mfm_motoron()
{
    (void)(*fd_chip->command)(C_SETMFM);	/* resets after disable */
    (void)(*fd_chip->command)(C_MOTORON);
    return(0);
}

/*----------------------------------*/
int
fd_setmfmmode()
{
    (void)(*fd_chip->setmfm)();		/* switch to MFM mode */
    return(0);
}

/*----------------------------------*/
int
mfm_optseek(lo,hi)
register short lo;
register short hi;
{
    /* MFM is constant speed */

    return(MIN((fd_drive->curcyl - lo), (hi - fd_drive->curcyl)));
}

/*----------------------------------*/
#define RD_NEXTNIBL(x)	{						\
			    while (!(sp->rhandshake & ISM_DAVAIL)) {	\
				DELAY256NS;				\
			    }						\
			    (x) = sp->rdata;				\
			}

/* We're called at high spl to prevent interrupts. */

int
mfm_rdadr(addr)
register struct addr *addr;
{

    register u_char *mp;
    register struct swim *sp	= (struct swim *)iwm_addr;
    register struct via *vp	= (struct via *)VIA1_ADDR;
    register int found		= 0;
    register int tries		= 8;	/* reasonable number of attempts */
    register u_char junk;
    register u_char shake;
    register int loops;

    addr->gap = 0;

    /* Apparently, we can encounter "phony" mark bytes depending on
     * how we sync-up with the data stream.
     */
    
    do {

	/* Chip setup: */

	junk        = sp->rerror;		/* clear error reg */
	DELAY256NS;
	sp->wzeroes = (ISM_WRITE | ISM_ACTION);	/* turn off ACTION */
	DELAY256NS;
	sp->wones   = ISM_CLRFIFO;		/* clr the fifo by toggling */
	DELAY256NS;
	sp->wzeroes = ISM_CLRFIFO;
	DELAY256NS;
	junk        = sp->rerror;		/* clear error reg again */
	DELAY256NS;
	sp->wones   = ISM_ACTION;		/* now turn on ACTION */

	mp = &mfm_amarks[0];
	loops = 20000;

	/* Get the address mark bytes: */

	do {
	    DELAY256NS;
	    shake = sp->rhandshake;
	} while (!(shake & ISM_DAVAIL) && --loops);

	if (loops == 0) {			/* timed out */
	    DELAY256NS;
	    sp->wzeroes = ISM_ACTION;		/* turn off ACTION */
	    return(EAMTIMEOUT);
	}

	if (shake & ISM_ISMARK) {
	    do {				/* a fast loop */
		do {
		    DELAY256NS;
		} while (!(sp->rhandshake & ISM_DAVAIL));

		DELAY256NS;
		if (sp->rmark != *mp++) {
		    tries--;			/* wrong mark */
		    goto tryagain;
		}
	    } while (*mp);
	    found++;				/* OK if all found */

	} else {
	    addr->gap++;			/* count gap bytes */
	}

tryagain:;

    } while (!found && (tries > 0));

    if (!found) {
	DELAY256NS;
	sp->wzeroes = ISM_ACTION;	/* turn off ACTION */
	return(EPAMARKS);
    }

    /* Get cylinder, side, sector, blocksize: */

    RD_NEXTNIBL(addr->cyl);
    RD_NEXTNIBL(addr->head);
    RD_NEXTNIBL(addr->sector);
    RD_NEXTNIBL(addr->blksize);

    /* Now check CRC bytes. */

    RD_NEXTNIBL(junk);			/* eat the first CRC byte */

    do {
	DELAY256NS;
    } while(!((shake = sp->rhandshake) & ISM_DAVAIL));

    DELAY256NS;
    junk = sp->rdata;			/* eat the 2nd CRC byte */

    DELAY256NS;
    sp->wzeroes = ISM_ACTION;		/* turn off ACTION */

    addr->sector--;			/* adjust to be zero-relative */

    if (shake & ISM_ERROR) {		/* an error occurred */
	if (shake & ISM_CRCERR) {
	    return(EASUM);
	} else {
	    return(fd_ism_error());	/* decode error reg */
	}
    } else {
	return(0);			/* OK */
    }

#ifdef LINT
/*NOTREACHED*/
    junk = junk;
#endif
}

/*----------------------------------*/
/* We're called at high spl to prevent interrupts. */
int
mfm_read(buf)
register u_char *buf;
{
    register struct swim *sp	= (struct swim *)iwm_addr;
    register struct via *vp	= (struct via *)VIA1_ADDR;
    register u_char *mp		= &mfm_dmarks[0];
    register u_char *endp	= (u_char *)&buf[512];
    register u_char junk;
    register u_char shake;

    /* Chip setup: */

    junk        = sp->rerror;		/* clear error reg */
    DELAY256NS;
    sp->wzeroes = (ISM_WRITE | ISM_ACTION); /* turn off WRITE and ACTION */
    DELAY256NS;
    sp->wones   = ISM_CLRFIFO;		/* clr the fifo by toggling */
    DELAY256NS;
    sp->wzeroes = ISM_CLRFIFO;
    DELAY256NS;
    junk        = sp->rerror;		/* clear error reg again */
    DELAY256NS;
    sp->wones   = ISM_ACTION;		/* now turn on ACTION */

    /* Get the data mark bytes. No need for timeout checking. */

    do {				/* a fast loop */
	do {
	    DELAY256NS;
	} while (!(sp->rhandshake & ISM_DAVAIL));	/* wait for it */

	if (sp->rmark != *mp++) {
	    DELAY256NS;
	    sp->wzeroes = ISM_ACTION;	/* turn off ACTION */
	    return(EDMARKS);		/* bad data marks */
	}
    } while (*mp);

    /* Get the data. */

    do {				/* another fast loop */
	RD_NEXTNIBL(*buf++);
    } while (buf < endp);

    /* Now check CRC bytes. */

    RD_NEXTNIBL(junk);			/* eat the first CRC byte */

    do {
	DELAY256NS;
    } while(!((shake = sp->rhandshake) & ISM_DAVAIL));

    DELAY256NS;
    junk = sp->rdata;			/* eat the 2nd CRC byte */

    sp->wzeroes = ISM_ACTION;		/* turn off ACTION */

    if (shake & ISM_CRCERR) {		/* CRC error */
	return(EDSUM);
    }

    if (shake & ISM_ERROR) {		/* other error */
	return(fd_ism_error());		/* decode error reg */
    }


#ifdef LINT
    junk = junk;
#endif

    return(0);				/* OK */
}
/*----------------------------------*/
int
fd_ism_error()
{
    register struct swim *sp	= (struct swim *)iwm_addr;
    register u_char err;

    err = sp->rerror;		/* read & clear the error register */
    TRACE(3,("swim err reg = %x\n",err));

    if (err & ISM_OVERRUN) {	/* CPU reading/writing too fast */
	return(EOVERRUN);
    }
    if (err & ISM_UNDERRUN) {	/* CPU not reading/writing fast enough */
	return(EUNDERRUN);
    }

    return(EOTHER);			/* other error */
}

/*----------------------------------*/
int
mfm_sectortime()
{
    return(8);				/* 8 ms before next sector */
}

/*----------------------------------*/
#define WRT_NEXTNIBL(x)	{						\
			    do {					\
				DELAY256NS;				\
			    } while (!(sp->rhandshake & ISM_DAVAIL));	\
			    sp->wdata = (x);				\
			}

/* We're called at high spl to prevent interrupts. */
/*ARGSUSED*/				/* we ignore sector number */
int
mfm_write(buf,sector)
register u_char *buf;
{
    void delay_100us();

    /* We copy 0x00 from these cells to avoid clr.b instruction. */
    static u_char sync = 0x00;
    static u_char zero = 0x00;
    static u_char pad  = 0x4e;

    register struct swim *sp	= (struct swim *)iwm_addr;
    register struct via *vp	= (struct via *)VIA1_ADDR;
    register u_char *cp		= &sync;
    register u_char *mp		= &mfm_dmarks[0];
    register u_char *endp	= (u_char *)&buf[512];
    register u_char savedphase;
    register u_char junk;
    register u_char shake;
    register int i;

    /* Chip setup: */

    /* Select a "constant" input from the drive to avoid crosstalk
     * from the index pulse.
     */
    savedphase = sp->rphase;
    DELAY256NS;
    sp->wphase	= 0xf5;			/* S_1MBDRIVE is constant */
    DELAY256NS;

    junk        = sp->rerror;		/* clear error reg */
    DELAY256NS;
    sp->wzeroes = (ISM_WRITE | ISM_ACTION); /* turn off ACTION */
    DELAY256NS;
    sp->wones   = ISM_WRITE;		/* indicate WRITE */
    DELAY256NS;
    sp->wones   = ISM_CLRFIFO;		/* clr the fifo by toggling */
    DELAY256NS;
    sp->wzeroes = ISM_CLRFIFO;
    DELAY256NS;
    junk        = sp->rerror;		/* clear error reg again */

    /* Delay a bit before writing data marks, so we have roughly 22
     * nibbles after the end of the address field.
     * THIS SHOULD BE TIMED ON THE EMULATOR. WE ASSUME A VALUE HERE.
     */

    delay_100us(3);

    /* Write sync bytes. We prime the fifo before setting ACTION. */

    sp->wdata = *cp;			/* first one into fifo */
    DELAY256NS;
    sp->wdata = *cp;			/* 2nd one into fifo */

    DELAY256NS;
    sp->wones = ISM_ACTION;		/* now turn on ACTION */

    for (i = 0; i < (12 - 2); i++) {
	WRT_NEXTNIBL(*cp);		/* finish up the 12 syncs */
    }

    /* Write the data mark bytes. No need for timeout checking. */

    do {				/* fast loop */
	do {
	    DELAY256NS;
	} while (!(sp->rhandshake & ISM_DAVAIL));

	sp->wmark = *mp++;
    } while (*mp);

    /* Write the data. */

    do {				/* another fast loop */
	WRT_NEXTNIBL(*buf++);
    } while (buf < endp);

    /* Write the CRC and pad to ensure it's on the disk before we exit. */

    do {
	DELAY256NS;
    } while (!(sp->rhandshake & ISM_DAVAIL));
    sp->wcrc = zero;			/* force write of 2-byte CRC */

    /* Pad with 5 gap bytes. We save handshake reg for error testing. */

    cp = &pad;				/* for speed */
    for (i = 0; i < 4; i++) {
	WRT_NEXTNIBL(*cp);
    }

    do {
	DELAY256NS;
    } while(!((shake = sp->rhandshake) & ISM_DAVAIL));

    if (shake & ISM_ERROR) {
	TRACE(3,("after CRC write"));
	DELAY256NS;
	sp->wzeroes = (ISM_WRITE | ISM_ACTION); /* turn off WRITE and ACTION */
	DELAY256NS;
	sp->wphase = savedphase;
	return(fd_ism_error());
    }

    DELAY256NS;
    sp->wdata = *cp;

    DELAY256NS;
    sp->wzeroes = (ISM_WRITE | ISM_ACTION); /* turn off WRITE and ACTION */

    DELAY256NS;
    sp->wphase = savedphase;

    if (shake & ISM_ERROR) {		/* some kinda error */
	return(EWUNDERRUN);
    } else {
	return(0);			/* OK */
    }

#ifdef LINT
/*NOTREACHED*/
    return(junk);
#endif
}
