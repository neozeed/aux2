#ifndef lint	/* .../sys/PAGING/os/umachdep.c */
#define _AC_NAME umachdep_c
#define _AC_NO_MAIN "@(#) Copyright (c) 1985-87 UniSoft Corporation, All Rights Reserved.  {Apple version 2.12 90/04/19 10:55:13}"
#include <apple_notice.h>

#ifdef _AC_HISTORY
  static char *sccsid = "@(#)Copyright Apple Computer 1989\tVersion 2.12 of umachdep.c on 90/04/19 10:55:13";
#endif		/* _AC_HISTORY */
#endif		/* lint */

#define _AC_MODS
/*	@(#)umachdep.c	UniPlus VVV.2.1.26	*/

#ifdef HOWFAR
extern int	T_hardflt;
extern int	T_hardsegflt;
extern int	T_umachdep;
extern int	T_swtch;
extern int	T_meminit;
int T_realvtop = 0;
int	T_mmuinit=0;
extern int	T_dophys;
#endif HOWFAR

#include "sys/param.h"
#include "sys/uconfig.h"
#include "sys/types.h"
#include "sys/mmu.h"
#include "sys/pfdat.h"
#include "sys/sysmacros.h"
#include "sys/page.h"
#include "sys/systm.h"
#include "sys/dir.h"
#include "sys/signal.h"
#include "sys/time.h"
#include "sys/user.h"
#include "sys/errno.h"
#include "sys/region.h"
#include "sys/proc.h"
#include "sys/reg.h"
#include "sys/psl.h"
#include "sys/utsname.h"
#include "sys/ipc.h"
#include "sys/shm.h"
#include "sys/var.h"
#include "sys/acct.h"
#include "sys/file.h"
#include "sys/vnode.h"
#include "sys/debug.h"
#include "setjmp.h"
#include "sys/uio.h"
#include "sys/buserr.h"
#include "sys/tuneable.h"
#include "compat.h"

#define LOW	(USTART&0xFFFF)		/* Low user starting address */
#define HIGH	((USTART>>16)&0xFFFF)	/* High user starting address */

Dt_t	*system_root = NULL;
Dt_t	*K_L1tbl = NULL;
char	k1to1tbl[LxTBLSIZE] = { 0 };

extern rp_t sup_rp,		/* long descriptor to load into srp */
	    cpu_rp;		/* " for crp */

extern int numbanks;
extern struct rambank rambanks[];

/*
 * Icode is the bootstrap program used to exec init.
 */
short icode[] = {
				/*	 .=USTART		*/
	0x2E7C,	HIGH, LOW+0x400,/*	 movl	#USTART+0x400,sp*/
	0x2F3C,	HIGH, LOW+0x26,	/*	 movl	#envp,-(%sp)	*/
	0x2F3C,	HIGH, LOW+0x22,	/*	 movl	#argp,-(%sp)	*/
	0x2F3C,	HIGH, LOW+0x2A,	/*	 movl	#name,-(%sp)	*/
	0x42A7,			/*	 clrl	-(%sp)		*/
	0x303C,	0x3B,		/*	 movw	#59.,d0		*/
	0x4E40,			/*	 trap	#0		*/
	0x60FE,			/*	 bra	.		*/
	HIGH,	LOW+0x2A,	/* argp: .long	name		*/
	0,	0,		/* envp: .long	0		*/
	0x2F65,	0x7463,	0x2F69,	/* name: .asciz	"/etc/init"	*/
	0x6E69,	0x7400
};
int szicode = sizeof(icode);

/*
 * mmusetup -
 *  allocate space for static translation tables and kernel udot,
 *	then clear tables and udot
 *	note: mch.s clears from edata to ptob(btop(tblstart))
 *  Then set up tables, but don't actually turn on the mmu.
 */
mmusetup(tblstart)
caddr_t tblstart;		/* _end */
{	register int i, j, tblix;
	register unsigned int cp;
	register Dt_t *Lxp;

	cp = (int)ptround(tblstart);
	system_root = (Dt_t *)cp;

	/*
	 * Allocate the L1 table for the kernel
	 */
	K_L1tbl = (Dt_t *)cp;
	cp += LxTBLSIZE;

	/* Round to next click after tables, then clear tables, udot.
	 * Note that udot precedes tblstart.
	 */
	{	register unsigned int click;

		click = btop(cp);
		cp = ptob(click);
		for (; click >= btop((int)tblstart); click--)
			clear((caddr_t)ptob(click), ptob(1));
	}

	/* Fill it in as either
	 *  invalid or early termination (the ET stuff will be fixed later,
	 *  where indicated)
	 * Note tat k1to1tbl is zero to begin with; we mark those L1 entries
	 *  where the mapping is 1-1 (phys mem, ROM and higher)
	 * Map real memory "straight through".
	 */
	for (i=0; i < numbanks; i++)
	{	register int k;

		if (rambanks[i].ram_real)
		{	/* Get # L2 tables for phys mem */
			j = L1ix(rambanks[i].ram_base);
			k = L1ix(rambanks[i].ram_end-1);
			for (Lxp = &K_L1tbl[j]; j<= k; j++)
			{	wtete(Lxp++, j << L1SHIFT, Lx_RW);
				k1to1tbl[j] = 1;
			}
		}
	}
	i = j;
	tblix = L1ix(KV_TEXT);
	for (; i < tblix; i++)
		*(int *)Lxp++ = 0;			/* Invalidate */

	/* Kernel virtual text */
	tblix = L1ix(KV_TEXT+K_TEXT_SIZE);
	for (; i <= tblix; i++)
		wtete(Lxp++, i << L1SHIFT, Lx_RW);
	tblix = L1ix(KV_DATA);
	for (; i < tblix; i++)
		*(int *)Lxp++ = 0;			/* Invalidate */

	/* Kernel virtual data */
	tblix = L1ix(KV_DATA+K_DATA_SIZE);
	for (; i <= tblix; i++)
		wtete(Lxp++, i << L1SHIFT, Lx_RW);
	tblix = L1ix(KV_BSS);
	for (; i < tblix; i++)
		*(int *)Lxp++ = 0;			/* Invalidate */

	/* Kernel virtual bss */
	tblix = L1ix(KV_BSS+K_BSS_SIZE);
	for (; i <= tblix; i++)
		wtete(Lxp++, i << L1SHIFT, Lx_RW);
	tblix = L1ix(KV_ROM);
	for (; i < tblix; i++)
		*(int *)Lxp++ = 0;			/* Invalidate */

	/* Map the rest "straight through" */
	tblix = L1ix(KV_IO);
	for (; i < tblix; i++)
	{	wtete(Lxp++, i << L1SHIFT, Lx_RW);
		k1to1tbl[i] = 1;
	}
	/* The I/O and beyond is not subject to cacheing - only for 020/030 */
	/* Note the "<=" */
	tblix = L1ix(KV_END);
	for (; i <= tblix; i++)
	{	wtete(Lxp++, i << L1SHIFT, Lx_RW|PG_CI);
		k1to1tbl[i] = 1;
	}

	/*
	 * Set up supervisor root pointer, use shared global, which is
	 *  valid on 020 only; for 030, this is not relevant
	 * The CPU root pointer is loaded when we turn on the MMU.
	 */
	wtrp(sup_rp, (RT_VALID | RT_SG), NL2PL1, (long)&system_root[0]);
	wtrp(cpu_rp, RT_VALID, NL2PL1, (long)&system_root[0]);

	return(cp);			/* end of udot for proc[0] */
}

/*
 * iocheck - check for an I/O device at given address
 */
iocheck(addr)
caddr_t addr;
{
	register int *saved_jb;
	register int i;
	jmp_buf jb;

	saved_jb = nofault;
	if (!setjmp(jb)) {
		nofault = jb;
		i = *addr;
		i = 1;
	} else
		i = 0;
	nofault = saved_jb;
	return(i);
}

/*
 * busaddr - Save the info from a bus or address error.
 */
/* VARARGS */
busaddr(frame)
struct {
	long regs[4];	/* d0,d1,a0,a1 */
	int baddr;	/* bsr return address */
	short fcode;	/* fault code */
	int aaddr;	/* fault address */
	short ireg;	/* instruction register */
} frame;
{
	u.u_fcode = frame.fcode;
	u.u_aaddr = frame.aaddr;
	u.u_ireg = frame.ireg;
}

/*
 * dophys - machine dependent set up portion of the phys system call
 */
dophys(phnum, laddr, bcount, phaddr)
unsigned phnum, laddr, phaddr;
register unsigned bcount;
{
	register struct phys *ph;
	register struct user *up;
	register struct region *rp;
	register struct pregion *prp;
	preg_t *vtopreg();

	up = &u;
	if (phnum >= v.v_phys)
		goto bad;

	if (L2off(laddr))
		goto bad;

	ph = &up->u_phys[(short)phnum];
/* if a region is already phys'ed in, then detach it */
	if (ph->u_regp != NULL)
	{	reglock(ph->u_regp);
		for (prp = u.u_procp->p_region; prp->p_reg; prp++)
		if (prp->p_type&PT_PHYSCALL && prp->p_regva == (caddr_t)ph->u_phladdr)
			break;
		if (prp->p_reg == NULL)
			panic("missing phys()");
		detachreg(prp, up->u_procp);
		ph->u_regp = NULL;
	}
	ph->u_phladdr = 0;
	ph->u_phsize = 0;
	ph->u_phpaddr = 0;
/* bcount is zero means we were to clear the mapping and return */
	if (bcount == 0)
		return;

/* test for overflow after btop(bcount) or last address of phys region */
	if (btop(bcount) == 0 || (bcount + laddr) < bcount)
		goto bad;

/* allocate attach and validate a phys region */
	if((rp = allocreg((struct vnode *)NULL, RT_PHYSCALL)) == NULL)
		goto bad;

	/*
	 * Is the attach address already in use
	 */
	if (vtopreg(up->u_procp, (caddr_t)laddr) != (preg_t *)NULL)
	{	freereg(rp);
		goto bad;
	}

	ASSERT(rp->r_noswapcnt >= 0);
	rp->r_noswapcnt++;

/* attachreg checks segment alignment of laddr */
	if ((prp = attachreg(rp, u.u_procp, (caddr_t)laddr, PT_PHYSCALL, Lx_RW))==NULL)
	{	freereg(rp);
		goto bad;
	}
/* growreg fills in zeroed page tables */
	if((growreg(prp, (int)btop(bcount), DBD_NONE)) < 0)
	{	detachreg(prp, u.u_procp);
		goto bad;
	}
	regrele(rp);
	ph->u_regp = rp;
/* pvalidate maps ptes to phys area (phaddr) */
	pvalidate(rp, (int)btop(bcount), (int)btotp(phaddr));
	ph->u_phladdr = laddr;
	ph->u_phsize = btop(bcount);
	ph->u_phpaddr = phaddr;
	return;
bad:
	up->u_error = EINVAL;
}

/* 
 * pvalidate loads the zero filled pts with actual values
 * By convention, we don't cache anything beyond the beginning of I/O space,
 *  while everything before it is cached.  This is probably system-dependent.
 */
pvalidate(rp, pg, phaddr)
register struct region *rp;
{
	register pte_t		**plist;
	register pte_t		*pt;

	plist = rp->r_plist;
	pt = *plist;
	while (pg--)
	{	if (pt >= &(plist[0][NPGPT]))
		{	if (*(++plist) == NULL)
			{	printf("pvalidate(%x,%x,%x) failed\n",
							rp, pg, phaddr);
				break;
			}
			pt = *plist;
		}
		if (phaddr < btop(KV_IO))
			wtpte(pt, phaddr++, PG_RW);
		else
			wtpte(pt, phaddr++, PG_RW|PG_CI);

		pg_setlock(pt);
		pt++;
	}
	clratb(USRATB);
}

/*
 * addupc - Take a profile sample.
 */
addupc(pc, p, incr)
unsigned pc;
register struct {
	short	*pr_base;
	unsigned pr_size;
	unsigned pr_off;
	unsigned pr_scale;
} *p;
{
	union {
		int w_form;		/* this is 32 bits on 68000 */
		short s_form[2];
	} word;
	register short *slot;

	slot = &p->pr_base[((((pc - p->pr_off) * p->pr_scale) >> 16) + 1)>>1];
	if ((caddr_t)slot >= (caddr_t)(p->pr_base) &&
	    (caddr_t)slot < (caddr_t)((unsigned)p->pr_base + p->pr_size)) {
		if ((word.w_form = fuword((caddr_t)slot)) == -1)
			u.u_prof.pr_scale = 0;	/* turn off */
		else {
			word.s_form[0] += (short)incr;
			(void) suword((caddr_t)slot, word.w_form);
		}
	}
}

#ifdef notdef
/*
 * dump the present contents of the stack
 */
dumpstack(ret)
{
	register unsigned short *ip;

	ip = (unsigned short *)&ret;
	if (ret != 0)
		pre_panic();
	printf("\n%x  ", ip);
	while (ip < (unsigned short *)((int)&u+ptob(v.v_usize))) {
		if (((int)ip & 31) == 0)
			printf("\n%x  ", ip);
		printf(" %x", *ip++);
	}
	printf("\n");
	if (ret != 0)
		panic("**** ABORTING ****");
}
#endif notdef


/* ARGSUSED */
dumpmm(f)
{
#if defined(DEBUG)
	register ste_t *tp;

	if (f == -1)
		return;
/*	printf("ktbla = 0x%x   kstbl = 0x%x\n", ktbla, kstbl);*/
/*	for (tp = ktbla ; tp < &ktbla[KNTBLA] ; tp++) {
		printf("ktbla[%d]:\t", tp - ktbla);
		switch (tp->segm.ld_dt) {
		case DTLINV:
			printf("INVALID\n");
			break;
		case DTPD:
			printf("maps %d segments of physmem %s at 0x%x\n",
				tp->segm.ld_limit + 1,
				tp->segm.ld_lu ? "ending" : "starting",
				tp->segm.ld_addr);
			break;
		case DT4B:
			printf("ERROR - type is DT4B\n");
			break;
		case DT8B:
			printf("points to segtbl at 0x%x with %s %d entries\n",
				tp->segm.ld_addr,
				tp->segm.ld_lu ? "upper" : "lower",
				tp->segm.ld_limit + 1);
			break;
		}
	}*/
#endif
}

/* 
 * hardflt(): pagein routine
 * Is this a hard bus error? Or can we recover with pfault/vfault?
 * Can be called by the kernel on behalf of the user (e.g., move between
 *  kernel, user spaces)
 */
hardflt(regs)
struct lbuserr regs;
{
	register struct	 lbuserr *ap = &regs;
	register struct	 user	 *up = &u;
	register caddr_t ifaddr;
	register caddr_t dfaddr;
	register preg_t *prp, *stkprp;
	register int is_24bit;
#define supv_mode (ap->ber_regs[RPS] & RPS_SUP)

	is_24bit = ((u.u_procp->p_flag&(SMAC24|SROOT24)) == (SMAC24|SROOT24));
	/*
	 * DATA FAULT: If the DF bit is set in the ssw then fault in
	 * data page at the data cycle fault address (ber_faddr)
	 */
	if (ap->ber_ssw.df) {
		register unsigned int fc;
		register unsigned int stkbase;

		/* If the function code indicates a kernel mode fault, it is
		 *  a real bus fault.  Just return.
		 */
		if ((fc = ap->ber_ssw.fnc) == 0x5 || fc == 0x6)
			return(-1);
		dfaddr = (caddr_t)ap->ber_faddr;
		/* Strip address if we're in 24-bit mode due to later
		 *  checks
		 */
		if (is_24bit)
			dfaddr = (caddr_t)((unsigned long)dfaddr & 0x00ffffff);
		/*
		 * Was the fault due to user write protection at 
		 * the segment level (i.e. not from copy-on-write)?
		 */
		if ((ap->ber_ssw.rw == 0) && isreadonly(up->u_procp->p_root, dfaddr))
			return(-1);
		if (prp = vtopreg(u.u_procp, dfaddr))
		{	/*
			 * Check for a phys'd-in region.  The fault may be a
			 * valid bus error, but in a region that claims the
			 * page is valid.  If so, we want to fail it here,
			 * to avoid an unending FAULT-CONTINUE loop.  If prp
			 * is NULL, this possibility is not meaningful.
			 */
			if (prp->p_reg->r_type&RT_PHYSCALL)
				return(-1);
		        stkbase = -1;
		} else
		{	/* The fault address is not in a region.  We may have
			 * to grow the stack, but because of the Mac environs,
			 * let's assure that the SP (saved on entry to the
			 * kernel in u.u_user[3]) is neither in a region nor in
			 * the stack region.  If so, assume implicit stack
			 * growth is called for.  Otherwise, the SP is in a
			 * non-stack region, and therefore, since automatic
			 * stack growth is not indicated, we fail (and cause
			 * a bus error).
			 */
		        stkbase = u.u_user[3];
			if ((stkprp = vtopreg(u.u_procp, stkbase)) &&
			    stkprp->p_type != PT_STACK)
				return(-1);
			stkbase -= 128;	/* Allow slop for movm's and such */
		}

		if (hardsegflt(dfaddr, stkbase, prp) == 0)
			return(-1);
		
		if (ap->ber_ssw.rm)
		        ld_userpg(dfaddr, ap->ber_ssw.fnc);
		else
		        fl_userpg(dfaddr);
	}
	/*
	 * INSTRUCTION FAULT: faults on instruction prefetch in 
	 * the pipeline occur because the instruction has been paged out. 
	 * If the FC or FB bit is set we need to fault in the page 
	 * at the appropriate PC. If we faulted in a text page set 
	 * the appropriate rerun bit in the ssw.
	 * If the fault occurs at the end of the text segment and the FB/RB bits
	 * are on, clear the RB bit and continue, hoping that this is really just
	 * some oddity on the part of early 020 parts.
	 */
	if (ap->ber_ssw.fc || ap->ber_ssw.fb) {
		if (supv_mode)	      /* can't deal with kernel mode instruction faults */
			return(-1);
		prp = NULL;		/* May be set below */
		if ((ap->ber_format & FMT_FMASK) == FMT_LONG)
		{	ifaddr = (caddr_t)ap->ber_baddr;
			/* Strip address if we're in 24-bit mode */
			if (is_24bit)
				ifaddr = (caddr_t)((unsigned long)ifaddr & 0x00ffffff);
		} else
		{	ifaddr = (caddr_t)(ap->ber_regs[PC] + 4);
			/* Strip address if we're in 24-bit mode */
			if (is_24bit)
				ifaddr = (caddr_t)((unsigned long)ifaddr & 0x00ffffff);
			if (ap->ber_ssw.fb && ap->ber_ssw.rb)
			{	/* If the ifaddr is right at the end of the
				 * segment, and we are trying to fill stage B,
				 * just continue, knocking down the RB bit
				 */
				if ((prp = vtopreg(u.u_procp, ifaddr-4)) &&
				    ifaddr == (prp->p_regva +
					       ptob(prp->p_reg->r_pgsz)))
					{	ap->ber_ssw.rb = 0;
						return(0);
					}
			}
		}
		if (ap->ber_ssw.fb)
			ap->ber_ssw.rb = 1;
		else if (ap->ber_ssw.fc) {
			ap->ber_ssw.rc = 1;
			ifaddr -= 2;
		}
		if ( !ap->ber_ssw.df || (btop((int)dfaddr) != btop((int)ifaddr))) {
			if (hardsegflt(ifaddr, -1, prp) == 0)
				return(-1);
			fl_userpg(ifaddr);
		}
	} 
	return(0);	/* Successful pagein completed. */
}
#undef supv_mode



static hardsegflt(vaddr, usp, prp)
register caddr_t vaddr;
register caddr_t usp;
register preg_t *prp;
{	register struct proc *p;
	register uint vdbdtype;
	register pte_t *ptp;

	p = u.u_procp;
	ptp = Pt_ent(p->p_root, vaddr);

	if ((int)ptp <= 0 ||
	    (ptp->pgi.pg_pte == 0 && dbdget(ptp)->dbd_type == DBD_NONE))
	{	if (vaddr < usp || !grow((unsigned)vaddr))
			return(0);
		ptp = Pt_ent(p->p_root, vaddr);

		if ((int)ptp <= 0 ||
		    (ptp->pgi.pg_pte == 0 && dbdget(ptp)->dbd_type == DBD_NONE))
			return(0);
	}
	if (!ptp->pgm.pg_v)
		return(vfault(vaddr, ptp, prp));

	if (ptp->pgm.pg_cw)
		return(pfault(vaddr, ptp, prp));

	/*
	 * Know from above check that the page in question is valid,
	 *  so we won't check again.  At this point, the fault must be
	 *  due to an out of date cache entry.  This may happen when an
	 *  indexed jmp (switch statement) falls near a page boundary (for
	 *  example).
	 */
	return(1);
}

#ifdef	HOWFAR
char	*signames[] = {
	"signal 0",
	"SIGHUP",
	"SIGINT",
	"SIGQUIT",
	"SIGILL",
	"SIGTRAP",
	"SIGIOT",
	"SIGEMT",
	"SIGFPE",
	"SIGKILL",
	"SIGBUS",
	"SIGSEGV",
	"SIGSYS",
	"SIGPIPE",
	"SIGALRM",
	"SIGTERM",
	"SIGUSR1",
	"SIGUSR2",
	"SIGCLD",
	"SIGPWR",
	"SIGTSTP",
	"SIGTTIN",
	"SIGTTOU",
	"SIGSTOP",
	"SIGXCPU",
	"SIGXFSZ",
	"SIGVTALRM",
	"SIGPROF",
	"SIGWINCH",
	"SIGCONT",
	"Signal 30",
	"Signal 31",
	"Signal 32"
};

char	*
signame(n)
{
	if (n >= 0 && n < sizeof(signames) / sizeof(signames[0]))
		return (signames[n]);
	else
		return ("unknown (signal out of range)");
}
#endif HOWFAR


/*
 * Use pmove (in vtopte_sr()) on 020/030.  Will need something different
 *  for the 040.
 */
caddr_t
realvtop(fcode, addr)
register int fcode;
register int addr;
{	register pte_t	*ptep;
	register caddr_t physaddr;
	short		 psr;
	pte_t		*vtopte_sr();

	ptep = vtopte_sr(fcode, addr, &psr);

	if (psr & 0xE400)
		return((caddr_t)-1);

	switch (psr & 0xF)
	{	case 0:	/* Early termination of RP */
			physaddr = (caddr_t)((sup_rp.rpi.rp_addr) + addr);
			break;

		case 1: /* Early termination, L1 */
			physaddr = (caddr_t)((int)DT_addr(ptep) | (addr&L1OFFMASK));
			break;

		case 2: /* Early Termination, L2 (not used) */
			physaddr = (caddr_t)((int)DT_addr(ptep) | (addr&L2OFFMASK));
			break;

		case 3:	/* Normal page table stuff */
			physaddr = (caddr_t)((int)Pt_addr(ptep) | (addr&POFFMASK));
			break;

		default:
			panic("realvtop: invalid search depth\n");
	}
	return(physaddr);
}

pte_t *
svtopte(vaddr)
{	extern pte_t *realvtopte();

	return(realvtopte(FCSUPVD, vaddr));
}

pte_t *
uvirttopte(vaddr)
{	extern pte_t *realvtopte();

	return(realvtopte(FCUSERD, vaddr));
}

caddr_t
realsvtop(vaddr)
{
	return(realvtop(FCSUPVD, vaddr));
}
