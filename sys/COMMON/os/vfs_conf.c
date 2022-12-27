#ifndef lint	/* .../sys/COMMON/os/vfs_conf.c */
#define _AC_NAME vfs_conf_c
#define _AC_NO_MAIN "@(#) Copyright (c) 1983-87 Sun Microsystems Inc., All Rights Reserved.  {Apple version 1.7 89/08/25 13:11:18}"
#include <apple_notice.h>

#ifdef _AC_HISTORY
  static char *sccsid = "@(#)Copyright Apple Computer 1989\tVersion 1.7 of vfs_conf.c on 89/08/25 13:11:18";
#endif		/* _AC_HISTORY */
#endif		/* lint */

#define _AC_MODS
/*	@(#)vfs_conf.c 1.1 86/02/03 SMI	*/
/*	@(#)vfs_conf.c	2.1 86/04/15 NFSSRC */

#include "sys/types.h"
#include "sys/errno.h"
#include "sys/vfs.h"
#include "sys/mount.h"

int	novfs_entry();
int	novfs_testfs();
struct vfsops novfs_vfsops = {
	novfs_entry,
	novfs_entry,
	novfs_entry,
	novfs_entry,
	novfs_entry,
	novfs_entry,
	novfs_entry,
	novfs_entry,
	novfs_testfs,
	novfs_entry
};

struct vfsops *vfssw[MOUNT_MAXTYPE + 1] = {
	&novfs_vfsops,		/* MOUNT_SVFS */
	&novfs_vfsops,		/* MOUNT_NFS */
	&novfs_vfsops,		/* MOUNT_PC */
	&novfs_vfsops,		/* MOUNT_UFS */
	&novfs_vfsops,		/* MOUNT_USER1 */
	&novfs_vfsops,		/* MOUNT_USER2 */
	&novfs_vfsops,		/* MOUNT_USER3 */
	&novfs_vfsops,		/* MOUNT_USER4 (== MOUNT_MAXTYPE) */
};

novfs_entry()
{
	return (ENODEV);
}

novfs_testfs()
{
	return (-1);
}
