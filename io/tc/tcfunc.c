/*
 * @(#)tcfunc.c  {Apple version 1.3 90/02/12 13:23:49}
 *
 * Copyright (c) 1987, 1988, 1989 by Apple Computer, Inc.
 * All Rights Reserved.
 *
 * THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF APPLE COMPUTER, INC.
 * The copyright notice above does not evidence any actual or
 * intended publication of such source code.
 */

#if !defined(lint) && !defined(NO_SCCS_IDS)
static char _sccsid[]="@(#)tcfunc.c  {Apple version 1.2 89/11/30 11:14:49}";
#endif

#include "tc.h"
#include <sys/file.h>
#include <sys/uio.h>
#include <sys/sysmacros.h>
/*--------------------------------*/
void
tc_close(dev,flag)
dev_t dev;
int flag;
{
    extern struct softc tc_softc[];
    extern int tc_d_verbose;

    register struct softc *s;
    register int ret;

    s = &tc_softc[(minor(dev) & TC_MINOR)];
    ret = 0;

    if (s->open == 0) {			/* not open */
	return;
    }
    if (s->hard) {			/* hard err, quick close */
	s->hard = 0;
	s->open = 0;
	s->file = 1;			/* force reposition to BOT */
	s->blk = 0;
	return;
    }

    /* If open for write and the last op was a write, we write two
     * tapemarks and position between the two.
     */
    if ((flag & FWRITE) && (s->write)) {
	if (s->modelinfo->flags & M_SINGLEEOF) {
	    s->maxblk += 2;			/* back to REAL maxblk value */
	    ret = (*s->modelinfo->weof)(s,1);	/* write one tapemark */
	} else {
	    s->maxblk += 2;			/* back to REAL maxblk value */
	    ret = (*s->modelinfo->weof)(s,2);	/* write two tapemarks */

	    if (ret == 0) {
		if (minor(dev) & TC_NOREW) {
		    ret = (*s->modelinfo->spacefile)(s,-1); /* back btwn 'em */
		}
	    }
	}
    }

    /* Call the positioning routines: */

    (void)tc_pos_close(s);

    /* If the rewinding device, rewind without waiting. */

    if ((minor(dev) & TC_NOREW) == 0) {
	(void)tc_rew(s,NOWAIT);		/* physically rewind it */
	s->rewinding = 1;		/* don't wait, but note it */
	s->file = 1;			/* reposition to BOT */
	s->blk = 0;
    }

    s->hard = 0;
    s->open = 0;
}

/*--------------------------------*/
int
tc_read(dev,uio)
dev_t dev;
struct uio *uio;
{
    void tc_strategy();
    extern int tc_d_verbose;
    extern struct softc tc_softc[];

    register struct softc *s;
    register int len;
    register int rc;

    s = &tc_softc[(minor(dev) & TC_MINOR)];

    if (s->open == 0) {			/* not opened */
	return(ENXIO);
    }
    if (s->hard) {			/* no I/O after hard error */
	return(EIO);
    }

    if (len = tc_badsize(s,uio)) {
	if (tc_d_verbose) {
	    printf("tc:read size = %d\n",len);
	}
	return(ENXIO);
    }

    /* We set the "newread" flag to properly simulate filemarks.
     * If a tapemark is encountered immediately upon beginning a
     * new read, we simply return a zero bytecount, with the tape
     * positioned past the tapemark.
     *
     * If a tapemark is encountered DURING a multiblock read,
     * returning a zero bytecount will simply give the user a short
     * read but he'll continue reading.  In this case, we want to
     * back up the tape so the tapemark is immediately encountered
     * on the NEXT read, thus causing a correct zero bytecount which
     * the user will treat as EOF.
     *
     * Note that on a REAL 9-track drive the interrecord gap would
     * terminate the short read and the tapemark will still be
     * "ahead" of the read head to be hit on the next read.
     */

    s->newread = 1;
    
    rc = physio(tc_strategy,(struct buf *)0,dev,B_READ,uio);

    /* If the read went successfully and we're told we have to
     * back up over a filemark, do it.  This operation has to be
     * done here because it waits, and we can't do that from the
     * bottom half of the driver...
     */
    if (rc == 0 && s->backovereof) {
	(void)(*s->modelinfo->spacefile)(s,-1);
	s->backovereof = 0;
    }

    return(rc);
}

/*--------------------------------*/
int
tc_write(dev,uio)
dev_t dev;
struct uio *uio;
{
    void tc_strategy();
    extern int tc_d_verbose;
    extern struct softc tc_softc[];

    register struct softc *s;
    register int len;

    s = &tc_softc[(minor(dev) & TC_MINOR)];

    if (s->open == 0) {			/* not opened */
	return(ENXIO);
    }
    if (s->hard) {			/* no I/O after hard error */
	return(EIO);
    }
    
    if (len = tc_badsize(s,uio)) {
	if (tc_d_verbose) {
	    printf("tc:write size = %d\n",len);
	}
	return(ENXIO);
    }
    
    return(physio(tc_strategy,(struct buf *)0,dev,B_WRITE,uio));
}

/*--------------------------------*/
static int				/* enforce I/O size to blksize mult */
tc_badsize(s,uio)
register struct softc *s;
register struct uio *uio;
{
    register struct iovec *iov;
    register int i;
    register int len = 0;

    iov = uio->uio_iov;

    for (i = 0; i < uio->uio_iovcnt; i++) {
	len += iov->iov_len;
	iov++;
    }

    if (len % s->modelinfo->blksize) {
	return(len);
    } else {
	return(0);
    }
}

/*--------------------------------*/
/*ARGSUSED*/
void
tc_print(dev,str)
dev_t dev;
char *str;
{
    /* XXX */
}
