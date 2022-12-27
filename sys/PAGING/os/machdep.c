#ifndef lint	/* .../sys/PAGING/os/machdep.c */
#define _AC_NAME machdep_c
#define _AC_NO_MAIN "@(#) Copyright (c) 1985-87 UniSoft Corporation, All Rights Reserved.  {Apple version 2.5 90/03/13 12:04:05}"
#include <apple_notice.h>

#ifdef _AC_HISTORY
  static char *sccsid = "@(#)Copyright Apple Computer 1989\tVersion 2.5 of machdep.c on 90/03/13 12:04:05";
#endif		/* _AC_HISTORY */
#endif		/* lint */

#define _AC_MODS
/*	@(#)machdep.c	UniPlus VVV.2.1.12	*/

#ifdef HOWFAR
extern int T_machdep;
#endif HOWFAR

#ifdef lint
#include "sys/sysinclude.h"
#else lint
#include "sys/types.h"
#include "sys/param.h"
#include "sys/mmu.h"
#include "sys/sysmacros.h"
#include "sys/page.h"
#include "sys/systm.h"
#include "sys/errno.h"
#include "sys/time.h"
#include "sys/user.h"
#include "sys/reg.h"
#include "sys/acct.h"
#include "sys/region.h"
#include "sys/proc.h"
#include "sys/vnode.h"
#include "sys/debug.h"
#include "sys/uconfig.h"
#include "sys/ivec.h"
#include "sys/var.h"
#include "sys/psl.h"
#include "sys/buserr.h"
#include "sys/pathname.h"
#include "sys/uio.h"
#include "sys/trace.h"
#include <sys/clock.h>
#include <compat.h>
#endif lint

extern int (*lockrel)();

/*
 * Sets up the 680x0 autovectors for Mac IIfx machine.
 */
setup_autovectors ( )
{
    register int *dvp, *svp;
    extern short machineID;

    if (machineID == MACIIfx) {
	extern long autovec[];
	extern int AutoVecInt1(), AutoVecInt2(), AutoVecInt3();
	extern int AutoVecInt4(), AutoVecInt5(), AutoVecInt6();

	autovec[0] = (long) AutoVecInt1;
	autovec[1] = (long) AutoVecInt2;
	autovec[2] = (long) AutoVecInt3;
	autovec[3] = (long) AutoVecInt4;
	autovec[4] = (long) AutoVecInt5;
	autovec[5] = (long) AutoVecInt6;
    }

    /* Copy 680xx interrupt vectors. */
    for (svp = ivect, dvp = (int *) 0; svp < &ivect[N_IVECS]; )
	*dvp++ = *svp++;
}

/*
 * Clear registers on exec
 */
setregs(pnp)
struct pathname *pnp;
{
	register struct user *up;
	register char *cp;
	register i, (**rp)(), (**fptr)();
	extern (*execfunc[])();
	extern int compatflags;
	extern int m20cache;
	extern int lineAFault();

	up = &u;
	/*
	** If COMPAT_EXEC is set, then all children will have
	** the same compat flags. If it is not set, then the
	** compat flags are reset to the system defaults.
	*/
	if ( !((up->u_procp->p_compatflags) & COMPAT_EXEC) )
		up->u_procp->p_compatflags = compatflags;
	for (rp = &up->u_signal[0]; rp < &up->u_signal[NSIG]; rp++)
		if (((int)(*rp) & 1) == 0)
			*rp = 0;
	up->u_procp->p_sigcatch = 0;
#ifdef SIG43
	up->u_sigonstack = 0;
	up->u_onstack = 0;
	up->u_sigsp = 0;
#endif SIG43
	/* SP set for real in caller */
	for (cp = &regloc[0]; cp < &regloc[15]; )
		up->u_ar0[*cp++] = 0;
	up->u_ar0[PC] = up->u_exdata.ux_entloc & ~01;
#ifndef lint	/* "constant in conditional context" when HOWFAR turned on */
	TRACE(T_machdep,("New pc = 0x%x\n", up->u_ar0[PC]));
#endif lint
	up->u_eosys = REALLYRETURN;

	for (i = 0; i < NOFILE; i++) {
		if ((up->u_pofile[i]&EXCLOSE) && up->u_ofile[i] != NULL) {
			(void)(*lockrel)(up->u_ofile[i]);
			closef(up->u_ofile[i]);
			up->u_ofile[i] = NULL;
		}
	}
	for (fptr = execfunc; *fptr; fptr++)
		(**fptr)();

	for (i = 0; i < USERSIZ; i++)
		up->u_user[i] = 0;
	u.u_user[0] = (int)lineAFault;
	u.u_user[2] = m20cache;

	/*
	 * Remember file name for accounting.
	 */
	up->u_acflag &= ~AFORK;
#ifdef POSIX
	up->u_procp->p_flag &= ~SFORK;
#endif POSIX
	i = MIN(COMMSIZ, pnp->pn_pathlen);
	bcopy((caddr_t)pnp->pn_path, (caddr_t)up->u_comm, i);
	if (i < COMMSIZ)
		up->u_comm[i] = 0;
}

/*
 * dump out the core of a process
 */
coredump(vp)
register struct vnode *vp;
{
	register struct user *up;
	register preg_t	*prp;
	register proc_t *pp;
	register int	gap;
	int offset = 0;

	up = &u;
	/*	Put the region sizes into the u-block for the
	 *	dump.
	 */
	
	pp = up->u_procp;

	if(prp = findpreg(pp, PT_TEXT))
		up->u_tsize = prp->p_reg->r_pgsz;
	else
		up->u_tsize = 0;
	
	/*	In the following, we do not want to write
	**	out the gap but just the actual data.  The
	**	caluclation mirrors that in loadreg and
	**	mapreg which allocates the gap and the
	**	actual space separately.  We have to watch
	**	out for the case where the entire data region
	**	was given away by a brk(0).
	*/

	if(prp = findpreg(pp, PT_DATA)){
		up->u_dsize = prp->p_reg->r_pgsz;
		gap = btotp((caddr_t)up->u_exdata.ux_datorg - prp->p_regva);
		if(up->u_dsize > gap)
			up->u_dsize -= gap;
		else
			up->u_dsize = 0;
	} else {
		up->u_dsize = 0;
	}

	if(prp = findpreg(pp, PT_STACK)){
		up->u_ssize = prp->p_reg->r_pgsz;
	} else {
		up->u_ssize = 0;
	}

	/*	Check the sizes against the current ulimit and
	**	don't write a file bigger than ulimit.	If we
	**	can't write everything, we would prefer to
	**	write the stack and not the data rather than
	**	the other way around.
	*/

	if(USIZE + up->u_dsize + up->u_ssize > dtop(up->u_limit)){
		up->u_dsize = 0;
		if(USIZE + up->u_ssize > dtop(up->u_limit))
			up->u_ssize = 0;
	}

	/*	Write the u-block to the dump file.
	 */

	/*
	 * make register pointer relative for adb
	 */
	up->u_ar0 = (int *)((int)up->u_ar0 - (int)up);
	/* Add the process stack base for debuggers */
	up->u_stack[1] = pp->p_stack;
	u.u_error = vn_rdwr(UIO_WRITE, vp,
	    (caddr_t)up,
	    ptob(v.v_usize),
	    0, UIOSEG_KERNEL, IO_UNIT, (int *)0);
	up->u_ar0 = (int *)((int)up->u_ar0 + (int)up);
	offset += ptob(v.v_usize);

	/*	Write the data and stack to the dump file.
	 */
	
	if(up->u_dsize){
		if (u.u_error == 0) {
			u.u_error = vn_rdwr(UIO_WRITE, vp,
			    (caddr_t)up->u_exdata.ux_datorg,
			    ptob(up->u_dsize) - 
					poff((caddr_t)up->u_exdata.ux_datorg),
			    offset, UIOSEG_USER, IO_UNIT, (int *)0);
		}
TRACE(T_machdep, ("coredump data: base=0x%x count=0x%x offset=0x%x vp=0x%x\n",
up->u_exdata.ux_datorg, ptob(up->u_dsize) - poff((caddr_t)up->u_exdata.ux_datorg),
offset, vp));

		offset += ptob(up->u_dsize);
	}
	if(up->u_ssize){
		if (u.u_error == 0)
			u.u_error = vn_rdwr(UIO_WRITE, vp,
				(caddr_t)(pp->p_stack-ptob(up->u_ssize)),
				ptob(up->u_ssize),
			    offset, UIOSEG_USER, IO_UNIT, (int *)0);
TRACE(T_machdep, ("coredump stack: base=0x%x count=0x%x offset=0x%x vp=0x%x\n",
up->u_exdata.ux_datorg, ptob(up->u_ssize), offset, vp));
	}
TRACE(T_machdep, ("coredump returning\n"));
}


/*	Clear an entire atb.
 */

/*ARGSUSED*/
clratb(flags)
int	flags;
{
	/* UNFORTUNATELY clratb / invsatb must invalidate the whole cache
		by writing the root pointer currently
	 */
	fl_atc();
}


/*	Invalidate a single atb.
 */

/*ARGSUSED*/
invsatb(flags, vaddr, count)
int	flags;
caddr_t vaddr;
int	count;
{
	if (flags == SYSATB)
		fl_sysatc();
	else
		fl_usratc();
}


#ifdef TRACE
int	nvualarm;

vtrace()
{
	register struct a {
		int	request;
		int	value;
	} *uap;
	int vdoualarm();

	uap = (struct a *)u.u_ap;
	switch (uap->request) {

	case VTR_DISABLE:		/* disable a trace point */
	case VTR_ENABLE:		/* enable a trace point */
		if (uap->value < 0 || uap->value >= TR_NFLAGS)
			u.u_error = EINVAL;
		else {
			u.u_rval1 = traceflags[uap->value];
			traceflags[uap->value] = uap->request;
		}
		break;

	case VTR_VALUE:		/* return a trace point setting */
		if (uap->value < 0 || uap->value >= TR_NFLAGS)
			u.u_error = EINVAL;
		else
			u.u_rval1 = traceflags[uap->value];
		break;

	case VTR_UALARM:	/* set a real-time ualarm, less than 1 min */
		if (uap->value <= 0 || uap->value > 60 * v.v_hz ||
		    nvualarm > 5)
			u.u_error = EINVAL;
		else {
			nvualarm++;
			timeout(vdoualarm, (caddr_t)u.u_procp->p_pid,
			    uap->value);
		}
		break;

	case VTR_STAMP:
		trace(TR_STAMP, uap->value, u.u_procp->p_pid);
		break;
	}
}

vdoualarm(arg)
	int arg;
{
	register struct proc *p;

	p = pfind(arg);
	if (p)
		psignal(p, SIGURG);
	nvualarm--;
}

/*VARARGS*/
trace1(args)
	int args;
{
	register int nargs;
	register int x;
	register int *argp, *tracep;

	nargs = 4;
	x = tracex % TRCSIZ;
	if (x + nargs >= TRCSIZ) {
		tracex += (TRCSIZ - x);
		x = 0;
	}
	argp = &args;
	tracep = &tracebuf[x];
	tracex += nargs;
	*tracep++ = (time.tv_sec%1000)*1000 + (time.tv_usec/1000);
	nargs--;
	do
		*tracep++ = *argp++;
	while (--nargs > 0);
}
#endif TRACE
/* <@(#)machdep.c	6.4> */
