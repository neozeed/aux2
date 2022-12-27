#ifndef lint	/* .../sys/psn/os/startup.c */
#define _AC_NAME startup_c
#define _AC_NO_MAIN "@(#) Copyright (c) 1985-87 UniSoft Corporation, All Rights Reserved.  {Apple version 2.9 90/03/13 12:34:05}"
#include <apple_notice.h>

#ifdef _AC_HISTORY
  static char *sccsid = "@(#)Copyright Apple Computer 1989\tVersion 2.9 of startup.c on 90/03/13 12:34:05";
#endif		/* _AC_HISTORY */
#endif		/* lint */

#define _AC_MODS
/*	@(#)startup.c	UniPlus VVV.2.1.24	*/
/*
 *	Initial system startup code
 *
 *	Copyright 1986 Unisoft Corporation of Berkeley CA
 *
 *
 *	UniPlus Source Code. This program is proprietary
 *	with Unisoft Corporation and is not to be reproduced
 *	or used in any manner except as authorized in
 *	writing by Unisoft.
 *
 */

#ifdef	HOWFAR
int	T_startup=0;
extern int T_meminit;
extern int T_availmem;
#endif HOWFAR
#ifdef lint
#include "sys/sysinclude.h"
#else lint
#include "sys/types.h"
#include "sys/mmu.h"
#include "sys/uconfig.h"
#include "sys/param.h"
#include "sys/sysmacros.h"
#include "sys/dir.h"
#include "sys/signal.h"
#include "sys/time.h"
#include "sys/user.h"
#include "sys/errno.h"
#include "sys/utsname.h"
#include "sys/ioctl.h"
#include "sys/tty.h"
#include "sys/var.h"

#include "sys/psl.h"
#include "sys/callout.h"

#include "sys/map.h"

#include "sys/page.h"
#include "sys/pfdat.h"
#include "sys/buf.h"
#include "sys/region.h"
#include "sys/proc.h"
#include "sys/systm.h"

#include "sys/ivec.h"
#include "sys/tuneable.h"
#include "sys/debug.h"
#endif lint
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/vnode.h>
#include <sys/file.h>
#include <svfs/inode.h>
#include <sys/mount.h>
#include <sys/mbuf.h>
#include <sys/locking.h>
#include <sys/flock.h>
#include <sys/conf.h>
#include <sys/heap_kmem.h>
extern unsigned long *Core;
extern struct freehdr *FreeHdr;
extern struct sem_undo **sem_undo;
extern struct tty *pts_tty;
extern ptsopen();
extern int cdevcnt;
extern struct pt_ioctl *pts_ioctl;

/*
 *	Autoconfig requires that the kernel .text/.data and .bss be
 *	able to be increased in size AFTER the 'final' link of the
 *	kernel. In order to do this they are mapped into kernel virtual
 *	addresses that are non-contiguous. These addresses are chosen so
 *	they do not conflict with any existing hardware in the systems
 *	physical address space (everything else is mapped 1-1 virtual
 *	to physical initially). They are set in the makefile (see
 *	TEXTSTART, DATASTART and BSSSTART). After the kernel is linked
 *	a program 'patch_kernel' is run on it. It changes the COFF headers
 *	so that the physical address fields for each section are such that
 *	the four sections are loaded consecutively (on page boundarys)
 *	the booter reads these fields and uses them to load the code.
 *	Below is an example of how this particular kernel is loaded
 *
 *	Section		Physical address	Virtual address
 *	=======		================	===============
 *	
 *	pstart		0x4000			0x4000
 *	.text		0xe000			0x10000000
 *	.data		0x2d000			0x11000000
 *	.bss		0x45000			0x12000000
 *
 *	When patch_kernel fills in the COFF headers it also fills in
 *	sectinfo below with the appropriate information. Great care must
 *	be taken prior to the mmu being turned on to make sure that 
 *	variables and routines called must lie within the section pstart
 *	or the the appropriate routines below are used to access them.
 *	Variables that are assumed to be in pstart are marked XXXX below.
 *
 *	
 */

struct sectinfo sectinfo[3] = {0};

#define RVTL(x)		rv_long(&(x), 0)	/* read virtual .text long */
#define RVDL(x)		rv_long(&(x), 1)	/* read virtual .data long */
#define RVBL(x)		rv_long(&(x), 2)	/* read virtual .bss long */
#define RVTS(x)		rv_short(&(x), 0)	/* read virtual .text short */
#define RVDS(x)		rv_short(&(x), 1)	/* read virtual .data short */
#define RVBS(x)		rv_short(&(x), 2)	/* read virtual .bss short */

#define WVTL(x, v)	wv_long(&(x), v, 0)	/* write virtual .text long */
#define WVDL(x, v)	wv_long(&(x), v, 1)	/* write virtual .data long */
#define WVBL(x, v)	wv_long(&(x), v, 2)	/* write virtual .bss long */

static long
rv_long(x, s)
long x, s;
{
	return(*(long *)(x+sectinfo[s].pstart-sectinfo[s].vstart));
}

static short
rv_short(x, s)
long x, s;
{
	return(*(short *)(x+sectinfo[s].pstart-sectinfo[s].vstart));
}

static 
wv_long(x, v, s)
long x, v, s;
{
	*(long *)(x+sectinfo[s].pstart-sectinfo[s].vstart) = v;
}

unsigned int *setup_rbv(), *map_through(), *map_pages();
static alloc_page_tables();

extern struct buf *sbuf;	/* start of buffer headers */
extern caddr_t	  iobufs;	/* start of buffers */
caddr_t physiobuf=0;		/* XXXX */

extern rp_t sup_rp,		/* supv root ptr (020/030) */
	    cpu_rp;		/* cpu root ptr (020/030) */
extern Dt_t	*system_root;	/* Pointer to the kernel's L1 table */

extern int maxmem;

extern int freemem;
int initial_freemem;
pte_t *upt_addr = 0,		/* XXXX Address of page table for U-blocks */
      *uptbl = 0;		/* XXXX Address of pte for U-block */
int physmem=0;			/* XXXX total number of pages of memory */

extern short physiosize;	/* XXXX size of the physiobuf (in pages) */

unsigned int *nendp = 0;	/* XXXX */

int symtabsize = 0;
int symtabaddr = 0;
int dbgtxtoffset = 0;
long romaline = 0;		/* XXXX MAC rom aline trap handler */
int zeroloc = 0;
extern Dt_t *K_L1tbl;

int numbanks = 0;
struct rambank rambanks[4] = {0};
struct pfdat_desc on_board_desc[4];

/* Total # bytes used before 1-1 mapping starts,
 * in the presence of rbv monitors
 */
#define RBV_MODE_BYTES	(336*1024)
/* # bytes used in rbv ram */
#define RBV_BYTES	(320*1024)
/* # bytes allocated at virtual zero for vectors, low mem globals, etc. */
#define LM_BYTES	(16*1024)

/* Setup kernel virtual address space.
 * Called from (nfs)mch.s after memory size has been calculated.
 */

vadrspace(tblend, up)
caddr_t tblend;		/* where to start using memory */
struct user *up;	/* where our current stack is .... to be used as */
{			/*	the idle udot */
	register int i;
	register struct rambank *rb;
	register unsigned int availmem;
	int ubase;
	register Dt_t *Lxp;
	register pte_t *Ptp;
	extern int kstart;
	extern int mtimer;
	extern int kernel_bank;
	extern int (*init_first[])(), init_firstl;
	extern int rbv_exists;
	extern int rbv_monitor;
	extern struct kernel_info *kernelinfoptr;

	/* 
	 * If MAXPMEM was set using kconfig, decrease the available
	 * amount of physical memory available for use.
	 */
	if (availmem = ptob(RVDL(v.v_maxpmem))) {
		register unsigned int banksize;

		for (rb = rambanks; rb < &rambanks[numbanks]; rb++) {
			if (!rb->ram_real)
				continue;
			if ((banksize = rb->ram_end - rb->ram_base) > availmem) {
				rb->ram_end -= banksize - availmem;
				availmem = 0;
			} else
				availmem -= banksize;
		 }
	}
	physmem = btop(rambanks[kernel_bank].ram_end);

	/* Round up to next page table boundary (040 requirement). */
	nendp = (unsigned int *)ptround(tblend);

	if (physiosize) {
		/* allocate physio pages */
		physiobuf = (caddr_t) ptob(btop((int) nendp));
		nendp = (unsigned int *)physiobuf + physiosize*(NBPP >> BPWSHFT);
	} else
		physiobuf = (caddr_t)NULL;

	/*
	 * Start building the kernel page tables.  Set up for each "segment"
	 * (see KV_* in mmu.h).  KV text, data, bss, and possibly symbols,
	 *  are provided.
	 * Physical memory has been mapped "early termination" in mmusetup().
	 * KV space is allocated dynamically; its pages may be reused but the
	 *  address space is never reclaimed.
	 * Therefore, we assume that the KV space begins on an
	 *  L2 boundary, and doesn't conflict with physical memory (thereby
	 *  limiting us to 256MB physical).  For now, we can live with it.
	 */
	if (rbv_exists && rbv_monitor)
		nendp = setup_rbv(nendp);

	/* If no monitor, take over low ram */
	if (!rbv_monitor)
		kstart = 0;

	/*
	 * Get the mac's version of the aline trap handler before
	 * we blow it away below.
	 */
	romaline = * (long *) (kstart + 0x28);

	/*
	 * Now, create the pagetables to map in .text/.data/.bss
	 */
	nendp = map_pages(KV_TEXT, KP_TEXT, K_TEXT_SIZE, nendp);
	nendp = map_pages(KV_DATA, KP_DATA, K_DATA_SIZE, nendp);
	nendp = map_pages(KV_BSS, KP_BSS, K_BSS_SIZE, nendp);

	if (symtabsize) {
		dbgtxtoffset = (int)KP_TEXT;
		nendp = map_pages(KV_SYMBOLS, symtabaddr, symtabsize, nendp);
	}

	/* Set up U-block page table entry.
	 *  If we have symbols, the L2 table should be there, but the page
	 *  table may not be.  Therefore, check both.
	 */
	Lxp = L1_ent(K_L1tbl, U_BLOCK);
	if (!Lx_valid(Lxp))
	{	nendp = (unsigned int *)ptround(nendp);
		wtl1e(Lxp, nendp, Lx_RW);
		nendp = (unsigned int *)((int)nendp + LxTBLSIZE);
	}

	Lxp = L2_ent(K_L1tbl, U_BLOCK);	/* Get the L2 entry */
	if (!Lx_valid(Lxp))
	{	wtl2e(Lxp, nendp, Lx_RW);
		upt_addr = (pte_t *)nendp;	/* Save for posterity and resume() */
		nendp = (unsigned int *)((int)nendp + PT_NSWAP_SZ);
	} else
		upt_addr = Pt_addr(Lxp);	/* Save for posterity and resume() */
	/* Point at th pre-allocated u-block (sent is as `up') */
	uptbl = &upt_addr[Ptix(U_BLOCK)];
	wtpte(uptbl, pnum(up), Lx_RW|DTPD);

	/*
	 * Turn on the MMU - the root pointers have been set up in mmusetup()
	 */
	turnon_mmu();

	*(int **)0xDDC = &zeroloc;   /* primary init needs this */

	setup_autovectors();

	/* write protect kernel text */
	Lxp = L2_ent(K_L1tbl, KV_TEXT);
	i = L2ix(KV_TEXT+K_TEXT_SIZE);
	for (; i >= 0; i--, Lxp++)
		Lxp->Dtm.Dt_W = 1;


	/*	No user page tables available yet.
	 */

	ptfree.pf_next = &ptfree;
	ptfree.pf_prev = &ptfree;
	pt2free.pf_next = &pt2free;
	pt2free.pf_prev = &pt2free;

	/*
	 * Allocate memory for paging tables
	 */

	ubase = btop((int)nendp);
	ubase = mktables(ubase);

	/* initialize the region table */

	reginit();

	/* Initialize the map of free kernel virtual address space 
	 *  and the kv limits.
	 */

	mapinit(sptmap, v.v_sptmap);

	kv_end = KV_SPT;
	softlimit = KV_SPT+MIN_SPTSIZE;
	hardlimit = KV_ROM;
	v.v_kernel_info = (char *) kernelinfoptr;

	/*
	 * Initialize callouts
	 */
	callfree = &callout[0];
	for (i = 1; i < v.v_call; i++)
		callout[i-1].c_next = &callout[i];

	/*
	 *	Call init_first routines ....
	 */
	for (i = 0; i < init_firstl && init_first[i]; i++)
		(*init_first[i])(&ubase);

	rambanks[kernel_bank].ram_start = ptob(ubase);	 /* update kernel region */


	/* initialize queues of free pages */
	{
		register struct rambank *rb;
		register struct pfdat_desc *pd;
		register int	memsize;

		availmem = 0;
		maxmem = 0;
		pd = on_board_desc;

		for (rb = rambanks; rb < &rambanks[numbanks]; rb++) {
			if ((memsize = rb->ram_end - rb->ram_start) > 0) {
				pd->pfd_dmaokay = 1;
				pd->pfd_dmaspeed = PFD_SLOW;
				pd->pfd_memokay = 1;
				pd->pfd_memspeed = PFD_FAST;
				memregadd(rb->ram_start, memsize, pd);
				if (rb->ram_real)
					maxmem += btop(rb->ram_end - rb->ram_base);
				availmem += memsize;
				pd++;
			}
		}
	}
	v.v_maxpmem = maxmem;

	memalloctables(btop(availmem));
	initmemfree();

	/*	Initialize process 0.
	 */
	p0init();

	/*	Indicate that we are the master cpu for timing.
	 *	This is checked in clock.
	 */

	mtimer = 1;

	/* Return to startup code, enter main()
	 *  NEVER RETURNS!
	 */
}

/* Setup proc[0] to look like a user process that has 
 * done a system call.
 */

p0init()
{
	register struct proc *p;
	register struct user *up;
	register pte_t *pt;

	p = proc;
	up = &u;
	/* Fetch the U-area for the "sched" process.
	 *  It's pointed to by uptbl.
	 */
	pt = Pt_ent(K_L1tbl, U_BLOCK);
	p->p_uptbl = *pt;
	/* Knock down the pmmu bits, leave dt entry */
	pg_clr(&p->p_uptbl, PG_M|PG_REF);
	/* Get U-block page pointer */
	p->p_addr = Pg_ptr(K_L1tbl, U_BLOCK);

	/* Kernel processes get the kernel page table */
	p->p_root = K_L1tbl;
	p->p_size = USIZE;
	p->p_stack = AUX_USR_STACK;	/* Pre-condition the stack location */
	clratb(SYSATB);			/* flush the mmu - needed? */
	up->u_stack[0] = STKMAGIC;
	up->u_procp = p;
}

/*
 * Create system space to hold page allocation and
 * buffer mapping structures and hash tables
 */

mktables(physpage)
register int physpage;
{
	register int	m;
	register int	i;
	register preg_t	*prp;
	extern int	pregpp;
	register caddr_t memp;


	memp = (caddr_t)ptob(physpage);

	/*
	 * Do the mbufs .... put them on a 'nice' boundary
	 */
	
	mbufbufs = (struct mbuf *)memp;
	memp += sizeof(struct mbuf) * (v.v_nmbufs+1);
	mbutl = (struct mbuf *)memp;
	memp += (NMBCLUSTERS * MCLBYTES) + MCLBYTES;

	/*
	 *	Allocate the process table
	 */

	proc = (struct proc *)memp;
	memp += sizeof(struct proc) * v.v_proc;
	v.ve_proc = (char *)&proc[1];
	v.ve_proctab = (char *)&proc[0];

	/*
	 *	Allocate the per-process semaphore undo table
	 */

	sem_undo = (struct sem_undo **)memp;
	memp += sizeof(struct sem_undo *) * v.v_proc;

	/*
	 *	Allocate the spt map
	 */

	sptmap = (struct map *)memp;
	memp += sizeof(struct map) * v.v_sptmap;

	/*
	 *	Allocate the region table
	 */

	region = (reg_t *)memp;
	memp += sizeof(reg_t) * v.v_region;

	/*
	 *	Allocate the file table
	 */

	file = (struct file *)memp;
	memp += sizeof(struct file) * v.v_file;
	v.ve_file = (char *)&file[v.v_file];

	/*
	 *	Allocate the mount table
	 */

	mounttab = (struct mount *)memp;
	memp += sizeof(struct mount) * v.v_mount;
	v.ve_mount = (char *)&mounttab[v.v_mount];

	/*
	 *	Allocate the physio buffers
	 */

	pbuf = (struct buf *)memp;
	memp += sizeof(struct buf) * v.v_pbuf;

	/*
	 *	Allocate the file lock table
	 */

	locklist = (struct locklist *)memp;
	memp += sizeof(struct locklist) * v.v_flock;

	/*
	 *	Allocate the callout table
	 */

	callout = (struct callout *)memp;
	memp += sizeof(struct callout) * v.v_call;
	v.ve_call = (char *)&callout[v.v_call];

	/*
	 *	Allocate pty data structures
	 */

	pts_tty = (struct tty *)memp;
	memp += sizeof(struct tty) * v.v_npty;
	pts_ioctl = (struct pt_ioctl *)memp;
	memp += sizeof(struct pt_ioctl) * v.v_npty;
	for (i = 0; i < cdevcnt; i++)
	if (cdevsw[i].d_open == ptsopen) {
		cdevsw[i].d_ttys = pts_tty;
		break;
	}

	/*
	 *	Allocate heap kmem data structures
	 *	N.B.:	the definitions for mbufbufs[], mbutl[], and Core[]
	 *		must occur in exactly that order.  Several macros depend
	 *		on it.
	 */

	Core = (unsigned long *)memp;
	memp += sizeof(unsigned long) + v.v_maxcore;
	FreeHdr = (struct freehdr *)memp;
	memp += sizeof(struct freehdr) * v.v_maxheader;

	/*	Allocate space for the pregion tables for each process
	 *	and link them to the process table entries.
	 *	The maximum number of regions allowed for is process is
	 *	3 for text, data, and stack plus the maximum number
	 *	of shared memory regions allowed.
	 */

	prp = (preg_t *)memp;
	memp += pregpp * sizeof(preg_t) * v.v_proc;
	for(i = 0  ;  i < v.v_proc  ;  i++, prp += pregpp)
		proc[i].p_region = prp;

	/* Return phys page number of the beginning of unallocated memory */
	return(btop((long)memp));
}

/*
 * Machine dependant startup
 * Called from main
 */
startup()
{
	register int i;
	register int (**fptr)();
	extern int (*init_normal[])();
	extern int (*init_second[])();
	extern int init_normall, init_secondl;

	/* Provide some of the promised init calls here (splhi) */
	for (i = 0, fptr = init_second; i < init_secondl && *fptr; fptr++)
		(**fptr)();
	for (i = 0, fptr = init_normal; i < init_normall && *fptr; fptr++)
		(**fptr)();

	/* 
	 * Now that we are about to take interrupts, set
	 * the Unix time from the Mac's real time clock.
	 */
	init_time (0);

	SPL0();

	printf("total memory size: %d bytes\n", ptob(maxmem));

	initial_freemem = freemem - USIZE;
	printf("available  memory: %d bytes\n", ptob(initial_freemem));
}


/*
 * Initialize clists
 */
struct	chead	cfreelist;
struct	cblock	*cfree;

cinit()
{
	register int n;
	register struct cblock *bp;
	extern putindx;

	/* allocate memory */
	n = btop(v.v_clist*sizeof(struct cblock));

	availrmem -= n;
	availsmem -= n;

	if (availrmem < tune.t_minarmem	 || availsmem  < tune.t_minasmem) {
		pre_panic();
		printf("cinit - can't get %d pages\n", n);
		panic("cannot allocate character buffers");
	}
	if((cfree=(struct cblock *)kvalloc(n, PG_ALL, -1)) == NULL)
		panic("cannot allocate character buffers");

	/* free all cblocks */
	bp = cfree;
	for(n = 0; n < v.v_clist; n++, bp++)
		putcf(bp);
	cfreelist.c_size = CLSIZE;

	/* print out messages so far */
}


/*
 * map_pages -
 *  set up MMU tables to cover from `vaddr' to `vaddr+size'
 * Don't overwrite Lx tables that have already been set up.
 * Assumed called prior to turnon_mmu().
 */
unsigned int *
map_pages(vaddr, physaddr, size, cp)
unsigned int vaddr, physaddr, size;
register unsigned int cp;
{	register int i1, i2, j1, j2, k1, k2;
	register Dt_t *Lxp, *Lxp1;
	register pte_t *Ptp;
	register unsigned int end_address;
	register unsigned int last_address;

	last_address = vaddr+size-1;
	i1 = L1ix(vaddr);
	i2 = L1ix(last_address);
	Lxp = &K_L1tbl[i1];		/* Allocated in mmusetup() */
	cp = (unsigned int)ptround(cp);
	if (physaddr & POFFMASK)
		panic("Squawk - mapping at non-page boundary!");;

	/*
	 * Loop through the L1 table, doing our thing.  If an L1 table
	 *  is early termination, just clobber it with a real L2 table.
	 *  (Note: L2_ptr reports early termination as a zero value, which
	 *  is "not valid".
	 */
	for (;i1++ <= i2; Lxp++)
	{	if (!Lx_valid(Lxp))
		{	/* Need to alloc L2 */
			wtl1e(Lxp, cp, Lx_RW);
			cp += LxTBLSIZE;
		}
		/* Have an L2 table.  Fill it */
		j1 = L2ix(vaddr);
		/* Index up to end of this L2 table */
		j2 = (int)(L2_round(vaddr+1)-1);
		end_address = min(last_address, (unsigned int)j2);
		j2 = L2ix(end_address);
		/* Now, get the proper entry within the table */
		Lxp1 = L2_ent(K_L1tbl, vaddr);
		for (; j1++ <= j2; Lxp1++)
		{	if (!Lx_valid(Lxp1))
			{	/* Need to alloc PT */
				wtl2e(Lxp1, cp, Lx_RW);
				cp += PT_NSWAP_SZ;
			}
			/* Have a Page Table.  Fill it */
			k1 = Ptix(vaddr);
			/* Index up to end of this pt */
			k2 = (int)(Pt_round(vaddr+1)-1);
			end_address = min(last_address, (unsigned int)k2);
			k2 = Ptix(end_address);
			/* Now, get the proper entry within the table */
			Ptp = Pt_ent(K_L1tbl, vaddr);
			for (; k1++ <= k2; Ptp++)
			{	if (!Ptp->pgm.pg_pfn)	/* Define it */
				{	Ptp->pgi.pg_pte = (physaddr | DTPD);
					physaddr += ptob(1);
				}
				vaddr += ptob(1);
			}
		}
	}
	return((unsigned int *)cp);
}

#ifdef HOWFAR
/*
 *	The following routines are a 'mini' printf, only included when
 *		debugging, they are used via AUTO_TRACE, prior to
 *		the mmu being turned on
 */

auto_trace(s, a)
register char *s;
int a;
{
	int *ap = &a;

	while (*s) {
		if (*s != '%') {
			auto_putchar(*s++);
			continue;
		}
		s++;
		switch(*s++) {
		case '%':
			auto_putchar('%');
			break;
		case 'x':
			auto_num(*ap++, 16);
			break;
		}
	}
}

auto_num(x, b)
int x;
{
	if (x == 0) {
		auto_putchar('0');
		return;
	}
	if (b == 16) {
		auto_x(x, 4);
	}
}

auto_x(n, s)
{
	int i;

	if (n) {
		auto_x(n>>s, s);
		i=n&((1<<s)-1);
		if (i >= 10) {
			auto_putchar('a'+i-10);
		} else {
			auto_putchar('0'+i);
		}
	}
}

/*
 *	NOTE: this routine (to output a character with the MMU turned
 *		off and using ONLY local variables) is system specific
 *		and is only used to get the system debugged enough to
 *		turn the mmu on
 */

#include <sys/scc.h>

#define	W5ON	(W5TXENABLE | W5RTS | W5DTR)	/* turn on to talk */

char sc_d5 = 0;

auto_putchar(c)
char c;
{
	register struct device *addr ;
	register int s;
	register int i;

	addr = (struct device *)0x50F04002;
	if((sc_d5 & W5ON) != W5ON) {
		sc_d5 |= W58BIT | W5ON;
		addr->csr = 5;
		addr->csr = sc_d5;
	}
	if (c == '\n') {
		auto_putchar('\r');
		for (i = 100000; i; i--);		/* DELAY */
	}
	i = 100000;
	while ((addr->csr & R0TXRDY) == 0 && --i)
		;
	addr->data = c;
}
#endif HOWFAR

int s_buf_fact = 10;
struct	map	*bufmap;

memalloctables(availmem)
register int availmem;
{
	register struct pfdd_type flags;
	register u_int m;

	if ( ! v.v_buf)
		v.v_buf = ptob(availmem) / (s_buf_fact * v.v_sbufsz);

	if (v.v_buf < v.v_mount + 10)
		v.v_buf = v.v_mount + 10;

	if (v.v_buf > (9 * ptob(availmem)) / (v.v_sbufsz * 10)) {
	    printf("System Buffers are more than 90%% of remaining memory\n");
	    printf("UNIX kernel may be unstable - may need to");
	    printf(" adjust NBUFS with kconfig\n");
	}

	flags.pfdt_dmaokay = 1;
	flags.pfdt_dmaspeed = PFD_FAST;
	flags.pfdt_memokay = 1;
	flags.pfdt_memspeed = PFD_ANY;

	iobufs = (caddr_t) memreg_alloc(v.v_buf * v.v_sbufsz,
					flags, v.v_sbufsz - 1);
	if (iobufs == (caddr_t) -1) {
		pre_panic();
		printf("memalloctable: can't get 0x%x bytes\n",
					    v.v_buf * v.v_sbufsz);
		panic("cannot allocate buffer cache");
	}

	flags.pfdt_memokay = 1;
	flags.pfdt_memspeed = PFD_FAST;
	flags.pfdt_dmaokay = 0;
	flags.pfdt_dmaspeed = PFD_ANY;

	sbuf = (struct buf *) memreg_alloc(sizeof(*sbuf) * v.v_buf, flags, 1);

	if (sbuf == (struct buf *) -1) {
		pre_panic();
		printf("memalloctable: can't get 0x%x bytes\n",
						    sizeof(*sbuf) * v.v_buf);
		panic("cannot allocate buffer headers");
	}

	bufmap = (struct map *)memreg_alloc(sizeof(*bufmap) * (v.v_buf+4), 
					    flags , 1);
	if (bufmap == (struct map *) -1) {
		printf("memalloctable: can't get 0x%x bytes\n", 
		       sizeof(*bufmap) * (v.v_buf+4));
		panic("cannot allocate bufmap");
	}

	/*	Compute the smallest power of two larger than
	 *	the size of available memory.
	 */

	m = availmem; /* availmem is in pages, want one bucket per page max */
	while (m & (m - 1))
		 m = (m | (m - 1)) + 1;

	phashmask = m - 1;

	/*
	 *	Allocate space for the page hash bucket
	 *	headers.
	 */

	flags.pfdt_memokay = 1;
	flags.pfdt_memspeed = PFD_FAST;
	flags.pfdt_dmaokay = 0;
	flags.pfdt_dmaspeed = PFD_ANY;

	phash = (struct pfdat **) memreg_alloc(m * sizeof(*phash), flags, 1);
	if (phash == (struct pfdat **) -1) {
		pre_panic();
		printf("memalloctables - can't get %d pages\n",
						    m * sizeof(*phash));
		panic("cannot allocate page hash table");
	}
}

/*
 * Handle the memory map for the IIci if we are using on-board video ram.
 * Its screen memory must be mapped from 0 (where it physically lives) to
 *  slot B (0xfbb08000) where it virtually lives.  320 1K pages make up
 *  the screen ram, and 16 1K pages make up the low memory globals.
 * The Toolbox Rom expects to be able to write a full screen starting at the
 *  middle, so we duplicate the map, just to be safe.
 *
 * The job is complicated since the MMU choices don't match the way the
 *  ram fits into the address space, leading to the need to map pieces outside
 *  ram.  We want to avoid early termination because (a) we don't expect
 *  this beyond L1 tables, and (b) the 040 doesn't support it.
 *
 * In doing this, we assume that either both RBV and LM fit into bank A or that
 *  neither do (this is true at the time this is written; who knows what
 *  tomorrow will bring).
 */
unsigned int *
setup_rbv(cp)
register unsigned int *cp;
{	register int i1, i2, j1, j2;
	register int BankA_size, BankB_size;

	/*
	 * Since the MacOS likes to have space for low memory globals and
	 *  vectors, we will map the 16K just following RBV to virtual zero,
	 *  leaving the preceding physical chunk for the monitor ram.
	 * This 16K at virtual zero will be used for vector table,
	 *  low-memory globals, SASH communication, and other scratch.
	 */
	/* Figure out how the video ram fits into the banks */
	BankA_size = rambanks[0].ram_end - rambanks[0].ram_base;
	BankB_size = rambanks[1].ram_end - rambanks[1].ram_base;
	if ((int)(j1 = rambanks[0].ram_end-RBV_MODE_BYTES) > 0)
	{	/* RBV+LM is smaller than Bank A */
		i2 = 0;
		i1 = RBV_MODE_BYTES;
		j2 = BankB_size;
		/* Map the LM segment to virtual zero */
		cp = map_pages(0, RBV_BYTES, LM_BYTES, cp);
		/* Map the remainder of Bank A straight through */
		cp = map_through(rambanks[0].ram_base+i1, j1, cp);
	} else
	{	/* RBV+LM is bigger than Bank A */
		i2 = -j1;
		i1 = BankA_size;
		j1 = 0;
		j2 = BankB_size-i2;
		/* Map the LM segment to virtual zero */
		cp = map_pages(0, rambanks[1].ram_base+(RBV_MODE_BYTES-BankA_size),
			       LM_BYTES, cp);
		/* Map the remainder of Bank B straight through */
		cp = map_through((int)(rambanks[1].ram_base)+i2, j2, cp);
	}

	/* Next, map the screen memory and duplicate the screen ram area
	 *  in virtual space
	 */
	if (i2 == 0)		/* RBV Smaller than bank A */
	{	cp = map_pages(KV_VIDRAM, 0, RBV_BYTES, cp);
		cp = map_pages(KV_VIDRAM+RBV_BYTES, 0, RBV_BYTES, cp);
	} else			/* RBV Larger than bank A */
	{	cp = map_pages(KV_VIDRAM, 0, i2, cp);
		cp = map_pages(KV_VIDRAM+RBV_BYTES, 0, i2, cp);
		cp = map_pages(KV_VIDRAM+i2, rambanks[1].ram_base, cp);
		cp = map_pages(KV_VIDRAM+RBV_BYTES+i2, rambanks[1].ram_base,
			       i2, cp);
	}

	/* Now, map through the piece of the L1 section not in the screen */
	i1 = (int)L1tob(L1ix(KV_VIDRAM));
	/* First, map the initial chunk of this L1 entry straight through */
	if (i2 = KV_VIDRAM-i1)
		cp = map_through(i1, i2, cp);

	i1 = KV_VIDRAM+2*RBV_BYTES;
	if (i2 = (unsigned int)L2_round(i1) - i1)
		cp = map_through(i1, i2, cp);
	return(cp);
}

/*
 * Map the indicated range 1-1.  For the 020/030, can use early
 *  termination; for the 040, must use full page trees.
 * Use cp as a source for page tables.
 */
unsigned int *
map_through(vaddr, size, cp)
caddr_t vaddr;
int size;
caddr_t cp;
{	register unsigned int i1, i2, j1, j2, k1, k2;
	register Dt_t *L2p;
	register pte_t *Ptp;
	register caddr_t cur_addr, end_addr;
	register unsigned int cur_page;
	register Dt_t *L1p;

	cur_addr = vaddr;
	end_addr = vaddr+size;
	L1p = L1_ent(K_L1tbl, cur_addr);
	i1 = L1ix(cur_addr);
	i2 = L1ix(end_addr-1);
	for (; i1++ <= i2; L1p++)
	{	j1 = L2ix(cur_addr);
		j2 = L2ix(min(L2_round(cur_addr+1), (unsigned int)end_addr)-1);
		if (j2-j1 != NL2TBL-1)	/* Otherwise assume already ET*/
		{	if (!Lx_valid(L1p))	/* ET shows "not valid" */
			{	wtl1e(L1p, cp, Lx_RW);
				cp += LxTBLSIZE;
			}
			/* We have a valid L2 table. */
			L2p = L2_ent(K_L1tbl, cur_addr);
			for (; j1++ <= j2; L2p++)
			{	k1 = Ptix(cur_addr);
				k2 = Ptix(min(Pt_round(cur_addr+1),
					      (unsigned int)end_addr)-1);
				cur_page = btop(cur_addr);
				/* Check for full PT */
				if (k2-k1 == NPGPT-1)
				{	wtpte(L2p, cur_page, Lx_RW);
					cur_addr = (caddr_t)Pt_round(cur_addr+1);
				} else
				{	if (!Lx_valid(L2p))
					{	wtl1e(L2p, cp, Lx_RW);
						cp += PT_NSWAP_SZ;
					}
					Ptp = Pt_ent(K_L1tbl, cur_addr);
					/* Assume cur_addr is page-aligned */
					for (; k1++ <= k2; Ptp++)
					{	wtpte(Ptp, cur_page++, Lx_RW);
						cur_addr += ptob(1);
					}
				}
			}
		}
	}
	return((unsigned int *)cp);
}
