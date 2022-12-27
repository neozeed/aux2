/*
 * @(#)tcioctl.c  {Apple version 1.4 90/01/19 14:59:44}
 *
 * Copyright (c) 1987, 1988, 1989 by Apple Computer, Inc.
 * All Rights Reserved.
 *
 * THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF APPLE COMPUTER, INC.
 * The copyright notice above does not evidence any actual or
 * intended publication of such source code.
 */

#if !defined(lint) && !defined(NO_SCCS_IDS)
static char _sccsid[]="@(#)tcioctl.c  {Apple version 1.4 90/01/19 14:59:44}";
#endif

#include "tc.h"
#include <sys/mtio.h>
#include <sys/file.h>
#include <sys/uio.h>
#include <sys/ioctl.h>
#include <sys/sysmacros.h>
#include <sys/param.h>

/*--------------------------------*/
/*ARGSUSED*/
int
tc_ioctl(dev,cmd,addr,mode)
dev_t dev;
int cmd;
caddr_t addr;
int mode;
{
    extern struct softc tc_softc[];
    extern int tc_t_misc;
    extern short tc_revnum;

    struct mtop *mtop;
    struct mtget *mtget;
    register struct softc *s;
    register struct modelinfo *mp;
    register int ret;

    s = &tc_softc[(minor(dev) & TC_MINOR)];

    if (s->open == 0) {			/* not opened */
	return(ENXIO);
    }
    if (s->hard) {			/* no I/O after hard error */
	return(EIO);
    }

    mp = s->modelinfo;

    switch (cmd) {
	
	case MTIOCTOP	:
		    mtop = (struct mtop *)addr;
		    switch (mtop->mt_op) {

			case MTWEOF:
			    if ((mode & FWRITE) == 0) {
				return(ENXIO);
			    } else {
				return(tc_weof(s,(int)(mtop->mt_count)));
			    }

			case MTFSF:
			    return((*mp->spacefile)(s,(int)(mtop->mt_count)));

			case MTBSF:
			    return((*mp->spacefile)(s,(int)(-mtop->mt_count)));

			case MTFSR:
			    ret = (*mp->spacerec)(s,(int)(mtop->mt_count));
			    return((ret < 0) ? EIO : ret);
			
			case MTBSR:
			    ret = (*mp->spacerec)(s,(int)(-mtop->mt_count));
			    return((ret < 0) ? EIO : ret);
			
			case MTREW:
			    return(tc_rew(s,1));
			    
			case MTOFFL:
			    return(tc_offl(s));

			case MTFORMAT:
			    if ((mode & FWRITE) == 0) {
				return(ENXIO);
			    } else {
				if (s->modelinfo->flags & M_DOFORMAT) {
				    return(tc_format(s));
				} else {
				    return(0);
				}
			    }
			
			case MTRETEN:
			    return(tc_reten(s));
			
			default:
			    return(ENXIO);
		    }

	case MTIOCGET	:
		    mtget = (struct mtget *)addr;
		    mtget->mt_erreg = tc_revnum;
		    mtget->mt_resid = (s->maxblk + 1) & 0xffff;
		    mtget->mt_type = s->modelinfo->mtiotype;
		    mtget->mt_fileno = (daddr_t)s->file;
		    mtget->mt_blkno = (daddr_t)s->blk;
		    return((*s->modelinfo->iocget)(s,mtget));
			
	default		:
		    return(ENXIO);
    }
}

/*--------------------------------*/
int
ar_iocget(s,mtget)
register struct softc *s;
register struct mtget *mtget;
{
    mtget->mt_dsreg = (s->maxblk + 1) >> 16;	/* high maxblk value */
    return(0);
}

/*--------------------------------*/
int
tc_iocget(s,mtget)
register struct softc *s;
register struct mtget *mtget;
{
    register int ret;

    /* Get "real" logical blk position via SENSE: */

    mtget->mt_dsreg = -1;	/* in case of error */
    scsig0cmd(&s->req,SOP_SENSE,0,0,s->modelinfo->senselen,0);

    s->req.datalen = s->modelinfo->senselen;
    s->req.databuf = (caddr_t)s->sensebuf;
    s->req.flags = SRQ_READ;
    s->req.timeout = tc_t_misc;
    s->errcnt = 0;
    s->buf.b_resid = 0;
    s->disc = 0;
    ret = tc_start(s,&s->buf,WAIT);
    if (ret == 0) {
	mtget->mt_dsreg = s->sensebuf->cur_blknum;
    }
    return(ret);
}

/*--------------------------------*/
int
tc_rew(s,wait)
register struct softc *s;
register int wait;
{

    scsig0cmd(&s->req,TCOP_REZERO,0,0,0,0);

    s->req.datalen = 0;
    s->req.flags = 0;
    s->req.timeout = s->modelinfo->rewtime;
    s->buf.b_resid = 0;

    s->write = 0;
    s->file = 1;
    s->blk = 0;
    s->errcnt = 0;
    s->disc = 1;
    return(tc_start(s,&s->buf,wait));
}

/*--------------------------------*/
int
tc_offl(s)
register struct softc *s;
{

    extern int tc_t_offl;

    register int ret;

    scsig0cmd(&s->req,TCOP_UNLOAD,0,0,0,0);

    s->req.datalen = 0;
    s->req.timeout = tc_t_offl;
    s->req.flags = 0;

    s->file = 1;
    s->blk = 0;
    s->write = 0;
    s->errcnt = 0;
    s->buf.b_resid = 0;
    s->disc = 1;
    ret = tc_start(s,&s->buf,WAIT);
    s->hard = 1;			/* don't allow any more I/O */
    return(ret);
}

/*--------------------------------*/
int
tc_format(s)
register struct softc *s;
{

    extern int tc_t_fmt;
    extern int tc_c_intlv;

    register int rc;

    /* Condition cartridge first, do retries, use tight threshold. */
    scsig0cmd(&s->req,SOP_FMT,0,(0x20 << 8),tc_c_intlv,0xc0);

    s->req.datalen = 0;
    s->req.timeout = tc_t_fmt;
    s->req.flags = 0;
    s->buf.b_resid = 0;

    s->file = 1;
    s->blk = 0;
    s->write = 0;
    s->errcnt = 0;

    s->disc = 1;
    rc = tc_start(s,&s->buf,WAIT);

    /* The drive appears unable to accept commands properly after doing
     * the format.  This has been observed, with a test program unable
     * to reopen the device for 12 seconds.  We wait 30 to be sure...
     */

    s->timeout = 30 * SECONDS;
    s->timertype = T_WAKEUP;
    (void)sleep((caddr_t)(&s->buf),PZERO);

    s->hard = 1;		/* must close & reopen to use it! */

    return(rc);
}

/*--------------------------------*/
int
tc_reten (s)
register struct softc *s;
{

    extern int tc_t_ret;
    int	ret;

    scsig0cmd(&s->req,0x1b,0,0,s->modelinfo->retenbits,0);
    s->req.databuf  = s->databuf;
    s->req.datalen  = 0;
    s->req.flags = SRQ_READ;
    s->req.timeout = tc_t_ret;
    s->buf.b_resid = 0;

    s->errcnt = 0;
    s->disc = 1;
    return(tc_start(s,&s->buf,WAIT));
}

