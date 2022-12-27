#ifndef lint	/* .../sys/COMMON/io/clone.c */
#define _AC_NAME clone_c
#define _AC_NO_MAIN "@(#) Copyright (c) 1985-87 UniSoft Corporation, All Rights Reserved.  {Apple version 2.1 89/10/13 18:38:39}"
#include <apple_notice.h>

#ifdef _AC_HISTORY
  static char *sccsid = "@(#)Copyright Apple Computer 1987\tVersion 2.1 of clone.c on 89/10/13 18:38:39";
#endif		/* _AC_HISTORY */
#endif		/* lint */

#define _AC_MODS
/*
 *	Streams NFS clone driver
 *
 *	Copyright 1986 Unisoft Corporation of Berkeley CA
 *
 *
 *	UniPlus Source Code. This program is proprietary
 *	with Unisoft Corporation and is not to be reproduced
 *	or used in any manner except as authorized in
 *	writing by Unisoft.
 *
 */
#include <sys/types.h>
#include <sys/param.h>
#include <sys/sysmacros.h>
#include <sys/file.h>
#include <sys/time.h>
#include <sys/vnode.h>
#include "svfs/inode.h"
#include "sys/dir.h"
#include "sys/signal.h"
#include <sys/mmu.h>
#include <sys/page.h>
#include <sys/user.h>
#include <sys/stream.h>
#include <sys/conf.h>
#include <sys/errno.h>
#include <sys/debug.h>
#include <sys/systm.h>
#include <sys/vfs.h>
#include <sys/buf.h>
#include <sys/mount.h>

static int clone_open();
extern nulldev();
extern struct vnode *devtovp();

static struct 	module_info clone_minfo = { 0, "clone", 0, 256, 256, 256, NULL };
static struct	qinit clonerdata = { nulldev, NULL, clone_open, nulldev,
			nulldev, &clone_minfo, NULL};
static struct	qinit clonewdata = { nulldev, NULL, clone_open, nulldev, 
			nulldev, &clone_minfo, NULL};
struct	streamtab cloneinfo = {&clonerdata, &clonewdata, NULL, NULL};

static int
clone_open(q, dev, flag, sflag, err, ndev)
register queue_t *q;
int *err;
dev_t dev, *ndev;
{
	register struct streamtab *qinfo;
	struct stdata *stp;
	int mn;

	if (sflag != DEVOPEN || (dev = minor(dev)) >= cdevcnt || 
			(qinfo = cdevsw[dev].d_str) == NULL) {
		*err = ENXIO;
		return(OPENFAIL);
	}
	stp = (struct stdata *)(q->q_next->q_ptr);
	q->q_qinfo = qinfo->st_rdinit;
	q->q_minpsz = q->q_qinfo->qi_minfo->mi_minpsz;
	q->q_maxpsz = q->q_qinfo->qi_minfo->mi_maxpsz;
	q->q_hiwat = q->q_qinfo->qi_minfo->mi_hiwat;
	q->q_lowat = q->q_qinfo->qi_minfo->mi_lowat;
	WR(q)->q_qinfo = qinfo->st_wrinit;
	WR(q)->q_minpsz = WR(q)->q_qinfo->qi_minfo->mi_minpsz;
	WR(q)->q_maxpsz = WR(q)->q_qinfo->qi_minfo->mi_maxpsz;
	WR(q)->q_hiwat = WR(q)->q_qinfo->qi_minfo->mi_hiwat;
	WR(q)->q_lowat = WR(q)->q_qinfo->qi_minfo->mi_lowat;
	if ((mn = (*q->q_qinfo->qi_qopen)(q,
			makedev(dev, 0), 
			flag,
			CLONEOPEN,
			err)) == OPENFAIL) {
		if (!*err)
			*err = ENXIO;
		return(OPENFAIL);
	}
	*ndev = makedev(dev, mn);
	return(0);
}

