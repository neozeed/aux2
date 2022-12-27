#ifndef lint	/* .../sys/COMMON/os/subr.c */
#define _AC_NAME subr_c
#define _AC_NO_MAIN "@(#) Copyright (c) 1987 Apple Computer, Inc., 1985-87 UniSoft Corporation, All Rights Reserved.  {Apple version 2.1 89/10/13 19:26:28}"
#include <apple_notice.h>

#ifdef _AC_HISTORY
  static char *sccsid = "@(#)Copyright Apple Computer 1987\tVersion 2.1 of subr.c on 89/10/13 19:26:28";
#endif		/* _AC_HISTORY */
#endif		/* lint */

#define _AC_MODS

#include "sys/param.h"
#include "sys/types.h"
#include "sys/sysmacros.h"
#include "sys/time.h"
#include "sys/mmu.h"
#include "sys/page.h"
#include "sys/systm.h"
#include "sys/vnode.h"
#include "sys/signal.h"
#include "sys/user.h"
#include "sys/errno.h"
#include "sys/buf.h"
#include "sys/vfs.h"
#include "sys/var.h"

/*
 * Routine which sets a user error; placed in
 * illegal entries in the bdevsw and cdevsw tables.
 */
nodev()
{
	return(ENODEV);
}

/*
 * Null routine; placed in insignificant entries
 * in the bdevsw and cdevsw tables.
 */
nulldev()
{
	return (0);
}

imin(a, b)
{
 	return (a < b ? a : b);
}

/*
 * Copy string s2 to s1.  s1 must be large enough.
 * return ptr to null in s1
 */
char *
strcpy(s1, s2)
        register char *s1, *s2;
{

    while (*s1++ = *s2++)
	;
    return (s1 - 1);
}
