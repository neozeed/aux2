#ifndef lint	/* .../sys/PAGING/os/intrmsg.c */
#define _AC_NAME intrmsg_c
#define _AC_NO_MAIN "@(#) Copyright (c) 1984-85 AT&T-IS, All Rights Reserved.  {Apple version 1.2 87/11/11 21:25:05}"
#include <apple_notice.h>

#ifdef _AC_HISTORY
  static char *sccsid = "@(#)Copyright Apple Computer 1987\tVersion 1.2 of intrmsg.c on 87/11/11 21:25:05";
#endif		/* _AC_HISTORY */
#endif		/* lint */

#define _AC_MODS
/*	@(#)intrmsg.c	UniPlus VVV.2.1.1	*/

#ifdef lint
#include "sys/sysinclude.h"
#else lint
#include "sys/types.h"
#endif lint

/*
 *	Print interrupt messages.  Called to print a message whenever
 *	wierd and wonderful things happen to the 3B-20.
 */


int callzero;
zerofunc()
{
	if(callzero)
		panic("call of function at location 0");
	else
		printf("call of function at location 0\n");
}

/* <@(#)intrmsg.c	6.1> */
