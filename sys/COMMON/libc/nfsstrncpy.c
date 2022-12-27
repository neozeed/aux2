#ifndef lint	/* .../sys/COMMON/libc/nfsstrncpy.c */
#define _AC_NAME nfsstrncpy_c
#define _AC_NO_MAIN "@(#) Copyright (c) 1983-87 Sun Microsystems Inc., All Rights Reserved.  {Apple version 1.2 87/11/11 21:15:54}"
#include <apple_notice.h>

#ifdef _AC_HISTORY
  static char *sccsid = "@(#)Copyright Apple Computer 1987\tVersion 1.2 of nfsstrncpy.c on 87/11/11 21:15:54";
#endif		/* _AC_HISTORY */
#endif		/* lint */

#define _AC_MODS
/*	@(#)nfsstrncpy.c	UniPlus 2.1.1	*/

/*	3.0 SID #	1.2	*/
/*LINTLIBRARY*/
/*
 * Copy s2 to s1, truncating to copy n bytes
 * return ptr to null in s1 or s1 + n
 */

char *
nfsstrncpy(s1, s2, n)
register char *s1, *s2;
register int n;
{
        register i;

        for (i = 0; i < n; i++) {
                if ((*s1++ = *s2++) == '\0') {
                        return (s1 - 1);
                }
        }
        return (s1);
}
