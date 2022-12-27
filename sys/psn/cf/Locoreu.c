#ifndef lint	/* .../sys/psn/cf/Locoreu.c */
#define _AC_NAME Locoreu_c
#define _AC_NO_MAIN "@(#) Copyright (c) 1985-87 UniSoft Corporation, All Rights Reserved.  {Apple version 1.3 89/05/22 14:43:32}"
#include <apple_notice.h>

#ifdef _AC_HISTORY
  static char *sccsid = "@(#)Copyright Apple Computer 1989\tVersion 1.3 of Locoreu.c on 89/05/22 14:43:32";
#endif		/* _AC_HISTORY */
#endif		/* lint */

#define _AC_MODS
#include "sys/sysinclude.h"

/*
 * Definitions of system specific kernel variables to keep lint happy
 */

char	SyncTbl[100];
char	TagData[100];
int	mmu_on = 0;

idle()
{
	parityinit();
	scintr((struct args *)NULL);
	via1intr((struct args *)NULL);
	via2intr((struct args *)NULL);
	netintr();
	abintr();
#ifdef UCB_NET
	{struct uba_driver *udp = (struct uba_driver *)0; if ((int)udp) return;}
	lrintr();
	netintr();
	(void) getsock(0);
#endif UCB_NET
#ifdef FLOAT
	fpintr();
	mnkintr();
#endif
}

int FormatTrack(i) {return(i);}

int snrd(addr)
  caddr_t addr; {return((int)addr);}

int snwr(addr)
  caddr_t addr; {return((int)addr);}

int rdhead(addr)
  caddr_t addr; {return((int)addr);}

struct file *getsock(fdes)
  int fdes; {return((struct file *)fdes);}
