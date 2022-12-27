/*
 * @(#)tcpos.c  {Apple version 1.3 90/02/12 13:23:53}
 *
 * Copyright (c) 1987, 1988, 1989 by Apple Computer, Inc.
 * All Rights Reserved.
 *
 * THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF APPLE COMPUTER, INC.
 * The copyright notice above does not evidence any actual or
 * intended publication of such source code.
 */

#if !defined(lint) && !defined(NO_SCCS_IDS)
static char _sccsid[]="@(#)tcpos.c  {Apple version 1.2 89/11/30 11:15:03}";
#endif

#include "tc.h"

/* This is all the code which depends on the tape formatting info
 * with regard to tapemarks.  It's all in one place so we can
 * change it if we want to...
 */

/* The tapemark is broken like this to prevent its contiguous appearance
 * in either source or object code.  The code handles it special.
 */

static char tmbuf1 = 'T';

static int tmbuflen;		/* causes nice breakup of text fields */

static char tmbuf2[] = "APEMARKtapemarkTAPEmarktapeMARKTapemarKtAPEMARk";

/*	The behavior of 9-track tape is emulated.
 *
 *	Specifically:
 *
 *	 1) File-positioning commands always leave you positioned on
 *	    the *other* side of the tapemark from where you started.
 *	 2) If you (would) hit physical BOT or EOT during a positioning
 *	    operation, you get EIO.
 *	 3) If an fsr or bsr operation would go past a tapemark, the
 *	    spacing operation stops after the tm and EIO is returned.
 *
 */

/*--------------------------------*/
/* We assume that databuf is available during a skip operation.
 * Skipping of records requires that we read each record and check for
 * tapemarks.  We have two routines to handle the differences in forward
 * and backward operations.
 */
int
tc_srec(s,count)			/* stop at count or tapemark */
register struct softc *s;
int count;				/* +- count */
{
    register int i;
    register int end;

    s->write = 0;

    if (count == 0) {			/* nothing to do */
	return(0);
    }

    end = s->blk + count;		/* where we should end up */

    if (count > 0) {			/* going forward */

	/* Just read records sequentially and stop at "end" or tapemark. */
	do {
	    if (tc_do_rw(s,B_READ)) {	/* read the record */
		return(EIO);
	    }
	    s->blk++;
	    if (tc_isfilemark(s)) {	/* check for tapemark */
		s->file++;
		return(-1);		/* give negative result for tm */
	    }
	} while (s->blk != end);

    } else {				/* going backward */
	do {
	    s->blk--;
	    if (s->blk < 0) {		/* hit BOT */
		s->file = 1;
		s->blk = 0;
		return(EIO);
	    }
	    if (tc_do_rw(s,B_READ)) {	/* read the record */
		return(EIO);
	    }
	    if (tc_isfilemark(s)) {	/* check for tapemark */
		s->file--;
		return(-1);		/* give negative result for tm */
	    }

	} while (s->blk != end);
    }

    return(0);
}

/*--------------------------------*/
/* We assume that databuf is available during a skip operation. */
int
tc_sfile(s,count)			/* space file */
register struct softc *s;
int count;				/* +- count */
{
    register int i;
    register int forever;
    register int ret;

    s->write = 0;

    if (count == 0) {			/* nothing to do */
	return(0);
    }

    if (count < 0) {
	forever = -999999;
	count = -count;			/* make positive for looping */
    } else {
	forever =  999999;
    }

    /* Just skip records till we hit a tapemark (ret < 0). */
    for (i = 0; i < count; i++) {
	ret = tc_srec(s,forever);
	if (ret > 0) {			/* read error */
	    return(EIO);
	}
    }
    return(0);
}

/*--------------------------------*/
int
tc_weof(s,count)
register struct softc *s;
register int count;
{
    register int i;

    s->databuf[0] = tmbuf1;			/* first byte */
    blt((char *)(&s->databuf[1]),tmbuf2,tmbuflen);

    for (i = 0; i < count; i++) {
	if (tc_do_rw(s,B_WRITE)) {		/* label is tapemark record */
	    return(EIO);
	}
	s->blk++;
	s->file++;
    }

    s->write = 0;				/* unset do_rw side effect */
    return(0);
}

/*--------------------------------*/
int					/* 0 if not, 1 if yes */
tc_isfilemark(s)
register struct softc *s;
{
    register char *buf;

    buf = s->b_addr;

    if ((buf[0] == tmbuf1) && (!strncmp(&buf[1],tmbuf2,tmbuflen))) {
	return(1);
    }
    return(0);				/* not a tapemark */
}

/* FOR ARCHIVE DRIVES THE FOLLOWING ROUTINES ARE USED: */
/*--------------------------------*/
static void  g0space (req, op, count)
register struct scsireq *req;	/* request being assembled */
register int op;		/* Space opcode:0=blocks, 1=FileMarks, 3=EOD */
register int count;		/* The number of EOF marks */
{
    struct scsig0cmd *cmd = (struct scsig0cmd *)req->cmdbuf;

    cmd->op = 0x11;  /* Space SCSI op code  */
    cmd->addrH = op;	
    cmd->addrM = (((int)count >> 16) & 0xFF);
    cmd->addrL = (int)count >> 8;
    cmd->len = (int)count;
    cmd->ctl = 0;
    req->cmdlen = 6;
}

/*--------------------------------*/
/*
 * Skipping of records is accomplished by using SCSI operations.
 */
int
ar_srec(s,count)			/* stop at count or tapemark */
register struct softc *s;
int count;				/* +- count */
{
    register struct modelinfo *mp;
    register int i;
    register int ret;

    mp = s->modelinfo;

    s->write = 0;

    if (count == 0) {			/* nothing to do */
	return(0);
    }

    g0space (&s->req,0,count * (mp->blksize / mp->physsize));
    s->req.databuf  = s->databuf;
    s->req.datalen  = 0;
    s->req.flags = SRQ_READ;
    s->req.timeout = mp->rewtime;
    s->buf.b_resid = 0;

    s->errcnt = 0;
    s->disc = 0;

    ret = tc_start(s,&s->buf,WAIT);

    /* This isn't exactly right, since we merely assume that any
     * error is a filemark...
     */
    if (ret) {				/* assume an error is a filemark */
	if (count > 0) {
	    s->file++;
	} else {
	    s->file--;
	}
    }
    return(ret);
}

/*--------------------------------*/
/* We assume that databuf is available during a skip operation. */
int
ar_sfile(s,count)			/* space file */
register struct softc *s;
int count;				/* +- count */
{
    register int i;
    register int forever;
    register int ret;
    extern int	tc_t_misc;

    s->write = 0;

    if (count == 0) {			/* nothing to do */
	return(0);
    }

    g0space (&s->req, 1, count);
    s->req.databuf  = s->databuf;
    s->req.datalen  = 0;
    s->req.flags = SRQ_READ;
    s->req.timeout = s->modelinfo->rewtime;
    s->buf.b_resid = 0;

    s->errcnt = 0;
    s->disc = 0;

    ret = tc_start(s,&s->buf,WAIT);

    if (ret == 0) {
	if (count > 0) {
	    s->file++;
	} else {
	    s->file--;
	}
    }
}

/*--------------------------------*/
static void  g0weof (req, op, count)
struct scsireq *req;	/* request being assembled */
int	op;		/* the SCSI op code */
int	count;		/* The number of EOF marks */
{
	struct scsig0cmd *cmd = (struct scsig0cmd *)req->cmdbuf;
	cmd->op = op;
	cmd->addrH = 0;
	cmd->addrM = (((int)count >> 16) & 0x1F);
	cmd->addrL = (int)count >> 8;
	cmd->len = (int)count;
	cmd->ctl = 0;
	req->cmdlen = 6;
}

/*--------------------------------*/
int
ar_weof(s,count)
register struct softc *s;
register int count;
{

    extern int tc_t_misc;
    int	ret;

    g0weof (&s->req,0x10, count);
    s->req.databuf  = s->databuf;	/* inq data goes into databuf */
    s->req.datalen  = 0;
    s->req.flags = SRQ_READ;
    s->req.timeout = tc_t_misc;
    s->buf.b_resid = 0;

    s->errcnt = 0;
    s->disc = 0;
    return(tc_start(s,&s->buf,WAIT));
}

/*--------------------------------*/
int
ar_isfilemark(s)
register struct softc *s;
{
    if (s->sensebuf->byte2 & 0x80) {
	return(1);
    } else {
	return(0);
    }
}

/*--------------------------------*/
int
ar_iseom(s)
register struct softc *s;
{
    if (s->sensebuf->byte2 & 0x40) {
	return(1);
    } else {
	return(0);
    }
}
/*--------------------------------*/
void
tc_pos_init()				/* main init */
{
    tmbuflen = strlen(tmbuf2);		/* for tc routines */
}

/*--------------------------------*/
/*ARGSUSED*/
int
tc_pos_open(s)				/* called after open complete */
register struct softc *s;
{
    return(0);
}

/*--------------------------------*/
/*ARGSUSED*/
int
tc_pos_close(s)				/* called after close complete */
register struct softc *s;
{
    return(0);
}
