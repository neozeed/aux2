#ifndef lint	/* .../sys/psn/io/gdisksubr.c */
#define _AC_NAME gdisksubr_c
#define _AC_NO_MAIN "@(#) Copyright (c) 1987 Apple Computer, Inc., All Rights Reserved.  {Apple version 1.16 90/03/10 17:35:49}"
#include <apple_notice.h>

#ifdef _AC_HISTORY
static char *sccsid = "@(#)Copyright Apple Computer 1987\tVersion 1.16 of gdisksubr.c on 90/03/10 17:35:49";
#endif		/* _AC_HISTORY */
#endif		/* lint */

#define _AC_MODS
#ifdef	HOWFAR
extern	int T_sdisk;
extern	int T_gdisk;
#endif	HOWFAR
/*	@(#)gdisksubr.c	1.9 - 7/9/87
 *
 *	gdisksubr -- generic disk driver subroutines.
 *
 *	Copyright 1987 UniSoft Corporation
 *
 *	UniPlus Source Code. This program is proprietary
 *	with Unisoft Corporation and is not to be reproduced
 *	or used in any manner except as authorized in
 *	writing by Unisoft.
 *
 *	     These are generic disk driver routines which are shared
 *	between the standalone and the kernel environments.  None of 
 *	these routines are device specific.
 */	

#include <sys/types.h>
#include <sys/sysmacros.h>
#include <sys/ioctl.h>
#include <sys/ssioctl.h>
#include <sys/param.h>
#include <sys/debug.h>

#ifndef	STANDALONE
#include <sys/utsname.h>
#include <sys/elog.h>
#include <sys/erec.h>
#include <sys/buf.h>
#include <sys/iobuf.h>
#endif	STANDALONE

#include <apple/types.h>
#include <apple/dpme.h>
#include <apple/abm.h>
#include <sys/errno.h>
#include <apple/bzb.h>
#include <sys/gdisk.h>
#include <sys/vio.h>
#include <sys/gdkernel.h>
#include <sys/diskformat.h>
#include <sys/module.h>
#include <sys/var.h>


extern struct genprocs sdgprocs;
struct drqual drqdefault = {
	0,		/* dev specific storage */
	0,		/* flags */
	32*1024,	/* maximum size of transfer */
	-17*4,		/* dqcyl */
	512,		/* block size */
	0x7FFFFFFF,	/* maximum block number */
	};
struct	gdctl *gdctllist;	/* list of devices, for gdrestart() */
char	gd_aux_ptype[33] = GD_AUX_PTYPE;

/*	The following digit is or'd into the controller task state word
 *	to indicate to gdinit_ret what initialization task is in progress.
 */
#define	RET_DRINIT	0x1000	/* currently working on drive init */
#define	RET_PTINIT	0x2000	/* working on partition init */
#define	RET_ALTINIT	0x3000	/* working on alt block init */
#define RET_MKBAD	0x4000	/* marking a bad block */
#define RET_ONESTATE	0x5000	/* The onestate() routine */
#define	RET_MASK	0xF000	/* mask off what we are initializing */

#define	ERRSTATE	0x0800	/* generic error state */

#define	PHYSNAME "Entire Disk"

/*	calc_altmask -- calculate sizeof bad block map array
 *	This is tunable.
 */

#define	calc_altmask(ents)	((ents) < 10 ? 0 : (ents) < 250 ? 7 : \
	(ents) < 500 ? 0x1f : (ents) < 5000 ? 0x3F : 0x7f)

/*	hash -- hash into the alt block array
 *	Note:  The internal operation of the hash algorithm is known in findalt.
 *	Change this, change findalt.
 */

#define	hash(ptp, n)	(((int)(n) >> 4) & ptp->ptbmask)

static gdinit_ret(), gdpartinit(), gdaltinit();
static addbad(), mkbad(), onestate(), sortalt(), sortcmp();
static struct bbent *findalt();

#ifdef	STANDALONE
#define CTBUF	ctbuf
#define	ERRMARK(bp)
#define	IODONE(dp)
#define	gdstart(xxx)	/* NOOP in STANDALONE */
static status_ret;	/* ZZZ what about status ret in ctl struct */
#else	STANDALONE
#define	CTBUF	ctbp->b_un.b_addr;
#define	ERRMARK(bp)	((bp)->b_flags |= B_ERROR)
#define	IODONE(dp)	{ struct buf *bp; \
			bp = (dp)->b_actf; \
			(dp)->b_actf = bp->av_forw; \
			(bp->b_flags & B_VIO) ? viodone(bp,bp->b_resid,0) : iodone(bp); \
			(dp)->b_active = 0; }
#endif	STANDALONE

/*	addbad -- add bad block info to an existing bucket list.
 *	     The bad block map is maintained on disk as sector numbers of the
 *	bad blocks.  The alternate sector may be derived from the position on the
 *	list.  Given a list of disk formatted entries, this routine adds
 *	the entries to the bad block list.
 *	     If the same block appears twice on the list, it will be undefined
 *	which copy will be used.  The normal ioctl mechanism will not allow 
 *	a block to be added twice.
 */


static
addbad(ptp, buf, offset, n)

struct gdpart *ptp;	/* The partition */
long	*buf;		/* the sector numbers */
int	offset;		/* offset of first replacement block index in list */
int	n;		/* number of entries to process */
{
	register struct bbhdr *bhp;
	struct bbent *nbep;
	register daddr_t *lp;
	int	i, mask, thistime;

	for(mask = 0, bhp = ptp->ptbm; mask <= ptp->ptbmask; ++mask, ++bhp) {
		thistime = 0;
		for(i = 0, lp = buf; i < n; ++i, ++lp) {
			if(*lp >= 0 && hash(ptp, *lp) == mask) {
				++thistime;
			}
		}
		if(thistime == 0)
			continue;
		nbep = (struct bbent *)kmem_alloc((unsigned)(bhp->bunum 
				+ thistime) * sizeof(struct bbent));
		if(bhp->bunum > 0) {
			bcopy((caddr_t)bhp->blist, (caddr_t)nbep, 
				(int)(bhp->bunum * sizeof(struct bbent)));
			kmem_free(bhp->blist, bhp->bunum * sizeof(struct bbent));
		}
		bhp->blist = nbep;
		nbep = &bhp->blist[bhp->bunum];
		for(i = 0, lp = buf; i < n; ++i, ++lp) {
			if(*lp >= 0 && hash(ptp, *lp) == mask) {
				nbep->bmfailed = *lp;
				nbep->bmrepl = offset + i;
				bhp->bunum++;
				++nbep;
			}
		}
		sortalt(bhp->blist, bhp->bunum);
	}
#ifdef	LOTS_OF_DEBUG
	for(mask = 0, bhp = ptp->ptbm; mask <= ptp->ptbmask; ++mask, ++bhp) {
		if(bhp->bunum > 0) {
			printf("bucket %d\n", mask);
			for(i = 0, nbep = bhp->blist; i < bhp->bunum; ++i, ++nbep) {
				printf("%d ", nbep->bmfailed);
			}
			printf("\n");
		}
	}
#endif	LOTS_OF_DEBUG
}


/*	findalt -- find an alternate block.
 *	    Hashed lists of blocks are arranged in groups of 16.  
 *	The routine returns any spared block n where 
 *		bn <= n < (bn & ~0xF) + 16
 *	In other words it checks the request block and also checks on to
 *	the end of this group on the hash chain.
 */

static struct bbent *
findalt(ptp, bn)

struct gdpart *ptp;	/* partition containing the block */
register daddr_t bn;	/* target logical block number */
{
	register struct bbhdr *bhp;
	register struct bbent *bep;
	register top, mid, bot;

	if(ptp->ptflags & PTF_NOALT) 
		return(NULL);

	bhp = &(ptp->ptbm[hash(ptp, bn)]);
	if((top = bhp->bunum) == 0)
		return(NULL);
	bot = 0;
	mid = top / 2;
	while(bot != top) {
		bep = &(bhp->blist[mid]);
		if(bep->bmfailed == bn)
			return(bep);
		else if(bep->bmfailed > bn)
			top = mid;
		else
			bot = mid + 1;
		mid = (top + bot) / 2;
	}
	bep = &(bhp->blist[bot]);
	if(bep->bmfailed > bn && bep->bmfailed <= ((bn & ~0xF) + 0xF))
		return(bep);
	return(NULL);
}		


/*	gdaltinit -- initialize alternate block map.
 *	     After the drive has been initialized, and the partition
 *	has been found, this routine is read to process the alt block
 *	map.
 */

static
gdaltinit(ctp)

register struct gdctl *ctp;
{
	register struct gdpart *ptp;
	register struct gddrive *drp;
	register struct gentask *tskp;
	register struct bbhdr *bhp;
	int i;

	if(!(drp = ctp->ctactive) || !(ptp = drp->drpart[drp->drpartnum])) {
		panic("uninitialized drive in gdaltinit");
	}
again:
	drp = ctp->ctactive;
	tskp = ctp->cttaskp;
	ptp = drp->drpart[drp->drpartnum];
	bhp = ptp->ptbm;

#ifdef	STANDALONE
	status_ret = -1;
#endif  STANDALONE
	tskp->gtaddr = ctp->CTBUF;
	tskp->gtretproc = gdinit_ret;
	tskp->gtnreq = DEV_BSIZE;
	TRACE(T_sdisk, ("in gdaltinint ctp->ctstate = 0x%x\n", ctp->ctstate));
	switch(ptp->ptstate) {
	case PTS_NORMAL:
		goto done;
	case PTS_NEEDALT:
		ptp->ptstate = PTS_ALTING;
		ctp->ctstate = RET_ALTINIT;
		/* Fall through to ... */
	case PTS_ALTING:
		switch(ctp->ctstate) {
		case RET_ALTINIT+0:	/* Set up for first read */
			tskp->gtmaj = ctp->ctmajor;
			tskp->gtdnum = drp->drnum;
			tskp->gtqual = drp->drqual;
			tskp->gtblock = ptp->ptastart;
			ctp->ctsector = (roundup(ptp->ptaents, DEV_BSIZE)
					>> DEV_BSHIFT) + ptp->ptastart;
			ptp->ptbm = NULL;
			if(bhp) {		/* Free existing structure */
				for(i = 0; i < ptp->ptbmask + 1; ++i) {
					if(bhp[i].blist)
						kmem_free((caddr_t)bhp[i].blist, 
						    bhp[i].bunum * sizeof(struct bbent));
				}
				kmem_free(bhp, (ptp->ptbmask+1) * sizeof(struct bbhdr));
			}
			ptp->ptbmask = calc_altmask(ptp->ptaents / 4);
			/* Allocate new structure */
			bhp = ptp->ptbm = (struct bbhdr *)kmem_alloc((unsigned)
				(ptp->ptbmask + 1) * sizeof(struct bbhdr));
			bzero((caddr_t)bhp, (ptp->ptbmask + 1) * sizeof(struct bbhdr));
			(*ctp->ctprocs->gpread)(tskp);
			break;
		s1:
		case RET_ALTINIT+1:	/* Back from read */
			i = (tskp->gtblock - ptp->ptastart) * DEV_BSIZE;
			addbad(ptp, (long *)tskp->gtaddr, 
				(int)(i / sizeof(daddr_t) + ptp->ptastart), 
				(int)((ptp->ptaents - i > DEV_BSIZE ? 
				DEV_BSIZE : ptp->ptaents - i)/sizeof(daddr_t)));
			tskp->gtblock++;
			if(ctp->ctsector <= tskp->gtblock) {
				ptp->ptstate = PTS_NORMAL;
				goto done;
			}
			ctp->ctstate = RET_ALTINIT+2;
			/* Fall through to ... */
		case RET_ALTINIT+2:	/* read next sector */
			(*ctp->ctprocs->gpread)(tskp);
			break;
		case RET_ALTINIT+3:	/* finished this read */
			ctp->ctstate = RET_ALTINIT+1;
			goto s1;
		case RET_ALTINIT+ERRSTATE:
			ptp->ptstate = PTS_REINIT;
			ERRMARK(drp->drtab.b_actf);
			IODONE(&drp->drtab);
			goto done;
		default:
			pre_panic();
			printf("state %d\n", ptp->ptstate);
			panic("unrecognized state in gdaltinit\n");
			break;
		}
		break;

	default:
		pre_panic();
		printf("state %d\n", ptp->ptstate);
		panic("unrecognized state in gdaltinit\n");
	}
#ifdef	STANDALONE
	if(status_ret == -1) {
		printf("\nincomplete operation on drive %d\n", drp->drnum);
		panic("error in standalone driver implementation");
	}
	goto again;
#else	STANDALONE
	return;
#endif	STANDALONE
done:
	ctp->ctstate = 0;
	TRACE(T_gdisk, ("Call gdstart() from altinit\n"));
	gdstart(ctp);	/* starting again will process the request */
	return;
}

/*	gdcalcblock -- calculate physical block from logical block.
 *	     This routine will decompose a large I/O into a smaller one
 *	to account for bad blocking.  It is the caller's responsibility
 *	to make more I/O calls until the original request is complete.
 */

gdcalcblock(ptp, tskp)

register struct gdpart *ptp;
register struct gentask *tskp;
{
	register daddr_t bn, lim;
	register struct bbent *bep;

	if(!(ptp->ptflags & PTF_NOALT)) {	/* Calculate Alt block number */
		lim = tskp->gtblock + ((tskp->gtnreq+DEV_BSIZE-1) >> DEV_BSHIFT);
		bn = tskp->gtblock;
		while(bn < lim) {
			if((bep = findalt(ptp, bn))) {
				if(bep->bmfailed == tskp->gtblock) {
					TRACE(T_sdisk, ("in gdcalcblock spare 0x%x with 0x%x\n",
						tskp->gtblock, bep->bmrepl));
					tskp->gtblock = bep->bmrepl;
					tskp->gtnreq = DEV_BSIZE;
					return;
				}
				else if(bep->bmfailed > tskp->gtblock 
				     && bep->bmfailed < lim) {
					TRACE(T_sdisk, ("in gdcalcblock: len 0x%x becomes 0x%x avoiding 0x%x\n",
						tskp->gtnreq, (bep->bmfailed 
						- tskp->gtblock) * DEV_BSIZE,
						bep->bmfailed));
					tskp->gtnreq = (bep->bmfailed - 
						tskp->gtblock) * DEV_BSIZE;
					tskp->gtblock += ptp->ptoffset;
					return;
				}
			}
			bn = (bn & ~0xF) + 0x10;
		}
	}
	tskp->gtblock += ptp->ptoffset;
}


/*	gdcmd -- generic disk command processing.
 *	     Special commands are performed from here.
 */

gdcmd(ctp)

register struct gdctl *ctp;
{
	register struct gdpart *ptp, **ptpp;
	struct drqual *drp;
	int i;

	ctp->ctstate = 0;
	ctp->ctretry = 0;
	ctp->ctretval = 0;
	ptp = ctp->ctactive->drpart[ctp->ctactive->drpartnum];
	TRACE(T_sdisk, ("in gdcmd ctp->ctcmd = 0x%x\n", ctp->ctcmd));
	switch(ctp->ctcmd) {
	case GD_ALTBLK:
		if(ctp->ctarg)	 	/* if true alt block mapping is active */
			ptp->ptflags &= ~(PTF_NOALT | PTF_USERALT);
		else
			ptp->ptflags |= PTF_USERALT;
		ptp->ptstate = PTS_REINIT;
		break;
	case GDC_FINISH:
		/* Mark block as done on exit */
		break;
	case GDC_INIT:
		ctp->ctcmd = GDC_FINISH;
		if(ctp->ctactive->drstate != DRS_NORMAL 
				|| ptp->ptstate != PTS_NORMAL)
			gddriveinit(ctp);
		return;
	case GD_MKBAD:
		mkbad(ctp);
		return;
	case GD_SPARE:
	case GDC_RDDPME:
	case GDC_WRDPME:
	case GDC_RDALTS:
	case UIOCFORMAT:
		onestate(ctp);
		return;
	case GD_SOFTERR:
		drp = ctp->ctactive->drqual;
		if(ctp->ctarg) {
			if(drp->dqflags & DQF_NOCORR)
				break;
			ctp->ctactive->drqual->dqflags |= DQF_NOCORR;
		}
		else {
			if(!(drp->dqflags & DQF_NOCORR))
				break;
			ctp->ctactive->drqual->dqflags &= ~DQF_NOCORR;
		}
		ctp->ctcmd = GDC_FINISH;
		ctp->ctactive->drstate = DRS_REINIT;
		gddriveinit(ctp);
		return;
	case GD_SHUTDOWN:
		if(ctp->ctarg == GD_SHUT_REINIT) {
			TRACE(T_gdisk, ("Shutdown reinit partitions\n"));
			ptpp = ctp->ctactive->drpart;
			for(i = 0; i < GD_MAXPART; ++i, ++ptpp) {
				if(*ptpp)
					(*ptpp)->ptstate = PTS_REINIT;
			}
			break;
		}
		ctp->cttaskp->gtnreq = ctp->ctarg;
		onestate(ctp);
		return;
	case GD_UNSETPNAME:
		ptp->ptflags &= ~PTF_NMMASK;
		ptp->ptflags |= PTF_NMNONE;
		ptp->ptstate = PTS_REINIT;
		break;
	default:
		panic("unknown command in gdcmd");
	}

	
	IODONE(&ctp->ctactive->drtab);
	gdstart(ctp);
}


/*	gdctlinit -- initialize controller structure.
 *	    Called on first open of a controller.  This routine creates
 *	the controller and procs data structures and sets reasonable
 *	defaults in place.
 */

struct gdctl *
gdctlinit(maj)

{
	register struct gdctl *ctp;

	ctp = (struct gdctl *)kmem_alloc((unsigned)sizeof(struct gdctl));
	bzero((caddr_t)ctp, sizeof(struct gdctl));
#ifndef	STANDALONE
	ctp->ctbp = geteblk(SBUFSIZE);
	bremhash(ctp->ctbp);
	ctp->ctbp->b_flags &= ~B_BUSY;
#endif	STANDALONE
	ctp->ctmajor = maj;
	ctp->ctprocs = (struct genprocs *)kmem_alloc((unsigned)sizeof(struct genprocs));
	ctp->ctnextct = gdctllist;
	gdctllist = ctp;
	bcopy(&sdgprocs, ctp->ctprocs, sizeof(struct genprocs));
	ctp->cttaskp = (struct gentask *)kmem_alloc((unsigned)sizeof(struct gentask));
	ctp->cttaskp->gtcp = ctp;
	return(ctp);
}

/*	gderr -- error routine
 *	     This routine formats a system V style error message for
 *	a a device specific interrupt routine.  The message will be 
 *	logged when the interrupt is complete.
 */

gderr(taskp, str, num)

struct	gentask *taskp;
char	*str;
int	num;
{
#ifndef	STANDALONE
	register struct deverreg *ep;
#endif	STANDALONE
	register struct gdctl *ctp;
	struct buf *bp;

	ctp = taskp->gtcp;
	bp = ctp->ctactive->drtab.b_actf;

	ASSERT( bp != (struct buf *)NULL);
#ifndef	STANDALONE
	if((ep = &ctp->cterr[ctp->cterrind++]) < &ctp->cterr[4]) {
		ep->draddr = 0;
		ep->drname = "SCSI";
		ep->drvalue = num;
		ep->drbits = str;
	}
#endif	STANDALONE
	GDPRINTF("Disk %s c%dd%ds%d Error: ",
		(bp->b_flags&B_READ)?"read":"write", ctp->ctmajor&7,
		ctp->ctactive->drnum, ctp->ctactive->drpartnum);
	GDPRINTF(str, num);
	GDPRINTF("\n");
}

/*	gdinit_ret -- interrupt handler for initialization.
 *	     This routine is the generic return point for low level 
 *	functions.  Called from the device specific interrupt
 *	routine, this routine handles any error messages or retries, and
 *	then turns the crank on the task state machine.
 *	The controller state word uses the following convention:
 *	bits 0-11:	state counter.  This is incremented once each time
 *			a successful operation is performed.
 *	bits 12-15:	routine flag.  An identifier for the routine calling.
 *
 *	The error handling has a side effect.  The buffer originally pointed
 *	to by the task structure is reset to the device scratch buffer, and
 *	the contents of the buffer become undefined.  If a mid-level generic 
 *	routine needs extended error capability, this must be reset.
 */

static
gdinit_ret(tskp, ret)

struct gentask *tskp;	/* task pointer being serviced */
{
	register struct gdctl *ctp;
	register struct gddrive *drp;
	int	errstate;

	ctp = tskp->gtcp;
	drp = ctp->ctactive;
	errstate = 0;
	TRACE(T_sdisk, ("gdinit_ret\n"));
	if((ctp->ctstate & ~RET_MASK) == ERRSTATE) {
		/* This code is being executed following a
		 * call to gprecover.  Any error means we
		 * cannot recover.
		 */
		if(ret != GDR_OK) {
		}
		if(drp->drstate == DRS_NORMAL)
			drp->drstate = DRS_REINIT;
#ifdef	STANDALONE
		ret = status_ret;
#endif	STANDALONE
		ctp->ctretry = 0;
	}
	else {
		switch(ret) {
		case GDR_OK:
			ctp->ctretry = 0;
			ctp->ctstate++;	/* advance to next state */
			break;
		case GDR_AGAIN:
			ctp->ctretry++;
			break;
		case GDR_CORR:
			if(!(drp->drqual->dqflags & DQF_NOCORR)) { 
				ctp->ctretry = 0;
				ctp->ctstate++;
				break;
			}
			/* else fall through to ... */
		case GDR_FAILED:
			errstate = GDR_FAILED;
			ctp->ctstate = (ctp->ctstate & RET_MASK) | ERRSTATE;
			break;
		}
	}
	if(ctp->ctretry > 3) {
		ctp->ctretry = 0;
		ctp->ctstate = (ctp->ctstate & RET_MASK) | ERRSTATE;
		errstate = GDR_AGAIN;
	}
#ifndef	STANDALONE
	if(ctp->cterrind) {
		fmtberr(&drp->drtab, drp->drnum, -1, -1, tskp->gtblock,
			ctp->cterrind, &ctp->cterr[0], &ctp->cterr[1],
			&ctp->cterr[2], &ctp->cterr[3]);
		logberr(&drp->drtab, 1);
		ctp->cterrind = 0;
	}
#endif	STANDALONE
	if(errstate) {
		GDPRINTF("generic disk c%dd%ds%d %s: ", gdctlnum(ctp->ctmajor),
			drp->drnum, drp->drpartnum, 
			errstate == GDR_FAILED ? "Fatal Error" : "Retry limit");
		GDPRINTF("Logical block %d, physical block %d\n", ctp->ctlbn, 
			tskp->gtblock);
		(*ctp->ctprocs->gprecover)(tskp);
#ifdef	STANDALONE
		status_ret = ret;
#endif	STANDALONE
		return;
	}
#ifdef	STANDALONE
	status_ret = ret;
#else	STANDALONE
	switch(RET_MASK & ctp->ctstate) {
	case RET_DRINIT:
		gddriveinit(tskp->gtcp);
		break;
	case RET_PTINIT:
		gdpartinit(tskp->gtcp);
		break;
	case RET_ALTINIT:
		gdaltinit(tskp->gtcp);
		break;
	case RET_MKBAD:
		mkbad(tskp->gtcp);
		break;
	case RET_ONESTATE:
		onestate(tskp->gtcp);
		break;
	default:
		panic("unknonwn state in gdinit_ret");
	}
#endif	STANDALONE
}

/*	gddriveinit -- initialize drive.
 *	This routine will initialize the drive, if required, and then
 *	invoke the partition initialization routine.
 */

gddriveinit(ctp)

struct gdctl *ctp;
{
	register struct gentask *tskp;
	register struct gddrive *drp;
	register struct gdpart **ptptr;
	register i;

again:
	TRACE(T_gdisk, ("in gddriveinit ctp->ctstate = 0x%x\n", ctp->ctstate));
 	drp = ctp->ctactive;
	tskp = ctp->cttaskp;
	tskp->gtaddr = ctp->CTBUF;
	tskp->gtcp = ctp;
	tskp->gtretproc = gdinit_ret;
	switch(drp->drstate) {
	case DRS_NOTINIT:
	case DRS_REINIT:
		drp->drstate = DRS_STARTING;
		ctp->ctstate = RET_DRINIT;
		ctp->ctretry = 0;
		ctp->ctlbn = 0;
		tskp->gtmaj = ctp->ctmajor;
		tskp->gtdnum = drp->drnum;
		tskp->gtqual = drp->drqual;
		/* fall through ... */
	case DRS_STARTING:
		switch(ctp->ctstate) {
		case RET_DRINIT:
			for(i = GD_MAXPART, ptptr = drp->drpart; i > 0; --i, ++ptptr) {
				if(*ptptr) 
					(*ptptr)->ptstate = PTS_REINIT;
			}
			(*ctp->ctprocs->gpdriveinit)(tskp);
			break;
		case RET_DRINIT+1:
			drp->drstate = DRS_NORMAL;
			goto done;
		case RET_DRINIT+ERRSTATE:
			drp->drstate = DRS_REINIT;
			ERRMARK(drp->drtab.b_actf);
			IODONE(&drp->drtab);
			goto done;
		default:
			panic("unknown task state in drive init\n");
		}
		break;
	case DRS_NORMAL:
		goto done;
	default:
		panic("unknown drive state in drive init\n");
	}
#ifdef	STANDALONE
	if(status_ret == -1) {
		printf("\nincomplete operation on drive %d\n", drp->drnum);
		panic("error in standalone driver implementation\n");
	}
	goto again;
#else	STANDALONE
	return;
#endif	STANDALONE
done:
	if(drp->drstate == DRS_NORMAL) {
		gdpartinit(ctp);
	}
	else {
		ctp->ctstate = 0;
		TRACE(T_gdisk, ("Call gdstart() from drive init\n"));
		gdstart(ctp);
	}
	return;
}

/*	gdpartinit -- initialize partition for access.
 *	    This routine will initialize a partition.  It should be invoked
 *	only when the drive is properly initailzed.  After the partition is
 *	initialized, alt block map initialization will be started.
 */


static
gdpartinit(ctp)

register struct gdctl *ctp;
{
	register struct gdpart *ptp, **chkptp;
	struct gddrive *drp;
	register struct gentask *tskp;
	register DPME *dpme;
	register struct bzb *bzb;
	int	part1size;
	int	i;
	int	found;
	
	drp = ctp->ctactive;
	ptp = drp->drpart[drp->drpartnum];
	tskp = ctp->cttaskp;
again:
	tskp->gtaddr = ctp->CTBUF;
	dpme = (DPME *) tskp->gtaddr;
	bzb = (struct bzb *)dpme->dpme_boot_args;
	tskp->gtretproc = gdinit_ret;
	TRACE(T_sdisk, ("in gdpartinit ctp->ctstate = 0x%x\n", ctp->ctstate));
	switch(ptp->ptstate) {
	case PTS_REINIT:
		if(drp->drpartnum == GD_PHYSPART) {
			ptp->ptflags &= ~(PTF_TYMASK | PTF_NMMASK);
			ptp->ptflags |= PTF_NOALT | PTF_TYPHYS | PTF_NMDEF;
			/* dqmaxbn (last readable bn) is length-1 */
			ptp->ptpsize = ptp->ptlsize = drp->drqual->dqmaxbn + 1;
			ptp->ptoffset = 0;
			ptp->ptdpme = 0;
			ptp->ptstate = PTS_NORMAL;
			strncpy(ptp->pttype, GD_AUX_PTYPE, sizeof(ptp->pttype));
			strncpy(ptp->ptname, PHYSNAME, sizeof(ptp->ptname));
			goto done;
		}

		if(drp->drpartnum == GD_HFSPART) {
			ptp->ptflags &= ~(PTF_TYMASK | PTF_NMMASK);
			ptp->ptflags |= PTF_NOALT | PTF_NMUSER;
			ptp->ptpsize = 0;
			ptp->ptoffset = 0;
			ptp->ptdpme = 0;
			strncpy(ptp->pttype, GD_HFS_PTYPE, sizeof(ptp->pttype));
			ptp->ptname[0] = '\0';
		}

		if((ptp->ptflags & PTF_NMMASK) == PTF_NMNONE) {
			strncpy(ptp->pttype, GD_AUX_PTYPE, sizeof(ptp->pttype));
		}
		tskp->gtmaj = ctp->ctmajor;
		tskp->gtdnum = drp->drnum;
		tskp->gtqual = drp->drqual;
		ptp->ptflags &= ~PTF_ESCH0;
		ptp->ptstate = PTS_STARTING;
		ctp->ctstate = RET_PTINIT;
		ctp->ctretry = 0;
		/* fall through ... */
	case PTS_STARTING:
		switch(ctp->ctstate) {
		case RET_PTINIT+0:	/* get 1st block */
state0:
			tskp->gtnreq = DEV_BSIZE; /* size of apple sector */
			ptp->ptdpme = tskp->gtblock = 1;
			ctp->ctlbn = tskp->gtblock;
			(*ctp->ctprocs->gpread)(tskp);
			break;
		case RET_PTINIT+1:	/* first block read completed */
			if(dpme->dpme_signature != DPME_SIGNATURE) {
				TRACE(T_gdisk, ("No signature on dpme 1\n"));
				if((ptp->ptflags & PTF_NMMASK) == PTF_NMUSER)
					goto errs;
				ctp->ctstate = RET_PTINIT+4;
				goto state4;
			}
			/* Assert: dpme is valid and legal */
			ctp->ctsector = dpme->dpme_map_entries;
			ctp->ctstate = RET_PTINIT+3;
			/* Fall through to ... */
		case RET_PTINIT+3:	/* Finished read on a search block */
			found = 0;
			if(dpme->dpme_signature == DPME_SIGNATURE 
			   && dpme->dpme_valid && dpme->dpme_allocated
			   && strncmp(dpme->dpme_dpident.dpitype, 
			    ptp->pttype, sizeof(ptp->pttype)) == 0) {
				if(((ptp->ptflags & PTF_NMMASK) == PTF_NMNONE
				   || (ptp->ptflags & PTF_NMMASK) == PTF_NMDEF)
				   && bzb->bzb_magic == BZBMAGIC) { 
					if (bzb->bzb_slice != 0) {
					    if (drp->drpartnum
					      == bzb->bzb_slice-1) {
						found = PTF_NMDEF;
					    } 
					} else {
					    switch(drp->drpartnum) { 
					    case ROOT_PART:
						if(bzb->bzb_type == FST
						   && bzb->bzb_root
						   && (bzb->bzb_cluster == CLUSTER
						   || (bzb->bzb_cluster == 0
						   && (ptp->ptflags & PTF_ESCH0))))
							found = PTF_NMDEF;
						break;
					    case USR_PART: 
						if(bzb->bzb_type == FST 
						   && !bzb->bzb_root
						   && bzb->bzb_usr
						   && (bzb->bzb_cluster == CLUSTER
						   || (bzb->bzb_cluster == 0
						   && (ptp->ptflags & PTF_ESCH0))))
							found = PTF_NMDEF;
						break;
					    case SWAP_PART:
						if(bzb->bzb_type == FSTSFS)
							found = PTF_NMDEF;
						break;
					    }
					}
				}
				else if((ptp->ptflags & PTF_NMMASK) == PTF_NMUSER &&
				  strncmp(dpme->dpme_dpident.dpiname, 
				  ptp->ptname, strlen(ptp->ptname)) == 0)
					found = PTF_NMUSER;
			}
			if(found) {	/* check already mounted */
				for(chkptp = drp->drpart, i = 0; i < GD_MAXPART;
						++i, ++chkptp) {
					if(*chkptp && 
					  *chkptp != ptp &&
					  ((*chkptp)->ptflags & PTF_NMMASK)
					  != PTF_NMNONE &&
					  (*chkptp)->ptdpme == tskp->gtblock) {
						found = 0;
						if((ptp->ptflags & PTF_NMMASK)
						   == PTF_NMUSER) {
							ctp->ctretval = EEXIST;
							goto errs;
						}
						break;
					}
				}
			}
			if(found) {
				ptp->ptflags &= ~(PTF_NMMASK | 
						PTF_TYMASK | PTF_RONLY);
				ptp->ptflags |= found | PTF_TYDPME;
				ptp->ptoffset = dpme->dpme_pblock_start;
				ptp->ptpsize = dpme->dpme_pblocks;
				ptp->ptlsize = dpme->dpme_lblocks;
				/* mark CDROM so GD_WRDPME writes ignored */
				if(!dpme->dpme_writable ||
					(ctp->ctflags & CTF_CDROM))
					ptp->ptflags |= PTF_RONLY;
				TRACE(T_gdisk, 
				("assign slice %d from dpme at %d len %d\n",
				drp->drpartnum, ptp->ptoffset, ptp->ptlsize));
				if(dpme->dpme_lblock_start) { 
					GDPRINTF("non-zero dpme logical block for partition %d\n", drp->drpartnum); 
					GDPRINTF("disk formatting error\n");
					goto errs;
				}
				if((ptp->ptflags & PTF_NMMASK) == PTF_NMDEF) {
					strncpy(ptp->ptname, 
					   dpme->dpme_dpident.dpiname, 
					   sizeof(dpme->dpme_dpident.dpiname));
				}
				if(bzb->bzb_magic == BZBMAGIC 
				   && bzb->bzb_abm.abm_start != NO_ALTMAP
				   && bzb->bzb_abm.abm_start >= dpme->dpme_lblocks
				   && !(ptp->ptflags & PTF_USERALT)) {
					ptp->ptflags &= ~PTF_NOALT;
					ptp->ptastart = bzb->bzb_abm.abm_start 
						+ ptp->ptoffset;
					ptp->ptasize = bzb->bzb_abm.abm_size;
				/* If the alt block map does not fit, we
				 * truncate it.
				 */
					if(bzb->bzb_abm.abm_start + 
					    (ptp->ptasize/sizeof(daddr_t))
					    > ptp->ptpsize) {
						ptp->ptasize = (ptp->ptpsize - 
							bzb->bzb_abm.abm_start) 
								* sizeof(long);
					}
					ptp->ptaents = bzb->bzb_abm.abm_ents;
					if(ptp->ptaents > ptp->ptasize)
						ptp->ptaents = ptp->ptasize;
					/* We set up alt block capacity
					 * regardless of any entries in the
					 * map */
					ptp->ptstate = PTS_NEEDALT;
					goto done;
				}
				else {
					ptp->ptflags |= PTF_NOALT;
					ptp->ptstate = PTS_NORMAL;
					goto done;
				}
			}
			ctp->ctstate = RET_PTINIT+2;
			/* goto state2; Fall through to ... */ 
		case RET_PTINIT+2:	/* Get next search block */
			if(++(ptp->ptdpme) > ctp->ctsector) {
				if(((ptp->ptflags & PTF_NMMASK) == PTF_NMNONE 
				    || (ptp->ptflags & PTF_NMMASK) == PTF_NMDEF)
				    && !(ptp->ptflags & PTF_ESCH0)) {
					ptp->ptflags |= PTF_ESCH0;
					ctp->ctstate = RET_PTINIT+0;
					goto state0;
				}
				goto errs;
			}
			tskp->gtblock = ptp->ptdpme;
			ctp->ctlbn = tskp->gtblock;
			tskp->gtnreq = DEV_BSIZE;
			(*ctp->ctprocs->gpread)(tskp);
			break;
		case RET_PTINIT+4:	/* Assign default partition sizes */
state4:
		/* If the device has no dpme's we assign it two partitions
		 * Partition two is one quarter of the disk, but not greater
		 * than ten meg.  Partition zero is the remainder of the
		 * disk, minus some room for front of disk partitioning.
		 */
			ptp->ptflags &= ~(PTF_NMMASK | PTF_TYMASK);
			ptp->ptflags |= (PTF_NMDEF | PTF_TYDEF | PTF_NOALT);
			ptp->ptdpme = 0;
			part1size = (drp->drqual->dqmaxbn / 4);
			if(part1size > (10 * 1024 * 1024 / DEV_BSIZE))
				part1size = 10 * 1024 * 1024 / DEV_BSIZE;
			switch(drp->drpartnum) {
			case ROOT_PART:		/* root file system */
				ptp->ptoffset = 204;
				ptp->ptpsize = ptp->ptlsize = 
					drp->drqual->dqmaxbn - part1size - 204;
				TRACE(T_gdisk, 
				("assign default root at %d len %d\n",
					ptp->ptoffset, ptp->ptpsize));
				break;
			case SWAP_PART:
				ptp->ptoffset = drp->drqual->dqmaxbn - part1size;
				ptp->ptpsize = ptp->ptlsize = part1size;
				TRACE(T_gdisk, 
				("assign default swap at %d len %d\n",
					ptp->ptoffset, ptp->ptpsize));
				break;
			default:
				TRACE(T_gdisk, ("gdpartinit: No default size\n"));
				goto errs;
			}
			ptp->ptstate = PTS_NORMAL;
			goto done;
		case RET_PTINIT+ERRSTATE:	/* Operation failed */
errs:
			/* must be careful to not dismantle too much here,
			 * Don't want to break a recoverable drive.
			 */
			ptp->ptstate = PTS_REINIT;
			ERRMARK(drp->drtab.b_actf);
			IODONE(&drp->drtab);
			TRACE(T_sdisk, ("gdpartinit: Error\n"));
			goto done;
		default:
			panic("unknown task state in gdpartinit");
		}
		break;
	case PTS_NORMAL:
		goto done;
	default:
		panic("unknown partition state in gdpartinit");
	}
#ifdef	STANDALONE
	if(status_ret == -1) {
		printf("\nincomplete operation on drive %d\n", drp->drnum);
		panic("error in standalone driver implementation");
	}
	goto again;
#else	STANDALONE
	return;
#endif	STANDALONE
done:
	if(ptp->ptstate == PTS_NEEDALT) {
		gdaltinit(ctp);
	}
	else {
		ctp->ctstate = 0;
		TRACE(T_gdisk, ("Call gdstart() from partinit\n"));
		gdstart(ctp);
	}
	return;

}


/*	mkbad -- handle alt blocking.
 *	     There are two types of bad blocking.  Software alt blocking is
 *	handled by this routine.  The software bad blocking relies on a 
 *	list of bad blocks placed on the disk.  This routine does not create
 *	the alt block list, but will add to it.
 */

static
mkbad(ctp)

struct	gdctl *ctp;
{
	struct gddrive *drp;
	register struct gdpart *ptp;
	register struct gentask *tskp;
	struct bbent *bep;
	daddr_t	*lp;

	drp = ctp->ctactive;
	ptp = drp->drpart[drp->drpartnum];
	tskp = ctp->cttaskp;

again:
	tskp->gtaddr = ctp->CTBUF;
	tskp->gtretproc = gdinit_ret;
	tskp->gtnreq = DEV_BSIZE;
#ifdef	STANDALONE
	status_ret = - 1;
#endif	STANDALONE
	TRACE(T_sdisk, ("in mkbad ctp->ctstate = 0x%x\n", ctp->ctstate));
	switch(ctp->ctcmd) {
	case GD_MKBAD:
		switch(ctp->ctstate) {
		case 0:	/* called from gdcmd */
			if((bep = findalt(ptp, ctp->ctarg)) != NULL &&
				bep->bmfailed == ctp->ctarg) {
				ctp->ctretval = EIO; 	/* already alt blocked */
				goto done;
			}
			ctp->ctstate = RET_MKBAD+0;
			tskp->gtblock = ptp->ptastart + (ptp->ptaents>>DEV_BSHIFT);
			tskp->gtmaj = ctp->ctmajor;
			tskp->gtdnum = drp->drnum;
			tskp->gtqual = drp->drqual;
			ctp->ctlbn = tskp->gtblock;
			/* Fall through to ... */
		case RET_MKBAD+0:	/* look for avail bad block */
state0:
			(*ctp->ctprocs->gpread)(tskp);
			break;
		case RET_MKBAD+1:	/* check block just read in */
			lp = (daddr_t *)&(tskp->gtaddr[ptp->ptaents & (DEV_BSIZE-1)]);
			while(lp < (daddr_t *)&(tskp->gtaddr[DEV_BSIZE])) {
				if((ptp->ptaents += sizeof(daddr_t)) > ptp->ptasize) {
					ctp->ctretval = ENOSPC;
					goto done;
				}
				if(*lp == ABM_FREE || *lp >= 0) {
					*lp = ctp->ctarg;
					ctp->ctstate = RET_MKBAD+2;
					goto state2;
				}
				++lp;
			}
			tskp->gtblock++;
			ctp->ctlbn = tskp->gtblock;
			ctp->ctstate = RET_MKBAD+0;
			goto state0;
		case RET_MKBAD+2:	/* write back alt block map */
state2:
			(*ctp->ctprocs->gpwrite)(tskp);
			break;
		case RET_MKBAD+3:	/* update bzb in dpme */
			tskp->gtblock = ptp->ptdpme;
			ctp->ctlbn = tskp->gtblock;
			(*ctp->ctprocs->gpread)(tskp);
			break;
		case RET_MKBAD+4:	/* Read on dpme completed */
			tskp->gtblock = ptp->ptdpme;
			ctp->ctlbn = tskp->gtblock;
			((struct bzb *)((DPME *)tskp->gtaddr)->dpme_boot_args)->bzb_abm.abm_ents 
				= ptp->ptaents;
			(*ctp->ctprocs->gpwrite)(tskp);
			break;
		case RET_MKBAD+5:
			addbad(ptp, &ctp->ctarg, (int)(ptp->ptastart + 
				ptp->ptaents / sizeof(daddr_t) - 1), 1);
			goto done;
		case RET_MKBAD+ERRSTATE:	/* Operation failed */
			ERRMARK(drp->drtab.b_actf);
			ctp->ctretval = EIO;
			goto done;
		}
		break;
	default:
		panic("software error unknown command in mkbad()");
	}
#ifdef	STANDALONE
	if(status_ret == -1)
		panic("Error in stanalone driver mkbad implementation");
	goto again;
done:
	return;
#else	STANDALONE
	return;
done:
	IODONE(&drp->drtab);
	ctp->ctstate = 0;
	TRACE(T_gdisk, ("Call gdstart() from mkbad\n"));
	gdstart(ctp);
	return;
#endif	STANDALONE

}


/*	onestate -- perform one state activity.
 *	     This routine performs activities which require the common 
 *	pattern of initialization, I/O, error recovery.   For a set of 
 *	defined commands, it passes the command along to the low 
 *	level I/O routines.
 */

static
onestate(ctp)

struct gdctl *ctp;
{
	register struct gdpart *ptp;
	struct gddrive *drp;
	register struct gentask *tskp;

	drp = ctp->ctactive;
	ptp = drp->drpart[drp->drpartnum];
	tskp = ctp->cttaskp;

again:
	TRACE(T_sdisk, ("in onestate ctp->ctstate = 0x%x\n", ctp->ctstate));
	switch(ctp->ctstate) {
	case 0:
		tskp->gtretproc = gdinit_ret;
		tskp->gtaddr = ctp->CTBUF;
		tskp->gtmaj = ctp->ctmajor;
		tskp->gtdnum = drp->drnum;
		tskp->gtqual = drp->drqual;
		ctp->ctlbn = tskp->gtblock;
		ctp->ctstate = RET_ONESTATE+0;
		/* Fall through to ... */
	case RET_ONESTATE+0:
		switch(ctp->ctcmd) {
		case GDC_RDDPME:
			tskp->gtblock = ptp->ptdpme;
			tskp->gtnreq = DEV_BSIZE;
			(*ctp->ctprocs->gpread)(tskp);
			break;
		case GDC_WRDPME:
			tskp->gtblock = ptp->ptdpme;
/*printf("onestate gtblock/WRDPME=%d\n", tskp->gtblock);*/
/*printf("onestate ptflags=0x%x, ro=0x%x\n", ptp->ptflags,ptp->ptflags&PTF_RONLY);*/
			/* ignore DPME writes if CDROM */
			if( ctp->ctflags & CTF_CDROM )
				goto done;	/* can't write anyway */
/*printf("onestate ctflags=0x%x, ro=0x%x\n", ctp->ctflags,ctp->ctflags&CTF_CDROM);*/
			tskp->gtnreq = DEV_BSIZE;
			(*ctp->ctprocs->gpwrite)(tskp);
			break;
		case GDC_RDALTS:
			tskp->gtblock = ctp->ctarg;
			tskp->gtnreq = DEV_BSIZE;
			(*ctp->ctprocs->gpread)(tskp);
			break;
		case GD_SPARE:
			tskp->gtblock = ctp->ctarg + ptp->ptoffset;
			(*ctp->ctprocs->gpbadblock)(tskp);
			break;
		case GD_SHUTDOWN:
			tskp->gtnreq = ctp->ctarg;
			(*ctp->ctprocs->gpshutdown)(tskp);
			break;
		case UIOCFORMAT:
			tskp->gtnreq = ctp->ctarg;
			(*ctp->ctprocs->gpformat)(tskp);
			break;
		default:
			ctp->ctretval = EINVAL;
			goto done;
		}
		break;
	case RET_ONESTATE+1:
		if(ctp->ctcmd == GD_SHUTDOWN || ctp->ctcmd == UIOCFORMAT) {
			ctp->ctactive->drstate = DRS_REINIT;
		}
		goto done;
	case RET_ONESTATE+ERRSTATE:
		ctp->ctretval = EIO;
		ERRMARK(drp->drtab.b_actf);
		goto done;
	}
#ifdef	STANDALONE
	if(status_ret == -1)
		panic("Error in stanalone driver onestate() implementation");
	goto again;
done:
	return;
#else	STANDALONE
	return;
done:
	IODONE(&drp->drtab);
	ctp->ctstate = 0;
	gdstart(ctp);
	return;
#endif	STANDALONE
}


static
sortalt(bep, n)

struct bbent *bep;
{
	qsort(bep, (unsigned)n, sizeof(struct bbent), sortcmp);
}

static
sortcmp(b1, b2)

struct	bbent *b1, *b2;
{
	return(b1->bmfailed - b2->bmfailed);
}
