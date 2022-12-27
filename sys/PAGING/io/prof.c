#ifndef lint	/* .../sys/PAGING/io/prof.c */
#define _AC_NAME prof_c
#define _AC_NO_MAIN "@(#) Copyright (c) 1984-85 AT&T-IS, All Rights Reserved.  {Apple version 2.1 89/10/13 14:38:27}"
#include <apple_notice.h>

#ifdef _AC_HISTORY
  static char *sccsid = "@(#)Copyright Apple Computer 1987\tVersion 2.1 of prof.c on 89/10/13 14:38:27";
#endif		/* _AC_HISTORY */
#endif		/* lint */

#define _AC_MODS
/*	@(#)prof.c	UniPlus VVV.2.1.1	*/

/*
 *	UNIX Operating System Profiler
 *
 *	Sorted Kernel text addresses are written into driver.  At each
 *	clock interrupt a binary search locates the counter for the
 *	interval containing the captured PC and increments it.
 *	The last counter is used to hold the User mode counts.
 */

#ifdef lint
#include "sys/sysinclude.h"
#else lint
#include "sys/param.h"
#include "sys/sysmacros.h"
#include "sys/psl.h"
#include "sys/types.h"
#include "sys/mmu.h"
#include "sys/dir.h"
#include "sys/errno.h"
#include "sys/signal.h"
#include "sys/time.h"
#include "sys/user.h"
#include "sys/page.h"
#include "sys/buf.h"
#include "sys/uio.h"
#include "sys/ioctl.h"
#endif lint

#  ifdef PRF
#     define PRFMAX  2048	/* maximum number of text addresses */
#  else PRF
#     define PRFMAX  4		/* maximum number of text addresses */
#  endif PRF
# define PRF_ON    1		/* profiler collecting samples */
# define PRF_VAL   2		/* profiler contains valid text symbols */
# define BPW	   4		/* bytes per word */
# define L2BPW	   2		/* log2(BPW) */

unsigned  prfstat;		/* state of profiler */
unsigned  prfmax;		/* number of loaded text symbols */
unsigned  prfctr[PRFMAX + 1];	/* counters for symbols; last used for User */
unsigned  prfsym[PRFMAX];	/* text symbols */

prfread(dev, uio)
dev_t	dev;
struct uio	*uio;
{
	int error;

	if((prfstat & PRF_VAL) == 0)
		return (ENXIO);
	if (uio->uio_iovcnt > 1)
		return (EINVAL);
	error = uiomove((caddr_t) prfsym,
		(int)min(uio->uio_resid, prfmax * BPW),
		UIO_READ, uio);
	if (!error)
		error = uiomove((caddr_t) prfctr,
			(int)min(uio->uio_resid, (prfmax + 1) * BPW),
			UIO_READ, uio);
	return (error);
}

prfwrite(dev, uio)
dev_t	dev;
struct uio	*uio;
{
	register  unsigned  *ip;
	register int count = uio->uio_resid;
	int error = 0;

	if (uio->uio_iovcnt > 1)
		return (EINVAL);
	else if (count > sizeof prfsym)
		return (ENOSPC);
	else if (count & (BPW - 1) || count < 3 * BPW)
		return (E2BIG);
	else if (prfstat & PRF_ON)
		return (EBUSY);
	for (ip = prfctr; ip != &prfctr[PRFMAX + 1]; )
		*ip++ = 0;
	prfmax = count >> L2BPW;
	uiomove((caddr_t) prfsym, (int)count, UIO_WRITE, uio);
	for (ip = &prfsym[1]; ip != &prfsym[prfmax]; ip++)
		if (*ip < ip[-1]) {
			error = EINVAL;
			break;
		}
	if (error)
		prfstat = 0;
	else
		prfstat = PRF_VAL;
	return (error);
}

/* ARGSUSED */
prfioctl(dev, cmd, data, mode)
dev_t	dev;
caddr_t	data;
{
	int error = 0;

	switch (cmd) {
		case _IO(0, 1):
			u.u_r.r_reg.r_val1 = prfstat;
			break;
		case _IO(0, 2):
			u.u_r.r_reg.r_val1 = prfmax;
			break;
		case _IO(0, 3):
			if (prfstat & PRF_VAL) {
				prfstat = PRF_VAL | ((*(int *) data) & PRF_ON);
				break;
			}
			/* FALL THROUGH */
		default:
			error = EINVAL;
	}
	return (error);
}

prfintr(pc, ps)
	register  unsigned  pc;
{
	register  int  h, l, m;

	if (usermode(ps))
		prfctr[prfmax]++;
	else {
		l = 0;
		h = prfmax;
		while((m = (l + h) / 2) != l)
			if(pc >= prfsym[m])
				l = m;
			else
				h = m;
		prfctr[m]++;
	}
}

/* <@(#)prof.c	6.1> */
