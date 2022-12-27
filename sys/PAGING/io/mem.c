#ifndef lint	/* .../sys/PAGING/io/mem.c */
#define _AC_NAME mem_c
#define _AC_NO_MAIN "@(#) Copyright (c) 1984-85 AT&T-IS, All Rights Reserved.  {Apple version 2.1 89/10/13 14:34:14}"
#include <apple_notice.h>

#ifdef _AC_HISTORY
  static char *sccsid = "@(#)Copyright Apple Computer 1989\tVersion 2.1 of mem.c on 89/10/13 14:34:14";
#endif		/* _AC_HISTORY */
#endif		/* lint */

#define _AC_MODS
/*	@(#)mem.c	UniPlus VVV.2.1.7	*/

/*
 *	mem, kmem and null devices.
 *
 *	Memory special file
 *	minor device 0 is physical memory
 *	minor device 1 is kernel memory
 *	minor device 2 is EOF/NULL
 */

#ifdef lint
#include "sys/sysinclude.h"
#else lint
#include "sys/types.h"
#include "sys/mmu.h"
#include "sys/var.h"
#include "sys/param.h"
#include "sys/sysmacros.h"
#include "sys/dir.h"
#include "sys/errno.h"
#include "sys/signal.h"
#include "sys/time.h"
#include "sys/user.h"
#include "sys/page.h"
#include "sys/systm.h"
#include "sys/buf.h"
#include "sys/uio.h"
#include "sys/conf.h"
#include "sys/pfdat.h"
#include "setjmp.h"
#endif lint

int	mem_no;

mminit()
{
	extern int cdevcnt;
	extern struct cdevsw cdevsw[];
	extern int mmread();
	register struct cdevsw *cdp;
	register i;

	cdp = &cdevsw[0]; 
	for (i = 0; i < cdevcnt; i++, cdp++)
		if (cdp->d_read == mmread) {
			mem_no = i;
			return;
		    }
	panic("mminit");
	/* NOTREACHED */
}

mmread(dev, uio)
	dev_t dev;
	struct uio *uio;
{
	return(mmrw(dev, uio, UIO_READ));
}

mmwrite(dev, uio)
	dev_t dev;
	struct uio *uio;
{
	return(mmrw(dev, uio, UIO_WRITE));
}

mmrw(dev, uio, rw)
	dev_t dev;
	struct uio *uio;
	enum uio_rw rw;
{
	register struct iovec *iov;
	register u_int c, vaddr, po;
	int error = 0;

	while (uio->uio_resid > 0 && error == 0) {
		iov = uio->uio_iov;
		if (iov->iov_len == 0) {
			uio->uio_iov++;
			uio->uio_iovcnt--;
			if (uio->uio_iovcnt < 0)
				panic("mmrw");
			continue;
		}
		switch(minor(dev)) {

/* minor device 0 is physical memory */
		case 0:
		{	register struct rambank *rb;
			extern int numbanks;
			extern struct rambank rambanks[];

			vaddr = btotp(uio->uio_offset);

			for (rb = rambanks; rb < &rambanks[numbanks]; rb++) {
			    if (rb->ram_real) {
				if (uio->uio_offset < rb->ram_end &&
				    uio->uio_offset >= rb->ram_base) {
				    break;
				}
			    }
			}
			if (rb >= &rambanks[numbanks])
			    return(EFAULT);
			po = poff(uio->uio_offset);
			c = min((u_int)(ptob(1) - po), (u_int)iov->iov_len);
			c = min(c, (u_int)(ptob(1) - (poff(iov->iov_base))));

			vaddr = kvalloc(btop(c) + 1, PG_ALL, vaddr);
			error = uiomove(vaddr + po, (int)c, rw, uio);
			kvfree(vaddr, btop(c) + 1, 0);
			continue;     /* uiomove takes care of the bookkeeping */
		}
/* minor device 1 is kernel memory */
		case 1:
			c = iov->iov_len;
			error = uiomove((caddr_t)uio->uio_offset, (int)c, rw, uio);
			continue;     /* uiomove takes care of the bookkeeping */

		case 2:
			if (rw == UIO_READ)
				return (0);
			c = iov->iov_len;
			break;

		default:
			error = ENXIO;
		}
		if (error)
			break;
		iov->iov_base += c;
		iov->iov_len -= c;
		uio->uio_offset += c;
		uio->uio_resid -= c;
	}
	return (error);
}


/*
 * mmioctl
 *	Ioctl routine for the mem device. This routine sets a user error;
 *	placed here so isatty(/dev/null) will return a correct value.
 */
mmioctl()
{
	return(ENOTTY);
}
