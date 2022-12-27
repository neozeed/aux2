#ifndef lint	/* .../sys/NET/sys/uipc_mbuf.c */
#define _AC_NAME uipc_mbuf_c
#define _AC_NO_MAIN "@(#) Copyright (c) 1983-87 Sun Microsystems Inc., All Rights Reserved.  {Apple version 2.3 90/05/03 12:57:42}"
#include <apple_notice.h>

#ifdef _AC_HISTORY
  static char *sccsid = "@(#)Copyright Apple Computer 1987\tVersion 2.3 of uipc_mbuf.c on 90/05/03 12:57:42";
#endif		/* _AC_HISTORY */
#endif		/* lint */

#define _AC_MODS
/*      @(#)uipc_mbuf.c 1.1 86/02/03 SMI; from UCB 1.43 83/05/27        */
/*      NFSSRC @(#)uipc_mbuf.c  2.1 86/04/15 */

#include "sys/param.h"
#include "sys/types.h"
#include "sys/mmu.h"
#include "sys/sysmacros.h"
#include "sys/signal.h"
#include "sys/errno.h"
#include "sys/page.h"
#include "sys/region.h"
#include "sys/time.h"
#include "sys/user.h"
#include "sys/proc.h"
#include "sys/mbuf.h"
#include "sys/socket.h"
#include "sys/socketvar.h"
#include "sys/map.h"
#include "sys/var.h"
#include "netinet/in_systm.h"		/* XXX */
#include "sys/domain.h"
#include "sys/protosw.h"

struct mbuf *mbufclusterstart;

caddr_t m_clalloc();

mbinit()
{
	int s;

	s = splimp();
	if (m_clalloc((v.v_nmbufs * sizeof(struct mbuf)) / MCLBYTES, 
		      MPG_MBUFS, M_DONTWAIT) == 0)
		goto bad;
	if (m_clalloc(NMBCLUSTERS, MPG_CLUSTERS, M_DONTWAIT) == 0)
		goto bad;
	splx(s);
	return;
bad:
	panic("mbinit");
}

/*
 * Must be called at splimp.
 */
/* ARGSUSED */
caddr_t
m_clalloc(ncl, how, canwait)
	register int ncl;
	int how;
	int canwait;
{
	register struct mbuf *m;
	register int i;
	unsigned int location;
	int slop;
	static int MbufClustersInitialized = 0, MbufsInitialized = 0;

	switch (how) {

	case MPG_CLUSTERS:
		if (MbufClustersInitialized)
			return ((caddr_t) 0);
		/* round mbuf clusters to MCLBYTES boundary */
		location = (unsigned int) &mbutl[0];
		slop = location & (MCLBYTES - 1);
		mbufclusterstart = (struct mbuf *) (location + MCLBYTES - slop);
		m = mbufclusterstart;
		for (i = 0; i < ncl; i++) {
			m->m_off = 0;
			m->m_next = mclfree;
			mclfree = m;
			m += MCLBYTES / sizeof (*m);
			mbstat.m_clfree++;
		}
		mbstat.m_clusters += ncl;
		MbufClustersInitialized = 1;
		break;

	case MPG_MBUFS:
		if (MbufsInitialized)
			return ((caddr_t) 0);
		/* round actual buffers to MSIZE boundary */
		location = (unsigned int) &mbufbufs[0];
		slop = location & (MSIZE - 1);
		m = (struct mbuf *) (location + MSIZE - slop);
		for (i = ncl * MCLBYTES / sizeof (*m); i > 0; i--) {
			m->m_off = 0;
			m->m_type = MT_DATA;
			mbstat.m_mtypes[MT_DATA]++;
			mbstat.m_mbufs++;
			(void) m_free(m);
			m++;
		}
		MbufsInitialized = 1;
		break;

	case MPG_SPACE:
		mbstat.m_space++;
		break;
	}
	return ((caddr_t)m);
}


/*
 * Must be called at splimp.
 */
m_expand(canwait)
	int canwait;
{
	register struct domain *dp;
	register struct protosw *pr;
	int tries;

	for (tries = 0;; ) {
		if (m_clalloc(1, MPG_MBUFS, canwait))
			return (1);
		if (canwait == 0 || tries++)
			return (0);

		/* ask protocols to free space */
		for (dp = domains; dp; dp = dp->dom_next)
			for (pr = dp->dom_protosw; pr < dp->dom_protoswNPROTOSW;
			    pr++)
				if (pr->pr_drain)
					(*pr->pr_drain)();
		mbstat.m_drain++;
	}
}

/* NEED SOME WAY TO RELEASE SPACE */

/*
 * Space allocation routines.
 * These are also available as macros
 * for critical paths.
 */
struct mbuf *
m_get(canwait, type)
	int canwait, type;
{
	register struct mbuf *m;

	MGET(m, canwait, type);
	return (m);
}

struct mbuf *
m_getclr(canwait, type)
	int canwait, type;
{
	register struct mbuf *m;

	MGET(m, canwait, type);
	if (m == 0)
		return (0);
	bzero(mtod(m, caddr_t), MLEN);
	return (m);
}

struct mbuf *
m_free(m)
	struct mbuf *m;
{
	register struct mbuf *n;

	MFREE(m, n);
	return (n);
}

/*
 * Get more mbufs; called from MGET macro if mfree list is empty.
 * Must be called at splimp.
 */
/*ARGSUSED*/
struct mbuf *
m_more(canwait, type)
	int canwait, type;
{
	register struct mbuf *m;

	while ((m_expand(canwait) == 0) && (mfreep == 0)) {
		if (canwait == M_WAIT) {
			mbstat.m_wait++;
			m_want++;
			sleep((caddr_t)&mfreep, PZERO - 1);
		} else {
			mbstat.m_drops++;
			return (NULL);
		}
	}
#define m_more(x,y) (panic("m_more"), (struct mbuf *)0)
	MGET(m, canwait, type);
#undef m_more
	return (m);
}

m_freem(m)
	register struct mbuf *m;
{
	register struct mbuf *n;
	register int s;

	if (m == NULL)
		return;
	s = splimp();
	do {
		MFREE(m, n);
	} while (m = n);
	splx(s);
}

/*
 * Mbuffer utility routines.
 */

/*
 * Make a copy of an mbuf chain starting "off" bytes from the beginning,
 * continuing for "len" bytes.  If len is M_COPYALL, copy to end of mbuf.
 * Should get M_WAIT/M_DONTWAIT from caller.
 */
struct mbuf *
m_copy(m, off, len)
	register struct mbuf *m;
	int off;
	register int len;
{
	register struct mbuf *n, **np;
	struct mbuf *top, *p;

	if (len == 0)
		return (0);
	if (off < 0 || len < 0)
		panic("m_copy");
	while (off > 0) {
		if (m == 0)
			panic("m_copy");
		if (off < m->m_len)
			break;
		off -= m->m_len;
		m = m->m_next;
	}
	np = &top;
	top = 0;
	while (len > 0) {
		if (m == 0) {
			if (len != M_COPYALL)
				panic("m_copy");
			break;
		}
		MGET(n, M_DONTWAIT, m->m_type);
		*np = n;
		if (n == 0)
			goto nospace;
		n->m_len = MIN(len, m->m_len - off);
		if (m->m_off > MMAXOFF && n->m_len > MLEN) {
			mcldup(m, n, off);
			n->m_off += off;
		} else
			bcopy(mtod(m, caddr_t)+off, mtod(n, caddr_t),
			    (unsigned)n->m_len);
		if (len != M_COPYALL)
			len -= n->m_len;
		off = 0;
		m = m->m_next;
		np = &n->m_next;
	}
	return (top);
nospace:
	m_freem(top);
	return (0);
}

m_cat(m, n)
	register struct mbuf *m, *n;
{
	while (m->m_next)
		m = m->m_next;
	while (n) {
		if (m->m_off >= MMAXOFF ||
		    m->m_off + m->m_len + n->m_len > MMAXOFF) {
			/* just join the two chains */
			m->m_next = n;
			return;
		}
		/* splat the data from one into the other */
		bcopy(mtod(n, caddr_t), mtod(m, caddr_t) + m->m_len,
		    (u_int)n->m_len);
		m->m_len += n->m_len;
		n = m_free(n);
	}
}

m_adj(mp, len)
	struct mbuf *mp;
	register int len;
{
	register struct mbuf *m;
	register int count;

	if ((m = mp) == NULL)
		return;
	if (len >= 0) {
		while (m != NULL && len > 0) {
			if (m->m_len <= len) {
				len -= m->m_len;
				m->m_len = 0;
				m = m->m_next;
			} else {
				m->m_len -= len;
				m->m_off += len;
				break;
			}
		}
	} else {
		/*
		 * Trim from tail.  Scan the mbuf chain,
		 * calculating its length and finding the last mbuf.
		 * If the adjustment only affects this mbuf, then just
		 * adjust and return.  Otherwise, rescan and truncate
		 * after the remaining size.
		 */
		len = -len;
		count = 0;
		for (;;) {
			count += m->m_len;
			if (m->m_next == (struct mbuf *)0)
				break;
			m = m->m_next;
		}
		if (m->m_len >= len) {
			m->m_len -= len;
			return;
		}
		count -= len;
		/*
		 * Correct length for chain is "count".
		 * Find the mbuf with last data, adjust its length,
		 * and toss data from remaining mbufs on chain.
		 */
		for (m = mp; m; m = m->m_next) {
			if (m->m_len >= count) {
				m->m_len = count;
				break;
			}
			count -= m->m_len;
		}
		while (m = m->m_next)
			m->m_len = 0;
	}
}

/*
 * Rearange an mbuf chain so that len bytes are contiguous
 * and in the data area of an mbuf (so that mtod and dtom
 * will work for a structure of size len).  Returns the resulting
 * mbuf chain on success, frees it and returns null on failure.
 * If there is room, it will add up to MPULL_EXTRA bytes to the
 * contiguous region in an attempt to avoid being called next time.
 */
struct mbuf *
m_pullup(n, len)
	register struct mbuf *n;
	int len;
{
	register struct mbuf *m;
	register int count;
	int space;

	if (n->m_off + len <= MMAXOFF && n->m_next) {
		m = n;
		n = n->m_next;
		len -= m->m_len;
	} else {
		if (len > MLEN)
			goto bad;
		MGET(m, M_DONTWAIT, n->m_type);
		if (m == 0)
			goto bad;
		m->m_len = 0;
	}
	space = MMAXOFF - m->m_off;
	do {
		count = MIN(MIN(space - m->m_len, len + MPULL_EXTRA), n->m_len);
		bcopy(mtod(n, caddr_t), mtod(m, caddr_t)+m->m_len,
		  (unsigned)count);
		len -= count;
		m->m_len += count;
		n->m_len -= count;
		if (n->m_len)
			n->m_off += count;
		else
			n = m_free(n);
	} while (len > 0 && n);
	if (len > 0) {
		(void) m_free(m);
		goto bad;
	}
	m->m_next = n;
	return (m);
bad:
	m_freem(n);
	return (0);
}

/* Allocate a "funny" mbuf, that is, one whose data is owned by someone else */
struct mbuf *
mclgetx(fun, arg, addr, len, wait)
	int (*fun)(), arg, len, wait;
	caddr_t addr;
{
	register struct mbuf *m;

	MGET(m, wait, MT_DATA);
	if (m == 0)
		return (0);
	m->m_off = (int)addr - (int)m;
	m->m_len = len;
	m->m_cltype = MCL_LOANED;
	m->m_clfun = fun;
	m->m_clarg = arg;
	m->m_clswp = NULL;
	return (m);
}
/* Generic cluster mbuf unallocator -- invoked from within MFREE */
mclput(m)
	register struct mbuf *m;
{

	switch (m->m_cltype) {
	case MCL_STATIC:
		m = MTOCL(m); 
		MCLFREE(m); 
		break;

	case MCL_LOANED:
		(*m->m_clfun)(m->m_clarg);
		break;

	default:
		panic("mclput");
	}
}

/*
 * Deallocation routine for MCL_LOANED cluster mbufs
 * created by mcldup.
 */
static
buffree(arg)
	int arg;
{
	extern int kmem_free_intr();

	kmem_free_intr((caddr_t)arg, *(u_int *)arg);
}

/*
 * Generic cluster mbuf duplicator
 * which duplicates <m> into <n>.
 * If <m> is a regular cluster mbuf, mcldup simply
 * bumps the reference count and ignores <off>.
 * If <m> is a funny mbuf, mcldup allocates a chunck
 * kernel memory and makes a copy, starting at <off>.
 * XXX does not set the m_len field in <n>!
 */
mcldup(m, n, off)
	register struct mbuf *m, *n;
	int off;
{
	extern caddr_t kmem_alloc();
	register struct mbuf *p;
	register caddr_t copybuf;

	switch (m->m_cltype) {
	case MCL_STATIC:
		p = mtod(m, struct mbuf *);
		n->m_off = (int)p - (int)n;
		n->m_cltype = MCL_STATIC;
		mclrefcnt[mtocl(p)]++;
		break;
	case MCL_LOANED:
		copybuf = kmem_alloc((u_int)(n->m_len + sizeof (int)));
		* (int *) copybuf = n->m_len + sizeof (int);
		bcopy(mtod(m, caddr_t) + off, copybuf + sizeof (int),
		    (u_int)n->m_len);
		n->m_off = (int)copybuf + sizeof (int) - (int)n - off;
		n->m_cltype = MCL_LOANED;
		n->m_clfun = buffree;
		n->m_clarg = (int)copybuf;
		n->m_clswp = NULL;
		break;
	default:
		panic("mcldup");
	}
}
