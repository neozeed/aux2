/*
 * @(#)tcsubs.c  {Apple version 1.3 89/11/30 11:15:16}
 *
 * Copyright (c) 1987, 1988, 1989 by Apple Computer, Inc.
 * All Rights Reserved.
 *
 * THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF APPLE COMPUTER, INC.
 * The copyright notice above does not evidence any actual or
 * intended publication of such source code.
 */

#if !defined(lint) && !defined(NO_SCCS_IDS)
static char _sccsid[]="@(#)tcsubs.c  {Apple version 1.3 89/11/30 11:15:16}";
#endif

#include "tc.h"

/*--------------------------------*/
int
tc_domode(s)
register struct softc *s;
{
    extern int tc_t_misc;

    register int ret;
    register struct mode *mp;
    register caddr_t savecmd;
    register caddr_t savebuf;

    /*
     * We issue a mode sense to get writeprotect status, and so we
     *  can do (an optional) ModeSelect to force blocksize.
     */

    savecmd = s->req.cmdbuf;		/* save current command & buffer */
    savebuf = s->req.databuf;
    s->req.cmdbuf = (caddr_t)&s->modecmd;
    s->req.databuf = (caddr_t)s->modebuf;
    s->req.datalen = TC_MODE_LEN;
    s->req.flags = SRQ_READ;
    s->req.timeout = tc_t_misc;

    scsig0cmd(&s->req,SOP_GETMODE,0,0,TC_MODE_LEN,0);

    s->errcnt = 0;
    s->buf.b_resid = 0;
    s->disc = 0;
    ret =  tc_start(s,&s->buf,WAIT);		/* mode sense */
    if (ret) {
	goto done;
    }

    mp = s->modebuf;
    s->wprot = (mp->byte_2 & M_WRTPROT) ? 1 : 0;

    if (s->modelinfo->flags & M_FORCESIZE) {
	mp->len = 0;
	mp->byte_2 = 0;
	mp->nblks = 0;
	mp->blklen = s->modelinfo->blksize;		/* Force blocksize */

	scsig0cmd(&s->req,SOP_SELMODE,0,0,TC_MODE_LEN,0);
	s->req.flags = 0;
	s->errcnt = 0;
	s->buf.b_resid = 0;
	s->disc = 0;
	ret = tc_start(s,&s->buf,WAIT);		/* do the mode select */
	if (ret) {
	    goto done;
	}

	/* We now do a gratuitous tc_check to eat the possible unit-attention
	 * that occurs if we changed mode-select params.
	 */

	(void)tc_check(s);			/* eat one unit-attention */
    }

done:;
    s->req.cmdbuf = savecmd;		/* restore current command & buf */
    s->req.databuf = savebuf;
    return(ret);
}

/*--------------------------------*/
int
tc_do_rw(s,flags)			/* internal read or write a block */
register struct softc *s;
long flags;
{
    register struct buf *bp;
    register int ret;

    if (s->blk < 0) {
	return(EIO);
    }

    bp = (struct buf *)&s->buf;
    bp->b_un.b_addr = s->databuf;	/* data buffer */
    s->b_addr = s->databuf;
    bp->b_resid = s->modelinfo->blksize;
    if (flags & B_READ) {		/* read or write */
	bp->b_flags = B_READ;
    } else {
	bp->b_flags = B_WRITE;
    }

    ret = tc_iosetup(s,bp,s->blk);
    if (ret) {
	return(ret);
    }
    s->errcnt = 0;
    return(tc_start(s,bp,WAIT));
}

/*--------------------------------*/
int
tc_iosetup(s,bp,blk)
register struct softc *s;
register struct buf *bp;
register int blk;
{
    extern int tc_t_rw;
    extern int tc_d_verbose;
    register struct modelinfo *mp;

    mp = s->modelinfo;

    if (blk < 0) {			/* then go sequentially */
	blk = s->blk++;
    }

    if (blk > s->maxblk) {
	return(ENXIO);
    }

    if (bp->b_flags & B_READ) {
	scsig0cmd(&s->req,SOP_READ, 0,blk,(mp->blksize / mp->physsize),0);
	s->req.flags = SRQ_READ;
    } else {
	scsig0cmd(&s->req,SOP_WRITE,0,blk,(mp->blksize / mp->physsize),0);
	s->req.flags = 0;
    }
    s->req.timeout = tc_t_rw;

    s->req.datalen = mp->blksize;
    s->req.databuf = s->b_addr;
    s->write = (bp->b_flags & B_READ) ? 0 : 1;	/* remember if a write */
    s->disc = 1;

    return(0);
}

/*--------------------------------*/
/*	g0build based on:	*/
/*****  scsig0cmd -- build group 0 Scsi command block. *********************
 *	Fills in the blanks of the request command bytes.  (See
 *	"scsiccs.h")  Sets request command-length to 6 bytes.  
 */
static void  g0build (req, op, nblks)
register struct scsireq *req;	/* request being assembled */
register int	op;		/* the SCSI op code */
register int	nblks;		/* The number of BUFFERSIZE byte blocks */
{
	struct scsig0cmd *cmd = (struct scsig0cmd *)req->cmdbuf;
	cmd->op = op;
	cmd->addrH = 1;
	cmd->addrM = (((int)nblks >> 16) & 0x1F);
	cmd->addrL = (int)nblks >> 8;
	cmd->len = (int)nblks;
	cmd->ctl = 0;
	req->cmdlen = 6;
}

/*--------------------------------*/
int
ar_iosetup(s,bp,blk)
register struct softc *s;
register struct buf *bp;
register int blk;
{
    extern int tc_t_rw;
    extern int tc_d_verbose;
    register struct modelinfo *mp;

    mp = s->modelinfo;

    if (blk < 0) {			/* then go sequentially */
	blk = s->blk++;
    }

    if (blk > s->maxblk) {
	return(ENXIO);
    }

    if (bp->b_flags & B_READ) {
	g0build(&s->req,SOP_READ,(mp->blksize / mp->physsize));
	s->req.flags = SRQ_READ;
    } else {
	g0build(&s->req,SOP_WRITE,(mp->blksize / mp->physsize));
	s->req.flags = 0;
    }
    s->req.timeout = tc_t_rw;

    s->req.datalen = mp->blksize;
    s->req.databuf = s->b_addr;
    s->write = (bp->b_flags & B_READ) ? 0 : 1;	/* remember if a write */

    return(0);
}

/*--------------------------------*/
int
tc_check(s)				/* issue a "test unit ready" command */
register struct softc *s;
{
    extern int tc_t_misc;

    scsig0cmd(&s->req,SOP_RDY,0,0,0,0);

    s->req.datalen  = 0;		/* no data to xfer */
    s->req.flags = 0;
    s->req.timeout = tc_t_misc;
    s->buf.b_resid = 0;

    s->errcnt = 0;
    s->disc = 0;
    return(tc_start(s,&s->buf,WAIT));
}

/*--------------------------------*/
int
tc_doinq(s)				/* issue an "inquiry" command */
register struct softc *s;
{
    extern int tc_t_misc;

    scsig0cmd(&s->req,SOP_INQ,0,0,INQ_LEN,0);
    s->req.databuf  = s->databuf;	/* inq data goes into databuf */
    s->req.datalen  = INQ_LEN;
    s->req.flags = SRQ_READ;
    s->req.timeout = tc_t_misc;
    s->buf.b_resid = 0;

    s->errcnt = 0;
    s->disc = 0;
    return(tc_start(s,&s->buf,WAIT));
}

int tc_blocks = 0;			/* patchable for testing limits */

/*--------------------------------*/
int
ar_readcap(s)
register struct softc *s;
{
    register struct mode *mp;
    register int megs;

    if (tc_blocks != 0) {		/* limit testing is quick */
	return(tc_blocks);
    }

    mp = s->modebuf;

    mp->density = 0x10;			/* XXX always returns zero, ugh XXX */

    switch (mp->density) {
	
	case 0x05:			/*  60mb */
		    megs =  60;
		    break;

	case 0x0f:			/* 125mb */
		    megs = 125;
		    break;

	case 0x10:			/* 150mb */
		    megs = 150;
		    break;

	default:			/* unknown */
		    megs = 0;
		    break;
    }

    return(((megs * 1048576) / s->modelinfo->blksize) - 1);

}
/*--------------------------------*/
int
tc_readcap(s)				/* issue a "read capacity" command */
register struct softc *s;
{
    extern int tc_t_misc;

    static char cap[] = { SOP_READCAP,0,0,0,0,0,0,0,0,0 };

    register caddr_t savecmd;
    register short *p;
    register int ret;

    if (tc_blocks != 0) {		/* limit testing is quick */
	return(tc_blocks);
    }

    savecmd = s->req.cmdbuf;
    s->req.cmdbuf = &cap[0];
    s->req.cmdlen = 10;			/* longer than normal command */

    s->req.databuf  = s->databuf;	/* data goes into databuf */
    s->req.datalen  = 8;
    s->req.flags = SRQ_READ;
    s->req.timeout = tc_t_misc;

    s->buf.b_resid = 0;

    s->errcnt = 0;

    s->disc = 0;
    ret = tc_start(s,&s->buf,WAIT);

    s->req.cmdbuf = savecmd;
    s->req.cmdlen = 6;			/* back to normal */

    if (ret == 0) {			/* ok */
	p = (short *)(s->databuf);
	return(*++p);
    } else {
	return(0);
    }

}
