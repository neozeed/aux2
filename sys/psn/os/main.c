#ifndef lint	/* .../sys/psn/os/main.c */
#define _AC_NAME main_c
#define _AC_MAIN "@(#) Copyright (c) 1987, 1988, 1989 Apple Computer, Inc., 1985-87 UniSoft Corporation, All Rights Reserved.  {Apple version 2.5 90/03/13 12:33:17}"
#include <apple_notice.h>

#ifdef _AC_HISTORY
  static char *sccsid = "@(#)Copyright Apple Computer 1987, 1988, 1989\tVersion 2.5 of main.c on 90/03/13 12:33:17";
#endif		/* _AC_HISTORY */
#endif		/* lint */

char _ac_s[] = "Copyright (c) 1987 Apple Computer, Inc., 1985 Adobe Systems \
Incorporated, 1983-87 AT&T-IS, 1985-87 Motorola Inc., \
1980-87 Sun Microsystems Inc., 1980-87 The Regents of the \
University of California, 1985-87 Unisoft Corporation, All \
Rights Reserved.";

#define _AC_MODS

#ifdef HOWFAR
extern int T_main;
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
#include "sys/time.h"
#include "sys/signal.h"
#include "sys/user.h"
#include "sys/region.h"
#include "sys/proc.h"
#include "sys/var.h"
#include "sys/debug.h"
#include "sys/utsname.h"
#include "sys/cpuid.h"
#include "sys/cpunames.h"
#include "sys/module.h"
#endif lint

int	phsymem, maxmem;

struct vnode	*swapdev_vp;
struct vnode	*pipedev_vp;
extern struct utsname utsname;
short readonlyroot = 0;


extern	short icode[];
extern	szicode;
/*
 *	Initialization code.
 *	fork - process 0 to schedule
 *	     - process 1 execute bootstrap
 *	     - process 2 to handle page scheduling
 *
 *	loop at low address in user mode -- /etc/init
 *	cannot be executed.
 */

main()
{
	register int i;
	register struct user *up;
	register  struct  proc	*p;
	register  int  (**fptr)();
	extern	int	(*initfunc[])();
	extern int		callzero;
	extern int cmask, cdlimit;
	extern int compatflags;
	extern int m20cache;
	extern int lineAFault();
	extern pte_t *ptblalloc();

	extern struct kernel_info *kernelinfoptr;
	extern int kputc_on;
	extern int putread;
	extern int putindx;
	int verbose;

	up = &u;

	/* 
	 * Determine if kernel printf's need to be displayed - this information
	 * is obtained from the kernel-info structure. If printf's need to be
	 * suppressed, kputc_on is disabled.  -ABS
	 */
	verbose = kernelinfoptr->ki_flags & KI_VERBOSE;
	if (verbose)
		print_copyrights();
	else
		kputc_on = 0;

	/* same kernel runs on 680[2,3]0 */
	if (cputype != v.v_cpuver) {
		strcpy(utsname.machine, cpuvers[cputype]);
		v.v_cpuver = cputype;
	}
	printf("Running on %s\n", utsname.machine);

	if (fp881)
		printf("%s Floating Point Coprocessor ID 1\n", fpunames[fp881]);

	/* set up system process */

	proc[0].p_stat = SRUN;
	proc[0].p_flag |= SLOAD|SSYS;
	proc[0].p_nice = NZERO;
	proc[0].p_compatflags = compatflags;

	up->u_cmask = cmask;
	up->u_limit = cdlimit;
	up->u_rdir = NULL;
	up->u_user[0] = (int)lineAFault;
	up->u_user[1] = m20cache;

	for (i = 0; i < sizeof(up->u_rlimit)/sizeof(up->u_rlimit[0]); i++)
		up->u_rlimit[i].rlim_cur = up->u_rlimit[i].rlim_max =
		    RLIM_INFINITY;

	bcopy("sched", up->u_comm, 5);
	bcopy("sched", up->u_psargs, 5);

	up->u_stack[0] = STKMAGIC;

	startup();
#if OSDEBUG == YES
	callzero = 1;
#endif

	/*
	 * initialize kernel memory allocator
	 */
	kmem_init();
 
	/*
	 * call all initialization routines
	 */

	for (fptr = initfunc; *fptr; fptr++)
		(**fptr)();

	up->u_start = time.tv_sec;
	/*
	 * Setup credentials
	 */
	up->u_cred = crget();
	{
	register int i;

	for (i = 1; i < NGROUPS; i++)
		up->u_groups[i] = NOGROUP;
	} 

	/*	This call of swapadd must come after devinit in case
	 *	swap is moved by devinit.  It must also come after
	 *	dskinit so that the disk is don'ed.  This call should
	 *	return 0 since the first slot of swaptab should be used.
	 */

	if(swapadd(swapdev, (int)swaplow, swapcnt) != 0 && !readonlyroot)
		panic("startup - swapadd failed");

	/*
	 * Create init process, add the necessary regions,
	 * and enter scheduling loop (when scheduling enabled).
	 */
	if(newproc(0, 0)) {
		register preg_t *prp;
		register reg_t *rp;

		up->u_cstime = up->u_stime = up->u_cutime = up->u_utime = 0;
		rp = allocreg((struct inode *)NULL, RT_PRIVATE);
		prp = attachreg(rp, u.u_procp, (caddr_t)v.v_ustart, PT_DATA, Lx_RW);
		if (growreg(prp, btop(szicode), DBD_DFILL) < 0)
			panic("main.c - icode growreg failure.");
		regrele(rp);
		if(copyout((caddr_t)icode, (caddr_t)v.v_ustart, szicode))
			panic("main.c - copyout of icode failed.");

		/* 
		 * If verbose isn't set, set kputc_on so kernel
		 * printf's are henceforth displayed. -ABS.
		 */
		if (verbose == 0) {
			kputc_on = 1;
			putread = putindx;
		}

		/*	The following line returns to user mode
		 *	and transfers to location zero where we
		 *	have just copied the code to exec init.
		 */

		return(0);
	}

	if(newproc(0, SSYS)) {
		maxmem -= (up->u_ssize + 1);
		up->u_cstime = up->u_stime = up->u_cutime = up->u_utime = 0;
		bcopy("vhand", up->u_psargs,5);
		bcopy("vhand", up->u_comm, 5);
		vhand();
	}

	/*
	 *	Add init_last ... the last init routine to be called during
	 *		system startup, it is here so we can start system
	 *		processes
	 */
	{
		register  int  (**fptr)();
		extern	int	(*init_last[])();
		extern	int	init_lastl;
		register int i;

		for (i = 0, fptr = init_last; i < init_lastl && *fptr; fptr++)
			(**fptr)();
	}

	sched();
#ifdef lint
	return(0);
#endif lint
}


print_copyrights()
{
	extern char timestamp[];

	printf("A/UX Copyright 1987-89 by Apple Computer, Inc.\n");
	printf("    All rights reserved.\n");
	printf("Portions of this product have been previously copyrighted by,\n");
	printf("and are licensed from\n");
	printf("    AT&T-IS,\n");
	printf("    UniSoft Corporation,\n");
	printf("    The Regents of the University of California,\n");
	printf("    Sun Microsystems Inc.,   and\n");
	printf("    Adobe Systems.\n\n");

	printf("\245 A/UX RELEASE %s VERSION %s \245\n\n",
		utsname.release,utsname.version);
	printf("A/UX kernel created %s.\n\n", timestamp);
}

/* <@(#)main.c	6.6> */
