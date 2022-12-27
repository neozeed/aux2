/*
 * @(#)scsidma.c  {Apple version 2.2 89/12/12 13:30:49}
 *
 * Copyright (c) 1987, 1988, 1989 by Apple Computer, Inc.
 * All Rights Reserved.
 *
 * THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF APPLE COMPUTER, INC.
 * The copyright notice above does not evidence any actual or
 * intended publication of such source code.
 */

#if !defined(lint)
static char _sccsid[]="@(#)scsidma.c  {Apple version 2.2 89/12/12 13:30:49}";
#endif

/*
 *         A/UX SCSI MANAGER
 *
 *    scsidma - scsi dma transfer
 *
 */

#ifdef lint
#include "sys/sysinclude.h"
#else lint
#include "sys/types.h"
#ifndef	STANDALONE
#include "sys/param.h"
#include "sys/uconfig.h"
#include "sys/var.h"

#include <sys/mmu.h>
#include <sys/signal.h>
#include <sys/time.h>
#include <sys/user.h>
#include "setjmp.h"
#endif	STANDALONE
#include "sys/debug.h"
#endif lint

#include <sys/vio.h>
#include <sys/ncr.h>
#include <sys/scsiccs.h>
#include <sys/scsireq.h>
#include <sys/scsifsm.h>
#include <sys/scsivar.h>
#include <sys/scsitask.h>
#include <sys/via6522.h>


/* 
 *  SCSI_VIO iterates over the iov array to move scsi data between
 *  the user buffer and scsi bus.  Each iov points to a contiguous
 *  user buffer with base and length, and has 8 option bits to control
 *  the dma transfer.  Up to  stp->datasent bytes will be moved.
 *  Returns 0 if length already transfered or -1 if pointer overrun,
 *  on entry, otherwise returns +datasent.
 */

/* Limits to determine speed shifts.  If a data transfer is >= a limit,
 * then we use that speed.
 * NB: It IS permissible to dynamically change the scsi_dmaon[] flags anytime.
 */

short scsi_dmaon[2]	= { 1, 1};	/* whether DMA_H1 is available */
static int up4len[2]	= { 512, 512};	/* min size which gets DMA_UP4 */
static int dmalen[2]	= { 512, 512};	/* min size which gets DMA_H1 */

static short lodmaid	= 0;		/* lowest dev to get DMA_H1 (testing) */

/* Decide on the DMA type to use.  We always use what's in the iov if
 * it's specified there, otherwise we choose the best we've got.
 */
int
scsi_dmatype(stp)			/* decide on DMA type to use */
register struct tasks *stp;
{
    static u_short pollflags[2] = {SDC_RDPOLL,SDC_WRPOLL};

    register struct iov *iov;
    register struct dmaspeeds *sp;
    register int rw;
    register int len;

    iov = &stp->req->vio.vio_iov[stp->niovsent];

    rw = stp->flags & SF_READ ? 0 : 1;	/* read or write */

    if (stp->devchar & pollflags[rw]) {	/* must poll if SCSICHAR says so */
	return(DMA_P1);
    }

    /* If the user specified a speed we honor it, unless he wants DMA_H1.
     * If we haven't got DMA hardware, we downgrade it to DMA_UP4.
     */
    if (iov->vio_flags) {		/* user specified it, we obey */
	if (iov->vio_flags == DMA_H1 && !scsi_dmaon[rw]) {
	    return(DMA_UP4);
	}
	return(iov->vio_flags);
    }

    len = scsi_dma_len(stp);		/* get transfer length needed */

    if (scsi_dmaon[rw] && len >= dmalen[rw]) {	/* we'll use DMA_H1 */
	if (stp - tasks >= lodmaid) {
	    return(DMA_H1);
	}
    }

    if (len >= up4len[rw]) {		/* we'll use DMA_UP4 */
	return(DMA_UP4);
    }

    return(DMA_P1);			/* otherwise we'll be slowest */
}

int
scsi_dma_len(stp)			/* return current data xfer length */
register struct tasks *stp;
{
    register struct vio *vp = &stp->req->vio;
    register struct iov *iov;
    register int acc;
    register int d;
    register int l;
    register int r;

    /* accumulate total length from iovs preceeding this one */
    acc = 0;
    for (l=0; l<stp->niovsent; l++)
	acc += vp->vio_iov[l].vio_len;	/* accumulative bytes */

    iov = &vp->vio_iov[stp->niovsent];

    /*  compute (possibly partial) dma transfer bounds */

    d = stp->datasent - acc;		/* offset into seg */
    l = iov->vio_len - d;		/* remaining in seg */
    r = stp->datalen - stp->datasent;	/* remaining data */
    return(min(l,r));			/* transfer length */
}

int
scsi_vio( stp, dma, speed)
register struct tasks *stp;
int (*dma)();
int speed;
{
	register struct ncr5380 *ncr = (struct ncr5380 *)scsi_addr;
	register struct iov *iov;
	register int acc;
	int d, l, r, m;

	struct vio *vp = &stp->req->vio;

	if (stp->datasent >= stp->datalen)  {
		tracescsi("scsi_vio buffer limits");
		return (0);		/* we already did it */
	}

	/* accumulate total length from iovs preceeding this one */
	acc = 0;
	for (l=0; l<stp->niovsent; l++)
		acc += vp->vio_iov[l].vio_len;	/* accumulative bytes */
	iov = &vp->vio_iov[stp->niovsent];

	/* If we're to do hardware DMA, get it started and return. */

	if (speed == DMA_H1) {
	    d = stp->datasent - acc;		/* offset into seg */
	    l = iov->vio_len - d;		/* remaining in seg */
	    r = stp->datalen - stp->datasent;	/* remaining data */
	    m = min(l,r);			/* move length */
	    (void)(*dma)(iov->vio_base+d, m, speed);

	    stp->dmafunc = dma;			/* in or out */
	    stp->iovsegcnt = l;			/* remaining bytes in seg */
	    stp->dmacount = m;			/* current DMA count */

	    return(0);
	}

	/*
	 * If we're to do software pseudo-DMA, iterate over the dma
	 * segments while still more data 
	 */
	do  {
		if ( stp->niovsent >= vp->vio_niov )  {
			printscsi("scsi_vio niovsent limits!");
			berrflag = 1; /* stimulate bus error recovery */
			return (-1);
		}

		/*  compute (possibly partial) dma transfer bounds */
		d = stp->datasent - acc;   	  /* offset into seg */
		l = iov->vio_len - d;	   	  /* remaining in seg */
		r = stp->datalen - stp->datasent; /* remaining data */
		m = min( l, r);		   	  /* move length */

#ifdef notdef
		/*  build trace for each dma move */
		traceltox( m, traceltox(iov->vio_base+d,
		  tracescpy((dma == scsi_in)?"dmai ":"dmao ",sstbuf)) );
		tracescsi(sstbuf);
#endif
		/*  apply dma function to move data */

		HW_SHAKE(1);			/* h/w handshake on */

		r = (*dma)( iov->vio_base+d, m, speed);

		HW_SHAKE(0);			/* h/w handshake off */

		stp->datasent += r;

		/*  are we done cooking with this segment? */
		if ( l == r )  {
			acc += iov->vio_len;	/* advance iov */
			++iov; ++stp->niovsent;
		}
		/*  quit if dma returned wrong length, not an error! */
		if ( r != m )
			break;	 
	} while ( stp->datasent < stp->datalen); /* more data */

	/*viodmp("SCSI-XFER",vp);/**/

	return (stp->datasent);
}

/*	scsi_in -- input bytes from the scsi bus.
 *	This routine inputs bytes from the ncr chip using one of several
 *	dma transfer methods.  Either each iov specifies its dma, or
 *	the method is derived from length.
 */

scsi_in( buf, len, speed)
register caddr_t buf;
register int len;
int speed;
{
	caddr_t start = buf;
	register caddr_t end   = buf + len;
	struct ncr5380  *ncr = (struct ncr5380 *)scsi_addr;
	int i;
	u_long dmactl;

	TRACE(T_scsi, ("\nscsi_in(0x%x,%d,%d) phase= 0x%x\n",
		buf, len, speed,
		(ncr->curr_stat >> 2) & 7));

	/* Hardware DMA? Great! */

	if (speed == DMA_H1) {

	    dmactl = ncr->dmactl & ~SCSI_HSENAB; /* ensure h/w handshake off */
	    ncr->dmactl = dmactl;
	    ncr->wdog_timer = 50000000L;	/* approx 5 seconds */
	    ASSERT(buf);
	    ASSERT(len);
	    ncr->dma_addr = buf;
	    ncr->dma_count = len;

	    dmactl = ncr->dmactl | SCSI_DMAENAB | SCSI_WDIRQENAB;
	    ncr->dmactl = dmactl;

	    ncr->mode |= SMD_DMA | SMD_EOP;	/* dma mode, enable EOP intr */

	    TRACESCSI(traceltox(ncr->dma_addr,
			(tracescpy(" @",
			  (traceltod(ncr->dma_count,
			    (tracescpy("dmaR ",sstbuf)))))));
				tracescsi(sstbuf));

	    /* We schedule the next state BEFORE starting the actual DMA
	     * operation, to ensure that it gets noted before the IRQ
	     * can arrive.  Also, scsisched() wants to enable the IRQ for us,
	     * and it mustn't touch the chip once we start the DMA.
	     */
	    SPLINTR();
	    scsisched(SI_DMA,"start dma");	/* DMA is running */

	    curstp->flags |= SF_HWDMA;		/* DMA is running */
	    ncr->start_Ircv = 0;		/* NOW start the chip reading */

	    return(0);
	}

	ncr->mode |= SMD_DMA;
	ncr->start_Ircv = 0;

	/*
	 *  unrolled 4-byte dma
	 */
	if (speed == DMA_UP4)  {
		register struct ncr5380  *ncr = (struct ncr5380 *)scsi_addr;
		register unsigned dmardy = SBS_DMA|SBS_PHASE;
		register  long  *shsk = (long *)shsk_addr;
		jmp_buf jb;
		int	*saved_jb;

		/* set up to catch bus-errors */
		saved_jb = u.u_nofault;
		if (setjmp(jb)) {
			/* FYI: setjmp invalidates all register vars */
			u.u_nofault = saved_jb;
			berrflag = 1;
			tracescsi("scsiin buserr");
			return (0);
		}
		u.u_nofault = jb;

#define RDY \
	while( (ncr->bus_status & dmardy) != dmardy )  { \
		++iowaits; \
		if ( !(ncr->bus_status & SBS_PHASE) \
			|| !(ncr->curr_stat & SCS_BSY) \
			|| unjam > 1 \
			|| --i < 0 )  { \
			ncr->mode &= ~SMD_DMA; \
			u.u_nofault = saved_jb; \
			return (buf - start); \
		} \
	}

		/* at start of each sector, poll until dma ready */
		i = MAXLOOP;
		do  {
		    /* are you ready... */
		    RDY;

#define R4 *((long *)buf)++ = *shsk

		    /* unrolled 4-byte pseudo-dma 128 bytes at a time */
		    R4;R4;R4;R4; R4;R4;R4;R4; R4;R4;R4;R4; R4;R4;R4;R4; /* 64 */
		    R4;R4;R4;R4; R4;R4;R4;R4; R4;R4;R4;R4; R4;R4;R4;R4; /* 64 */
		}  while  ( (len -= 128) >= 128);
		u.u_nofault = saved_jb;
	}

	if ( len == 0 )
		return (buf - start);
	else if ( len < 0 )
		return 0;

	ASSERT( len > 0 );
	i = MAXLOOP;

	/*
	 *  Unrolled 1-byte psuedo-dma
	 */
	if ( buf + 128 < end )  
	    while ( buf + 15 < end )  {
		struct ncr5380  *ncr = (struct ncr5380 *)scsi_addr;
		register u_char *sdma  = (u_char *)sdma_addr;
		register u_char *ncrbs = &((struct ncr5380 *)scsi_addr)->bus_status;
		register u_char dmardy = SBS_DMA|SBS_PHASE;
		register u_char dmaRDY = SBS_DMA|SBS_PHASE|SBS_ACK;

#define WRDY(leave) \
		/* spin-wait for ready */ \
		while( (*ncrbs & dmardy) != dmardy )  { \
			++iowaits; \
			if ( !(ncr->bus_status & SBS_PHASE) \
				|| !(ncr->curr_stat & SCS_BSY) \
				|| unjam > 1 \
				|| --i < 0 )  { \
				ncr->mode &= ~SMD_DMA; \
				leave; \
			} \
		}
		
		/* wait for data ready, then input up to 16 bytes */
		WRDY(return(buf-start));
		*buf++ = *(char *) sdma;
		if ( *ncrbs == dmaRDY )      *buf++ = *(char *) sdma;
		if ( *ncrbs == dmaRDY )      *buf++ = *(char *) sdma;
		if ( *ncrbs == dmaRDY )      *buf++ = *(char *) sdma;

		if ( *ncrbs == dmaRDY )      *buf++ = *(char *) sdma;
		if ( *ncrbs == dmaRDY )      *buf++ = *(char *) sdma;
		if ( *ncrbs == dmaRDY )      *buf++ = *(char *) sdma;
		if ( *ncrbs == dmaRDY )      *buf++ = *(char *) sdma;

		if ( *ncrbs == dmaRDY )      *buf++ = *(char *) sdma;
		if ( *ncrbs == dmaRDY )      *buf++ = *(char *) sdma;
		if ( *ncrbs == dmaRDY )      *buf++ = *(char *) sdma;
		if ( *ncrbs == dmaRDY )      *buf++ = *(char *) sdma;

		if ( *ncrbs == dmaRDY )      *buf++ = *(char *) sdma;
		if ( *ncrbs == dmaRDY )      *buf++ = *(char *) sdma;
		if ( *ncrbs == dmaRDY )      *buf++ = *(char *) sdma;
		if ( *ncrbs == dmaRDY )      *buf++ = *(char *) sdma;
	}
	while ( buf < end )  {
		struct ncr5380  *ncr = (struct ncr5380 *)scsi_addr;
		register unsigned dmardy   = SBS_DMA|SBS_PHASE;
		register char   *sdma  = (char *)sdma_addr;
		register u_char *ncrbs = &((struct ncr5380 *)scsi_addr)->bus_status;

		WRDY( return(buf - start));	/* wait ready */
		*buf++ = *(char *) sdma;	/* read char in */
	}

	unjam = 0;			/* indicate activity */
	return (buf - start);
}

/*	scsi_out -- output bytes on the scsi bus.
 *	The routine returns the number of bytes actually transferred.
 *not yet:
 *	    DMA_SLOW or len >= 1       ==>  looped   software REQ/ACK
 */

scsi_out(buf, len, speed)

	register caddr_t buf;
	int len;
	int	speed;
{
	register struct ncr5380 *ncr = (struct ncr5380 *)scsi_addr;
	register unsigned dmardy = SBS_DMA|SBS_PHASE;
	caddr_t start = buf;
	register caddr_t end   = buf + len;
	int i;
	u_long dmactl;

	TRACE(T_scsi, ("scsi_out(%x,%d,%d)phase= 0x%x\n",
		buf, len, speed,
		(ncr->curr_stat >> 2) & 7));
	if ( len < 0 )
		return 0;

	/* Hardware DMA? Great! */

	if (speed == DMA_H1) {
	    dmactl = ncr->dmactl & ~SCSI_HSENAB; /* ensure h/w handshake off */
	    ncr->dmactl = dmactl;
	    ncr->wdog_timer = 50000000L;	/* approx 5 seconds */
	    ASSERT(buf);
	    ASSERT(len);
	    ncr->dma_addr = buf;
	    ncr->dma_count = len;

	    dmactl = ncr->dmactl | SCSI_DMAENAB | SCSI_WDIRQENAB;
	    ncr->dmactl = dmactl;

	    ncr->mode |= SMD_DMA | SMD_EOP;	/* dma mode, enable EOP intr */

	    TRACESCSI(traceltox(ncr->dma_addr,
			(tracescpy(" @",
			  (traceltod(ncr->dma_count,
			    (tracescpy("dmaW ",sstbuf)))))));
				tracescsi(sstbuf));

	    /* We schedule the next state BEFORE starting the actual DMA
	     * operation, to ensure that it gets noted before the IRQ
	     * can arrive.  Also, scsisched() wants to enable the IRQ for us,
	     * and it mustn't touch the chip once we start the DMA.
	     */
	    SPLINTR();
	    scsisched(SI_DMA,"start dma");	/* DMA is running */

	    curstp->flags |= SF_HWDMA;		/* DMA is running */
	    ncr->start_xmt = 0;			/* NOW start the chip reading */

	    return(0);
	}

	/* Gonna do the data transfer by pseudo-dma: */

	ncr->mode |= SMD_DMA;
	ncr->start_xmt = 0;

	/* inline speedup for >= 512 bytes */
	if (speed == DMA_UP4)  {
		register  long  *shsk = (long *)shsk_addr;
		jmp_buf jb;
		int	*saved_jb;

		/* set up to catch bus-errors */
		saved_jb = u.u_nofault;
		if (setjmp(jb)) {
			/* FYI: setjmp invalidates all register vars */
			u.u_nofault = saved_jb;
			berrflag = 1;
			tracescsi("scsiout buserr");
			return (0);
		}
		u.u_nofault = jb;

#define POLL \
	while( (ncr->bus_status & dmardy) != dmardy )  {	\
		++iowaits; \
		if ( !(ncr->bus_status & SBS_PHASE) \
			|| !(ncr->curr_stat & SCS_BSY)	 \
			|| unjam > 1  \
			|| --i < 0 )  { \
			u.u_nofault = saved_jb; \
			if ( !BS(SBS_PHASE) )	\
				--buf;	/* the one that got away */ \
			return (buf - start); \
		} \
	} \

#define W1 *(char *)sdma_addr = *buf++
#define W4 *shsk = *((long *)buf)++ 

		/* begin unrolled sector transfer */
		i = MAXLOOP;
		do  {
		    /* at start of each sector, poll until dma ready */
		    /* 4-byte pseudo-dma 64 bytes inline */
		    POLL; W1; POLL; W1; POLL; W1; POLL; W1; POLL;
		    W4;W4;W4; W4;W4;W4;W4; W4;W4;W4;W4; W4;W4;W4;W4; /* 64 */
		}  while  ( (len -= 64) >= 64);
		u.u_nofault = saved_jb;
	}

	/* we don't have slow mode here yet */
	if ( 1 /*speed == DMA_P1*/ )  {
		while ( buf < end )  {
			register  char *sdma  = (char *)sdma_addr;
			i = MAXLOOP;
			while( BS(SBS_DMA|SBS_PHASE) !=
					 (SBS_DMA|SBS_PHASE) )  {
				++iowaits;
				if ( !(ncr->bus_status & SBS_PHASE)
					|| !(ncr->curr_stat & SCS_BSY)	
					|| unjam > 1
					|| --i < 0 )  {
					if ( !BS(SBS_PHASE) )
					    --buf; /* the one that got away */
					ncr->init_comm &= ~SIC_DB;
					return (buf-start);
				}
			}
			*(char *) sdma = *buf++;
		}
	}
	unjam = 0;			/* indicate activity */
	
	/* delay till ncr drq comes back (byte accepted) or phase changes */
	i =  MAXLOOP;
	while ( BS(SBS_DMA|SBS_PHASE) == (SBS_PHASE) && --i)
		++iowaits;
	if (!i) printscsi("write finish");

	return(buf-start);
}
