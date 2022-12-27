#ifndef lint	/* .../sys/COMMON/libc/qsort.c */
#define _AC_NAME qsort_c
#define _AC_NO_MAIN "@(#) Copyright (c) 1984-85 AT&T-IS, All Rights Reserved.  {Apple version 1.2 87/11/11 21:15:59}"
#include <apple_notice.h>

#ifdef _AC_HISTORY
  static char *sccsid = "@(#)Copyright Apple Computer 1987\tVersion 1.2 of qsort.c on 87/11/11 21:15:59";
#endif		/* _AC_HISTORY */
#endif		/* lint */

#define _AC_MODS
/*	@(#)qsort.c	1.2	*/
/*	3.0 SID #	1.2	*/
/*LINTLIBRARY*/

#if !defined(pdp11) && !defined(KERNEL)
#ifdef vax         /* number is determined experimentally on vax-11/780 */
#define MINCPY	24 /* minimum number of characters worth using memcpy for */
#else              /* number is determined experimentally on 3b20s */
#define MINCPY	8  /* minimum number of characters worth using memcpy for */
#endif
#define NULL	0
#define CPY(i, j) ((void) memcpy(i, j, n))
extern char *malloc(), *realloc(), *memcpy();
static char *qsbuf = NULL;
#endif

static 	qses, (*qscmp)();

void
qsort(a, n, es, fc)
char	*a;
unsigned n, es;
int	(*fc)();
{
	void qs1();

#if !defined(pdp11) && !defined(KERNEL)
	{
		static unsigned qsbufsize;

		if (es >= MINCPY)
			if (qsbuf == NULL)
				qsbuf = malloc(qsbufsize = es);
			else if (qsbufsize < es)
				qsbuf = realloc(qsbuf, qsbufsize = es);
	}
#endif
	qscmp = fc;
	qses = es;
	qs1(a, a+n*es);
}

static void
qs1(a, l)
char	*a, *l;
{
	register char *i, *j;
	register int es;
	register char *lp, *hp;
	register int c;
	void	qsexc(), qstexc();
	unsigned n;

	es = qses;
start:
	if((n=l-a) <= es)
		return;
	n = es * (n / (2*es));
	hp = lp = a+n;
	i = a;
	j = l-es;
	while(1) {
		if(i < lp) {
			if((c = (*qscmp)(i, lp)) == 0) {
				qsexc(i, lp -= es);
				continue;
			}
			if(c < 0) {
				i += es;
				continue;
			}
		}

loop:
		if(j > hp) {
			if((c = (*qscmp)(hp, j)) == 0) {
				qsexc(hp += es, j);
				goto loop;
			}
			if(c > 0) {
				if(i == lp) {
					qstexc(i, hp += es, j);
					i = lp += es;
					goto loop;
				}
				qsexc(i, j);
				j -= es;
				i += es;
				continue;
			}
			j -= es;
			goto loop;
		}

		if(i == lp) {
			if(lp-a >= l-hp) {
				qs1(hp+es, l);
				l = lp;
			} else {
				qs1(a, lp);
				a = hp+es;
			}
			goto start;
		}

		qstexc(j, lp -= es, i);
		j = hp -= es;
	}
}

static void
qsexc(ri, rj)
register char *ri, *rj;
{
	register int n = qses;

#if !defined(pdp11) && !defined(KERNEL)
	if (n >= MINCPY && qsbuf != NULL) {
		CPY(qsbuf, ri);
		CPY(ri, rj);
		CPY(rj, qsbuf);
		return;
	}
#endif
	do {
		register char c = *ri;
		*ri++ = *rj;
		*rj++ = c;
	} while(--n);
}

static void
qstexc(ri, rj, rk)
register char *ri, *rj, *rk;
{
	register int n = qses;

#if !defined(pdp11) && !defined(KERNEL)
	if (n >= MINCPY && qsbuf != NULL) {
		CPY(qsbuf, ri);
		CPY(ri, rk);
		CPY(rk, rj);
		CPY(rj, qsbuf);
		return;
	}
#endif
	do {
		register char c = *ri;
		*ri++ = *rk;
		*rk++ = *rj;
		*rj++ = c;
	} while(--n);
}
