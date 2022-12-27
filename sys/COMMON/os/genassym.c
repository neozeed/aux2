#ifndef lint	/* .../sys/COMMON/os/genassym.c */
#define _AC_NAME genassym_c
#define _AC_MAIN "@(#) Copyright (c) 1985-87 UniSoft Corporation, All Rights Reserved.  {Apple version 2.4 90/03/13 11:54:20}"
#include <apple_notice.h>

#ifdef _AC_HISTORY
  static char *sccsid = "@(#)Copyright Apple Computer 1989\tVersion 2.4 of genassym.c on 90/03/13 11:54:20";
#endif		/* _AC_HISTORY */
#endif		/* lint */

#define _AC_MODS
/*	@(#)genassym.c	UniPlus 2.1.3	*/
#include <sys/param.h>
#include <sys/types.h>
#include <sys/sysinfo.h>
#include <sys/signal.h>
#include <sys/errno.h>
#include <sys/dir.h>
#include <sys/mmu.h>
#include <sys/time.h>
#include <sys/page.h>
#include <sys/user.h>
#include <sys/reg.h>
#include <sys/region.h>
#include <sys/proc.h>
#include <sys/oss.h>


main()
{
	register struct sysinfo *s = (struct sysinfo *)0;
	register struct user *u = (struct user *)0;
	register struct proc *p = (struct proc *)0;
	register struct oss *o = (struct oss *)0;

	printf("\tglobal\tsysinfo\n");
	printf("\tset\tV_INTR%%,%d\n", &s->intr);
	printf("\tset\tU_USER%%,%d\n", &u->u_user[0]);

	printf("\tset\tU_FPSAVED%%,%d\n", &u->u_fpsaved);
	printf("\tset\tU_FPSTATE%%,%d\n", &u->u_fpstate[0]);
	printf("\tset\tU_FPSYSREG%%,%d\n", &u->u_fpsysreg[0]);
	printf("\tset\tU_FPDREG%%,%d\n", &u->u_fpdreg[0][0]);

	printf("\tset\tU_FLTSP%%,%d\n", &u->u_fltsp);
	printf("\tset\tU_FLTRV%%,%d\n", &u->u_fltrv);
	printf("\tset\tU_PROCP%%,%d\n", &u->u_procp);
	printf("\tset\tU_SR%%,%d\n", &u->u_sr);
	printf("\tset\tU_TRAPTYPE%%,%d\n", &u->u_traptype);
	printf("\tset\tP_SR%%,%d\n", &p->p_sr);
	printf("\tset\tP_SIG%%,%d\n", &p->p_sig);
	printf("\tset\tP_ROOT%%,%d\n", &p->p_root);
	printf("\tset\tP_UPTBL%%,%d\n", &p->p_uptbl);

	printf("\tset\tO_OSSINTPND%%,%d\n", &o->oss_intpnd);

	exit(0);	/* sai -6/17/85- Vax make blows up without this */
}
