/*
 * @(#)tcopen.c  {Apple version 1.2 89/11/30 11:14:57}
 *
 * Copyright (c) 1987, 1988, 1989 by Apple Computer, Inc.
 * All Rights Reserved.
 *
 * THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF APPLE COMPUTER, INC.
 * The copyright notice above does not evidence any actual or
 * intended publication of such source code.
 */

#if !defined(lint) && !defined(NO_SCCS_IDS)
static char _sccsid[]="@(#)tcopen.c  {Apple version 1.2 89/11/30 11:14:57}";
#endif

#include "tc.h"
#include <sys/file.h>
#include <sys/param.h>
#include <sys/sysmacros.h>

/*--------------------------------*/
int
tc_open(dev,flags)
dev_t dev;
int flags;
{
    void timeout();
    void tc_init();
    char *memalloc();
    int tc_timer();

    extern int tc_timeron;
    extern int tc_t_timer;
    extern int tc_c_noeofok;
    extern struct softc tc_softc[];

    struct buf *bp;
    register int i;
    register int ret;
    int unformatted = 0;
    register struct softc *s;
    
    s = &tc_softc[(minor(dev) & TC_MINOR)];

    if (s->opening || s->open) {	/* only allow exclusive opens */
	return(EBUSY);
    }

    if ((!tc_c_noeofok) && (minor(dev) & TC_NOEOF)) {
	return(ENXIO);			/* only allow NOEOF dev if configged */
    }
    
    s->opening = 1;			/* open in progress */
    s->hard = 0;

    if (tc_timeron == 0) {
	timeout(tc_timer,(caddr_t)0,tc_t_timer);	/* kick it off */
	tc_timeron = 1;
    }

    /* Do per-device initialization if necessary: */

    if (s->initted == 0) {
	s->databuf = (caddr_t)memalloc((unsigned)8192);
	s->req.databuf = (caddr_t)s->databuf;
	s->sensebuf = (struct sense *)memalloc((unsigned)sizeof(struct sense));
	s->req.sensebuf = (caddr_t)s->sensebuf;
	s->modebuf = (struct mode *)memalloc((unsigned)sizeof(struct mode));

	bp = (struct buf *)&s->buf;
	bp->b_dev = dev;
	bp->b_un.b_addr = s->databuf;

	s->initted = 1;
    }

    /* If a rewind-on-close is still in progress, wait for it.
     *
     * We do it in this nonstandard way rather than the usual "while"
     * loop to show the SPL going on.  The SPL avoids interference from
     * tc_ret, which is called by the SCSI manager at high spl.  If we
     * didn't protect here, we'd have a race condition whereby we could
     * decide to sleep, and the rewind would complete between the
     * decision and the actual sleep system call.  We'd be dead forever.
     *
     * Oh, yeah.  Sleep() doesn't mind being called at high SPL; it will
     * simply return at low SPL.
     */
    for (;;) {
	SPL7();
	if (s->rewinding) {
	    i = sleep((caddr_t)(&s->buf),((PZERO + 1) | PCATCH));
	    if (i == 1) {			/* then signaled */
		s->opening = 0;
		return(EINTR);
	    }
	} else {
	    SPL0();
	    break;
	}
    }

    /* Get device info to make sure it's our device.  We know that
     *	the "inquiry" command returns vendor data, and it works even
     *	during autoload, so it's the one to do.
     *
     *	We don't use "test unit ready" since it will respond negatively
     *	during autoload.  The drive issues "unit attention" errors due
     *  to power-up, bus-reset, and media-changed.  BUT it doesn't do
     * this during "inquiry" cmds.
     *
     * An interesting observation: if we had to wait for rewind above,
     * we could probably bypass this inquiry stuff, since it *must* be
     * one of our tape drives....oh, well...
     */

    if (i = getmodel(s)) {		/* If an error, you lose */
	s->opening = 0;
	return(i);
    }

    /* We allow unformatted tapes to get thru if we're open for WRITE,
     * to allow the caller to issue the "Format" IOCTL.  Checking of
     * write status will occur later, when we have write-protect info.
     *
     * It is assumed that the device will give an error if anything
     * is attempted to the unformatted tape (except formatting!).
     */

#define EUNFORMATTED 999

    if (s->modelinfo->flags & M_WAITLOAD) {

	ret =  waitload(s);			/* wait for tape to autoload */
	switch (ret) {

	    case  0:			/* OK */
		    break;

	    case EUNFORMATTED:		/* unformatted tape */
		    unformatted = 1;

	    case -1:			/* media changed */
		    s->file = 1;
		    s->blk = 0;
		    break;

	    default:			/* some error */
		    s->opening = 0;
		    return(ret);
	}
    } else {
	s->file = 1;			/* always assume first file */
	s->blk = 0;
    }

    /* Setup the drive modes and get writeprotect status. This has to be
     * done after autoload completes, because, although we *could* do the
     * tc_domode() without any tape in the drive, it would always return
     * "protected" on an empty drive.
     */

    if (tc_domode(s)) {
	s->opening = 0;
	return(EIO);
    }

    if ((flags & FWRITE) && (s->wprot)) {	/* protected */
	s->opening = 0;
	return(EROFS);
    }

    /* If the tape isn't formatted and we're not opening for WRITE,
     * then there's no way he'll be able to format and use it.
     */
    if (((flags & FWRITE) == 0) && unformatted) {
	s->opening = 0;
	return(EINVAL);
    }
    
    s->maxblk = 0;			/* so unformatted shows in MTIOCGET */

    /* Get max block on this cartridge if it's formatted: */
    if (!unformatted) {
	if ((s->maxblk = (*s->modelinfo->readcap)(s)) == 0) {
	    s->opening = 0;
	    return(EIO);		/* unhappy */
	}
	s->maxblk -= 2;		/* reserve 2 blocks for EOF usage */
    }

    /* Setup positioning routines if they want: */
    if (tc_pos_open(s)) {
	s->opening = 0;
	return(EIO);			/* unhappy */
    }

    s->open = 1;			/* it's open */
    s->opening = 0;

    return(0);
}

/*--------------------------------*/
int
getmodel(s)
register struct softc *s;
{
    extern short tc_model_max;
    extern struct modelinfo tc_modelinfo[];
    register struct modelinfo *mp;
    register int i;

    for (i = 0; i <= tc_model_max; i++) {
	mp = s->modelinfo = &tc_modelinfo[i];

	/* A race condition can occur here if we do the inquiry while the
	 * drive is trying to logical-load a blank tape.  It will fail to
	 * select and we'll (wrongly) assume ENODEV.
	 */

	if (tc_doinq(s)) {
	    return(ENODEV);			/* couldn't select it */
	}

	if ((s->req.datasent != INQ_LEN) ||
	    (strncmp((s->databuf + INQ_MFG_OFF),mp->mfg_name,
		strlen(mp->mfg_name))) ||
	    (strncmp((s->databuf + INQ_PROD_OFF),mp->prod_name,
		strlen(mp->prod_name)))) {
	    continue;
	} else {
	    return(0);				/* found it */
	}
    }

    return(EBADDEV);
}

/*--------------------------------*/
/* Wait for autoload to complete.
 * Returns 0 if ok, +<code> if error, -1 if media change.
 * We impose an overall time limit on the whole autoloading process.
 * Note that when the drive goes busy (during Logical Load) we don't
 * count the retry-timeouts in the total "timeout".  Not a big problem
 * since it only goes busy for about 5 seconds anyway.
 */
static int
waitload(s)
register struct softc *s;
{
    extern int tc_t_autoload;		/* total timeout in seconds */
    extern int tc_t_loadpoll;		/* polling delay in tc_timer ticks */

    register long timeout;
    register int i;
    register int result = 0;

    timeout = tc_t_autoload;

    while (tc_check(s)) {		/* loop till it's ready! */
	
	if (s->req.stat != 0x08) {	/* drive not busy, we have sensecode */


	    /* Allow unformatted tapes. */
	    if (SENSEKEY(s) == S_UNFORMATTED) {
		result = EUNFORMATTED;
		goto out;
	    }

	    switch (SENSECODE(s)) {

		case LOADING:
		    if ((timeout -= tc_t_loadpoll) <= 0) {
			result = EIO;
			goto out;
		    } else {
			s->timeout = tc_t_loadpoll;
			s->timertype = T_WAKEUP;
			i = sleep((caddr_t)(&s->buf),((PZERO + 1) | PCATCH));
			if (i == 1) {	/* then signaled */
			    result = EINTR;
			    goto out;
			}
		    }
		    break;

		case CHANGED:
		    result = -1;	/* just remember it */
		    break;

		/* Assume load failure means "unformatted tape" */
		case LOADFAIL:
		    result = EUNFORMATTED;
		    goto out;

		default:			/* other error */
		    result = EIO;
		    goto out;
	    }
	}
    }

out:;

    return(result);
}

/*--------------------------------*/
#define	MALLOC	kmem_alloc
char *memalloc(size)
int size;
{
    char *MALLOC();
    register char *p;
    register char *cp;

    p = MALLOC(size);

    if (p) {
	for (cp = p; cp < (p + size); cp++) {	/* zero it out */
	    *cp = 0;
	}
	return(p);
    } else {
	return((char *)0);
    }
}
