#ifndef lint	/* .../sys/PAGING/io/osm.c */
#define _AC_NAME osm_c
#define _AC_NO_MAIN "@(#) Copyright (c) 1984-85 AT&T-IS, All Rights Reserved.  {Apple version 2.1 89/10/13 14:39:18}"
#include <apple_notice.h>

#ifdef _AC_HISTORY
  static char *sccsid = "@(#)Copyright Apple Computer 1989\tVersion 2.1 of osm.c on 89/10/13 14:39:18";
#endif		/* _AC_HISTORY */
#endif		/* lint */

#define _AC_MODS
/*	@(#)osm.c	UniPlus VVV.2.1.1	*/

/*
 *	OSM - operating system messages, allows system printf's to
 *	be read via special file.
 *	minor 0: starts from beginning of buffer and waits for more.
 *	minor 1: starts from beginning of buffer but doesn't wait.
 *	minor 2: starts at current buffer position and waits, this
 *		 device is unique open & suppresses console output.
 */

#ifdef lint
#include "sys/sysinclude.h"
#else lint
#include "sys/param.h"
#include "sys/types.h"
#include "sys/mmu.h"
#include "sys/page.h"
#include "sys/region.h"
#include "sys/dir.h"
#include "sys/errno.h"
#include "sys/systm.h"
#include "sys/signal.h"
#include "sys/time.h"
#include "sys/user.h"
#include "sys/buf.h"
#include "sys/file.h"
#include "sys/proc.h"
#include "sys/uio.h"
#include "sys/sysmacros.h"
#endif lint

extern	char	putbuf[];	/* system putchar circular buffer */
extern	int	putbsz;		/* size of above */
extern	int	putindx;	/* next position for system putchar */
extern	int	putread;	/* next position to read from dev 2 */
extern	int	putwrap;	/* has the buffer wrapped yet ? */
extern	dev_t	locputdev;	/* console device number */
extern	int	kputc_on;	/* whether kputchar actually does anything */

#define	OSM_RWAIT	0x1
#define	OSM_RCOLL	0x2
#define	OSM_UNIQUE	0x4
int osm_state;			/* misc info about the osm device */
struct proc *osm_rsel;		/* proc doing the select - if only one */

/*ARGSUSED*/
osmopen(dev, flag)
register dev_t	dev;
{       register struct file *fp;
	register int fd;

	if ((dev = minor(dev)) > 2)
	        return(ENODEV);

	if ((fd = u.u_rval1) < NOFILE)
	        fp = u.u_ofile[fd];
	else
	        fp = u.u_gofile[fd];

	if (dev == 2) {
		if (osm_state & OSM_UNIQUE)
			return(EINVAL);
		fp->f_offset = putread;
		kputc_on = 0;
		osm_state |= OSM_UNIQUE;
	} else {
		if (putwrap && putindx < putbsz-1)
			fp->f_offset = putindx+1;
		else
		        fp->f_offset = 0;
	}
	return(0);
}

/*ARGSUSED*/
osmclose(dev, flag)
register dev_t dev;
{
	dev = minor(dev);
	if (dev == 2) {
		kputc_on = 1;
		putread = putindx;
		osm_state &= ~OSM_UNIQUE;
	}
}

osmread(dev, uio)
dev_t	dev;
struct uio	*uio;
{
	register int o, c, i;

	dev = minor(dev);
	o = uio->uio_offset % putbsz;
	if (dev == 1 && o == putindx)
		return(0);
	SPLCLK();
	while ((i = putindx) == o) {
		osm_state |= OSM_RWAIT;
		(void)sleep(putbuf, PWAIT);
	}
	SPL0();
	if (o < i)
		c = MIN(i - o, uio->uio_resid);
	else
		c = MIN(putbsz - o, uio->uio_resid);
	if (dev == 2) {
		putread = o + c;
		if (putread >= putbsz)
			putread = 0;
	}
	return(uiomove(&putbuf[o], c, UIO_READ, uio));
}

osmselect(dev, rw)
dev_t dev;
int rw;
{
	int s = splclock();

	dev = minor(dev);
	switch (rw) {
	case FREAD:
		if (dev == 2 && putread != putindx) {
			/* succeed */
			break;
		}
		if (osm_rsel && osm_rsel->p_wchan == (caddr_t)&selwait)
			osm_state |= OSM_RCOLL;
		else
			osm_rsel = u.u_procp;
		splx(s);
		return (0);

	case FWRITE:
		/* always succeeds */
		break;
	}
	splx(s);
	return (1);
}

osmwakeup()
{
	if (osm_rsel) {
		selwakeup(osm_rsel, osm_state & OSM_RCOLL);
		osm_state &= ~OSM_RCOLL;
		osm_rsel = 0;
	}
	if (osm_state & OSM_RWAIT) {
		osm_state &= ~OSM_RWAIT;
		wakeup(putbuf);
	}
}

/* <@(#)osm.c	6.2> */
