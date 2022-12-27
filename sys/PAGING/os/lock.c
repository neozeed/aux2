#ifndef lint	/* .../sys/PAGING/os/lock.c */
#define _AC_NAME lock_c
#define _AC_NO_MAIN "@(#) Copyright (c) 1984-85 AT&T-IS, All Rights Reserved.  {Apple version 2.1 89/10/13 12:05:47}"
#include <apple_notice.h>

#ifdef _AC_HISTORY
  static char *sccsid = "@(#)Copyright Apple Computer 1987\tVersion 2.1 of lock.c on 89/10/13 12:05:47";
#endif		/* _AC_HISTORY */
#endif		/* lint */

#define _AC_MODS
/*	@(#)lock.c	UniPlus VVV.2.1.2	*/

#ifdef HOWFAR
extern int T_availmem;
#endif HOWFAR
#ifdef lint
#include "sys/sysinclude.h"
#include "sys/lock.h"
#else lint
#include "sys/types.h"
#include "sys/mmu.h"
#include "sys/param.h"
#include "sys/signal.h"
#include "sys/time.h"
#include "sys/user.h"
#include "sys/errno.h"
#include "sys/lock.h"
#include "sys/page.h"
#include "sys/region.h"
#include "sys/proc.h"
#include "sys/systm.h"
#include "sys/tuneable.h"
#include "sys/debug.h"
#endif lint

lock()
{
	struct a {
		long oper;
	};

	if (!suser())
		return;

	switch(((struct a *)u.u_ap)->oper) {
	case TXTLOCK:
		if((u.u_lock & (PROCLOCK|TXTLOCK)) || textlock() == 0)
			goto bad;
		break;
	case PROCLOCK:
		if(u.u_lock&(PROCLOCK|TXTLOCK|DATLOCK))
			goto bad;
		if(u.u_exdata.ux_mag != 0407  &&  textlock() == 0)
			goto bad;
		(void)datalock();
		proclock();
		break;
	case DATLOCK:
		if ((u.u_lock&(PROCLOCK|DATLOCK))  ||  datalock() == 0)
			goto bad;
		break;
	case UNLOCK:
		if (punlock() == 0)
			goto bad;
		break;

	default:
bad:
		if (u.u_error == 0)
			u.u_error = EINVAL;
	}
}

textlock()
{
	register preg_t	*prp;
	register reg_t	*rp;


	prp = findpreg(u.u_procp, PT_TEXT);
	if(prp == NULL)
		return(0);
	rp = prp->p_reg;
	reglock(rp);

	ASSERT(rp->r_noswapcnt >= 0);

	if (rp->r_noswapcnt == 0) {
		availrmem -= rp->r_pgsz;
		if (availrmem < tune.t_minarmem) {
			availrmem += rp->r_pgsz;
			regrele(rp);
			printf("textlock - can't lock %d pages\n", rp->r_pgsz);
			u.u_error = EAGAIN;
			return(0);
		}
		TRACE(T_availmem,("textlock: taking %d avail[R]mem pages\n", 
					rp->r_pgsz));
	}
	++rp->r_noswapcnt;
	regrele(rp);
	u.u_lock |= TXTLOCK;
	return(1);
}
		
tunlock()
{
	register preg_t	*prp;
	register reg_t	*rp;

	prp = findpreg(u.u_procp, PT_TEXT);
	if(prp == NULL)
		return(0);
	rp = prp->p_reg;
	reglock(rp);

	ASSERT(rp->r_noswapcnt);
	if ( ! --rp->r_noswapcnt) {
		TRACE(T_availmem,("tunlock: returning %d avail[R]mem pages\n", 
					rp->r_pgsz));
		availrmem += rp->r_pgsz;
	}
	regrele(rp);
	u.u_lock &= ~TXTLOCK;
	return(1);
}

datalock()
{
	register preg_t	*prp;
	register reg_t	*rp;
	register reg_t	*rp2;


	prp = findpreg(u.u_procp, PT_DATA);
	if(prp == NULL)
		return(0);
	rp = prp->p_reg;
	reglock(rp);
 
        ASSERT(rp->r_noswapcnt >= 0);
 
	if (rp->r_noswapcnt == 0) {
		availrmem -= rp->r_pgsz;
		if (availrmem < tune.t_minarmem) {
			availrmem += rp->r_pgsz;
			regrele(rp);
			printf("datalock - can't lock %d pages\n", rp->r_pgsz);
			u.u_error = EAGAIN;
			return(0);
		}
		TRACE(T_availmem,("datalock: taking %d avail[R]mem pages\n", 
					rp->r_pgsz));
	}
        ++rp->r_noswapcnt;
	prp = findpreg(u.u_procp, PT_STACK);
	if(prp == NULL) {
		if ( ! --rp->r_noswapcnt) {
			TRACE(T_availmem,("datalock: returning %d avail[R]mem pages\n", rp->r_pgsz));
			availrmem += rp->r_pgsz;
		}
		regrele(rp);
		return(0);
	}
	rp2 = prp->p_reg;
	reglock(rp2);

	ASSERT(rp2->r_noswapcnt >= 0);

	if (rp2->r_noswapcnt++ == 0) {
		availrmem -= rp2->r_pgsz;
		if (availrmem < tune.t_minarmem) {
			if ( ! --rp->r_noswapcnt) {
				TRACE(T_availmem,
					("datalock: returning %d avail[R]mem pages\n",
					rp->r_pgsz));
				availrmem += rp->r_pgsz;
			}
			availrmem += rp2->r_pgsz;
			printf("datalock(stack) - can't lock %d pages\n", 
				rp2->r_pgsz);
			--rp2->r_noswapcnt;
			regrele(rp);
			regrele(rp2);
			u.u_error = EAGAIN;
			return(0);
		}
		TRACE(T_availmem,("datalock: taking %d avail[R]mem pages\n", 
					rp2->r_pgsz));
	}
	regrele(rp);
	regrele(rp2);
	u.u_lock |= DATLOCK;
	return(1);
}


dunlock()
{
	register preg_t	*prp;
	register reg_t	*rp;

	prp = findpreg(u.u_procp, PT_DATA);
	if(prp == NULL)
		return(0);
	rp = prp->p_reg;
	reglock(rp);

        ASSERT(rp->r_noswapcnt);
 
        if ( ! --rp->r_noswapcnt) {
		TRACE(T_availmem,("datalock: returning %d avail[R]mem pages\n",
						rp->r_pgsz));
                availrmem += rp->r_pgsz;
	}
	regrele(rp);

        prp = findpreg(u.u_procp, PT_STACK);
        if (prp == NULL)
                return(0);
        rp = prp->p_reg;
        reglock(rp);
 
        ASSERT(rp->r_noswapcnt);
 
        if ( ! --rp->r_noswapcnt) {
		TRACE(T_availmem,("datalock: returning %d avail[R]mem pages\n",
						rp->r_pgsz));
                availrmem += rp->r_pgsz;
	}
	regrele(rp);
	u.u_lock &= ~DATLOCK;
	return(1);
}

proclock()
{
	u.u_procp->p_flag |= SSYS;
	u.u_lock |= PROCLOCK;
}

punlock()
{
	if ((u.u_lock&(PROCLOCK|TXTLOCK|DATLOCK)) == 0)
		return(0);
	u.u_procp->p_flag &= ~SSYS;
	u.u_lock &= ~PROCLOCK;
	if (u.u_lock & TXTLOCK)
		(void) tunlock();
	if (u.u_lock & DATLOCK)
		(void) dunlock();
	return(1);
}

/* <@(#)lock.c	6.2> */
