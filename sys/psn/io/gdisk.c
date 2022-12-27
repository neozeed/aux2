#ifndef lint	/* .../sys/psn/io/gdisk.c */
#define _AC_NAME gdisk_c
#define _AC_NO_MAIN "@(#) Copyright (c) 1987 Apple Computer Inc., All Rights Reserved.  {Apple version 2.3 90/03/20 15:38:29}"
#include <apple_notice.h>

#ifdef _AC_HISTORY
  static char *sccsid = "@(#)Copyright Apple Computer 1987\tVersion 2.3 of gdisk.c on 90/03/20 15:38:29";
#endif		/* _AC_HISTORY */
#endif		/* lint */

#define _AC_MODS

#ifdef	HOWFAR
extern	int T_sdisk;
extern	int T_gdisk;
#endif	HOWFAR
/*	@(#)gdisk.c	1.9 - 10/19/87
 *
 *	gdisk -- generic disk driver routines.
 *
 *	Copyright 1987 UniSoft Corporation
 *
 *	UniPlus Source Code. This program is proprietary
 *	with Unisoft Corporation and is not to be reproduced
 *	or used in any manner except as authorized in
 *	writing by Unisoft.
 *
 *	     These are generic disk driver routines which are unique
 *	to the kernel environment.  None of these routines are
 *	device specific.
 */	

#include <sys/types.h>
#include <sys/sysmacros.h>
#include <sys/param.h>
#include <sys/debug.h>

#include <sys/utsname.h>
#include <sys/elog.h>
#include <sys/erec.h>
#include <sys/buf.h>
#include <sys/iobuf.h>

#include <sys/mmu.h>
#include <sys/signal.h>
#include <sys/time.h>
#include <sys/user.h>

#include <sys/ioctl.h>
#include <sys/ssioctl.h>
#include <sys/gdisk.h>
#include <sys/vio.h>
#include <sys/scsireq.h>
#include <sys/gdkernel.h>
#include <apple/dpme.h>
#include <apple/bzb.h>
#include <apple/abm.h>
#include <sys/errno.h>
#include <sys/diskformat.h>
#include <sys/file.h>

#define	ERRSTATE 3
#define	MAXRETRY 3

/*	Concurrency control.
 *	    For ioctl commands, there are two levels of concurrency control.
 *	There is a single command buffer.  It is manipulated with the GETBUF 
 *	and FREEBUF macros.  Only one process may have this buffer assigned 
 *	at a time.  Once the buffer is assigned, the process may set the
 *	ctl data structure fields associated with command processing, (eg
 *	ctcmd and ctarg).  But assigning the command buffer does not imply
 *	that the device is inactive and ready for processing.  Once
 *	the command buffer is allocated and the command ready, the
 *	command buffer enters strategy routine processing, after which
 *	it will be scheduled and the command acted upon.  When complete,
 *	the buffer is iodone(), and the device continues other processing.
 *	When the requestor is awoken (due to the iodone), the controller
 *	data structure fields related to command processing (esp. ctretval)
 *	are valid, however the controller may be busy with another I/O.
 *	After the controller data structure is freed, another command
 *	may begin.
 */

#define GETBUF(bp)					\
	SPL2();						\
	while (bp->b_flags & B_BUSY) {			\
		bp->b_flags |= B_WANTED;		\
		(void)sleep((caddr_t)bp, PRIBIO+1);	\
	}						\
	bp->b_flags |= B_BUSY;				\
	bp->b_flags &= ~B_DONE;				\
	SPL0()

#define FREEBUF(bp)			\
	SPL2();				\
	if (bp->b_flags & B_WANTED)	\
		wakeup((caddr_t)bp);	\
	bp->b_flags = 0;		\
	SPL0()

extern struct drqual drqdefault;
extern char gd_aux_ptype[];

static	int gdrw_ret(), queuecmd();

/*	gdclose -- close the device.
 *	    The close routine passes control to the low level device handler.
 *	One should remember that system V will sometimes close a device that
 *	is still active.
 */

gdclose(ctp, dev)

register struct gdctl *ctp;
dev_t	dev;
{
	register struct gddrive *drp;
	register struct gdpart *ptp;
	int	disk;

	TRACE(T_sdisk, ("in gdclose dev = 0x%x\n", dev));
	if(!ctp)
		return(ENXIO);
	disk = gddisknum(minor(dev));
	for(drp = ctp->ctdrive; drp && (drp->drnum != disk); 
							drp = drp->drnxt) {
	}
	if(!drp || !(ptp = drp->drpart[gdslicenum(minor(dev))])) {
		return(ENXIO);
	}
	SPL2();
	while(ctp->ctactive || (ctp->ctflags & CTF_CLOSING))  {
		ctp->ctflags |= CTF_CLOSING;
		sleep(&ctp->ctflags, PRIBIO);
	}
	SPL0();
	GETBUF(ctp->ctbp);
	ctp->ctcmd = GD_SHUTDOWN;
	ctp->ctarg = GD_SHUT_CLOSE;
	queuecmd(ctp, dev);
	FREEBUF(ctp->ctbp);
	return(0);
}


/*	gddevctl -- device control handler.
 *	    Since the generic code schedules the device, the device specific
 *	code does not know if the device is currently active, or idle.  
 *	This routine allows the device specific code to schedule a request
 *	and gain control when the device is available.  It may be used as
 *	part of a device specific ioctl, for example.
 *	    The users routine is not called from a valid process context.
 *	This routine must be called from a valid process context.
 */

gddevctl(ctp, dev, dproc, arg)

register struct	gdctl *ctp;
dev_t	dev;
int	(*dproc)();
int	arg;
{
	register struct buf *bp;
	int	ret;

	TRACE(T_gdisk, ("gddevctl dev = 0x%x proc = 0x%x\n"));
	bp = ctp->ctbp;
	GETBUF(bp);
	ctp->ctcmd = GDC_DEVCTL;
	ctp->ctarg = arg;
	ctp->ctdevctl = dproc;
	queuecmd(ctp, dev);
	ret = ctp->ctretval;
	FREEBUF(bp);
	return(ret);
}


/*	gdioctl -- generic, standalone, ioctl processing.
 */

/*ARGSUSED*/
gdioctl(ctp, dev, cmd, addr, flag)

register struct gdctl *ctp;
dev_t dev;
int cmd;
caddr_t addr;
int flag;
{
	register struct gddrive *drp;
	register struct gdpart *ptp;
	register struct bzb *bzb;
	char	*cp;
	struct	diskformat *dfmtp;
	long	n, sector;
	int	i, disk;

	TRACE(T_sdisk, ("in gdioct dev = 0x%x cmd = 0x%x\n", dev, cmd));
	disk = gddisknum(minor(dev));
	for(drp = ctp->ctdrive; drp && (drp->drnum != disk); 
							drp = drp->drnxt) {
	}
	if(!drp || !(ptp = drp->drpart[gdslicenum(minor(dev))])) {
		GDPRINTF("ioctl on unopen device");	 /* can't happen */
		return(ENXIO);
	}
	switch(cmd) {		/* check permissions on these ioctls */
	case GD_SHUTDOWN:
		if (*(int *)addr == GD_SHUT_EJECT && (ctp->ctflags&CTF_CDROM))
			break;		/* Grant all ability to eject CDROM */
	case GD_ALTBLK:
	case GD_MKBAD:
	case GD_SBZBTMADE:
	case GD_SBZBTMOUNT:
	case GD_SBZBTUMOUNT:
	case GD_SETPNAME:
	case GD_SOFTERR:
	case GD_SPARE:
	case GD_UNSETPNAME:
	case UIOCEXTE:
	case UIOCFORMAT:
	case UIOCNEXTE:
	case GD_TRACE:
		if (!(flag & FWRITE) && !suser()) {
			return(EPERM);
		}
		
	}
	switch(cmd) {		/* These may only be performed on whole device */
	case GD_SOFTERR:
	case UIOCFORMAT:
		if(gdslicenum(minor(dev)) != GD_PHYSPART) {
			return(EACCES);
		}
	}
	switch(cmd) {		/* init partitions on first access */
	case GD_GBZBTMADE:
	case GD_GBZBTMOUNT:
	case GD_GBZBTUMOUNT:
	case GD_GETABM:
	case GD_GETMAP:
	case GD_PARTSIZE:
	case GD_SPARE:
	case GD_MKBAD:
	case GD_SBZBTMADE:
	case GD_SBZBTMOUNT:
	case GD_SBZBTUMOUNT:
		if(drp->drstate != DRS_NORMAL || ptp->ptstate != PTS_NORMAL) {
			GETBUF(ctp->ctbp);
			ctp->ctcmd = GDC_INIT;
			queuecmd(ctp, dev);
			n = ctp->ctretval;
			FREEBUF(ctp->ctbp);
			if(n || drp->drstate != DRS_NORMAL 
						|| ptp->ptstate != PTS_NORMAL) {
				TRACE(T_gdisk, ("gdioctl cannot init\n"));
				return(ENXIO);
			}
		}
		switch(cmd) {
		case GD_SPARE:
		case GD_PARTSIZE:
			break;
		default:
			if((ptp->ptflags & PTF_TYMASK) != PTF_TYDPME)
				return(ENXIO);
			break;
		}
		break;
	}
	switch(cmd) {
	case GD_GETABM:
		if(ptp->ptflags & PTF_NOALT) {
			return(ENXIO);
		}
		((struct abm *) addr)->abm_size = ptp->ptasize;
		((struct abm *) addr)->abm_ents = ptp->ptaents;
		((struct abm *) addr)->abm_start = ptp->ptastart - ptp->ptoffset;
		break;
	case GD_ALTBLK:
	case GD_SOFTERR:
	case GD_UNSETPNAME:
		GETBUF(ctp->ctbp);
		ctp->ctcmd = cmd;
		ctp->ctarg = *((int *)addr);
		queuecmd(ctp, dev);
		n = ctp->ctretval;
		FREEBUF(ctp->ctbp);
		break;
	case GD_GBZBTMADE:
	case GD_SBZBTMADE:
	case GD_GBZBTMOUNT:
	case GD_SBZBTMOUNT:
	case GD_GBZBTUMOUNT:
	case GD_SBZBTUMOUNT:
		GETBUF(ctp->ctbp);
		ctp->ctcmd = GDC_RDDPME;
		queuecmd(ctp, dev);
		bzb = (struct bzb *)((DPME *)ctp->ctbp->b_un.b_addr)->dpme_boot_args;
		if(ctp->ctretval != 0 || bzb->bzb_magic != BZBMAGIC) {
			n = ctp->ctretval;
			FREEBUF(ctp->ctbp);
			if(n)
				return(n);
			return(ENXIO);
		}
		i = 0;	/* flag that this is a write operation */
		n = 0;
		switch(cmd) {
		case GD_GBZBTMADE:
			u.u_rval1 = bzb->bzb_tmade;
			break;
		case GD_GBZBTMOUNT:
			u.u_rval1 = bzb->bzb_tmount;
			break;
		case GD_GBZBTUMOUNT:
			u.u_rval1 = bzb->bzb_tumount;
			break;
		case GD_SBZBTMADE:
			bzb->bzb_tmade = *((long *)addr);
			i = 1;
			break;
		case GD_SBZBTMOUNT:
			bzb->bzb_tmount = *((long *)addr);
			i = 1;
			break;
		case GD_SBZBTUMOUNT:
			bzb->bzb_tumount = *((long *)addr);
			i = 1;
			break;
		}
		if(i) {
			ctp->ctcmd = GDC_WRDPME;
			queuecmd(ctp, dev);
			n = ctp->ctretval;
		}
		FREEBUF(ctp->ctbp);
		return(0);
	case GD_GETMAP:
		ASSERT(ptp->ptstate == PTS_NORMAL);
		if(ptp->ptflags & PTF_NOALT) {	
			return(ENXIO);
		}
		n = ((struct abmi *)addr)->abmi_nbytes;
		cp = ((struct abmi *)addr)->abmi_buf;
		if(n < 0 || n > ptp->ptasize) {
			return(EINVAL);
		}
		GETBUF(ctp->ctbp);
		sector = ptp->ptastart;
		while(n > 0 && ptp->ptstate == PTS_NORMAL) {
			ctp->ctcmd = GDC_RDALTS;
			ctp->ctarg = sector;
			queuecmd(ctp, dev);
			if(ctp->ctretval != 0) {
				return(ctp->ctretval);
			}
			i = n < DEV_BSIZE ? n : DEV_BSIZE;
			copyout(ctp->ctbp->b_un.b_addr, cp, i);
			n -= i;
			cp += i;
			sector++;
		}
		FREEBUF(ctp->ctbp);
		break;
	case GD_GETPNAME:
		if((ptp->ptflags & PTF_NMMASK) == PTF_NMNONE ||
			(ptp->ptflags & PTF_TYMASK) == PTF_TYDEF) {
			return(ENXIO);
		}
		strncpy(((struct dpident *)addr)->dpiname, ptp->ptname, 
					sizeof(ptp->ptname));
		strncpy(((struct dpident *)addr)->dpitype, ptp->pttype,
					sizeof(ptp->pttype));
		return(0);
	case GD_MKBAD:
		if(ptp->ptflags & PTF_NOALT) {
			return(ENXIO);
		}
		if(*((daddr_t *)addr) < 0 || *((daddr_t *)addr) > ptp->ptpsize) {
			return(EINVAL);
		}
		GETBUF(ctp->ctbp);
		ctp->ctcmd = GD_MKBAD;
		ctp->ctarg = *((daddr_t *)addr);
		queuecmd(ctp, dev);
		n = ctp->ctretval;
		FREEBUF(ctp->ctbp);
		return(n);
	case GD_PARTSIZE:
		u.u_rval1 = ptp->ptflags & PTF_USERALT ? 
				ptp->ptpsize : ptp->ptlsize;
		return(0);
	case GD_SETPNAME:
		if(gdslicenum(minor(dev)) >= GD_PHYSPART) {
			return(ENXIO);
		}
		if(((struct dpident *)addr)->dpiname[0] == '\0')
			return(EINVAL);
		if(((struct dpident *)addr)->dpitype[0] == '\0') {
			strncpy(((struct dpident *)addr)->dpitype,
				gd_aux_ptype, 
				sizeof(((struct dpident *)addr)->dpitype));
		}
		GETBUF(ctp->ctbp);
		SPL2();
		if((ptp->ptflags & PTF_NMMASK) != PTF_NMNONE || 
					ptp->ptstate != PTS_REINIT ) { 
			FREEBUF(ctp->ctbp);
			SPL0();
			return(EBUSY); 
		}
		ptp->ptflags &= ~PTF_NMMASK;
		ptp->ptflags |= PTF_NMUSER;
		strncpy(ptp->ptname, ((struct dpident *)addr)->dpiname,
					sizeof(ptp->ptname));
		strncpy(ptp->pttype, ((struct dpident *)addr)->dpitype,
					sizeof(ptp->pttype));
		SPL0();
		TRACE(T_gdisk, ("Look for name %s type %s\n", ptp->ptname,
			ptp->pttype));
		ctp->ctcmd = GDC_INIT;
		queuecmd(ctp, dev);
		n = ctp->ctretval;
		FREEBUF(ctp->ctbp);
		if(ptp->ptstate != PTS_NORMAL) {
			ptp->ptflags &= ~PTF_NMMASK;
			ptp->ptflags |= PTF_NMNONE;
			return(n != 0 ? n : ENXIO);
		}
	case GD_SHUTDOWN:
		GETBUF(ctp->ctbp);
		ctp->ctcmd = GD_SHUTDOWN;
		ctp->ctarg = *((int *)addr);
		queuecmd(ctp, dev);
		FREEBUF(ctp->ctbp);
		return(0);
	case GD_SPARE:
		GETBUF(ctp->ctbp);
		ctp->ctcmd = GD_SPARE;
		ctp->ctarg = *((daddr_t *)addr);
		queuecmd(ctp, dev);
		n = ctp->ctretval;
		FREEBUF(ctp->ctbp);
		return(n);	/* ZZZ check error code EIO and ENXIO */
	case UIOCEXTE:
		ctp->ctflags &= ~CTF_NOPRINT;
		break;
	case UIOCFORMAT:
		GETBUF(ctp->ctbp);
		dfmtp = (struct diskformat *)kmem_alloc(
				(unsigned) sizeof(struct diskformat));
		*dfmtp = *((struct diskformat *) addr);
		ctp->ctcmd = cmd;
		ctp->ctarg = (long)dfmtp;
		queuecmd(ctp, dev);
		n = ctp->ctretval;
		FREEBUF(ctp->ctbp);
		kmem_free(dfmtp, sizeof(struct diskformat));
		return(n);
	case UIOCNEXTE:
		ctp->ctflags |= CTF_NOPRINT;
		break;
	case GD_TRACE:
		i = (*addr) ? SDC_TRACE : 0;
		scsichar( ctp->ctmajor & 7, i, SDC_TRACE);
		break;
	case GD_TRACEPR:
		i = (*addr) ? SDC_TRACEPR : 0;
		scsichar( ctp->ctmajor & 7, i, SDC_TRACEPR);
		break;
	default:
		return(ENOTTY);
	}
	return(0);
}

/*	gdopen -- generic, standalone, open routine.
 */

/*ARGSUSED*/
gdopen(ctp, dev, flag)

register struct gdctl *ctp;
dev_t	dev;
{
	register struct gddrive *drp, **pdrp;
	register disk, part;

	TRACE(T_sdisk, ("in gdopen dev = 0x%x\n", dev));
	if(!ctp) {
		return(ENXIO);
	}
	SPL2();
	while(ctp->ctflags & CTF_CLOSING) {
		sleep(&ctp->ctflags, PRIBIO+1);
	}
	SPL0();
	disk = gddisknum(minor(dev));
	part = gdslicenum(minor(dev));
	for(pdrp = &(ctp->ctdrive), drp = ctp->ctdrive; 
	    drp && drp->drnum < disk; 
	    pdrp = &(drp->drnxt), drp = drp->drnxt) {
		if(drp->drnum == disk) {
			break; /*return(0)*/;
		}
	}
	/* drive not found, make drive structure */
	/* Assert:  No asynchronous activity will be accessing this 
	 * drive while we create it. Nothing will depend on the order
	 * of the drive linked list associated with the controller.
	 */
	SPL2();
	if(!(drp && drp->drnum == disk)) {
		drp = (struct gddrive *)kmem_alloc((unsigned)sizeof(struct gddrive));
		bzero((caddr_t)drp, sizeof(struct gddrive));
		drp->drnum = disk;
		drp->drtab.io_stp = &drp->driostat;
		drp->drqual = (struct drqual*)kmem_alloc(
			(unsigned)sizeof(struct drqual));
		bcopy(&drqdefault, drp->drqual, sizeof(struct drqual));
		drp->drnxt = *pdrp;
		*pdrp = drp;
	}
	/* first access for partition, make structure */
	if(!drp->drpart[part]) {
		drp->drpart[part] = 
			(struct gdpart *)kmem_alloc((unsigned)sizeof(struct gdpart));
		bzero((caddr_t)(drp->drpart[part]), sizeof(struct gdpart));
	}
	SPL0();
	return(0);
}



/*	gdrestart -- restart after a gddevctl call.
 *	    Following a gdevctl call, this routine will cause processing
 *	for a device to be resumed.
 */

gdrestart(maj, reinit)

{
	register struct gdctl *ctp;
	struct buf *bp;
	struct iobuf *dp;
	extern struct gdctl *gdctllist;

	for(ctp = gdctllist; ctp != NULL; ctp = ctp->ctnextct) {
		if(ctp->ctmajor == maj) {
			bp = ctp->ctbp;
			dp = &ctp->ctactive->drtab;
			if(reinit)
				ctp->ctactive->drstate = DRS_REINIT;
			bp = dp->b_actf;
			dp->b_actf = bp->av_forw;
			iodone(bp);
			dp->b_active = 0;
			gdstart(ctp);
			return;
		}
	}
	/* Shouldn't happen.
	 */
	pre_panic();
	printf("Debug: maj = 0x%x caller = 0x%x\n", maj, caller());
	panic("bad major number in gdrestart");
}

static int
gdrw_ret(taskp, ret)

struct gentask *taskp;
{
	register struct gdctl *ctp;
	register struct gddrive *drp;
	struct buf *bp;
	char	*rw;
	int	errnow;

	ctp = taskp->gtcp;
	TRACE(T_sdisk, ("in gdrw_ret ret = %d ctp = 0x%x maj = %d\n", 
		ret, ctp, ctp->ctmajor));
	drp = ctp->ctactive;
	/* important for user to know whether reading or writing disk */
	bp = drp->drtab.b_actf;
	ASSERT(bp != (buf *)NULL);
	rw = (bp->b_flags&B_READ) ? "read" : "write";
	errnow = 0;

#ifdef notdef
	/* force 10 percent retries */
	if ( ret == GDR_OK && (ctp->ctmajor&7) == 1 )
		ret = (rand(10) != 1) ? ret : GDR_AGAIN;
#endif
	if(ctp->ctstate == ERRSTATE) {
		/* This code is executed following the call to gprecover
		 * any error indicates we cannot recover the drive.
		 */
		if(ret != GDR_OK) { 
			drp->drstate = DRS_REINIT;
		}
		else  { /* ret == GDR_OK */
			if ( ctp->ctretry )
				GDPRINTF("Disk %s c%dd%ds%d retry #%d successful.\n",
					rw,
					ctp->ctmajor&7, drp->drnum,
					drp->drpartnum, ctp->ctretry);
		}
	}
	else {
		switch(ret) {
		case GDR_OK:
			if ( ctp->ctretry)  {
				GDPRINTF("Disk %s c%dd%ds%d retry #%d successful.\n",
					rw,
					ctp->ctmajor&7, drp->drnum,
					drp->drpartnum, ctp->ctretry);
			}
					
			ctp->ctretry = 0;
			ctp->ctstate++;	/* advance to next state */
			break;
		case GDR_AGAIN:
			ctp->ctretry++;
			if ( ctp->ctretry > 1 )  {
				/*
				 *  Force the slower, but more reliable
				 *  POLL-io (sic?) if RETRY failed too.
				 */
				int slow;
				ASSERT(bp != (buf *)NULL);
				slow = (bp->b_flags&B_READ) ? SDC_RDPOLL : SDC_WRPOLL;

				scsichar( ctp->ctmajor&7, slow, slow);
			}
			break;
		case GDR_CORR:
			if(!(drp->drqual->dqflags & DQF_NOCORR)) {
				ctp->ctretry = 0;
				ctp->ctstate++;
				break;
			}
			/* else fall through to ... */
		case GDR_FAILED:
			GDPRINTF("Generic Disk %s c%dd%ds%d Failure: ",
				rw,
				ctp->ctmajor&7,
				drp->drnum, drp->drpartnum);
			ctp->ctstate = ERRSTATE;
			errnow = 1;
			break;
		}
	}
	if(ctp->ctretry > MAXRETRY) {
		ctp->ctstate = ERRSTATE;
		ctp->ctretry = 0;
		errnow = 1;
		GDPRINTF("Generic Disk c%dd%ds%d retry limit exceeded: ",
			ctp->ctmajor&7,
			drp->drnum, drp->drpartnum);
		if ( tracescsi("MAXRETRY") )
			tracesnap(20);
			
	}
	if(errnow || ctp->cterrind) {
		int n = taskp->gtnreq>>9;	/* 512 byte blocks */
	    	if ( n <= 1)  {			/* if 1 block b */
		    GDPRINTF("Logical block %d, physical block %d\n",
			ctp->ctlbn, taskp->gtblock);
	    }  else { 	/* if multiple blocks, print b,b or range b-b */
		    GDPRINTF("Logical block %d, physical blocks %d%c%d\n",
			ctp->ctlbn, taskp->gtblock,
			(n==2) ? ',' : '-', taskp->gtblock+n-1);
	    }
		fmtberr(&drp->drtab, drp->drnum, -1, -1, taskp->gtblock,
			ctp->cterrind, &ctp->cterr[0], &ctp->cterr[1],
			&ctp->cterr[2], &ctp->cterr[3]);
		logberr(&drp->drtab, 1);
		ctp->cterrind = 0;
	}
	if(errnow) {
		(*ctp->ctprocs->gprecover)(taskp);
		return;
	}
	TRACE(T_gdisk, ("call gdstart from gdrw_ret\n"));
	gdstart(ctp);
}


/*	gdstart -- begin the next I/O activity.
 *	     This routine is always called with disk interrupts disabled.
 */

gdstart(ctp)

register struct gdctl *ctp;
{
	register struct gddrive *drp;
	register struct gdpart *ptp;
	register struct buf *bp;
	register struct iobuf *dp;
	register struct gentask *tskp;
	int	n;

	if(!ctp->ctactive) {
		ctp->ctactive = ctp->ctdrive;
		ctp->ctstate = 0;
	}
loop:
	ASSERT(ctp->ctactive);
	/* state 0 -- pick drive to start next */
	if(ctp->ctstate == 0) {
		dp = NULL;
		drp = ctp->ctactive;
		do {
			if(!(drp = drp->drnxt))
				drp = ctp->ctdrive;
			if((bp = drp->drtab.b_actf)) {
				dp = &drp->drtab;
				break;
			}
		} while(drp != ctp->ctactive);
		if(!dp)  {
			ctp->ctactive = NULL;
			if(ctp->ctflags & CTF_CLOSING) {
				ctp->ctflags &= ~CTF_CLOSING;
				wakeup(&ctp->ctflags);
			}
			TRACE(T_gdisk, ("gdstart: no active drive\n"));
			return;		/* nothing to do */
		}
		ctp->ctstate = 1;
		ctp->ctactive = drp;
		ctp->ctretry = 0;
	}
	else {
		drp = ctp->ctactive;
		dp = &drp->drtab;
		ASSERT(dp->b_actf);
		ASSERT(dp->b_active);
		bp = dp->b_actf;
	}
	ASSERT(bp == dp->b_actf);
	drp->drpartnum = gdslicenum(minor(bp->b_dev));
	ptp = drp->drpart[drp->drpartnum];
	tskp = ctp->cttaskp;
	ASSERT(ptp);
	ASSERT(tskp);
	tskp->gtmaj = ctp->ctmajor;
	tskp->gtdnum = drp->drnum;
	tskp->gtqual = drp->drqual;

	if(bp == ctp->ctbp) {
		TRACE(T_gdisk, printf("gdstart: command buffer at 0x%x\n", bp));
		gdcmd(ctp);
		return;
	}

	switch(ctp->ctstate) {
				/* State 0 deciding what drive to do */
	case 1:			/* State 1 setting up a read */
		break;
	case 2:			/* State 2 back from a read */
		drp->drcount += tskp->gtndone;
		drp->drniov  += tskp->gtvdone;
		if((drp->drtab.b_actf->b_resid -= tskp->gtndone) < DEV_BSIZE)
			goto blkdone;
		ctp->ctstate = 1;
		break;
	case ERRSTATE:		/* ERRSTATE A read has failed */
		TRACE(T_gdisk, ("ERRSTATE in gdstart\n"));
		bp->b_flags |= B_ERROR;
		goto blkdone;
	default:
		panic("unknown state in gdstart");	/* can't happen */
	}
	if(drp->drstate != DRS_NORMAL || ptp->ptstate != PTS_NORMAL) {
	        TRACE(T_gdisk, ("gdstart: drive init\n"));
		gddriveinit(ctp);
		return;
	}

	/*
	 *  if first time for this block, initialize pointer sets
	 */
	if(dp->b_active == 0) {
		dp->b_active++;
		drp->drniov = 0; drp->drcount = 0;	/* reset progress indices */
		tskp->gtvdone = 0; tskp->gtndone = 0;	/* reset task indices */

		/*  set up vio in gentask gtvio area */
		if ( (bp->b_flags & (B_VIO)) )  {
			/* copy in acceleration-supplied vio */
			struct vio *vp = (struct vio *)bp->b_resid;
			if ( !vp || vp->vio_niov < 1 || vp->vio_niov >= MAXVIO)
				panic("gdiskvio");	/* bad vio*/
			VIOCOPY( vp, &tskp->gtvio);
			releasemem( vp, sizeof *vp);	/* alloc in vbread/vwrite */
		}  else  { 
			/* build dummy vio pointers from bp params */
			struct vio *vp = &tskp->gtvio;
			vp->vio_tot = 0;
			IOVADD( vp, 0, (struct buf *)bp, bp->b_un.b_addr, bp->b_bcount);
		}
		bp->b_resid = tskp->gtvio.vio_tot; /* set io count */
		ASSERT( tskp->gtvio.vio_niov != 0);
		if(bp->b_bcount & (DEV_BSIZE - 1)) {
		        bp->b_error = EIO;         /* must xfer multiple of DEV_BSIZE */
			bp->b_flags |= B_ERROR;
			goto blkdone;
		}
	}

	/* check bad start block number.  */
	if((tskp->gtblock = bp->b_blkno + (drp->drcount>>DEV_BSHIFT)) < 0) {
		TRACE(T_gdisk, ("bad block num for major %d\n",
			ctp->ctmajor));
		bp->b_error = ENXIO;
		bp->b_flags |= B_ERROR;
blkdone:
		dp->b_actf = bp->av_forw;
		viodone( bp, &tskp->gtvio, drp->drniov);
		dp->b_active = 0;
		ctp->ctstate = 0;
		goto loop;
	}

	/* num blocks thru last in Partition = nblks - startbn */
	n = ((ptp->ptflags & PTF_USERALT ? ptp->ptpsize : ptp->ptlsize) 
		- tskp->gtblock) << DEV_BSHIFT;
	if(n > bp->b_resid) {
		tskp->gtnreq = bp->b_resid;
	}
	else if(n > 0) {
		TRACE(T_gdisk, ("Truncating block %d for major %d n = %d\n",
			bp->b_blkno, ctp->ctmajor, n));
		tskp->gtnreq = n;
	}
	else {
		TRACE(T_gdisk, ("Emptying block %d for major %d n = %d\n",
			bp->b_blkno, ctp->ctmajor, n));
		if(n < 0 || (!(bp->b_flags & B_READ)
		   && bp->b_resid == bp->b_bcount)) {
			bp->b_error = ENXIO;
			bp->b_flags |= B_ERROR;
		}
		goto blkdone;
	}

	if(tskp->gtnreq > drp->drqual->dqxfermax)  {
		TRACE(T_gdisk, ("shortening %d to %d",
			tskp->gtnreq,drp->drqual->dqxfermax));
		tskp->gtnreq = drp->drqual->dqxfermax;
	}

	tskp->gtaddr = (caddr_t) -1;	/* set flag for 1.1 scsi vio */
	tskp->gtretproc = gdrw_ret;
	ctp->ctlbn = tskp->gtblock;
	gdcalcblock(ptp, tskp);		/* Decompose for bad blocking */

	if(bp->b_flags & B_READ)
		(*ctp->ctprocs->gpread)(tskp);
	else {
		if(ptp->ptflags & PTF_RONLY) {
			bp->b_error = EROFS;
			bp->b_flags |= B_ERROR;
			goto blkdone;
		}
		(*ctp->ctprocs->gpwrite)(tskp);
	}
}

/*	gdstrategy -- perform buffered I/O.
 *	     A selected buffer is placed on the I/O device queue.  If
 *	no pending requests are being processed, the start routine is
 *	called.
 */
gdstrategy(ctp, bp)

register struct gdctl *ctp;
register struct buf *bp;
{
	register struct iobuf *dp;
	register struct gddrive *drp;
	register struct vio *vp;
	int	drive;
	long    rsave;

	TRACE(T_gdisk, ("in gdstrategy ctp 0x%x bn %d\n", ctp, bp->b_blkno));
	SPL2();
	ASSERT(ctp);
	drive = gddisknum(minor(bp->b_dev));
	for(drp = ctp->ctdrive; drp ; drp = drp->drnxt) {
		if(drp->drnum == drive)
			break;
	}
	ASSERT(drp);
	ASSERT(drp->drpart[gdslicenum(minor(bp->b_dev))]);
	dp = &drp->drtab;
	rsave = bp->b_resid;		/* save b_resid in case of VIO bp */
	if(bp == ctp->ctbp) {	/* Command buffer always last */
		bp->av_forw = NULL;
		if(dp->b_actf == NULL) {
			dp->b_actf = bp;
		} else {
			dp->b_actl->av_forw = bp;
		}
		dp->b_actl = bp;
	} else {
#define	b_cylin b_resid
		bp->b_cylin = bp->b_blkno + 
			drp->drpart[gdslicenum(minor(bp->b_dev))]->ptoffset;
		disksort(dp, bp);
	}
	bp->b_resid = rsave;		/* restore */
	if(!ctp->ctactive) {
		TRACE(T_gdisk, ("call gdstart from gdstrategy\n"));
		gdstart(ctp);
	}
	SPL0();
}
	

/*	queuecmd -- place command buffer in device queue
 */

static
queuecmd(ctp, dev)

struct gdctl *ctp;
dev_t	dev;
{
	register struct buf *bp;

	TRACE(T_gdisk, ("Queue cmd for dev 0x%x\n", dev));
	SPL2();
	bp = ctp->ctbp;
	bp->b_flags &= ~B_DONE;
	bp->b_dev = dev;
	bp->b_blkno = 0x7fffffff;	/* insure sorted at end */
	gdstrategy(ctp, bp);
	while(!(bp->b_flags & B_DONE)) {
		sleep(bp, PRIBIO);
	}
	SPL0();
}
#ifdef notdef
rand(m)
{
	static randx=69;
	randx = ((randx * 1103515245L + 12345)>>16) & 0x7FFF;
	return (randx % m);
}
#endif
