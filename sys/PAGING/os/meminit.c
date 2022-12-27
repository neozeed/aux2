#ifndef lint	/* .../sys/PAGING/os/meminit.c */
#define _AC_NAME meminit_c
#define _AC_NO_MAIN "@(#) Copyright (c) 1987 Apple Computer, Inc., 1985-87 UniSoft Corporation, All Rights Reserved.  {Apple version 2.1 89/10/13 12:22:08}"
#include <apple_notice.h>

#ifdef _AC_HISTORY
  static char *sccsid = "@(#)Copyright Apple Computer 1987, 1988, 1989\tVersion 2.1 of meminit.c on 89/10/13 12:22:08";
#endif		/* _AC_HISTORY */
#endif		/* lint */

#define _AC_MODS
/*	@(#)meminit.c	UniPlus VVV.2.1.2	*/

#ifdef HOWFAR
extern int	T_page;
extern int T_swapalloc;
extern int T_availmem;
#endif HOWFAR
#ifdef lint
#include "sys/sysinclude.h"
#else lint
#include "sys/types.h"
#include "sys/mmu.h"
#include "sys/param.h"
#include "sys/sysmacros.h"
#include "sys/page.h"
#include "sys/systm.h"
#include "sys/dir.h"
#include "sys/signal.h"
#include "sys/time.h"
#include "sys/user.h"
#include "sys/vnode.h"
#include "sys/var.h"
#include "sys/vfs.h"
#include "sys/mount.h"
#include "sys/buf.h"
#include "sys/map.h"
#include "sys/pfdat.h"
#include "sys/region.h"
#include "sys/proc.h"
#include "sys/swap.h"
#include "sys/tuneable.h"
#include "sys/debug.h"
#include "svfs/inode.h"
#include "netinet/in.h"
#include "setjmp.h"
#endif lint

#ifdef HOWFAR
extern int T_meminit;
#endif

extern int freemem;

/*
 * This is not called by A/UX kernel code; it is used by some external memory
 *  cards to add NuBus memory to the system.  This is not a Good Thing,
 *  since we can't distinguish this from motherboard memory, and the
 *  MacII NuBus controller doesn't like RMC cycles from the motherboard
 *  to NuBus memory.  If a page table slips out onto the card, POW!
 */
memregprobe(start, len)
register caddr_t start;
int len;
{
	register caddr_t cp;
	register caddr_t max = (caddr_t) -1;
	register int *ip;
	register int zeroi = 0;
	register char zero = 0;
	int ret;
	int *saved_jb;
	jmp_buf jb;
	static caddr_t current;

	if (len != 0)
		max = start + len;

	current = start;
	saved_jb = nofault;
	if ( ! setjmp(jb)) {
		nofault = jb;

		*start = 0x61;
		start[1] = zero;
		if ( *start != 0x61) {
			TRACE(T_meminit, ("\n"));
			return 0;
		}
		cp = current += ptob(1);

		while (max == (caddr_t) -1 || cp < max) {

			cp[1] = 0x61;
			cp[0] = zero;		/* Clear hardware lines */

			if (cp[1] != 0x61 || ! *start)
				break;

			current = cp += ptob(1);
		}
		*start = zero;
	}

	nofault = saved_jb;

	if (v.v_maxpmem && maxmem + btop(current - start) > v.v_maxpmem) {
		printf("memregadd: adding memory region from 0x%x to 0x%x would\n",
				start, current);
		printf("	exceed MAXPMEM (see kconfig(1M)).  Ignoring 0x%x bytes.\n\n",
				current - start - ptob(v.v_maxpmem - maxmem));
		ret = ptob(v.v_maxpmem - maxmem);
		maxmem = v.v_maxpmem;
	}
	else {
		ret = current - start;
		maxmem += btop(ret);
	}

	for (ip = (int *) start; ip < (int *) current;)
		*ip++ = zeroi;

	return ret;
}

memregadd(start, len, npfdd)
u_int start;
u_int len;
register struct pfdat_desc *npfdd;
{
	register struct pfdat_desc *pfdd = pfdat_desc;
	register u_int end = start + len;
	u_int s1, e1;
	extern int T_startup;

TRACE(T_meminit, ("memregadd: adding memory from 0x%x to 0x%x\n", start, end));

	for (; pfdd; pfdd = pfdd->pfd_next) {
		s1 = pfdd->pfd_start;
		e1 = pfdd->pfd_end;

		if ((s1 > start && s1 < end)
					|| (e1 > start && e1 < end)
					|| (s1 < start && e1 > end)) {
			printf("Overlapping Memory Regions:\n");
			printf("	0x%x to 0x%x and 0x%x to 0x%x\n",
				s1, e1, start, end);
			printf("Ignoring 0x%x to 0x%x\n", start, end);
			return -1;
		}
	}

	npfdd->pfd_curbase = npfdd->pfd_start = start;
	npfdd->pfd_end = end;

	npfdd->pfd_next = pfdat_desc;
	pfdat_desc = npfdd; 

	return 0;
}

initmemfree()
{
	register struct pfdat_desc *pfdd = pfdat_desc;
	register pfd_t *pfd;
	register u_int pgcnt;
	register u_int pfnum;

	phead.pf_next = &phead;
	phead.pf_prev = &phead;

	for (; pfdd; pfdd = pfdd->pfd_next) {

		if (pfdd->pfd_empty)
			continue;
		pfd = pfdd->pfd_head = (struct pfdat *) pfdd->pfd_curbase;

		pfnum = btotp(pfdd->pfd_end + 1) - 1;	/* Find last page in region */

		if (ptob(pfnum) < (int) (pfd + 1))
			continue;

		pgcnt = 1;

TRACE(T_meminit, ("initmemfree: first pfdat 0x%x, end 0x%x, last page frame 0x%x\n",
				pfd, pfdd->pfd_end, pfnum));

		/* Allocate page/pfdat pairs until they meet in the middle */

		while ((int) (pfd + pgcnt + 1) < ptob(pfnum - 1))
			pgcnt++, pfnum--;

		TRACE(T_meminit, ("first page frame 0x%x, last pfdat 0x%x, pgcnt 0x%x\n",
				pfnum, pfd + pgcnt, pgcnt));

		pfdd->pfd_pfcnt = pgcnt;
		pfdd->pfd_pfnum = pfnum;

		if (pgcnt <= 0)
			continue;

		availsmem += pgcnt;
		availrmem += pgcnt;
		freemem += pgcnt;

		/*
		 * Add pages to queue, high memory at end of queue
		 * Pages added to queue FIFO
		 */
		for ( ; pgcnt; pgcnt--, pfd++) {
			ASSERT(((int) pfd & 1) == 0);
			pfd->pf_next = &phead;
			pfd->pf_hchain = NULL;
			pfd->pf_prev = phead.pf_prev;
			phead.pf_prev->pf_next = pfd;
			phead.pf_prev = pfd;
			pfd->pf_flags = P_QUEUE;
		}
	}
}

#define MEM_ROUND(x,mask)	(((x) + (mask)) & ~(mask))
#define MEM_REM(x,mask)		(MEM_ROUND((x),(mask)) - (x))

memreg_alloc(nbytes, flags, mask)
struct pfdd_type flags;
{
	register struct pfdat_desc *pfdd = pfdat_desc;
	struct pfdat_desc *save_pfdd = (struct pfdat_desc *) 0;
	u_int retval;

	for (; pfdd; pfdd = pfdd->pfd_next) {
		if (pfdd->pfd_empty || pfdd->pfd_user_reserv || pfdd->pfd_kern_reserv
				|| pfdd->pfd_end - pfdd->pfd_curbase 
				< MEM_REM(pfdd->pfd_curbase, mask) + nbytes) {
			TRACE(T_meminit,
				("memreg_alloc: region empty or too small\n"));
			continue;
		}
		if (flags.pfdt_reqonly && 
				((flags.pfdt_memspeed
					&& flags.pfdt_memspeed != pfdd->pfd_memspeed)
				|| (flags.pfdt_dmaspeed
					&& flags.pfdt_dmaspeed != pfdd->pfd_dmaspeed))) {
			TRACE(T_meminit, ("memreg_alloc: missed PFD_ONLY\n"));
			continue;
		}
		if (flags.pfdt_dmaokay) {
			if ( ! pfdd->pfd_dmaokay) {
				TRACE(T_meminit, ("memreg_alloc: non dma region\n"));
				continue;
			}
			if (flags.pfdt_dmaspeed && (flags.pfdt_dmareqslower
					&& flags.pfdt_dmaspeed < pfdd->pfd_dmaspeed))
				continue;
			if ( ! save_pfdd || save_pfdd->pfd_dmaspeed < pfdd->pfd_dmaspeed)
				save_pfdd = pfdd;
			continue;
		}
		else if (flags.pfdt_memokay) {
			if ( ! pfdd->pfd_memokay)
				continue;
			if (flags.pfdt_memspeed && (flags.pfdt_memreqslower
					&& flags.pfdt_memspeed > pfdd->pfd_memspeed))
				continue;
			if ( ! save_pfdd || save_pfdd->pfd_memspeed < pfdd->pfd_memspeed)
				save_pfdd = pfdd;
			continue;
		}

	}
	if ( ! save_pfdd) {
		TRACE(T_meminit, 
			("memreg_alloc: Can't find region of 0x%x bytes, type 0x%x\n", nbytes, flags));
		return -1;
	}

	retval = save_pfdd->pfd_curbase += MEM_REM(save_pfdd->pfd_curbase, mask);

	if ((save_pfdd->pfd_curbase += nbytes) >= save_pfdd->pfd_end)
		save_pfdd->pfd_empty = 1;

	TRACE(T_meminit,
		("memreg_alloc: returning 0x%x, mask 0x%x, flag request 0x%x\n",
			retval, mask, flags));
	TRACE(T_meminit, ("	actual flags 0x%x new region base 0x%x\n",
			save_pfdd->pfd_type, save_pfdd->pfd_curbase));

	return retval;
}
