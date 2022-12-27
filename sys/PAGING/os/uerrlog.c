#ifndef lint	/* .../sys/PAGING/os/uerrlog.c */
#define _AC_NAME uerrlog_c
#define _AC_NO_MAIN "@(#) Copyright (c) 1985-87 UniSoft Corporation, All Rights Reserved.  {Apple version 2.1 89/10/13 12:09:03}"
#include <apple_notice.h>

#ifdef _AC_HISTORY
  static char *sccsid = "@(#)Copyright Apple Computer 1987\tVersion 2.1 of uerrlog.c on 89/10/13 12:09:03";
#endif		/* _AC_HISTORY */
#endif		/* lint */

#define _AC_MODS
/*	@(#)uerrlog.c	UniPlus VVV.2.1.1	*/

#ifdef lint
#include "sys/sysinclude.h"
#else lint
#include "sys/types.h"
#include "sys/mmu.h"
#include "sys/param.h"
#include "sys/page.h"
#include "sys/systm.h"
#include "sys/signal.h"
#include "sys/dir.h"
#include "sys/time.h"
#include "sys/user.h"
#include "sys/buf.h"
#include "sys/conf.h"
#include "sys/map.h"
#include "sys/utsname.h"
#include "sys/elog.h"
#include "sys/erec.h"
#include "sys/err.h"
#include "sys/iobuf.h"
#include "sys/var.h"
#include "sys/sysmacros.h"
#endif lint

typedef int	mem_t;
extern unsigned int malloc();

int	blkacty;

errinit()
{
	register struct err *errp;

	errp = &err;
	if(errp->e_nslot) {
		mapinit(err.e_map, ((err.e_nslot+3)/2));
		mfree(errp->e_map, (mem_t)errp->e_nslot, (mem_t)1);
	}

	errp->e_org = errp->e_ptrs;
	errp->e_nxt = errp->e_ptrs;
}

struct errhdr *
geteslot(size)
{
	register ns, *p;
	register struct errhdr *ep;
	int sps;
	unsigned int n;

	ns = (size+sizeof(struct errhdr)+sizeof(struct errslot)-1)
		/sizeof(struct errslot);
	sps = spl7();
	n = malloc(err.e_map, (mem_t)ns);
	splx(sps);
	if(n == 0)
		return(NULL);
	ep = (struct errhdr *)(&err.e_slot[--n]);
	ns *= sizeof(struct errslot)/sizeof(int);
	p = (int *)ep;
	do {
		*p++ = 0;
	} while(--ns);
	ep->e_len = size + sizeof(struct errhdr);
	return(++ep);
}

freeslot(ep)
register struct errhdr *ep;
{
	register ns, sps;

	ns = (ep->e_len+sizeof(struct errslot)-1)/sizeof(struct errslot);
	sps = spl7();
	mfree(err.e_map, (mem_t)ns,
		(mem_t)((((struct errslot *)ep)-err.e_slot)+1));
	splx(sps);
}

struct errhdr *
geterec()
{
	register sps;
	register struct errhdr *ep;
	register struct err *errp;

	errp = &err;
 	sps = splhi();
	while (*errp->e_org == NULL)
		(void) sleep((caddr_t)&errp->e_org, PZERO+1);
 	ep = *errp->e_org;
 	*errp->e_org++ = NULL;
 	if (errp->e_org >= &errp->e_ptrs[errp->e_nslot])
 		errp->e_org = errp->e_ptrs;
	splx(sps);
	return(ep);
}

puterec(ep, type)
register struct errhdr *ep;
{
	register sps;
	register struct err *errp;

	errp = &err;
	(--ep)->e_type = type;
	ep->e_time = time.tv_sec;
 	sps = splhi();
 	*errp->e_nxt++ = ep;
 	if (errp->e_nxt >= &errp->e_ptrs[errp->e_nslot])
 		errp->e_nxt = errp->e_ptrs;
	splx(sps);
 	wakeup((caddr_t)&errp->e_org);
}

logstart()
{
	register sps;
	register struct estart *ep;
	register struct err *errp;
	extern nodev();

	errp = &err;

 	sps = splhi();
 	for(err.e_org = &err.e_ptrs[err.e_nslot-1];
 					err.e_org >= err.e_ptrs; err.e_org--)
 		if (*err.e_org != NULL) {
 			freeslot(*err.e_org);
 			*err.e_org = NULL;
		}
	errp->e_org = errp->e_ptrs;
	errp->e_nxt = errp->e_ptrs;
	ep = (struct estart *)geteslot(sizeof(struct estart));
	splx(sps);
	if(ep == NULL)
		return;
	ep->e_name = utsname;
	puterec((struct errhdr *)ep, E_GOTS);
}

logtchg(nt)
time_t nt;
{
	register struct etimchg *ep;

	if((ep = (struct etimchg *)geteslot(sizeof(struct etimchg))) != NULL) {
		ep->e_ntime = nt;
		puterec((struct errhdr *)ep,E_TCHG);
	}
}

logstray(addr)
physadr addr;
{
	register struct estray *ep;

	if((ep = (struct estray *)geteslot(sizeof(struct estray))) != NULL) {
		ep->e_saddr = addr;
		ep->e_sbacty = blkacty;
		puterec((struct errhdr *)ep,E_STRAY);
	}
}

logparity(addr)
register paddr_t addr;
{
	register struct eparity *ep;

	if((ep = (struct eparity *)geteslot(sizeof(struct eparity))) != NULL) {
		ep->e_parreg = addr;
		puterec((struct errhdr *)ep,E_PRTY);
	}
}

/*
 *	fmtberr() is used by block device drivers to build up a valid
 *	eblock structure to be sent to the error log.
 *
 *	dp 		the address of the io queue item.
 *	unit		the Physical Device error report field
 *			the Logical Device field is the minor device number
 *	cyl		the cylinder number
 *	trk		the track number
 *	sector		the sector number
 *	regcnt		the number of following register structures
 *	regs		is the address of an array of structures each of which
 *			contain the elements described in struct deverreg.
 */

/* VARARGS7 */
fmtberr(dp, unit, cyl, trk, sector, regcnt, regs)
register struct iobuf *dp;
unsigned unit;
unsigned cyl;
unsigned trk;
unsigned sector;
long regcnt;
struct deverreg *regs;
{
	register struct eblock *ep;
	register struct buf *bp;
	register struct deverreg **dr;
	register char *str1;
	register char *pp;
	register short argc;
	register short nn;
	struct br {	/* just used to generate addr after eblock */
		struct eblock eb;
		char cregs[1];
	};
	struct  iostat  *iosp;
	extern char *longcopy();

	if(dp->io_erec != NULL) {
		dp->io_erec->e_rtry++;
		return;
	}
	/* count the length of the values and strings */
	nn = 0;
	argc = regcnt;
	dr = &regs;
	while (argc--) {
		nn += sizeof((*dr)->draddr);
		nn += sizeof((*dr)->drvalue);
		nn += strlen((*dr)->drname) + 1;	/* + null */
		nn += strlen((*dr)->drbits) + 1;	/* + null */
		nn += (nn & 1);	/* round to even number of bytes */
		dr++;
	}
	iosp = dp->io_stp;
	/* want sizeof eblock to the next long address */
	if((ep = (struct eblock *)
		geteslot(sizeof(struct eblock) + nn)) == NULL) {
		iosp->io_unlog++;
		return;
	}
	nn = major(dp->b_dev);
	bp = dp->b_actf;
	ep->e_dev = makedev(nn,(bp==NULL)?minor(dp->b_dev):minor(bp->b_dev));
	ep->e_bacty = blkacty;
	ep->e_stats.io_ops = iosp->io_ops;
	ep->e_stats.io_misc = iosp->io_misc;
	ep->e_stats.io_unlog = iosp->io_unlog;
	ep->e_pos.unit = unit;
	ep->e_pos.cyl = cyl;
	ep->e_pos.trk = trk;
	ep->e_pos.sector = sector;
	if(bp != NULL) {
		ep->e_bflags = (bp->b_flags&B_READ) ? E_READ : E_WRITE;
		if(bp->b_flags & B_PHYS)
			ep->e_bflags |= E_PHYS;
		ep->e_bnum = bp->b_blkno;
		ep->e_bytes = bp->b_bcount;
		ep->e_memadd = paddr(bp);
	}
	else
		ep->e_bflags = E_NOIO;
	ep->e_nreg = regcnt;
	pp = &(((struct br *)ep)->cregs[0]);
	dr = &regs;
	while(--regcnt >= 0) {
		/* copy out the number values */
		pp = longcopy((char *)&((*dr)->draddr),pp);
		pp = longcopy((char *)&((*dr)->drvalue),pp);
		/* copy out the strings themselves */
		str1 = (*dr)->drname;
		while (*str1) {
			*pp++ = *str1++;
		}
		/* copy the terminating null too */
		*pp++ = '\0';
		str1 = (*dr)->drbits;
		while (*str1) {
			*pp++ = *str1++;
		}
		*pp++ = '\0';
		dr++;
	}
	dp->io_erec = ep;
}

logberr(dp,error)
register struct iobuf *dp;
{
	register struct eblock *ep;

	if((ep = dp->io_erec) == NULL)
		return;
	if(error)
		ep->e_bflags |= E_ERROR;
	puterec((struct errhdr *)ep,E_BLK);
	dp->io_erec = NULL;
}

/* may not be on long address boundary when copied to b2,
   avoiding any alignment problems on some machines? */
char *
longcopy(b1,b2)
register char *b1,*b2;
{
	register int ii;

	for (ii=0; ii < sizeof(long); ii++) {
		*b2++ = *b1++;
	}
	return(b2);
}
