#ifndef lint	/* .../sys/COMMON/io/err.c */
#define _AC_NAME err_c
#define _AC_NO_MAIN "@(#) Copyright (c) 1985-87 UniSoft Corporation, All Rights Reserved.  {Apple version 2.1 89/10/13 18:38:46}"
#include <apple_notice.h>

#ifdef _AC_HISTORY
  static char *sccsid = "@(#)Copyright Apple Computer 1987\tVersion 2.1 of err.c on 89/10/13 18:38:46";
#endif		/* _AC_HISTORY */
#endif		/* lint */

#define _AC_MODS
/*	@(#)err.c	UniPlus 2.1.3	*/

#include "sys/param.h"
#include "sys/types.h"
#include "sys/sysmacros.h"
#include "sys/buf.h"
#include "sys/signal.h"
#include "sys/mmu.h"
#include "sys/time.h"
#include "sys/user.h"
#include "sys/errno.h"
#include "sys/file.h"
#include "sys/utsname.h"
#include "sys/uio.h"
#include "sys/elog.h"
#include "sys/erec.h"

static	short	logging;

erropen(dev, flg)
dev_t	dev;
{
	if (logging)
		return(EBUSY);
	if ((flg & FWRITE) || (minor(dev) != 0))
		return(ENXIO);
	if (suser()) {
		logstart();
		logging++;
		return(0);
	}
	else
		return(EPERM);
}

/* ARGSUSED */
errclose(dev, flg)
dev_t	dev;
{
	logging = 0;
}

/* ARGSUSED */
errread(dev, uio)
dev_t	dev;
register struct uio *uio;
{
	register struct errhdr *eup;
	int error, n;
	struct errhdr	*geterec();

	if (logging == 0)
		return(0);
	/*
	 *	This could all be done better, but ...
	 *	Note that we disallow gathered reads, and that we must
	 *	free the error record even if we cannot successfully
	 *	copy the data to user space.
	 */
	if (uio->uio_iovcnt != 1)
		return(EINVAL);
	eup = geterec();
	n = MIN((uint) eup->e_len, (uint) uio->uio_iov->iov_len);
	error = uiomove((caddr_t) eup, n, UIO_READ, uio);
	freeslot(eup);
	return(error);
}
