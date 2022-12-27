#ifndef lint	/* .../sys/PAGING/os/malloc.c */
#define _AC_NAME malloc_c
#define _AC_NO_MAIN "@(#) Copyright (c) 1984-85 AT&T-IS, All Rights Reserved.  {Apple version 2.1 89/10/13 12:04:07}"
#include <apple_notice.h>

#ifdef _AC_HISTORY
  static char *sccsid = "@(#)Copyright Apple Computer 1987\tVersion 2.1 of malloc.c on 89/10/13 12:04:07";
#endif		/* _AC_HISTORY */
#endif		/* lint */

#define _AC_MODS
/*	@(#)malloc.c	UniPlus VVV.2.1.2	*/

#ifdef lint
#include "sys/sysinclude.h"
#else lint
#include "sys/types.h"
#include "sys/mmu.h"
#include "sys/sysmacros.h"
#include "sys/param.h"
#include "sys/page.h"
#include "sys/systm.h"
#include "sys/map.h"
#include "sys/debug.h"
#endif lint

/* Allocate 'size' units from the given map.
 * Return the base of the allocated space.
 * In a map, the addresses are increasing and the
 * list is terminated by a 0 size.
 * Algorithm is first-fit.
 */

malloc(mp, size)
	struct map *mp;
{
	register int a;
	register struct map *bp;

	if(size <= 0)
		return(NULL);

	for (bp = mapstart(mp); bp->m_size; bp++) {
		if (bp->m_size >= size) {
			a = bp->m_addr;
			bp->m_addr += size;
			if ((bp->m_size -= size) == 0) {
				do {
					bp++;
					(bp-1)->m_addr = bp->m_addr;
				} while ((bp-1)->m_size = bp->m_size);
				mapsize(mp)++;
			}
			return(a);
		}
	}
	return(0);
}

/* Free the previously allocated space.
 * Sort into map and combine on
 * one or both ends if possible.
 */

mfree(mp, size, a)
	struct map *mp;
	register int a;
{
	register struct map	*bp;
	register int		t;

	if(size <= 0)
		return;

	bp = mapstart(mp);

	for (; bp->m_addr<=a && bp->m_size!=0; bp++);

	ASSERT(a + size <= bp->m_addr  ||  bp->m_size == 0);
	ASSERT(bp == mapstart(mp) || (bp-1)->m_addr + (bp-1)->m_size <= a);

	if (bp>mapstart(mp) && (bp-1)->m_addr+(bp-1)->m_size == a) {
		(bp-1)->m_size += size;
		if (a+size == bp->m_addr) {
			(bp-1)->m_size += bp->m_size;
			while (bp->m_size) {
				bp++;
				(bp-1)->m_addr = bp->m_addr;
				(bp-1)->m_size = bp->m_size;
			}
			mapsize(mp)++;
		}
	} else {
		if (a+size == bp->m_addr && bp->m_size) {
			bp->m_addr -= size;
			bp->m_size += size;
		} else {
			if (mapsize(mp) == 0) {
				printf("\nDANGER: mfree map overflow %x\n", mp);
				printf("  lost %d items at %d\n", size, a);
				return;
			}
			do {
				t = bp->m_addr;
				bp->m_addr = a;
				a = t;
				t = bp->m_size;
				bp->m_size = size;
				bp++;
			} while (size = t);
			mapsize(mp)--;
		}
	}
	if (mapwant(mp)) {
		/* blast everyone waiting for space off of queue */
		mapwant(mp) = 0;
		wakeup((caddr_t)mp);
	}
}
/* <@(#)malloc.c	6.2> */
