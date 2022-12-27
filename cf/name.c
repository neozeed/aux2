#ifndef lint	/* .../sys/PAGING/cf/name.c */
#define _AC_NAME name_c
#define _AC_NO_MAIN "@(#) Copyright (c) 1984-85 AT&T-IS, All Rights Reserved.  {Apple version 1.2 87/11/11 21:29:55}"
#include <apple_notice.h>

#ifdef _AC_HISTORY
  static char *sccsid = "@(#)Copyright Apple Computer 1987\tVersion 1.2 of name.c on 87/11/11 21:29:55";
#endif		/* _AC_HISTORY */
#endif		/* lint */

#define _AC_MODS
/*	@(#)name.c	*/
#include "sys/utsname.h"

#ifdef lint
#define	SYS	"sys"
#define	NODE	"node"
#define	REL	"rel"
#define	VER	"ver"
#define	MACH	"m68000"
#define	TIMESTAMP "timestamp"
#endif
struct utsname utsname = {
	SYS,
	NODE,
	REL,
	VER,
	MACH,
};

char timestamp[] = TIMESTAMP;
