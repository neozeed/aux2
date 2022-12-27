/*
 * @(#)tcstrat.c  {Apple version 1.5 90/03/22 09:25:14}
 *
 * Copyright (c) 1987, 1988, 1989 by Apple Computer, Inc.
 * All Rights Reserved.
 *
 * THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF APPLE COMPUTER, INC.
 * The copyright notice above does not evidence any actual or
 * intended publication of such source code.
 */

#if !defined(lint) && !defined(NO_SCCS_IDS)
static char _sccsid[]="@(#)tcstrat.c  {Apple version 1.2 89/11/30 11:15:09}";
#endif

#include "tc.h"
#include <sys/file.h>
#include <sys/uio.h>
#include <sys/param.h>
#include <sys/sysmacros.h>

    extern int tc_d_scsi;
    extern int tc_d_verbose;

/*--------------------------------*/
int
tc_start(s,bp,wait)			/* issue a SCSI command with WAIT */
register struct softc *s;
register struct buf *bp;
int wait;
{
    char *cmd2str();
    char *ret2str();

    register struct scsig0cmd *rp;
    register int ret;

    if (s->hard) {			/* then don't even TRY it */
	return(EIO);
    }

    s->req.driver = (long)bp;		/* remember buf in context */
    s->req.senselen = s->modelinfo->senselen;	/* sense length */

    bp->b_flags &= ~B_DONE;		/* really only need for internal bufs */

    if (tc_d_scsi) {
	rp = (struct scsig0cmd *)s->req.cmdbuf;
	printf("tc_start scsi op %s\n",cmd2str(rp->op));
    }

    if (s->disc) {			/* force disconnect for slow op */
	scsichar(s->id,SDC_DISC,SDC_DISC);
    } else {
	scsichar(s->id,0,SDC_DISC);
    }

    ret = scsireq(s->id,&s->req,(struct vio *)0);	/* fire it off */

    if (ret) {				/* failed */
	s->req.driver = 0;		/* nothing queued */
	if (tc_d_verbose) {
	    printf("tc_start:id=%d, q-result: %x\n",s->id,ret);
	}
    } else {
	if (wait) {
	    iowait(bp);			/* wait for completion */
	    ret = (bp->b_flags & B_ERROR) ? bp->b_error : 0;
	}
    }

    return(ret);
}

/*--------------------------------*/
void
tc_strategy(bp)
register struct buf *bp;
{
    extern int tc_d_strat;
    extern struct softc tc_softc[];

    register struct softc *s;
    register int ret;
    register int lastblk;

    s = &tc_softc[(minor(bp->b_dev) & TC_MINOR)];

    if (tc_d_strat) {
	printf("tc_strat: bp=%x\n",bp);
	if (tc_d_verbose) {
	    printf("bp->dev=%x,lblk=%x,pblk=%x,flags=%x\n",
			bp->b_dev,bp->b_blkno,
			    (bp->b_blkno / s->modelinfo->blksize),bp->b_flags);
	}
    }

    bp->b_resid = bp->b_bcount;
    s->b_addr = bp->b_un.b_addr;

    if (bp->b_bcount < s->modelinfo->blksize) {
	bp->b_flags |= (B_ERROR | B_DONE);
	bp->b_error = EIO;
	return;
    }
    s->errcnt = 0;

    /* Ignore blkno and use current physical position: */

    ret = (*s->modelinfo->iosetup)(s,bp,-1);
    lastblk = s->blk + (bp->b_bcount / s->modelinfo->blksize);

    if (ret) {

	if (bp->b_flags & B_READ) {		/* read gets EOF */
	    bp->b_flags &= ~B_ERROR;
	    bp->b_flags |= B_DONE;
	} else {				/* write gets ENXIO */
	    bp->b_flags |= (B_ERROR | B_DONE);
	    bp->b_error = ret;
	}

	if (tc_d_verbose) {
	    printf("tc_strat: ilgl blk=%d, maxblk=%d\n",s->blk,s->maxblk);
	}

	return;
    }

    /* On writes, if the entire request won't fit, we bust him. */

    if ((bp->b_flags & B_READ) == 0 && (lastblk > s->maxblk)) {
	bp->b_flags |= (B_ERROR | B_DONE);
	bp->b_error = ENXIO;
	return;
    }

    if (tc_start(s,bp,NOWAIT)) {	/* fire it off, no wait */
	bp->b_flags |= (B_ERROR | B_DONE);
	bp->b_error = EIO;		/* couldn't queue request?! */
	s->hard = 1;
	return;
    }
}

/*--------------------------------*/
#define	TCERR_OK	0		/* no error */
#define	TCERR_RETRY	1		/* retry */
#define	TCERR_EIO	2		/* unrecoverable error */
#define	TCERR_EOF	3		/* end of file */
#define	TCERR_HARD	4		/* hard error */
#define	TCERR_MORE	5		/* doing more of multi-record I/O */

/*--------------------------------*/
int					/* called when scsi_request done */
tc_ret(req)
register struct scsireq *req;
{
    extern struct softc tc_softc[];

    void iodone();

    long delay = 0;
    register struct softc *s;
    register struct buf *bp;
    register int action;

    bp = (struct buf *)req->driver;	/* recover buf address */

    s = &tc_softc[(minor(bp->b_dev) & TC_MINOR)];	/* recover softc */

    action = tc_err(s,bp,req,&delay);	/* get appropriate action to do */

    switch (action) {

	case TCERR_OK:					/* no error */
			    bp->b_flags &= ~B_ERROR;
			    bp->b_error = 0;
			    s->hard = 0;

			    /* Setup next I/O if more to do. */	
			    bp->b_resid -= s->modelinfo->blksize;
			    if (bp->b_resid > 0) {
				s->b_addr += s->modelinfo->blksize;
				if ((bp->b_error = 
				    (*s->modelinfo->iosetup)(s,bp,-1)) != 0) {
				    bp->b_flags |= B_ERROR;
				} else {
				    (void)tc_start(s,bp,NOWAIT);
				    action = TCERR_MORE;
				}
			    }
			    break;
	
	case TCERR_RETRY:				/* retry */
			    /* Either delay or retry immediately. */
			    if (delay) {		/* tc_timer'll do it */
				s->timeout = delay;
				s->timertype = T_RESTART;
			    } else {
				(void)tc_start(s,bp,NOWAIT);
			    }
			    break;
	
	case TCERR_EOF:	    
			    /* bp->resid says how much was left to go. */
			    bp->b_flags &= ~B_ERROR;
			    bp->b_error = 0;
			    s->hard = 0;

			    /* Only bump file number if a user read */
			    if (bp != &s->buf) {
				s->file++;
			    }

			    /* If we got the filemark DURING a multiblock
			     * read, we'll have to back up over the filemark
			     * so that the NEXT read will get zero bytes,
			     * and the caller will consider it a real EOF.
			     * (Because this read is simply a short-read)
			     */
			    if (!s->newread) {
				s->backovereof = 1;
			    }

			    break;

	case TCERR_HARD:
			    s->hard = 1;

	case TCERR_EIO:					/* unrecoverable */
			    bp->b_flags |= B_ERROR;
			    bp->b_error = EIO;
			    break;
	
	default:	    bp->b_flags |= B_ERROR;
			    bp->b_error = EIO;
			    s->hard = 1;	/* don't allow any more I/O */
			    printf("tc: internal logic error!\n");
			    break;
    }

    s->newread = 0;

    if ((action != TCERR_RETRY) && (action != TCERR_MORE)) {
	iodone(bp);			/* done, wakeup the process */
	s->rewinding = 0;		/* in case a nowait-rewind on close */
    }
    return(0);
}

/*--------------------------------*/
static int
tc_err(s,bp,req,delay)
register struct softc *s;
register struct buf *bp;
register struct scsireq *req;
register long *delay;
{
    extern int tc_c_maxtries;
    extern int tc_c_errs;
    extern int tc_c_retry;
    extern int tc_t_again;
    extern int tc_t_busy;
    extern int tc_t_timeout;

    register int ret;
    register int skey = 0;
    register int scode = 0;
    register int result = 0;

    /* Get status: */
    ret = req->ret;			/* SCSI mgr return code */

    switch (ret) {

	case 0 :				/* no error */
		    /* How'd it go? */
		    if (req->stat == 0x08) {	/* device is busy */
			*delay = tc_t_busy;	/* delay a bit */
			result = TCERR_RETRY;
		    } else {

			/* Check for filemarks if reading the standard dev. */
			if ((bp != &s->buf)		&& /* user I/O */
			    (bp->b_flags & B_READ)	&& /* it's a read */
			    !(bp->b_dev & TC_NOEOF)) {	   /* EOF as normal */
			    if (!(s->modelinfo->flags & M_FMERROR) &&
				(*s->modelinfo->isfilemark)(s)) {
				result = TCERR_EOF;
			    } else {
				result = TCERR_OK;
			    }
			}
		    }
		    break;
	
	case SST_STAT:				/* got sense */

		    if ((bp != &s->buf) &&
			(s->modelinfo->flags & M_FMERROR)) {
			if ((*s->modelinfo->isfilemark)(s)) {
			    if (s->cmd.op == SOP_READ) {
				result = TCERR_EOF;
			    } else {
				result = TCERR_EIO;
			    }
			    break;
			}
		    }

		    if (s->modelinfo->flags & M_EOMERROR) {
			if ((*s->modelinfo->iseom)(s)) {
			    result = TCERR_HARD;
			    break;
			}
		    }

		    skey = SENSEKEY(s);
		    scode = SENSECODE(s);
		    switch (skey) {

			case S_ILLEGAL:
			/* XXX Actually, S_ILLEGAL occurs at EOT also */
			/* Maybe we shouldn't be so severe about it. */
			case S_PROTECTED:
			case S_VENDOR:
			case S_ABORTED:
				result = TCERR_HARD;
				break;

			case S_NOTREADY:
				result = TCERR_EIO;
				break;

			case S_UNFORMATTED:
				if (s->cmd.op == SOP_READ) {
				    result = TCERR_EOF;
				} else {
				    result = TCERR_EIO;
				}
				break;
			
			case S_MEDERROR:
			case S_HWERROR:
				result = TCERR_RETRY;
				break;

			/* Note that we don't automatically do a
			 * new mode-select on attention.
			 */
			case S_ATTENTION:
				if (scode == CHANGED) {
				    result = TCERR_EIO;
				} else {
				    *delay = 0;		/* retry immediately */
				    result = TCERR_RETRY;
				}
				break;
			
		    }
		    break;
	
	case SST_MORE:				/* more data than exp. */
	case SST_LESS:				/* less data than exp. */
	case SST_SENSE:				/* error getting sense */
		    *delay = 0;			/* retry immediately */
		    result = TCERR_RETRY;
		    break;

	case SST_AGAIN:				/* xfer error, retry it */
		    *delay = tc_t_again;	/* delay a bit */
		    result = TCERR_RETRY;
		    break;
	
	case SST_TIMEOUT:			/* scsi reset occurred */
		    *delay = tc_t_timeout;	/* delay a bit */
		    result = TCERR_RETRY;
		    break;
	
	case SST_SEL:
		    result = TCERR_HARD;
		    break;

	case SST_CMD:
	case SST_COMP:
	case SST_PROT:
	case SST_MULT:
	default:				/* unknown error! */
		    printf("tc_err:unexpected scsi err %s\n",ret2str(ret));
		    result = TCERR_HARD;
		    break;
    }

    /* Don't retry forever. */
    if ((result == TCERR_RETRY) && (++s->errcnt > tc_c_maxtries)) {
	result = TCERR_EIO;
    }

    if ((tc_c_errs) && ((ret != 0) || (req->stat))) {
	printf(
    "tc_err: id=%x,ret=%x,stat=%s (%x),skey=%x,scode=%x,errcnt=%d,result=%x\n",
		s->id,ret,
		(req->stat == 8) ? "busy" : "",req->stat,
		skey,scode,s->errcnt,result);
    }

    if (tc_c_retry && (result == TCERR_RETRY)) {
	printf("tc retry, delay=%d.\n",*delay);
    }
    return(result);
}

/* Adapted from scsiccs.h for nice printing of messages. */

static char *
cmd2str(op)
register u_char op;
{

    struct g0t {
	u_char	g0op;
	char   *g0nam;
    };

    static struct g0t g0tab[] = {
	{0x00, "Test Unit Ready"},
	{0x01, "Rezero Unit"},
	{0x03, "Request Sense"},
	{0x04, "Format Unit"},
	{0x08, "Read"},
	{0x0A, "Write"},
	{0x0B, "Seek"},
	{0x12, "Inquiry"},
	{0x15, "Mode Select"},
	{0x1A, "Mode Sense"},
	{0x1B, "Unload"},
	/* g1 */
	{0x25, "Read Capacity"},
    };

	register struct g0t *gp;
	register int i;

	for( i = 0, gp = g0tab; gp->g0nam; i++, gp++)
		if (gp->g0op == op)
			break;
	return((gp->g0nam) ? gp->g0nam : "?op");
}

static char *
ret2str(stat)
register u_char stat;
{

    struct st {
	u_char	val;
	char   *name;
    };

    static struct st sttab[] = {
	{00, "OK"},
	{01, "Device dropped BUSY"},
	{02, "Err in CMD xmsn"},
	{03, "Err in status phase"},
	{04, "Err during SENSE"},
	{05, "Couldn't select device"},
	{06, "Timeout"},
	{07, "Multiple requests attempted"},
	{08, "SCSI Protocol problem"},
	{09, "More data than dev expected"},
	{10, "Less data than dev expected"},
	{11, "Error: Sense performed"},
	{12, "Data transfer aborted"},
	{13, "SCSI manager version mismatch"},
	{14, "Request canceled via scsicancel()"},
    };

	register struct st *sp;
	register int i;

	for( i = 0, sp = sttab; sp->name; i++, sp++)
		if (sp->val == stat)
			break;
	return((sp->name) ? sp->name : "?stat");
}
