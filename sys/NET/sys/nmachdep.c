#ifndef lint	/* .../sys/NET/sys/nmachdep.c */
#define _AC_NAME nmachdep_c
#define _AC_NO_MAIN "@(#) Copyright (c) 1980-87 The Regents of the University of California, 1985-87 UniSoft Corporation, All Rights Reserved.  {Apple version 2.1 89/10/13 19:46:00}"
#include <apple_notice.h>

#ifdef _AC_HISTORY
  static char *sccsid = "@(#)Copyright Apple Computer 1987\tVersion 2.1 of nmachdep.c on 89/10/13 19:46:00";
#endif		/* _AC_HISTORY */
#endif		/* lint */

#define _AC_MODS
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/param.h>
#include <sys/mbuf.h>
#include <sys/signal.h>
#include <sys/mmu.h>
#include <sys/time.h>
#include <sys/user.h>
#include <sys/errno.h>

uiomove(cp, n, rw, uio)
	register caddr_t cp;
	register int n;
	enum uio_rw rw;
	register struct uio *uio;
{
	register struct iovec *iov;
	u_int cnt;
	int error = 0;

	while (n > 0 && uio->uio_resid) {
		iov = uio->uio_iov;
		cnt = iov->iov_len;
		if (cnt == 0) {
			uio->uio_iov++;
			uio->uio_iovcnt--;
			if (uio->uio_iovcnt < 0)
				panic("uiomove");
			continue;
		}
		if (cnt > n)
			cnt = n;
		switch (uio->uio_seg) {

		case 0:
		case 2:
			if (rw == UIO_READ)
				error = copyout(cp, iov->iov_base, cnt);
			else
				error = copyin(iov->iov_base, cp, cnt);
			if (error)
				return (error);
			break;

		case 1:
			if (rw == UIO_READ)
				bcopy((caddr_t)cp, iov->iov_base, cnt);
			else
				bcopy(iov->iov_base, (caddr_t)cp, cnt);
			break;
		}
		iov->iov_base += cnt;
		iov->iov_len -= cnt;
		uio->uio_resid -= cnt;
		uio->uio_offset += cnt;
		cp += cnt;
		n -= cnt;
	}
	return (error);
}

/*
 * Give next character to user as result of read.
 */
ureadc(c, uio)
        register int c;
        register struct uio *uio;
{
        register struct iovec *iov;
 
again:
        if (uio->uio_iovcnt == 0)
                panic("ureadc");
        iov = uio->uio_iov;
        if (iov->iov_len <= 0 || uio->uio_resid <= 0) {
                uio->uio_iovcnt--;
                uio->uio_iov++;
                goto again;
        }
        switch (uio->uio_seg) {
         
        case 0:
                if (subyte(iov->iov_base, c) < 0)
                        return (EFAULT);
                break;
 
        case 1:  
                *iov->iov_base = c;
                break;
                 
	default:
		pre_panic();
		printf("ureadc:  uio_seg:  0x%x\n", uio->uio_seg);
		panic("ureadc");
                break;
        }
        iov->iov_base++;
        iov->iov_len--;
        uio->uio_resid--;
        uio->uio_offset++;
        return (0);
}

/*
 * Get next character written in by user from uio.
 */
uwritec(uio)
        struct uio *uio;
{
        register struct iovec *iov;
        register int c;

again:
        if (uio->uio_iovcnt <= 0 || uio->uio_resid <= 0)
                panic("uwritec");
        iov = uio->uio_iov;
        if (iov->iov_len == 0) {
                uio->uio_iovcnt--;
                uio->uio_iov++;
                goto again;
        }                
        switch (uio->uio_seg) {
                         
        case 0:          
                c = fubyte(iov->iov_base);
                break;   
                         
        case 1:   
                c = *iov->iov_base & 0377;
                break;
           
	default:
		pre_panic();
		printf("uwritec:  uio_seg:  0x%x\n", uio->uio_seg);
		panic("uwritec");
                break;
        }
        if (c < 0)
                return (-1);
        iov->iov_base++;
        iov->iov_len--;
        uio->uio_resid--;
        uio->uio_offset++;
        return (c & 0377);
}

netdown()
{
	u.u_error	= ENETDOWN;
}
