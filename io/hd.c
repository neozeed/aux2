#ifndef lint	/* .../sys/psn/io/hd.c */
#define _AC_NAME hd_c
#define _AC_NO_MAIN "@(#) Copyright (c) 1987 Apple Computer Inc., All Rights Reserved.  {Apple version 1.5 88/03/25 17:55:19}"
#include <apple_notice.h>

#ifdef _AC_HISTORY
  static char *sccsid = "@(#)Copyright Apple Computer 1987\tVersion 1.5 of hd.c on 88/03/25 17:55:19";
#endif		/* _AC_HISTORY */
#endif		/* lint */

#define _AC_MODS
#ifdef	HOWFAR
extern	int T_sdisk;
#endif	HOWFAR

/*	@(#)hd.c	1.3 - 6/4/87
 *
 *	hd -- SCSI hard disk interface
 *
 *
 *
 *	    These are the SCSI interface routines to the generic disk driver.
 *	All of these are high level device specific routines.
 */

#include <sys/param.h>
#include <sys/types.h>
#include <sys/debug.h>

#include <sys/sysmacros.h>
#include <sys/utsname.h>
#include <sys/elog.h>
#include <sys/erec.h>
#include <sys/buf.h>
#include <sys/iobuf.h>

#include <sys/vio.h>
#include <sys/gdisk.h>
#include <sys/gdkernel.h>
#include <sys/errno.h>

#define	NUMDISK 8

struct gdctl *activedevs[NUMDISK];

static	int concurctl; /* concurrency control before data structures built */

int hdstrategy();


/*	hddump -- dump memory image to disk.
 */

hddump()

{
	/* Not implemented */
}


/*	hdioctl -- pass control to generic ioctl.
 */

hdioctl(dev, cmd, addr, flag)

dev_t	dev;
int	cmd;
caddr_t	addr;
int	flag;
{
	ASSERT(gdctlnum(major(dev)) < NUMDISK);
	return(gdioctl(activedevs[gdctlnum(major(dev))], dev, cmd, addr, flag));
}


/*	hdopen -- open device.
 *	     Opening the device entails assigning data structures on first
 *	open and jumping to the generic routine.
 */

hdopen(dev, flag)

dev_t	dev;
{
	int	maj;

	maj = major(dev);
	TRACE(T_sdisk, ("in hdopen dev = 0x%x\n", dev));
	if(gdctlnum(maj) >= NUMDISK || gddisknum(minor(dev)) >= GD_MAXDRIVE
		|| gdslicenum(minor(dev)) >= GD_MAXPART) {
		return(ENXIO);
	}
	while(activedevs[gdctlnum(maj)] == (struct gdctl *)NULL) {
		SPL2();
		if(concurctl != 0) {
			concurctl = 2;
			sleep((caddr_t)&concurctl);
			continue;
		}
		concurctl = 1;
		SPL0();
		activedevs[gdctlnum(maj)] = gdctlinit(maj);  
		if(concurctl == 2) {
			wakeup((caddr_t)&concurctl);
		}
		concurctl = 0;
	}
	return(gdopen(activedevs[gdctlnum(maj)], dev, flag));
}


/*	hdread -- physical / character device read.
 */

hdread(dev, uio)

dev_t	dev;
struct	uio *uio;
{
	return(physio(hdstrategy, (struct buf *)NULL, dev, B_READ, uio));
}


/*	hdstrategy -- pass to generic strategy routine.
 */

hdstrategy(bp)

struct buf *bp;
{
	gdstrategy(activedevs[gdctlnum(major(bp->b_dev))], bp);
	return;
}


/*	hdwrite -- physical / character device write.
 */

hdwrite(dev, uio)

dev_t	dev;
struct	uio *uio;
{
	return(physio(hdstrategy, (struct buf *)NULL, dev, B_WRITE, uio));
}


/*ARGSUSED*/
hdclose(dev)

dev_t	dev;
{
	ASSERT(gdctlnum(major(dev)) < NUMDISK);
	return(gdclose(activedevs[gdctlnum(major(dev))], dev));
}


hdprint()

{
	/* XXX */
}
