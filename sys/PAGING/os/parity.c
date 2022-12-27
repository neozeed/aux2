#ifndef lint	/* .../sys/PAGING/os/parity.c */
#define _AC_NAME parity_c
#define _AC_NO_MAIN "@(#) Copyright (c) 1985-87 UniSoft Corporation, All Rights Reserved.  {Apple version 2.1 89/10/13 12:04:50}"
#include <apple_notice.h>

#ifdef _AC_HISTORY
  static char *sccsid = "@(#)Copyright Apple Computer 1987\tVersion 2.1 of parity.c on 89/10/13 12:04:50";
#endif		/* _AC_HISTORY */
#endif		/* lint */

#define _AC_MODS
/*	@(#)parity.c	UniPlus VVV.2.1.1	*/

/*
 * Memory parity error handler and
 * memory driver diagnostic interface
 */

#ifdef lint
#include "sys/sysinclude.h"
#else lint
#include "sys/types.h"
#include "sys/mmu.h"
#include "sys/param.h"
#include "sys/page.h"
#include "sys/systm.h"
#include "sys/sysmacros.h"
#include "sys/dir.h"
#include "sys/errno.h"
#include "sys/signal.h"
#include "sys/time.h"
#include "sys/user.h"
#include "sys/var.h"
#include "sys/pfdat.h"
#include "sys/region.h"
#include "sys/proc.h"

#include "sys/debug.h"
#endif lint

#define	TRAPPED	0x08000000	/* trapped address valid */
#define	SYSACC	0x00800000	/* access made by system not refresh */
#define	NCPF	0x00000002	/* non-correctable parity error */
#define	MOD1	0x80000000	/* error in controller 1 */
#define LOW23	0x007fffff	/* low 23 bits of trapped address */
#define	HIGHBIT	0x40000000	/* high order bit of trapped adress */

#define NPPMASA	(mm_cnt/2)	/* number of pages per mainstore array */
#define NAPMASC	16		/* maximum number of arrays per controller */

#define	MAXERR	10		/* number of correctable errors before removal */
#define	MAXBAD	1000		/* max bad pages to keep track of */
#define MAXRMV	1000		/* max bad pages pending removal */

struct pgerr	{		/* list of pages that have had correctable errors */
	short	page;		/* ...page number */
	short	cnt;		/* ...number of errors */
} pgerr[MAXBAD];

short pgrmv[MAXRMV];		/* queue of pages to be removed */
struct pfdat pbad;		/* linked list of removed memory pages */


/*
 * Attempt to remove queued pages
 * Called by memwake after memory pages have
 * been freed, if bad pages are queued for removal
 * Returns the number of bad pages removed from free queue
 */
rmvqpg()
{
	register short *g, page, *g1;
	register struct pgerr *p;
	register struct pfdat *pfd;
	int ret = 0;

	SPLHI();
	
	/*
	 * Search the pgrmv queue looking
	 * for pages on the free queue
	 */
	for (g = pgrmv; page = *g; ) {
		pfd = pfdat + page;
		if (pfd->pf_flags & P_QUEUE) {
			/* Remove from free queue */
			ASSERT(!(pfd->pf_flags & P_BAD));
			(void)premove(pfd);
			pfd->pf_prev->pf_next = pfd->pf_next;
			pfd->pf_next->pf_prev = pfd->pf_prev;

			/* Insert into bad list */
			pfd->pf_hchain = pbad.pf_hchain;
			pbad.pf_hchain = pfd;
			pfd->pf_flags = P_BAD;
			clearpage(page);
			ret++;

			/* Take page out of pgrmv and pgerr lists */
			for (g1 = g; *g1 = g1[1]; g1++);
			for (p = pgerr; p->cnt; p++)
				if (p->page == page) {
					do *p = p[1];
					while ((++p)->cnt);
					break;
				}
		} else
			g++;
	}
	SPL0();
	return(ret);
}


/* <@(#)parity.c	6.4> */
