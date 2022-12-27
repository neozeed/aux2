#ifndef lint	/* .../sys/COMMON/os/streamio.c */
#define _AC_NAME streamio_c
#define _AC_NO_MAIN "@(#) Copyright (c) 1987 Apple Computer, Inc., 1984-85 AT&T-IS, 1985-87 UniSoft Corporation, All Rights Reserved.  {Apple version 2.7 90/04/10 18:28:44}"
#include <apple_notice.h>

#ifdef _AC_HISTORY
  static char *sccsid = "@(#)Copyright Apple Computer 1989\tVersion 2.7 of streamio.c on 90/04/10 18:28:44";
#endif		/* _AC_HISTORY */
#endif		/* lint */

#define _AC_MODS
/*	@(#)streamio.c	UniPlus 2.1.19	*/

/*   Copyright (c) 1984 AT&T	and UniSoft Systems */
/*     All Rights Reserved  	*/

/*   THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T and UniSoft Systems */
/*   The copyright notice above does not evidence any   	*/
/*   actual or intended publication of such source code.	*/

#include "sys/types.h"
#include "sys/file.h"
#include "sys/sysmacros.h"
#include "sys/param.h"
#include "sys/dir.h"
#include "sys/buf.h"
#include "sys/errno.h"
#include "sys/signal.h"
#include "sys/mmu.h"
#include "sys/page.h"
#include "sys/time.h"
#include "sys/uio.h"
#include "sys/user.h"
#include "sys/region.h"
#include "sys/proc.h"
#include "sys/ioctl.h"
#include "sys/conf.h"
#include "sys/stropts.h"
#include "sys/stream.h"
#include "sys/vnode.h"
#include "sys/var.h"
#include "sys/termio.h"
#include "sys/debug.h"
#ifdef	HOWFAR
int T_streamhead = 0;
#endif	HOWFAR
#include "compat.h"

/*
 *	Added by UniSoft
 */

#ifndef	ASSERT
#define ASSERT(x)
#endif	ASSERT
#ifndef	min
#define min(x,y) (x < y ? x : y)
#endif	min
#define sigbit(a) (1<<(a-1))
static int str_unwait();
extern	int	lbolt;

/*
 */

int	stropen(), strclose();
int	strread(), strwrite();
int	strioctl(), queuerun();
int	strrput(), strwsrv(),  nulldev();
long	ioc_id;

extern char dblkflag;

/*
 *  Qinit structure and Module_info structures
 *        for stream head read and write queues
 */

struct 	module_info strm_info = { 0, "strrhead", 0, INFPSZ, STRHIGH, STRLOW, NULL };
struct  module_info stwm_info = { 0, "strwhead", 0, 0, 0, 0, NULL };
struct	qinit strdata = { strrput, NULL, NULL, NULL, NULL, &strm_info, NULL};
struct	qinit stwdata = { NULL, strwsrv, NULL, NULL, NULL, &stwm_info, NULL};

/*
 *	Global structures allocated on the fly
 */

dblk_t *dblock;
mblk_t *mblock;
queue_t *queue;
struct stdata *streams;
int nmblock;

/*
 *	System streams initialization routine
 */

strinit(ubase)
int *ubase;
{
	register long i, ndblock, bsize, size, base;
	register caddr_t addr, addr2;
	extern int physmem;

	base = *ubase;
	ndblock = v.v_nblk4096 + v.v_nblk2048 + v.v_nblk1024 + v.v_nblk512 +
	    v.v_nblk256 + v.v_nblk128 + v.v_nblk64 + v.v_nblk16 +
	    v.v_nblk4;
	nmblock = (ndblock * 5) >> 2;
	bsize =	v.v_nblk4096*4096 + v.v_nblk2048*2048 + v.v_nblk1024*1024 + 
		v.v_nblk512*512 + v.v_nblk256*256 + v.v_nblk128*128 + 
		v.v_nblk64*64 + v.v_nblk16*16 + v.v_nblk4*4;
	size = nmblock*sizeof(mblk_t) +
		ndblock*sizeof(dblk_t) +
		v.v_nqueue*sizeof(queue_t) +
		v.v_nstream*sizeof(struct stdata) +
		bsize;

	/*
	 *	add extra 4 byte blocks to use up any extra space
	 */

	i = ptob(btop(size)) - size;
	i /= sizeof(dblk_t) + sizeof(mblk_t) + 4;
	v.v_nblk4 += i;
	nmblock += i;
	ndblock += i;
	bsize += 4*i;
	size += i * (sizeof(dblk_t) + sizeof(mblk_t) + 4);
	
	/*
	 *	Allocate the space
	 */
 
	addr2 = addr = (caddr_t)ptob(base);
	base += btop(size);
	if (base >= physmem) {
		printf("Streams space not available");
		dblkflag++;
		return;
	}
	*ubase = base;

	/*
	 *	parcel it out to the various data structures (the first, page
	 *		alligned, bits become buffers)
	 */	

	addr += bsize;
	mblock = (mblk_t *)addr;
	addr += nmblock*sizeof(mblk_t);
	dblock = (dblk_t *)addr;
	addr += ndblock*sizeof(dblk_t);
	queue = (queue_t *)addr;
	addr += v.v_nqueue*sizeof(queue_t);
	streams = (struct stdata *)addr;
	qinit(addr2);
	stream_close = strclose;
	stream_read = strread;
	stream_write = strwrite;
	stream_ioctl = strioctl;
	stream_run = queuerun;
	stream_open = stropen;
}

/*
 * open a stream device
 */

stropen(dev, flag, vp)
register struct vnode *vp;
dev_t *dev;
{
	register queue_t *qp;
	register struct stdata *stp;
	register struct file *fp;
	register nok;
	register enum vtype fmt;
	int err;

retry:
	if ((stp = vp->v_sptr) == (struct stdata *) NULL) {

		/*
		 * This vnode isn't streaming, but another vnode
		 * may refer to same device, so look for it in file
		 * table to avoid building 2 streams to 1 device.
		 */

		fmt = vp->v_type;
		for (fp = file; fp < (struct file *)v.ve_file; fp++) {
			register struct vnode *tvp;

			if (fp->f_type == DTYPE_VNODE && fp->f_count) {
				tvp = (struct vnode *)fp->f_data;
				if (tvp->v_type == fmt && 
				     tvp->v_rdev == *dev && tvp != vp) {

/*
 *	Added by UniSoft
 */
					if ((stp = tvp->v_sptr)->sd_flag&STWOPEN) {
						stp->sd_flag |= STSOPEN;
						if(sleep((caddr_t) stp,
							STOPRI|PCATCH)) {
							return(EINTR);
						}
						goto retry;
					}

/*
 */
					vp->v_sptr = stp;
					break;
				}
			}
		}
	}

	if (stp != (struct stdata *) NULL) {	/* already streaming? */

		/*
		 * Waiting for stream to be created to device
		 * due to another open
		 */

		if (stp->sd_flag&STWOPEN) {
			stp->sd_flag |= STSOPEN;
			if (sleep((caddr_t)stp, STOPRI|PCATCH)) {
				return(EINTR);
			}
			goto retry;
		}

		if (stp->sd_flag&(STRHUP|STRERR)) {
			return(EIO);
		}

		/*
		 * Used to find controlling tty
		 */

#ifdef POSIX
		if (flag & FNOCTTY)
			;
		else if (!u.u_procp->p_ttyp)
#else /* !POSIX */
		if (!u.u_procp->p_ttyp)
#endif /* POSIX */
			stp->sd_flag |= CTTYFLG;

		/*
		 * Open all modules and devices down stream to notify
		 * that another user is streaming.
		 * For drivers pass down minor device number.
		 * For modules pass down -1, indicating to the open
		 * procedure that the open is for a module (some
		 * code may double as both a module and a driver)
		 */

		qp = stp->sd_wrq;
		while (qp = qp->q_next) {
			if ((*RD(qp)->q_qinfo->qi_qopen)(RD(qp),
					*dev, 
					qp->q_next? 0: flag,
					qp->q_next? MODOPEN: DEVOPEN,
					&err,
					NULL)
						== OPENFAIL) {
				stp->sd_flag &= ~CTTYFLG;
				if (!err)
					return(ENXIO);
				return(err);
			}
		}
#ifdef POSIX
		if (((flag & FGETCTTY) || u.u_procp->p_ttyp)
#else /* !POSIX */
		if (u.u_procp->p_ttyp
#endif /* POSIX */
			&& (stp->sd_flag&CTTYFLG)) {

			/*
			 * Unisoft signals:  There is one process group
			 * variable for the stream.  It's maintained in the
			 * stream head.  Things down stream use pointers to it.
			 * Things down stream may determine if controlling
		 	 * tty stuff should be set. 
	 		 */

			u.u_procp->p_ttyp = &(stp->sd_pgrp);
			stp->sd_pgrp = u.u_procp->p_pgrp;
			u.u_procp->p_flag |= SPGRPL;
		}
		stp->sd_flag &= ~CTTYFLG;

	} else {

		/* 
		 * Not already streaming so create a stream to driver.
		 */

		/* 
		 * Get queue pair for stream head
		 */

		if (!(qp = allocq())) {
			printf("stropen:out of queues\n");
			return(ENOSR);
		}

		/*
		 * The qstrlock protects the streams[] table.
		 */

		/*
		 * Get a free stream head structure.  A stream head
		 * structure is free iff its sd_wrq field is NULL.
		 */

		for (stp = streams; stp < &streams[v.v_nstream]; stp++)
			if (!(stp->sd_wrq))
			{
				stp->sd_wrq = WR(qp);
				break;
			}


		if (stp >= &streams[v.v_nstream]) {
			printf("stropen: out of streams\n");
			freeq(qp);
			return(ENOSR);
		}



		/* 
		 * Initialize stream head
		 */

		stp->sd_pgrp = 0;
		stp->sd_vnode = vp;
		stp->sd_flag = 0;
		stp->sd_error = 0;
		stp->sd_wroff = 0;
		stp->sd_iocwait = 0;

/*
 *	Added by UniSoft
 */

		stp->sd_wait = 0;
		stp->sd_wsel = 0;
		stp->sd_rsel = 0;

/*
 */
		stp->sd_iocblk = NULL;
		qp->q_ptr = (caddr_t)stp;
		qp->q_qinfo = &strdata;
		qp->q_hiwat = strm_info.mi_hiwat;
		qp->q_lowat = strm_info.mi_lowat;
		qp->q_minpsz = strm_info.mi_minpsz;
		qp->q_maxpsz = strm_info.mi_maxpsz;
		WR(qp)->q_ptr = (caddr_t)stp;
		WR(qp)->q_qinfo = &stwdata;
		WR(qp)->q_hiwat = stwm_info.mi_hiwat;
		WR(qp)->q_lowat = stwm_info.mi_lowat;
		WR(qp)->q_minpsz = stwm_info.mi_minpsz;
		WR(qp)->q_maxpsz = stwm_info.mi_maxpsz;
		stp->sd_flag |= STWOPEN;
		vp->v_sptr = stp;


		/*
		 * Used to find controlling tty
		 */

#ifdef POSIX
		if (flag & FNOCTTY)
			;
		else if (!u.u_procp->p_ttyp)
#else /* !POSIX */
		if (!u.u_procp->p_ttyp)
#endif /* POSIX */
			stp->sd_flag |= CTTYFLG;

		/*
		 * Open driver and create stream to it (via qattach). Device
		 * opens may sleep, but must set PCATCH if they do so that
		 * signals will not cause a longjump.  Failure to do this may
		 * result in the queues and stream head not being freed.
		 */

		nok = qattach(cdevsw[major(*dev)].d_str, qp, dev, flag);


		/*
		 * Wake up others that are waiting for stream to
		 * be created.
		 */

		stp->sd_flag &= ~STWOPEN;
		if (stp->sd_flag&STSOPEN) {
			wakeup((caddr_t)stp);
			stp->sd_flag &= ~STSOPEN;
		}
		if (nok) {
			stp->sd_flag |= STRHUP;
 			stp->sd_wrq = NULL; 		/* free stream */

			/* free queue pair */
			freeq(qp);

			/* NULL out stream pointer in vnode */
			vp->v_sptr = NULL;

			return(nok);
		}

		/*
		 * Assign process group if controlling tty
		 */
#ifdef POSIX
		if (((flag & FGETCTTY) || u.u_procp->p_ttyp)
#else /* !POSIX */
		if (u.u_procp->p_ttyp
#endif /* POSIX */
			&& (stp->sd_flag&CTTYFLG)) {
			/*
			 * Unisoft signals:  There is one process group variable			 * for the stream.  It's maintained in the stream head.
			 * Things down stream use pointers to it.  However,
			 * things down stream may determine if controlling tty
			 * should be set.  On first open p_ttyp is overloaded
			 * and carries back the location of the module's
			 * pointer.  We notice this and set things right.
	 		 */
			u.u_procp->p_ttyp = &(stp->sd_pgrp);
			stp->sd_pgrp = u.u_procp->p_pgrp;
			u.u_procp->p_flag |= SPGRPL;
		}
		stp->sd_flag &= ~CTTYFLG;
	}
	return(0);
}




/*
 * Shut down a stream
 *  -- pop all line disciplines
 *  -- shut down the driver
 */

strclose(vp,flag)
register struct vnode *vp;
{
	register struct stdata *stp;
	register queue_t *qp;
	register s;
	extern strtime();

	ASSERT(vp->v_sptr);
	if (vp->v_sptr == NULL)
		return;		/* can be null if previously attached to
				   console psuedo tty (see tty_pty.c)   */

	stp = vp->v_sptr;

/*
 *	Added by UniSoft
 */

	while (stp->sd_flag&STWOPEN) {		/* wait for pending opens/closes */
		stp->sd_flag |= STSOPEN;
		(void) sleep((caddr_t)stp, STOPRI|PCATCH);
	}
	stp->sd_flag |= STWOPEN;

/*
 */

	qp = stp->sd_wrq;

	/* 
	 * Pop all modules and close driver. (via qdetach)
	 * Wait STRTIMOUT seconds for write queue to empty.
	 * If not wake up and close via qdetach anyway. 
	 * qdetach is called with (1) as the 2nd arg to indicate
	 * that the device close should be called.
	 */

	while (qp->q_next) {
#ifdef POSIX
		if (!(flag&(FNDELAY|FNONBLOCK))) {
#else /* !POSIX */
		if (!(flag&FNDELAY)) {
#endif /* POSIX */
			s = splstr();
			stp->sd_flag |= (STRTIME | WSLEEP);
			timeout(strtime,stp,STRTIMOUT*v.v_hz);

			/*
			 * sleep until awakened by strwsrv() or strtime()
			 */

			while((stp->sd_flag &STRTIME) && qp->q_next->q_count) {

/*
 *	UniSoft 
 */

				qp->q_next->q_flag |= QWANTW;
				stp->sd_flag |= WSLEEP;
/*
 */
				if (sleep((caddr_t)qp, STIPRI|PCATCH))
					stp->sd_flag &= ~(STRTIME | WSLEEP);
			}
			untimeout(strtime, stp);
			stp->sd_flag &= ~(STRTIME | WSLEEP);
			splx(s);
		}

		/* qdetach pops off module, altering qp->q_next.
		 * The 2nd arg (1) indicates the detached module or
		 * driver was open and must be closed.
		 */

		qdetach(RD(qp->q_next), 1, flag);
	}
	flushq(qp, 1);
	flushq(RD(qp), 1);
	vp->v_sptr = NULL;
	if (stp->sd_flag&STSOPEN) {
		stp->sd_flag &= ~STSOPEN;
		wakeup((caddr_t)stp);
	}
	stp->sd_flag &= ~(STWOPEN | STR_TOSTOP);

#ifdef POSIX
	stp->sd_pgrp = 0;
	if (u.u_procp->p_ttyp == &stp->sd_pgrp)
	    u.u_procp->p_ttyp = NULL;
#endif /* POSIX */

	/* free stream head structure */
	stp->sd_wrq = NULL;

	/* free stream head queue pair */
	freeq(RD(qp));
}




/*
 * Read a stream according to the mode flags in sd_flag field of 
 * the stream head:
 *
 * (default mode)              - Byte stream, msg boundries are ignored
 * RMSGDIS (msg discard)       - Read on msg boundries and throw away 
 *                               any data remaining in msg
 * RMSGNODIS (msg non-discard) - Read on msg boundries and put back
 *		                 any remaining data on head of read queue
 *
 * If the number of bytes on queue is <= uio->uio_resid
 * and greater than 0, read data and return.
 * If no data on queue then block unless O_NDELAY is set.
 *
 * In default mode a 0 length message signifies an end-of-file
 * condition.  It is removed from the queue only if it is the only
 * message read, and it terminates any read that encounters it.
 * In the message modes, a 0 length message is handled like any
 * other message.
 */

strread(vp, uio)
register struct vnode *vp;
register struct uio *uio;
{
	register struct stdata *stp;
	register mblk_t *bp;
	register mblk_t *mp;
	register n;
	register err;
	register s;
	register rflg = 0;

/*
 *	Added by UniSoft
 */

	int count = uio->uio_resid;

/*
 */

	ASSERT(vp->v_sptr);
	if (vp->v_sptr == NULL)
		return (EIO);	/* can be null if previously attached to
				   console psuedo tty (see tty_pty.c)   */
	stp = vp->v_sptr;

	if (u.u_procp->p_flag & SPGRP42) {

		/*
		 * Hang process if it's in the background.
		 */

		while (stp->sd_pgrp && u.u_procp->p_ttyp == &stp->sd_pgrp &&
		    u.u_procp->p_pgrp != stp->sd_pgrp) {
			if ((u.u_procp->p_sigignore & sigbit(SIGTTIN)) ||
			   (P_SIGMASK(u.u_procp) & sigbit(SIGTTIN)) )
				return (EIO);
#ifdef POSIX
			if ((u.u_procp->p_compatflags & COMPAT_POSIXFUS)
				&& orphanage(u.u_procp))
				return (EIO);
#endif /* POSIX */
			if (stp->sd_flag&STRERR) {
				return(stp->sd_error);
			}
			signal(u.u_procp->p_pgrp, SIGTTIN);
			if(sleep((caddr_t)&lbolt, STIPRI|PCATCH))
				return(EINTR);
		}
	}

	/*
	 * If an error has been posted by a downstream
	 * module/driver, return the error.
	 */

	if (stp->sd_flag&STRERR) {
		return(stp->sd_error);
	}

	/* 
	 * If in tty_pty packet mode, return status as first byte of read.
  	 * If status is non-zero, return only the status.
 	 */

	if (stp->sd_flag&STR_PKTMOD) {
		err = uiomove(&stp->sd_pktstat, 1, UIO_READ, uio);
		if (stp->sd_pktstat || err) {
			stp->sd_pktstat = 0;
			return(err);
		}
	}

	/* this loop terminates on error or when uio->uio_resid == 0 */
	for (;;) {
		s = splstr();
		/* 
		 * get message at head of queue
		 */
		if (!(bp = getq(RD(stp->sd_wrq)))) {
			if (stp->sd_flag&STRERR) {
				err = stp->sd_error;
				splx(s);
				return(err);
			}

			/*
			 * if O_NDELAY and no data read, error
			 */

#ifdef POSIX
			if ((u.u_fmode&FNONBLOCK) && !rflg) {
				splx(s);
				return(EAGAIN);
			}
#endif /* POSIX */
			if ((u.u_fmode&FNDELAY) && !rflg) {
				splx(s);
				return(ENODATA);
			}

			/*
 			 * The same but for Berkeley style NBIO (UniSoft)
			 */

			if (stp->sd_flag&STR_NBIO) {
				if (!rflg) err = EWOULDBLOCK; else err = 0;
				splx(s);
				return(err);
			}

			/*
			 * if already read data or a HANGUP has 
			 * occurred return number of bytes read.
			 */

			if (stp->sd_flag&STRHUP) {
				splx(s);
				return(0);
			}


/*
 *	Added by UniSoft
 */

			/*
			 * are we doing timeout for system V stuff?
			 */

			if (stp->sd_flag&TIME_OUT) {
				if (rflg && stp->sd_min <= count - uio->uio_resid) {
					splx(s);
					return(0);
				}
				if (stp->sd_wait &&
					!(stp->sd_flag&TIME_WAIT) &&
					(rflg || stp->sd_min == 0)) {
					stp->sd_flag &= ~TIMED_OUT;
					stp->sd_flag |= TIME_WAIT;
					timeout(str_unwait, stp, 
						stp->sd_wait*v.v_hz/10);
				}
			} else

/*
 */
			/*
			 * if already read data
			 * return number of bytes read.
			 */

			if (rflg) {
				splx(s);
				return(0);
			}


			/*
			 * no data at head of queue - block
			 */

			stp->sd_flag |= RSLEEP;
			if (sleep((caddr_t)RD(stp->sd_wrq), STIPRI|PCATCH)) {

/*
 *	Modified by UniSoft
 */

				if (stp->sd_flag&TIME_WAIT)
					untimeout(str_unwait, stp);
				stp->sd_flag &= ~(RSLEEP|TIME_WAIT|TIMED_OUT);
				wakeup((caddr_t)RD(stp->sd_wrq));
/*
 */
				splx(s);
				if(stp->sd_flag & STRHUP)
					return(0);
				return(EINTR);
			}

/*
 *	Added by UniSoft
 */

			if ((stp->sd_flag&(RSLEEP|TIMED_OUT|STRHUP|STRERR)) ==
					(RSLEEP|TIMED_OUT)) {
				stp->sd_flag &= ~(RSLEEP|TIMED_OUT);
				splx(s);
				return(0);
			} else
			if (stp->sd_wait&TIME_WAIT) {
				stp->sd_flag &= ~TIME_WAIT;
				untimeout(str_unwait, stp);
			}
				
/*
 */
			splx(s);
			continue;
		}
		splx(s);

		/*
		 * if taking message off queue has caused a backenable,
		 * run the queues now.
		 */

		if (qready()) runqueues();

		switch (bp->b_datap->db_type) {

		case M_DATA:
			if ((bp->b_wptr - bp->b_rptr) == 0) {

				/*
				 * if already read data put zero
				 * length message back on queue else
				 * free msg and return 0.
				 */

				if (rflg) 
					putbq(RD(stp->sd_wrq),bp);
				else
					freemsg(bp);
				return(0);
			}

			/*
			 * read msg (traverse down message blocks)
			 */

			rflg++;
			while (bp) {

				if (n = min(uio->uio_resid, bp->b_wptr - bp->b_rptr)) {
					err = uiomove(bp->b_rptr, n, UIO_READ, uio);
					if (err) {
						freemsg(bp);
						return(err);
					}
				}

				bp->b_rptr += n;
				while (bp && (bp->b_rptr >= bp->b_wptr)) {
					mp = unlinkb(bp);
					freeb(bp);
					bp = mp;
				}
				if (uio->uio_resid == 0)
					break;
			}


			if (bp) {

				/* 
				 * Have reamining data in message.
				 * 
				 * If msg mode do appropriate thing
				 */

				if (stp->sd_flag & RMSGDIS)
					freemsg(bp);
				else
					putbq(RD(stp->sd_wrq),bp);
			}

			if ((uio->uio_resid == 0) || (stp->sd_flag&(RMSGDIS|RMSGNODIS))) {
				return(0);
			}
			
			continue;

		default:

			/*
			 * Garbage on stream head read queue
			 */

			ASSERT(0);
			freemsg(bp);
			break;
		}
	}
}

/*
 *	Stream timeouts (UniSoft)
 */

static
str_unwait(stp)
struct stdata *stp;
{
	if (stp->sd_flag&TIME_WAIT) {
		stp->sd_flag |= TIMED_OUT;
		stp->sd_flag &= ~TIME_WAIT;
		wakeup(RD(stp->sd_wrq));
	}
}

/*
 * ioctl for streams
 */

/*ARGSUSED*/			/* lint: flag unused */
strioctl(vp, cmd, arg, flag, ucred)
register struct vnode *vp;
int arg;
int flag;
struct ucred *ucred;
{
	register struct stdata *stp;
	register mblk_t *bp;
	register s;
	register int i;
	int fmt;
	int err;
	char ch;
	register struct iocblk *iocbp;
	char *dataptr = NULL;
	extern str2time(), str3time();
	register struct strioctl *striocp;
	int copyinflg = 0;
	int copyoutflg = 0;
	int ttyioctl = 0;
	register int timeoutval = 0;
#ifdef POSIX
	register struct proc *q;
	int pgrp;
	int samegroup;
	int samectty;
#endif /* POSIX */

	ASSERT(vp->v_sptr);
	if (vp->v_sptr == NULL)
		return (EIO);	/* can be null if previously attached to
				   console psuedo tty (see tty_pty.c)   */
	stp = vp->v_sptr;
	err = 0;

	if (stp->sd_flag & (STRHUP|STRERR)) {
		return(stp->sd_error);
	}
	switch(cmd) {
	/* Unisoft: ioctls handled entirely in stream head */
	/* should allow SPGRP and GPGRP only if tty open for reading */
	case TIOCSPGRP:
		stp->sd_pgrp = *(int *)arg;
		break;

#ifdef POSIX
	/* POSIX form of TIOCSPGRP */
	case TC_PX_SETPGRP:
		pgrp = *(int *)arg;
		if (pgrp <= 0 || pgrp > MAXPID) {
		    err = EINVAL;
		    break;
		}
		if (!u.u_procp->p_ttyp || u.u_procp->p_ttyp != &stp->sd_pgrp) {
		    err = ENOTTY;
		    break;
		}

#ifndef POSIX123
		samegroup = 0;
		samectty = 0;
		for (q = &proc[1]; q < (struct proc *)v.ve_proc; ++q) {
		    if (q->p_pgrp == pgrp) {
			samegroup = 1;
			if (q->p_ttyp == u.u_procp->p_ttyp) {
			    samectty = 1;
			    break;
			}
		    }
		}
		if (samegroup && !samectty) {
		    err = EPERM;
		    break;
		}
#else
		for (q = &proc[1]; q < (struct proc *)v.ve_proc; ++q) {
		    if (q != u.u_procp && (pgrp == q->p_pid || pgrp == q->p_pgrp) &&
		    p->p_ttyp != q->p_ttyp)
			break;
		}
		if (q < v.ve_proc) {
		    err = EPERM;
		    break;
		}
#endif POSIX123
		stp->sd_pgrp = pgrp;
		break;
#endif /* POSIX */

	case TIOCGPGRP:
#ifdef POSIX
	case TC_PX_GETPGRP:	/* POSIX form of TIOCSPGRP */
#endif /* POSIX */
		*(int *)arg = stp->sd_pgrp;
		break;

	case TIOCGCOMPAT:
		*(int *)arg = (stp->sd_flag & STR_TOSTOP) ? TOSTOP : 0;
		break;

	case TIOCSCOMPAT:
		if(*(int *)arg & TOSTOP)
			stp->sd_flag |= STR_TOSTOP;
		else
			stp->sd_flag &= ~STR_TOSTOP;
		break;

	default:
		/*
		 * Change to get 'f' (for FIONREAD etc .... UniSoft)
		 */

		if (_IOCTYPE(cmd) == ('f' << 8) ){
			switch(cmd) {

			/*
	 		 * Return number of characters immediately available.
	 		 */

			case FIONREAD:
				{
					off_t nread;
					register mblk_t *m;

					nread = 0;
					s = splstr();
					m = RD(stp->sd_wrq)->q_first;
					while (m) {
						nread += msgdsize(m);
						m = m->b_next;
					}
					splx(s);
					*(int *)arg = nread;
					return(0);
				}

			case FIONBIO:
				{
					int	nbio;

					nbio = *(int *)arg;
					if (nbio)
						stp->sd_flag |= STR_NBIO;
					else
						stp->sd_flag &= ~STR_NBIO;
					return(0);
				}

			case FIOASYNC:
				{
					int	async;

					async = *(int *)arg;
					if (async)
						stp->sd_flag |= STR_ASYNC;
					else
						stp->sd_flag &= ~STR_ASYNC;
					return(0);
				}
			}
			return(EINVAL);
		}

		/*
		 * This default case will only work if the ioctl
		 * is a tty ioctl as specified in termio.h.
		 */

		if ( (_IOCTYPE(cmd) == TIOC) || (_IOCTYPE(cmd) == LDIOC)
			|| _IOCTYPE(cmd) == ('U' << 8) || _IOCTYPE(cmd) == ('t' << 8)) {
			register struct proc *pp;

			/*
			 * If the ioctl involves modification,
			 * insist on being able to write the device,
			 * and hang if in the background.
			 */

			switch (cmd) {

			case TCSETAW:
			case TCSETAF:
			case TCSETA:
			case TCSBRK:
			case TCXONC:
			case TCFLSH:
			case TIOCSLTC:
			case TIOCSPGRP:
#ifdef POSIX
			case TC_PX_SETPGRP:
#endif /* POSIX */
			case UIOCMODEM:
			case UIOCNOMODEM:
			case UIOCDTRFLOW:
			case UIOCEMODEM:
			case UIOCNOFLOW:
			case UIOCFLOW:
				pp = u.u_procp;
				while (stp->sd_pgrp &&
				   (pp->p_flag & SPGRP42) &&
				   pp->p_pgrp != stp->sd_pgrp &&
				   &stp->sd_pgrp == u.u_procp->p_ttyp &&
				   !(pp->p_sigignore & sigbit(SIGTTOU)) &&
				   !(P_SIGMASK(pp) & sigbit(SIGTTOU))) {
					signal(pp->p_pgrp, SIGTTOU);
					if(sleep((caddr_t)&lbolt, STOPRI|PCATCH))
						return(EINTR);
					if (stp->sd_flag & (STRHUP|STRERR)) {
						return(stp->sd_error);
					}
				}
				break;
			}
			if (!(bp = allocb(sizeof(struct iocblk),BPRI_HI))) {

				/*
				 * out of space - for now just return error
				 */

				err = EAGAIN;
				break;
			}

			/*
			 * Mark that ioctl is from  this group of
			 * tty ioctls (needed below)
			 */

			ttyioctl++;

			/* 
			 * Construct as much of ioctl block as possible
			 * 
			 * Copyflg determines if data to be copied in
			 * is from user land or kernel land.
			 * U_TO_K means that "arg" points to data in user land.
			 * K_TO_K means that "arg" itself must be copied into
			 * ioctl block which is already in kernel.
			 * Dataptr points to address of data that must be
			 * copied into ioctl block. ioc_count is set to
			 * the amount of data to copy.
			 */

			iocbp = (struct iocblk *)bp->b_wptr;
			iocbp->ioc_cmd = cmd;
			iocbp->ioc_count = 0;
			copyinflg = K_TO_K;
			switch (cmd) {
/* Unlisted termio ioctls are treated as having no arguments */

				case TCXONC:
				case TCSBRK:
				case TCFLSH:
					iocbp->ioc_count = sizeof(int);
					dataptr = (caddr_t)arg;
					break;

				case TCSETA:
				case TCSETAW:
				case TCSETAF:
					iocbp->ioc_count = sizeof(struct termio);
					dataptr = (caddr_t)arg;
					break;

				case TCGETA:
					copyoutflg = K_TO_K;
					dataptr = (caddr_t)arg;
					
					break;

				case TIOCSLTC:
					iocbp->ioc_count = sizeof(struct ltchars);
					dataptr = (caddr_t)arg;
					break;

				case TIOCGLTC:
					copyoutflg = K_TO_K;
					dataptr = (caddr_t)arg;
					break;

				case LDSETT:
					iocbp->ioc_count = sizeof(struct termcb);
				case LDGETT:
					dataptr = (caddr_t)arg;
					break;

				case TIOCGWINSZ:
					copyoutflg = K_TO_K;
					dataptr = (caddr_t)arg;
					break;

				case UIOCTTSTAT:
					iocbp->ioc_count = 3; /* ! */
					dataptr = (caddr_t)arg;
					copyoutflg = K_TO_K;
					break;
			}
			goto do_ioctl;
		}

		/*
		 * Not a legal ioctl
		 */

		err = EINVAL;
		break;

	
	case I_STR:
		/*
		 * This is the new ioctl that is needed to form a
		 * ioctl message that has to be shipped downstream.
		 */

		/*
		 * get a buffer
		 */

		if (!(bp = allocb(sizeof(struct iocblk),BPRI_HI))) {

			/*
			 * out of space - for now just return error
			 */

			err = EAGAIN;
			break;
		}

		/*
		 * copy in structure that describes ioctl arguments
		 */

		striocp = (struct strioctl *)arg;

		/*
		 * initialize as much as possible
		 */

		iocbp = (struct iocblk *)bp->b_wptr;
		iocbp->ioc_cmd = striocp->ic_cmd;
		iocbp->ioc_count = striocp->ic_len;
		dataptr = striocp->ic_dp;
		timeoutval = striocp->ic_timout;
		copyinflg = U_TO_K;
		copyoutflg = U_TO_K;


do_ioctl:

		/*
		 * Initialize
		 */

		iocbp->ioc_error = 0;
		iocbp->ioc_rval = 0;
		iocbp->ioc_uid = ucred->cr_uid;
		iocbp->ioc_gid = ucred->cr_gid;
		bp->b_datap->db_type = M_IOCTL;
		bp->b_wptr += sizeof(struct iocblk);


		/*
		 * If there is data to copy into ioctl block, do so
		 */
		if (iocbp->ioc_count) {
			if (err = putiocd(bp, dataptr, copyinflg))
				break;
		}

/*
 *	Stream timeout (ICANON) ioctls .... UniSoft
 */

		if (cmd == TCSETA || cmd == TCSETAF || cmd == TCSETAW) {
			struct termio *t;

			t = (struct termio *)bp->b_cont->b_rptr;
			if (t->c_lflag&ICANON) {
				stp->sd_flag &= ~TIME_OUT;
				stp->sd_flag = (stp->sd_flag & ~RMSGDIS) | RMSGNODIS;
				stp->sd_wait = 0;
			} else {
				stp->sd_flag |= TIME_OUT;
				stp->sd_flag &= ~(RMSGDIS | RMSGNODIS);
				stp->sd_wait = t->c_cc[VTIME];
				stp->sd_min = t->c_cc[VMIN];
			}
		}
/*
 */
		s = splstr();


		/*
		 * Block for up to STRTIMOUT sec if there is a outstanding
		 * ioctl for this stream already pending.
		 *
		 * This process will wakeup and return EINTR if the 
		 * outstanding ioctl times out, or STRTIMOUT sec have
		 * elapsed, or it another ioctler waiting for this
		 * stream times out.  It will be awakened and attempt
		 * to proceed only if the outstanding ioctl completes
		 * normally.
		 */

		timeout(str2time, stp, STRTIMOUT*v.v_hz);
		while (stp->sd_flag & IOCWAIT) {
			if (stp->sd_flag & (STRHUP|STRERR)) {
				splx(s);
				freemsg(bp);
				untimeout(str2time, stp);
				return(stp->sd_error);
			}
			stp->sd_iocwait++;
			stp->sd_flag |= STR2TIME;
			if (sleep((caddr_t)&stp->sd_iocwait,STIPRI|PCATCH) ||
			    !(stp->sd_flag & STR2TIME)) {
				err = (stp->sd_flag & STR2TIME ? EINTR : ETIME);
				if (!--stp->sd_iocwait) stp->sd_flag &= ~STR2TIME;
				splx(s);
				freemsg(bp);
				untimeout(str2time, stp);
				return(err);
			}
			if (!--stp->sd_iocwait) stp->sd_flag &= ~STR2TIME;
		}
		untimeout(str2time, stp);



		/*
		 * Have control of ioctl mechanism.
		 * Send down ioctl packet and wait for
		 * response
		 */

		stp->sd_flag |= IOCWAIT;
		stp->sd_iocblk = NULL;

		/* 
		 * assign sequence number
		 */

		iocbp->ioc_id = ++ioc_id;
		stp->sd_iocid = ioc_id;
		splx(s);

		(*stp->sd_wrq->q_next->q_qinfo->qi_putp)(stp->sd_wrq->q_next, bp);

		if (qready()) runqueues();


		/*
		 * Wait for acknowledgment
		 * Wait up to timeout value as specified by user.
		 * 0 -> STRTIMOUT sec, -1 means forever
		 * A 0 timeout value makes no sense (thats why 0 -> STRTIMOUT
		 * sec)
		 * Note: str2time governs the processes waiting to do an ioctl,
		 * str3time governs the process waiting for the acknowledgement
		 * to its outstanding ioctl.  If either function runs, it
		 * wakes up both sets of processes.
		 */

		s = splstr();

		if (timeoutval >= 0) {
		        if (timeoutval == 0)
			        i = STRTIMOUT * v.v_hz;
			else {
			        if ((i = (timeoutval * v.v_hz)) <= 0)
				        i = 0x7fffffff;
			}
			timeout(str3time, stp, i);
		}
		while (!(bp = stp->sd_iocblk)) {
			if (stp->sd_flag & (STRHUP|STRERR)) {
				stp->sd_flag &= ~IOCWAIT;
				err = stp->sd_error;
				if (timeoutval >= 0)
					untimeout(str3time, stp);
				wakeup((caddr_t)&stp->sd_iocwait);
				splx(s);
				return(err);
			}
			stp->sd_flag |= STR3TIME;
			if (sleep((caddr_t)stp,STIPRI|PCATCH) ||
			    !(stp->sd_flag & STR3TIME)) {
				err = (stp->sd_flag & STR3TIME ? EINTR : ETIME);
				stp->sd_flag &= ~(STR3TIME|IOCWAIT);
				if (timeoutval >= 0)
					untimeout(str3time, stp);
				wakeup((caddr_t)&stp->sd_iocwait);
				splx(s);
				return(err);
			}
			stp->sd_flag &= ~STR3TIME;
		}


		/*
		 * Have recieved acknowlegment
		 */

		if (timeoutval >= 0)
			untimeout(str3time, stp);
		stp->sd_iocblk = NULL;
		stp->sd_flag &= ~IOCWAIT;
		splx(s);
		iocbp = (struct iocblk *)bp->b_rptr;
		switch (bp->b_datap->db_type) {
		case M_IOCACK:
			/*
			 * Positive ack
			 */

			/*
			 * set error if indicated
			 */

			if (iocbp->ioc_error) {
				err = iocbp->ioc_error;
				break;
			}

			/*
			 * set return value
			 */

			u.u_rval1 = iocbp->ioc_rval;

			/*
			 * Copy out data if need to.  Downstream module
			 * or driver should have loaded data into continuation
			 * message blocks and set the data size in ioc_count.
			 */

			if (iocbp->ioc_count && copyoutflg) {
				if (err = getiocd(bp, dataptr, copyoutflg)) {
					wakeup((caddr_t)&stp->sd_iocwait);
					freemsg(bp);
					return(err);
				}
 			}

			/*
			 * If not an ack for the upward compatible tty
			 * ioctls (see default case above) then
			 * update user ioctl structure and copy it out
			 */

			if (!ttyioctl) {
				striocp->ic_len = iocbp->ioc_count;
			}
			break;

	
		case M_IOCNAK:
			/*
			 * Negative ack
			 *
			 * The only thing to do is set error as specified
			 * in neg ack packet
			 */

			err = (iocbp->ioc_error ? iocbp->ioc_error : EINVAL);
			break;
		default:
			printf("strioctl: illegal ioctl ack cell\n");
			break;
		}

		/*
		 * wake up any ioctler's waiting for control of mechanism
		 */

		wakeup((caddr_t)&stp->sd_iocwait);
		freemsg(bp);
		break;


	case I_NREAD:
		/*
		 * return number of bytes of data in first message
		 * in queue in "arg" and return the number of messages
		 * in queue in return value
		 */

		fmt = 0;
		bp = RD(stp->sd_wrq)->q_first;
		if (bp)
			fmt = msgdsize(bp);
		*(int *)arg = fmt;
		u.u_rval1 = qsize(RD(stp->sd_wrq));
		err = 0;
		break;

	case I_PUSH:

		/*
		 * Push a module
		 */


		/*
		 * needed to set controling tty if 
		 * this push establishes it
		 */

#ifdef POSIX
		if (flag & FNOCTTY)
			;
		else if (!u.u_procp->p_ttyp)
#else /* !POSIX */
		if (!u.u_procp->p_ttyp)
#endif /* POSIX */
			stp->sd_flag |= CTTYFLG;

/*
 *	Unisoft: do forwarder specific PUSH if possible
 */

		i = stp->sd_wrq->q_next->q_qinfo->qi_minfo->mi_idnum;
		if (i >= FORWARDERMIN && i <= FORWARDERMAX) {
			if ((*RD(stp->sd_wrq->q_next)->q_qinfo->qi_qopen)(
					RD(stp->sd_wrq->q_next),
				        stp->sd_vnode->v_rdev,
					(int)arg,
					FWDPUSH,
					&err,
					NULL) != OPENFAIL) {
#ifdef POSIX
				if (((flag & FGETCTTY) || u.u_procp->p_ttyp)
#else /* !POSIX */
				if (u.u_procp->p_ttyp
#endif /* POSIX */
					&& (stp->sd_flag&CTTYFLG)) {
					u.u_procp->p_ttyp = &(stp->sd_pgrp);
					stp->sd_pgrp = u.u_procp->p_pgrp;
					u.u_procp->p_flag |= SPGRPL;
				}
				stp->sd_flag &= ~CTTYFLG;
				break;
			} else
			if (err != EINVAL) 
				break;
		}

/*
 */

		/*
		 * find module in fmodsw
		 */

		if ((i = findmod((char *)arg)) < 0) {
			stp->sd_flag &= ~CTTYFLG;
			err = EINVAL;
			break;
		}


		/*
		 * push new module and call its open routine
		 * via qattach
		 */

		if (!(err = qattach(fmodsw[i].f_str, RD(stp->sd_wrq),
				&stp->sd_vnode->v_rdev, 0))) {
			/*
			 * if controlling tty established
			 * mark the process group
			 */

#ifdef POSIX
			if (((flag & FGETCTTY) || u.u_procp->p_ttyp)
#else /* !POSIX */
			if (u.u_procp->p_ttyp
#endif /* POSIX */
				&& (stp->sd_flag&CTTYFLG)) {
				u.u_procp->p_ttyp = &(stp->sd_pgrp);
				stp->sd_pgrp = u.u_procp->p_pgrp;
				u.u_procp->p_flag |= SPGRPL;
			}
		}
		stp->sd_flag &= ~CTTYFLG;
		break;


	case I_POP:
		/*
		 * Pop module ( if module exists )
		 */

/*
 *	Unisoft: do forwarder specific POP if possible
 */

		i = stp->sd_wrq->q_next->q_qinfo->qi_minfo->mi_idnum;
		if (i >= FORWARDERMIN && i <= FORWARDERMAX) {
			if ((*RD(stp->sd_wrq->q_next)->q_qinfo->qi_qopen)(
					RD(stp->sd_wrq->q_next),
				        stp->sd_vnode->v_rdev,
					0,
					FWDPOP,
					&err,
					NULL) != OPENFAIL)
				break;
		}

/*
 */

		if (stp->sd_wrq->q_next->q_next) {
			qdetach(RD(stp->sd_wrq->q_next), 1, 0);
			break;
		}
		err = EINVAL;
		break;


	case I_LOOK:
		/*
		 * Get name of first module downstream
		 * If no module (return error)
		 */
		i = stp->sd_wrq->q_next->q_qinfo->qi_minfo->mi_idnum;
		if (i >= FORWARDERMIN && i <= FORWARDERMAX) {
			(void) (*RD(stp->sd_wrq->q_next)->q_qinfo->qi_qopen)(
					RD(stp->sd_wrq->q_next),
				        stp->sd_vnode->v_rdev,
					(int)arg,
					FWDLOOK,
					&err,
					NULL);
			break;
		}
		for (i=0; i<fmodcnt; i++)
			if(fmodsw[i].f_str->st_wrinit==stp->sd_wrq->q_next->q_qinfo) {
				bcopy(fmodsw[i].f_name,(char *)arg,FMNAMESZ+1);
				return(0);
			}
		err = EINVAL;
		break;


	case I_FLUSH:
		/*
		 * send a flush message downstream
		 * flush message can indicate 
		 * FLUSHR - flush read queue
		 * FLUSHW - flush write queue
		 * FLUSHRW - flush read/write queue
		 */
		if ((*(int *)arg) & ~FLUSHRW) {
			return(EINVAL);
		}
		if (!putctl1(stp->sd_wrq->q_next, M_FLUSH, (*(int *)arg))){
			return(EAGAIN);
		}
		if (qready()) runqueues();
		break;

	case I_SRDOPT:
		/*
		 * Set read options
		 *
		 * RNORM - default stream mode
		 * RMSGN - message no discard
		 * RMSGD - message discard
		 */
		if ((*(int *)arg) != RNORM) {
			if ((*(int *)arg) != RMSGD && (*(int *)arg) != RMSGN) {
				err = EINVAL;
				break;
			}
			stp->sd_flag &= ~(RMSGDIS|RMSGNODIS);
			if ((*(int *)arg) == RMSGD)
				stp->sd_flag |= RMSGDIS;
			else
				stp->sd_flag |= RMSGNODIS;
		} else
			stp->sd_flag &= ~(RMSGDIS|RMSGNODIS);
		break;

	case I_GRDOPT:
		/*
		 * Get read option and return the value
		 * to spot pointed to by arg
		 */

		fmt = RNORM;
		if (stp->sd_flag&RMSGDIS)
			fmt = RMSGD;
		else
			if (stp->sd_flag&RMSGNODIS)
				fmt = RMSGN;
		*(int *)arg = fmt;
		break;

	case I_FIND:
		{
			queue_t *q;
			int j;
		
			i = findmod((char *)arg);
			s = splstr();
			if ( i >= 0 )
				u.u_rval1 = 0;
			for (q = stp->sd_wrq->q_next; q; q = q->q_next) {
				j = q->q_qinfo->qi_minfo->mi_idnum;
				if (j >= FORWARDERMIN && j <= FORWARDERMAX) {
					(void) (*RD(q)->q_qinfo->qi_qopen)(
							RD(q),
				        		stp->sd_vnode->v_rdev,
							(int)arg,
							FWDFIND,
							&err,
							NULL);
					if (err == 0) {
						i = 0;
						u.u_rval1 = 1;
					} else if (err == ENODEV) {
						i = 0;
						u.u_rval1 = 0;
						err = 0;
					} else if (err == EINVAL)
						err = 0;
					else
						i = 0;
					break;
				}
				if (i >= 0 && q->q_qinfo == fmodsw[i].f_str->st_wrinit) {
					u.u_rval1 = 1;
					break;
				}
			}
			splx(s);
			if (i < 0)
				err = EINVAL;
			break;
		}

	case I_MNAME:
		{
			queue_t *q;
			mblk_t *tmp;
			int j;
		
			if (err = copyin((caddr_t)(*(int *)arg), &ch, 1)) {
				break;
			}
			i = ch;
			if (i < 0) {
				err = EINVAL;
				break;
			}
			q = stp->sd_wrq;
			while (i >= 0) {
				if (q == NULL) {
					err = EINVAL;
					break;
				}
				j = q->q_qinfo->qi_minfo->mi_idnum;
				if (j >= FORWARDERMIN && j <= FORWARDERMAX) {
					/*
					 * We use a streams buffer to insure we have in-core 
					 * memory for the fwdicp_int interrupt service 
					 * routine to write to. (local storage only)
					 */
					if (!(tmp = allocb(FMNAMESZ+1, BPRI_HI))) {
						err = ENOBUFS;
						break;
					}
					*tmp->b_wptr = i;
					(void) (*RD(q)->q_qinfo->qi_qopen)(
							RD(q),
				        		stp->sd_vnode->v_rdev,
							(int) tmp->b_wptr,
							FWDMNAME,
							&err,
							NULL);
					if (!err) {
						err = copyout(tmp->b_wptr,
							      (caddr_t)(*(int *)arg),
							      FMNAMESZ+1);
					}
					freeb(tmp);
					break;
				}
				if (i == 0) {
					for (i = 0; i < fmodcnt; i++)
					if (fmodsw[i].f_str->st_wrinit ==
							q->q_qinfo) 
						break;
					if (i >= fmodcnt) {
						if (q->q_qinfo->qi_minfo &&
					    	        q->q_qinfo->qi_minfo->mi_idname) {
							err = copyout(q->q_qinfo->qi_minfo->mi_idname, (caddr_t)(*(int *)arg), FMNAMESZ+1);
						} else {
							ch = 0;
							err = copyout(&ch,
								(caddr_t)(*(int *)arg),1);
						}
					} else {
						err = copyout(fmodsw[i].f_name,
						      (caddr_t)(*(int *)arg), FMNAMESZ+1);
					}
					break;
				}
				i--;
				q = q->q_next;
			}
		}
		break;
	}
	return(err);
}

/*
 * Stream read put procedure.  Called from downstream driver/module
 * with messages for the stream head.  Data messages are placed on
 * the read queue, all others are processed directly.
 */

strrput(q, bp)
register queue_t *q;
register mblk_t *bp;
{
	register struct stdata *stp = (struct stdata *)q->q_ptr;
	register struct iocblk *iocbp;
	register struct stroptions *sop;

	switch (bp->b_datap->db_type) {

	case M_DATA:

#ifdef	OSDEBUG
		{
			register mblk_t *tmp;

			for (tmp = bp; tmp; tmp = tmp->b_cont)
				ASSERT(bp->b_rptr <= bp->b_wptr);
		}
#endif


		/* 
		 * reader sleeping? - wake it up
		 */
		if (stp->sd_flag & RSLEEP) {
			stp->sd_flag &= ~RSLEEP;
			wakeup((caddr_t)q);
		}

		/*
		 *	Select wakeup code (UniSoft)
		 */

		strwakeup(stp);

		/*
		 * Put message on read queue
		 */
		putq(q, bp);
		return;

	case M_ERROR:
		/* first byte of message contains error number */
		if (*bp->b_rptr != 0) {
			stp->sd_flag |= STRERR;
			stp->sd_error = *bp->b_rptr;
			wakeup((caddr_t)q);	/* the readers */
			wakeup((caddr_t)WR(q));	/* the writers */
			wakeup((caddr_t)stp);	/* the ioctllers */
			flushq(q, 0);

			/*
			 * Wake up selects (UniSoft)
			 */

			strwakeup(stp);
			/* 
			 * send down flush for all queues
			 */
			putctl1(stp->sd_wrq->q_next, M_FLUSH, FLUSHRW);
		}
		freemsg(bp);
		return;

	case M_HANGUP:
		freemsg(bp);
#ifdef POSIX
		stp->sd_error = EIO;
#else /* !POSIX */
		stp->sd_error = ENXIO;
#endif /* POSIX */
		stp->sd_flag |= STRHUP;

		/*
		 * send signal if controlling tty
		 */
		if (stp->sd_pgrp)
			signal(stp->sd_pgrp, SIGHUP);

		wakeup((caddr_t)q);	/* the readers */
		wakeup((caddr_t)WR(q));	/* the writers */
		wakeup((caddr_t)stp);	/* the ioctllers */
		return;

	case M_SIG:	/* Revived for Unisoft job control */
	case M_EXSIG:
	case M_PCSIG:
		/*
		 * send signal if controlling tty.  The signal to send
		 * is contained in the first byte of the message.
		 */
		if (stp->sd_pgrp)
			signal(stp->sd_pgrp, *bp->b_rptr);
		flushq(q, 0);
		freemsg(bp);
		return;

	case M_FLUSH:
		/*
		 * if flush is for read queue then flush it.  The first
		 * byte of the message contains the flush flags.
		 */
		if (*bp->b_rptr & FLUSHR) {
			flushq(q, 0);

			/*
			 * Wake up selects (UniSoft)
			 */

			strwakeup(stp);
		}
		if (*bp->b_rptr & FLUSHW) {
			*bp->b_rptr &= ~FLUSHR;
			qreply(q, bp);
			return;
		}
		freemsg(bp);
		return;

	/* these are device control messages and are ignored here (relic) */
	case M_BREAK:
	case M_CTL:
	case M_DELAY:
	case M_START:
	case M_STOP:
		freemsg(bp);
		return;

	case M_IOCACK:
	case M_IOCNAK:
		iocbp = (struct iocblk *)bp->b_rptr;
		/*
		 * if not waiting for ACK or NAK then just free msg
		 * if incorrect id sequence number then just free msg
		 * if already have ACK or NAK for user then just free msg
		 */
		if ((stp->sd_flag&IOCWAIT)==0 || stp->sd_iocblk || (stp->sd_iocid != iocbp->ioc_id)) {
			freemsg(bp);
			return;
		}

		/*
		 * assign ACK or NAK to user and wake up
		 */
		stp->sd_iocblk = bp;
		wakeup((caddr_t)stp);
		return;

	case M_IOCTL:
		/*
		 * This is a device control message which should not
		 * be seen here.  This is a relic of V8 streams.
		 */
		bp->b_datap->db_type = M_IOCNAK;
		qreply(q, bp);
		return;

	case M_SETOPTS:
		/*
		 * Set stream head options (read option, write offset,
		 * min/max packet size, and/or high/low water marks for
		 * the read side only)
		 */

		ASSERT((bp->b_wptr - bp->b_rptr) == sizeof(struct stroptions));
		sop = (struct stroptions *)bp->b_rptr;
		if (sop->so_flags & SO_READOPT) {
			switch(sop->so_readopt) {
			case RNORM:
				stp->sd_flag &= ~(RMSGDIS | RMSGNODIS);
				break;
			case RMSGD:
				stp->sd_flag = (stp->sd_flag & ~RMSGNODIS) | RMSGDIS;
				break;
			case RMSGN:
				stp->sd_flag = (stp->sd_flag & ~RMSGDIS) | RMSGNODIS;
				break;
			}
		}

		if (sop->so_flags & SO_WROFF) stp->sd_wroff = sop->so_wroff;
		if (sop->so_flags & SO_MINPSZ) q->q_minpsz = sop->so_minpsz;
		if (sop->so_flags & SO_MAXPSZ) q->q_maxpsz = sop->so_maxpsz;
		if (sop->so_flags & SO_HIWAT) q->q_hiwat = sop->so_hiwat;
		if (sop->so_flags & SO_LOWAT) q->q_lowat = sop->so_lowat;

		freemsg(bp);

		if ((q->q_count <= q->q_lowat) && (q->q_flag & QWANTW)) {
			q->q_flag &= ~QWANTW;
			for (q = backq(q); q && !q->q_qinfo->qi_srvp; q = backq(q));
			if (q) qenable(q);
		}

		return;

	case M_PKT:
		stp->sd_flag |= STR_PKTMOD;
		stp->sd_pktstat = *bp->b_rptr;
		freemsg(bp);
		return;

	case M_PKTSTOP:
		stp->sd_flag &= ~STR_PKTMOD;
		stp->sd_pktstat = 0;
		freemsg(bp);
		return;


	default:
		ASSERT(0);
		freemsg(bp);
		return;
	}
}




/*
 * Write will break data up into SMSG byte messages if QBIG is not set 
 * downstream and strmsgsz (default 4096) byte messages if QBIG is set.
 *
 * Write will always attempt to get the largest buffer it can to satisfy the
 * message size. If it can not, then it will try up to 2 classes down to try
 * to satisfy the write. Write will not block if downstream queue is full and
 * O_NDELAY is set, otherwise it will block waiting for the queue to get room.
 * 
 * A write of zero bytes gets packaged into a zero length message and sent
 * downstream like any other message.
 *
 * If write can not get a buffer and some buffers have already been sent
 * thenwrite will return the number of bytes sent downstream, but
 * write fails to get a buffer and no messages have been sent downstream,
 * then write will return EAGAIN.
 *
 * Write (if specified) will supply a write offset in a message if it
 * makes sense. This can be specified by downstream modules by sending
 * up a M_WROFF message. Write will not supply the write offset if it
 * can not supply any data in a buffer. In other words, write will never
 * send down a empty packet due to a write offset.
 */

strwrite(vp, uio)
register struct vnode *vp;
register struct uio *uio;
{
	register struct stdata *stp;
	register mblk_t *bp;
	register mblk_t *mp;
	register class;
	register n;
	register err;
	register s;
	int offlg;
	int cnt = uio->uio_resid;
	register int maxmsgsz, size, msgsize;
	register short rmin, rmax;


	ASSERT(vp->v_sptr);
	if (vp->v_sptr == NULL)
		return (EIO);	/* can be null if previously attached to
				   console psuedo tty (see tty_pty.c)   */
	stp = vp->v_sptr;

	if (u.u_procp->p_flag & SPGRP42) {
		/*
		 * Hang the process if it's in the background.
		 */
		while (u.u_procp->p_ttyp == &stp->sd_pgrp &&
		    u.u_procp->p_pgrp != stp->sd_pgrp &&
		    (stp->sd_flag&STR_TOSTOP) &&
		    !(u.u_procp->p_sigignore & sigbit(SIGTTOU)) &&
		    !(P_SIGMASK(u.u_procp) & sigbit(SIGTTOU)) ) {
#ifdef POSIX
			if ((u.u_procp->p_compatflags & COMPAT_POSIXFUS)
				&& orphanage(u.u_procp))
				return (EIO);
#endif /* POSIX */
			signal(u.u_procp->p_pgrp, SIGTTOU);
			if(sleep((caddr_t)&lbolt, STOPRI|PCATCH))
				return(EINTR);
		}
	}

	rmin = stp->sd_wrq->q_next->q_minpsz;
	rmax = stp->sd_wrq->q_next->q_maxpsz;
	if ((uio->uio_resid < rmin) || ((uio->uio_resid > rmax) && (rmax != INFPSZ) && (rmin > 0))) {
		return(ERANGE);
	}

	/*
	 * get maximum allowed message size to next module
	 * Note: (rmax == INFPSZ) ==> infinite max
	 */
	if (rmax == INFPSZ)
		maxmsgsz = strmsgsz;
	else
		maxmsgsz = min(strmsgsz, rmax);


	/*
	 * do until satisfied or error
	 */
	do {
		s = splstr();
		while (((stp->sd_flag&(STRHUP|STRERR))==0) && !canput(stp->sd_wrq->q_next)) {
			/* 
			 * Do not block if downstream queue is full
			 * and O_NDELAY is set.  If nothing's been sent
			 * downstream, return an error.  Else, return what's
			 * been written so far.
			 */
#ifdef POSIX
			if (u.u_fmode&FNONBLOCK) {
				if(uio->uio_resid == cnt) {
					err = EAGAIN;
				} else {
					err = 0;
				}
				splx(s);
				return(err);
			}
#endif /* POSIX */
			if (u.u_fmode&FNDELAY) {
				if(uio->uio_resid == cnt) {
					err = EWOULDBLOCK;
				} else {
					err = 0;
				}
				splx(s);
				return(err);
			}

			/*
 			 * The same but for Berkeley style NBIO (UniSoft)
			 */

			if (stp->sd_flag&STR_NBIO) {
				if ((uio->uio_resid == cnt) &&
				    (u.u_procp->p_compatflags & COMPAT_BSDNBIO))
					err = EWOULDBLOCK;
				else
					err = 0;
				splx(s);
				return(err);
			}

			stp->sd_flag |= WSLEEP;

			/*
			 * Block until downstream queue can accept messages
			 */
			if (sleep((caddr_t)stp->sd_wrq, STOPRI|PCATCH)) {
				stp->sd_flag &= ~WSLEEP;
				splx(s);
				return(EINTR);
			}
		}
		splx(s);


		if (stp->sd_flag & (STRHUP|STRERR)) {
			return(stp->sd_error);
		}

		/*
		 * Determine the size of the next message to be
		 * packaged.  May have to break write into several
		 * messages based on min/max receive packet size or
		 * based on system limit (strmsgsz).  Careful - can't
		 * break messages into any fragment that will be less
		 * than the min receive packet size.
		 */
		if (uio->uio_resid <= maxmsgsz)
			msgsize = uio->uio_resid;
		else if (uio->uio_resid > maxmsgsz && (uio->uio_resid - maxmsgsz) < rmin)
			msgsize = uio->uio_resid - rmin;
		else
			msgsize = maxmsgsz;

		/*
		 * package the next message
		 */
		mp = NULL;
		offlg = 0;
		do {
			/*
			 * calculate size of buffer to ask for.
			 * If first block of message and write offset
			 * specified, then add it to size.
			 * If not then just take msgsize.
			 */
			if (stp->sd_wroff && !offlg)
				size = msgsize + stp->sd_wroff;
			else
				size = msgsize;

			bp = NULL;
			while (!bp) {
			    /*
			     * If size is less then QBSIZE bytes just ask for
			     * size buffer needed else do algorithm below.
			     * It will try up to 2 classes smaller looking for
			     * a buffer.
			     */
			    if (size <= QBSIZE)
				bp = allocb(size,BPRI_LO);
			    else {
				/*
				 * Try allocating as big a block as needed.
				 * If none try a few smaller.
				 */
				if ((class = getclass(size)) == NCLASS){
					class = NCLASS -1;
					size = rbsize[class];
				}
				if (!(bp = allocb(rbsize[class],BPRI_LO)))
				   if (!(bp = allocb(rbsize[--class],BPRI_LO)))
					bp = allocb(rbsize[--class],BPRI_LO);
			    }
			    if (!bp)
				/* 
				 * Wait for block to become available 
				 */
				if (sleep( &rbsize[getclass(size)], STOPRI|PCATCH)) {
					freemsg(mp);
					return(EINTR);
				}
			}

			/*
			 * Adjust buffer pointers for write offset if
			 * first block in message and write offset is
			 * specified and it does not make an empty block.
			 */
			if (stp->sd_wroff && !offlg++ &&
			   (stp->sd_wroff < bp->b_datap->db_lim - bp->b_wptr)){
				bp->b_rptr += stp->sd_wroff;
				bp->b_wptr += stp->sd_wroff;
			}

			bp->b_datap->db_type = M_DATA;
			if (n = min(bp->b_datap->db_lim - bp->b_wptr, msgsize)) {
				err = uiomove(bp->b_wptr, n, UIO_WRITE, uio);
				if (err) {
					freeb(bp);
					freemsg(mp);
					return(err);
				}
			}
			bp->b_wptr += n;
			if (!mp)
				mp = bp;
			else
				linkb(mp,bp);
			msgsize -=n;
		} while (msgsize > 0);
		/*
		 * Send block downstream.  If this causes a queue to be
		 * enabled, run the queues.
		 */
		(*stp->sd_wrq->q_next->q_qinfo->qi_putp)(stp->sd_wrq->q_next, mp);

		if (qready()) runqueues();

	} while (uio->uio_resid > 0);
	return(0);
}


/*
 * Stream head write service routine
 * Its job is to wake up any sleeping writers when a queue
 * downstream needs data.  (part of the flow control in putq and getq)
 */

strwsrv(q)
register queue_t *q;
{
	register struct stdata *stp = (struct stdata *)q->q_ptr;

	if (stp->sd_flag & WSLEEP) {
		stp->sd_flag &= ~WSLEEP;
		wakeup((caddr_t)q);
		wakeup(stp->sd_wrq);
	}

	/*
	 *	Select wakeups (UniSoft)
 	 */

	if (stp->sd_wsel) {
		selwakeup(stp->sd_wsel, (int) (stp->sd_flag & STR_WCOLL));
		stp->sd_wsel = 0;
		stp->sd_flag &= ~STR_WCOLL;
	}
}


/*
 * attach a stream device or line discipline
 *   qp is a read queue; the new queue goes in so its next
 *   read ptr is the argument, and the write queue corresponding
 *   to the argument points to this queue.  1 is returned if
 *   successful, 0 if not.
 */

qattach(qinfo, qp, dev, flag)
register struct streamtab *qinfo;
register queue_t *qp;
dev_t *dev;
{
	register queue_t *nq;
	register s;
	int err;
	extern putq();

	if (!(nq = allocq())) {
		printf("qattach: out of queues\n");
		return(ENOSR);
	}
	err = 0;
	s = splstr();
	nq->q_next = qp;
	WR(nq)->q_next = WR(qp)->q_next;
	if (WR(qp)->q_next)
		OTHERQ(WR(qp)->q_next)->q_next = nq;
	WR(qp)->q_next = WR(nq);
	nq->q_qinfo = qinfo->st_rdinit;
	nq->q_minpsz = nq->q_qinfo->qi_minfo->mi_minpsz;
	nq->q_maxpsz = nq->q_qinfo->qi_minfo->mi_maxpsz;
	nq->q_hiwat = nq->q_qinfo->qi_minfo->mi_hiwat;
	nq->q_lowat = nq->q_qinfo->qi_minfo->mi_lowat;
	nq->q_flag |= QREADR|QWANTR;
	
	WR(nq)->q_qinfo = qinfo->st_wrinit;
	WR(nq)->q_minpsz = WR(nq)->q_qinfo->qi_minfo->mi_minpsz;
	WR(nq)->q_maxpsz = WR(nq)->q_qinfo->qi_minfo->mi_maxpsz;
	WR(nq)->q_hiwat = WR(nq)->q_qinfo->qi_minfo->mi_hiwat;
	WR(nq)->q_lowat = WR(nq)->q_qinfo->qi_minfo->mi_lowat;
	WR(nq)->q_flag |= QWANTR;
	if ((*nq->q_qinfo->qi_qopen)(nq,
				     *dev,
				     flag,
				     WR(nq)->q_next?MODOPEN: DEVOPEN,
				     &err,
				     dev) == OPENFAIL){
		qdetach(nq, 0, 0);
		splx(s);
		if (err == 0)
			err = ENXIO;
		return(err);
	}
	splx(s);
	return(0);
}

/*
 * Detach a stream device or line discipline.
 * if clmode==1, then the device close routine should be
 * called.  If clmode==0, the detach is a result of a
 * failed open and so the device close routine must not
 * be called.
 */
qdetach(qp, clmode, flag)
register queue_t *qp;
{
	register s;
	register i;
	register queue_t *q, *prev = NULL;

	s = splstr();
	if (clmode) {
		if (qready()) runqueues();
		(*qp->q_qinfo->qi_qclose)(qp,flag);
		/*
		 * Remove the service functions from the run list.
		 * Note that qp points to the read queue.
		 */
		for (i=0; (qp->q_flag|WR(qp)->q_flag)&QENAB; i++) {
			runqueues();
			if (i>10) {
				for (q = qhead; q; q = q->q_link)  {
					if ((q == qp) || (q == WR(qp))) {
						if (prev)
							prev->q_link = q->q_link;
						else
							qhead = q->q_link;
						if (q == qtail)
							qtail = prev;
					}
					prev = q;
				}
				break;
			}
		}
		flushq(qp, 1);
		flushq(WR(qp), 1);
	}
	if (WR(qp)->q_next)
		backq(qp)->q_next = qp->q_next;
	if (qp->q_next)
		backq(WR(qp))->q_next = WR(qp)->q_next;
	freeq(qp);
	splx(s);
}


/*
 * This function is placed in the callout table to wake up a process
 * waiting to close a stream which has not completely drained.
 */
strtime(stp)
register struct stdata *stp;
{

	if (stp->sd_flag & STRTIME) {
		wakeup(stp->sd_wrq);
		stp->sd_flag &= ~STRTIME;
	}
}

/*
 * This function is placed in the callout table to wake up any
 * process that is waiting to perform an ioctl on the given stream.
 */
str2time(stp)
register struct stdata *stp;
{

	if (stp->sd_flag & STR2TIME) {
		wakeup(&stp->sd_iocwait);
		stp->sd_flag &= ~STR2TIME;
	}
}

/*
 * This function is placed in the callout table to wake up a process
 * which is waiting for an acknowledgement for an outstanding ioctl
 * on the given stream.  Note that processes that run str2time on the
 * stream will also be awakened.  
 */
str3time(stp)
register struct stdata *stp;
{

	if (stp->sd_flag & STR3TIME) {
		wakeup(stp);
		stp->sd_flag &= ~STR3TIME;
	}
}

/*
 *  put ioctl data from user land to ioctl buffers
 */
putiocd(bp, arg, copymode)
register mblk_t *bp;
register caddr_t arg;
register int copymode;
{
	register mblk_t *tmp, *last = bp;
	register int count, n;
	register struct iocblk *iocbp;

	iocbp = (struct iocblk *)bp->b_rptr;
	count = iocbp->ioc_count;

	ASSERT(count >= 0);

	while (count) {
		n = min(MAXIOCBSZ,count);
		if (!(tmp = allocb(n,BPRI_HI))) {
			freemsg(bp);
			return(EAGAIN);
		}
		switch (copymode) {
			case K_TO_K:
				bcopy((caddr_t)arg, tmp->b_wptr, n);
				break;

			case U_TO_K:
				if (!copyin((char *)arg, tmp->b_wptr, n))
					break;

			default:
				freeb(tmp);
				freemsg(bp);
				return(EFAULT);
		}
		arg += n;
		tmp->b_datap->db_type = M_DATA;
		tmp->b_wptr += n;
		count -= n;
		last->b_cont = tmp;
		last = tmp;
	}
	return(0);
}

/*
 * copy ioctl data to user land
 */
getiocd(bp, arg, copymode)
register mblk_t *bp;
register caddr_t arg;
register int copymode;
{
	register mblk_t *tmp;
	register int err, count,n;
	register struct iocblk *iocbp = (struct iocblk *)bp->b_rptr;
	

	count = iocbp->ioc_count;
	ASSERT(count >= 0);

	for(tmp = bp->b_cont; tmp && count; 
				count -= n, tmp = tmp->b_cont ,arg += n) {
		n = min(count, tmp->b_wptr - tmp->b_rptr);
		switch (copymode) {
		case K_TO_K:
			bcopy(tmp->b_rptr, (caddr_t)arg, n);
			break;

		case U_TO_K:
			if (err = copyout(tmp->b_rptr, arg, n)) {
				return(err);
			}
			break;
	
		default:
			return(EFAULT);
		}
	}
	ASSERT(count == 0);
	return(0);
}


/*
 *	strselect, strwakeup ... added for select (UniSoft)
 */

/* ARGSUSED */
strselect(dev, rw)
	dev_t dev;
	int rw;
{
	register int s = splstr();
	extern int	selwait;
	register struct stdata *stp;
	register struct file *fp;

	for (fp = file; fp < (struct file *)v.ve_file; fp++) {
		register struct vnode *tvp;

		if (fp->f_type == DTYPE_VNODE && fp->f_count) {
			tvp = (struct vnode *)fp->f_data;
			if (tvp->v_type == VCHR && 
			     tvp->v_rdev == dev && tvp->v_sptr) {
				stp = tvp->v_sptr;
				break;
			}
		}
	}
	ASSERT (stp != NULL);

	switch (rw) {

	case FREAD:
		if (RD(stp->sd_wrq)->q_first)
			goto win;
		if (stp->sd_rsel && stp->sd_rsel->p_wchan == (caddr_t)&selwait)
			stp->sd_flag |= STR_RCOLL;
		else
			stp->sd_rsel = u.u_procp;
		break;

	case FWRITE:
		if (canput(stp->sd_wrq->q_next))
			goto win;
		if (stp->sd_wsel && stp->sd_wsel->p_wchan == (caddr_t)&selwait)
			stp->sd_flag |= STR_WCOLL;
		else
			stp->sd_wsel = u.u_procp;
		break;
	}
	splx(s);
	return (0);
win:
	splx(s);
	return (1);
}

strwakeup(stp)
register struct stdata	*stp;
{
	if (stp->sd_rsel) {
		selwakeup(stp->sd_rsel, (int) (stp->sd_flag & STR_RCOLL));
		stp->sd_flag &= ~STR_RCOLL;
		stp->sd_rsel = 0;
	}
	if ((stp->sd_flag&STR_ASYNC) && stp->sd_pgrp)
		signal(stp->sd_pgrp, SIGIO); 
}
