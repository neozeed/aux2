/*
 * @(#)bnetuconf.c  {Apple version 2.7 90/03/13 12:20:00}
 *
 * Copyright (c) 1987, 1988, 1989 by Apple Computer, Inc.
 * All Rights Reserved.
 *
 * THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF APPLE COMPUTER, INC.
 * The copyright notice above does not evidence any actual or
 * intended publication of such source code.
 */

#if !defined(lint) && !defined(NO_SCCS_IDS)
static char _sccsid[]="@(#)bnetuconf.c  {Apple version 2.7 90/03/13 12:20:00}";
#endif

/*	@(#)bnetuconf.c		UniPlus VVV.2.1.21	*/
#ifdef HOWFAR
extern int	T_uconfig;
#endif HOWFAR

/*
 * This file contains
 *	1. oem modifiable configuration personality parameters
 *	2. oem modifiable system specific kernel personality code
 */

#ifdef lint
#include "sys/sysinclude.h"
#else lint
#include "compat.h"
#include "sys/param.h"
#include "sys/uconfig.h"
#include "sys/types.h"
#include "sys/mmu.h"
#include "sys/sysmacros.h"
#include "sys/page.h"
#include "sys/systm.h"
#include "sys/map.h"
#include "sys/dir.h"
#include "sys/signal.h"
#include "sys/time.h"
#include "sys/user.h"
#include "sys/errno.h"
#include "sys/region.h"
#include "sys/proc.h"
#include "sys/buf.h"
#include "sys/reg.h"
#include "sys/file.h"
#include "sys/acct.h"
#include "sys/var.h"
#include "sys/ipc.h"
#include "sys/shm.h"
#include "sys/termio.h"
#include "sys/utsname.h"
#include "sys/pfdat.h"
#include "vaxuba/ubavar.h"
#include "sys/debug.h"
#include "sys/ttychars.h"
#endif lint
#include "sys/via6522.h"
#include "sys/module.h"
#include "sys/gdisk.h"
#include "sys/cpuid.h"
#include "sys/oss.h"

char oemmsg[] =	"A/UX by Apple Computer.";

int	sspeed = B9600;		/* default console speed */
int	parityno = 31;		/* parity interrupt vector (NMI on Aurora) */

int	compatflags = COMPAT_BSDPROT | COMPAT_BSDNBIO; /* BSD compat defaults */
int	cmask       = CMASK;		/* default file creation mask */
int	cdlimit     = CDLIMIT;	        /* default file size limit */
short   physiosize  = PHYSIOSIZE;	/* size of the physiobuf (in pages) */

int	rbv_exists  = 0;		/* RBV chip replaces VIA2 */
int     rbv_monitor = 0;                /* monitor attached to the rbv */
int     rbv_monid   = 0;
int	cpuspeed    = 16;		/* CPU speed, nominal MhZ */

struct via *via2_addr   = (struct via *) VIA2_ADDR;
struct oss *oss		= (struct oss *) OSS_ADDR;

struct uba_device ubdinit[5] = {
	/* ui_driver,	ui_unit,	ui_addr,		ui_flags */
	0,			0,			 0,		0
};

int ubdcnt = sizeof(ubdinit)/sizeof(ubdinit[0]);

#ifdef PDISK
int	pdisksize = btop(1<<18);/* size of pseudo disk */
#endif

/*
 * tty output low and high water marks
 */
#define TTHIGH
#ifdef TTLOW
#define M	1
#define N	1
#endif
#ifdef TTHIGH
#define M	3
#define N	1
#endif
int	tthiwat[16] = {
	0*M,	60*M,	60*M,	60*M,	60*M,	60*M,	60*M,	120*M,
	120*M,	180*M,	180*M,	240*M,	240*M,	240*M,	100*M,	100*M,
};
int	ttlowat[16] = {
	0*N,	20*N,	20*N,	20*N,	20*N,	20*N,	20*N,	40*N,
	40*N,	60*N,	60*N,	80*N,	80*N,	80*N,	50*N,	50*N,
};

/*
 * Default terminal characteristics
 */
char	ttcchar[NCC] = {
	0x03,
	CQUIT,
	0x7f,
	CKILL,
	CEOF,
	0,
	0,
	0
};
struct ttychars ttycdef = {
	0x7f,
	CKILL,
	0x03,
	CQUIT,
	CSTART,
	CSTOP,
	CEOF,
	CBRK,
	0xFF,	/* suspc */
	0xFF,	/* dsuspc */
	0xFF,	/* rprntc */
	0xFF,	/* flushc */
	0xFF,	/* werasc */
	0xFF,	/* lnextc */
};

/*
 * Kernel initialization fucntions.
 * Called from main.c (through the initfuncs[] array) while at spl0.
 */
oem0init()
{
	register int (**fptr)();
	register int i;
	extern int (*init_0[])();
	extern int init_0l;

	for (i=0, fptr = init_0; i < init_0l && *fptr; fptr++)
		(**fptr)();
}

int kputc_on = 1;
extern int scputchar();
int (*locputchar)() = scputchar;
dev_t locputdev = makedev(0,0);

/*
 * generic output to console
 */
kputchar(c)
char c;
{
    if (kputc_on) {
	(*locputchar)(c);
    }
    if (c == '\0')
	osmwakeup();
}

/*
 * output to internal buffer as well as console
 */
outchar(c)
char c;
{
	putchar(c);
	kputchar(c);
}

#ifdef DEBUG
/* generic get a character from console */
getchar()
{
	return(scgetchar());
}
#endif DEBUG

/*	onesec is hacked together as follows.
 *	The data value one_sec is only decremented when a full second has
 *	passed.  When a second has passed, we see if ticks are obviously
 *	missing from lbolt, if so, we add them.  The upshot of this code
 *	should be that lbolt time will be improved, but still marginal during
 *	long kernel critical sections, but time in seconds will be reliable.
 */

onesec()
{
	extern int one_sec;
	extern int lticks;
	static time_t nextlbolt;

	viaclrius();
	if (lbolt < nextlbolt) {
		register int diff;

		diff = nextlbolt - lbolt;
		lticks -= diff;
		one_sec -= diff;
		lbolt = nextlbolt;
	}
	nextlbolt = lbolt + HZ - 1;
}

int parity_enabled;

/*
 * Enables parity if parity hardware is present.
 * Assumes caching hasn't been permanently enabled yet.
 */
parityinit()
{
    int flags;
    extern short machineID;
    extern struct kernel_info *kernelinfoptr;

    /*
     * The MacOS has already determined if parity checking should be
     * enabled or not.  The A/UX Startup launch program passes this
     * information.  Parity hardware must be both present, and enabled.
     */
    flags = kernelinfoptr->ki_flags;
    if (!(flags & KI_PARITY_EXISTS && flags & KI_PARITY_ENABLED))
	return;

    /* 
     * Both on-chip and any external cache cards need to be disabled
     * to test for parity hardware.  Cacheing should be enabled
     * permanently in a later init routine.
     */
    disable_caches ( );

    switch (machineID) {
	case MACIIci: {
	    register struct via *vp = (struct via *) VIA1_ADDR;

	    /*
	     * Ensure the data direction register is correct
	     */
	    vp->ddrb |= RBV_PARDIS;		/* PARDIS is an output */
	    vp->ddrb &= ~RBV_PAROK;		/* PARERR is an input */

	    while ((vp->regb & RBV_PARDIS))	/* enable checking */
		vp->regb &= ~RBV_PARDIS;
	    break;
	}
	case MACIIfx:
	    oss->oss_intlev[OSS_PARITY]	= OSS_INTLEV_PARITY;
	    break;
    }
    parity_enabled = 1;
    printf ("Parity checking enabled\n");
}

/*
 * parityerror()
 *	Called from trap for parity error traps via
 *	return code:
 *	- negative - fatal error - causes panic if not in
 *		user mode, or user is sent SIGBUS
 *	- zero - soft (recoverable) error
 *	- positive - number of trap that should be taken
 */
parityerror()
{
	return(-1);
}

/*
 * OEM supplied subroutine called on process exit
 */

/* ARGSUSED */
oemexit(p)
struct proc *p;
{
}

/*
 * mmu error reset and report code - optional
 */
/* ARGSUSED */
mmuerror(f)
{
}

/*
 * set real time clock - optional
 */
/* ARGSUSED */
setrtc(tvar)
time_t tvar;
{
}

/*
 * dummy spurintr
 */
spurintr()
{
}

/*
 *	abort switch
 */

abintr(args)
	struct args *args;
{
	register int i;

	printf("Minimal UNIX Debugger\n");
	printf("sr = %x, pc = %x\n", args->a_ps, args->a_pc);
	printf("d0 - d7 ");
	for (i = 0; i < 8; i++)
		printf("%x ", args->a_regs[i]);
	printf("\na0 - a7 ");
	for (; i < 16; i++)
		printf("%x ", args->a_regs[i]);
	printf("\n");
}

/*
 *	Powerfail (from rear power switch)
 */

powerintr()
{
	psignal(&proc[1], SIGPWR);
}

/*
 * cache initialization
 */
cacheinit()
{
        enable_caches();
	printf("%s caching enabled\n", utsname.machine);
}

clr_cache(off)
int off;
{
#ifdef	lint
	off = off;
#endif 	lint
}


/* generic init functions */

extern	binit(), cinit(), errinit(), choose_root(), iinit();
extern	shmfork(), shmexec(), shmexit();
extern	sxtinit(), msginit();
extern	seminit(), semexit();
extern	bhinit();
extern	dnlc_init();
extern	nvram_init();
extern	fpnull();
extern	vio_init();

int	(*initfunc[])() = {
	bhinit,
	vio_init,
	binit,
	cinit,
	dnlc_init,
	errinit,
	choose_root,
	iinit,
	nvram_init,
	msginit,
	seminit,
	sxtinit,
	oem0init,
	(int (*)())0
} ;

int	(*forkfunc[11])() = {
	shmfork,
	(int (*)())0
} ;

int forksize = (sizeof(forkfunc)/sizeof(forkfunc[0])) - 1;

int	(*execfunc[11])() = {
	shmexec,
	fpnull,
	(int (*)())0
} ;

int execsize = (sizeof(execfunc)/sizeof(execfunc[0])) - 1;

int	(*exitfunc[15])() = {
	shmexit,
	semexit,
	oemexit,
	(int (*)())0
} ;

int exitsize = (sizeof(exitfunc)/sizeof(exitfunc[0])) - 1;

oem_mmuinit()
{
}

/* This routine returns the sizes of Bank A and Bank B RAM.
 *
 * ASSUMPTIONS ABOUT THE HARDWARE:
 *	- Memory in most Mac systems wraps around so we can't just write any
 *	value and check for it being there.
 *
 *	- Bus capacitance can cause a nonexistent location to "hold" the
 *	data for a short time, so we can't just readback without waiting.
 *
 *	- Each bank must be fully populated, since each socket is a bytelane.
 *	Each bank contains 0/1/2/4/8/16/32/64 MB.
 *
 *	- Accesses within the RAM address space never bus-error, they wrap.
 *
 *	- There's always some memory in Bank A, since PSTART has to run at
 *	a low physical address.
 *
 * On Mac II/IIx/IIcx, the physical address of Bank B can be set by
 * setting via2 bits as follows, based on what's in Bank A:
 *
 *			   Bank A contents
 *	PA6	PA7	chip size	bytes		Bank B Physaddr
 *	0	0	256 Kbit	 1 MB		0x00100000
 *	0	1	 1  Mbit	 4 MB		0x00400000
 *	1	0	 4  Mbit	16 MB		0x01000000
 *	1	1	16  Mbit	64 MB		0x04000000
 *
 * On an Aurora, Bank B is hardwired to start at--->	0x04000000
 *
 * We don't worry about NuBus expansion RAM.
 */


#define BANKA_ADDR  0x0000000
#define BANKB_ADDR  0x4000000

int kernel_bank = 0;


memsize(tblend)
register int tblend;
{   register int i;
    extern int  kstart;
    extern int  numbanks;
    extern struct rambank rambanks[];
    extern long endpstart;
    extern long _start;
    extern struct kernel_info *kernelinfoptr;

    if ((int) kernelinfoptr != 0x400) {
        if (!rbv_monitor) {
	    register int *p,*q;

	    p = (int *)0;
	    q = (int *)kstart;
	    while (q < (int *)&_start)
	        *p++ = *q++;
        }
	kernelinfoptr = (struct kernel_info *) ((int) kernelinfoptr - kstart);
    }
    if (!rbv_exists)
        banksize(BANKA_ADDR, tblend, &rambanks[0]);
    else {
        if (tblend > BANKB_ADDR) {
	    banksize(BANKA_ADDR, &endpstart, &rambanks[0]);
	    banksize(BANKB_ADDR, tblend, &rambanks[1]);
	    kernel_bank = 1;
	} else {
	    banksize(BANKA_ADDR, tblend, &rambanks[0]);
	    banksize(BANKB_ADDR, BANKB_ADDR, &rambanks[1]);
	    kernel_bank = 0;
	}
    }
    if (!rbv_monitor) {
        register struct rambank *rambank;
        register u_int addr;

	rambank = &rambanks[numbanks];
        rambank->ram_base  = BANKA_ADDR;
	rambank->ram_start = (int)&_start - kstart;
	rambank->ram_end   = (int)&_start;

	if (rambank->ram_end > rambank->ram_start) {
	    for (addr = rambank->ram_start; addr < rambank->ram_end;
		                            addr += ptob(1))
	        clear(addr, ptob(1));
	    numbanks++;
	  }
    }
}


/*
 * banksize(beg, start, rambank)
 *	size and clear memory starting at start
 *	pigco(ev):  this routine is still not totally correct
 *	because it assumes that someone else has set up the via2
 *	bits.  however, it does detect non-existent memory (you
 *	can not just write a value and read it back as that works even
 *	on non-existent memory [don't gag - it's not polite]) and also
 *	the wrap to the 'middle' of memory when bank 1 has smaller 
 *	chips than bank 0.
 */
banksize(beg, start, rambank)
register int start;
register struct rambank *rambank;
{
	register u_short *addr;
	register u_long limit;
	register long save0;
	register long save1;
	register long *mid;
	extern int  numbanks;

	start = btop(start);
	start = ptob(start);       /* make sure we're on a page boundary */
	rambank->ram_base  = beg;
	rambank->ram_start = start;
	rambank->ram_end   = start;

	limit = 1;
	while (limit < start)      /* compute nearest power of 2 greater than start */
		limit <<= 1;

	mid = (long *)(limit>>1);
	save1 = *mid;
	save0 = *((long *)0);

	for (;;) {
		addr = (u_short *)(rambank->ram_end);
		addr[0] = 0x5AA5;
		addr[1] = 0xA55A;
		if (*((long *)0) != save0 || addr[0] != 0x5AA5
				|| addr[1] != 0xA55A)
			break;
		if (*mid != save1) {
			*mid = save1;
			break;
		}
		clear((caddr_t)addr, ptob(1));
		rambank->ram_end += ptob(1);

		if (rambank->ram_end > limit) {
			mid = (long *)limit;
			save1 = *mid;
			limit <<= 1;
		}
	}
	*((long *)0) = save0;

	if (rambank->ram_end > rambank->ram_start) {
	        rambank->ram_real = 1;
	        numbanks++;
	}
}


long	scsi_addr	= SCSI_ADDR_R8;
long	sdma_addr	= SDMA_ADDR_R8;
long	shsk_addr	= SHSK_ADDR_R8;
long	iwm_addr	= IWM_ADDR;
long	scc_addr	= SCC_ADDR;
long	sound_addr	= SOUND_ADDR;


/*
 * boardinit -- initialize for proper system motherboard.
 * Here we detect the board type, and initialize the addresses.
 */

boardinit()
{       
    register struct via *vp;
    extern short machineID;

    switch (machineID) {

	case MACII:
	case MACIIx:
	case MACIIcx:
	case MACSE30:
	    break;

	case MACIIci:
	    rbv_exists = 1;
	    via2_addr = (struct via *) RBV_ADDR;

	    rbv_monid = via2_addr->rbv_monparams & RBV_MONID;
	    if (rbv_monid == MON_NONE)
		via2_addr->rbv_monparams = RBV_VIDOFF;
	    else
		rbv_monitor = 1;
	    cpuspeed = 25;		/* Aurora is 25MhZ */
	    break;

	case MACIIfx:
	    iwm_addr  = -1;	/* not used */
	    scc_addr  = SCC_ADDR_MACIIfx;
	    scsi_addr = SCSI_ADDR_MACIIfx;
	    sdma_addr = SCSI_ADDR_MACIIfx;
	    shsk_addr = SCSI_ADDR_MACIIfx;
	    sound_addr= SOUND_ADDR_MACIIfx;
	    cpuspeed = 40;
	    setup_oss ( );
	    break;

	default:
	    panic ("unsupported motherboard type");
    }
}

/*	choose_root -- select root file system.
 *	     The booter firmware leaves us the controller and
 *	drive number of the boot disk.  We use that as the boot
 *	drive if 1) the magic number 0xffff is stored as rootdev
 *	in the kernel file, and 2) the id left is reasonable.
 *	WARNING there are constants (major number 24) specific to this release
 */

choose_root()
{
	int	newctrl, newdrive;
	struct kernel_info *kip;

	kip = (struct kernel_info *) v.v_kernel_info;
	if (kip->root_ctrl >= 0 && kip->root_ctrl <= 7
	  && kip->root_drive >= 0 && kip->root_drive <= 7) {
		newctrl = kip->root_ctrl + 24;
		newdrive = kip->root_drive;
	}
	else {
		newctrl = 24;
		newdrive = 0;
	}
	if(rootdev == 0xFFFF)
		rootdev = makedev(newctrl, mkgdminor(newdrive, 0));
	if(swapdev == 0xFFFF)
		swapdev = makedev(newctrl, mkgdminor(newdrive, 1));
	if(pipedev == 0xFFFF)
		pipedev = makedev(newctrl, mkgdminor(newdrive, 0));
}

setup_oss ( )
{
    register int i;

    for (i = OSS_NUBUS0; i <= OSS_NUBUS5; i++)
	oss->oss_intlev[i] = OSS_INTLEV_NUBUS;
    
    oss->oss_intlev[OSS_IOPISM]	= OSS_INTLEV_IOPISM;
    oss->oss_intlev[OSS_IOPSCC]	= OSS_INTLEV_IOPSCC;
    oss->oss_intlev[OSS_SOUND]	= OSS_INTLEV_SOUND;
    oss->oss_intlev[OSS_SCSI]	= OSS_INTLEV_SCSI;
    oss->oss_intlev[OSS_60HZ]	= OSS_INTLEV_60HZ;
    oss->oss_intlev[OSS_VIA1]	= OSS_INTLEV_VIA1;
    oss->oss_intlev[OSS_UNUSED1]= OSS_INTLEV_DISABLED;	/* ignored */
    oss->oss_intlev[OSS_UNUSED2]= OSS_INTLEV_DISABLED;	/* ignored */
    oss->oss_intlev[OSS_UNUSED3]= OSS_INTLEV_DISABLED;	/* ignored */
    /*
     * parityinit might enable parity later...
     */
    oss->oss_intlev[OSS_PARITY]	= OSS_INTLEV_DISABLED;
}


Level1Int (args)
    struct args *args;
{
    register unsigned short *isr = &oss->oss_intpnd;

    if (*isr & OSS_IP_60HZ) {
	oss->oss_60hz_ack = 0;	/* clear 60 Hz interrupt */
	clock (args);
    }

    if (*isr & OSS_IP_IOPISM)
	iop_intr_swim();

    if (*isr & OSS_IP_VIA1)
	via1intr (args);
}

Level2Int (args)
    struct args *args;
{
    register unsigned short *isr = &oss->oss_intpnd;
    extern int (*lvl2funcs[])();
    extern int noviaint();

    /* 
     * If the interrupt is from the sound chip, and the sound driver
     * has the interrupt enabled, call the service routine (but only
     * if it has been).
     */
    if ((*isr & OSS_IP_SOUND) && oss->oss_intlev[OSS_SOUND])
	if (lvl2funcs[4] != noviaint)
	    (*lvl2funcs[4])();
	else
	    printf ("Level2Int: Bogus sound interrupt!\n");

    if (*isr & OSS_IP_SCSI)
	scsiirq();
}

#define OSS_IP_LEVEL3	(OSS_IP_NUBUS0 | OSS_IP_NUBUS1 | OSS_IP_NUBUS2 | \
			 OSS_IP_NUBUS3 | OSS_IP_NUBUS4 | OSS_IP_NUBUS5)
Level3Int (args)
    struct args *args;
{
    register unsigned short *isr = &oss->oss_intpnd;
    register (**fp)(), msk, num;
    extern int (*slotfuncs[])();

    msk = 1;
    num = SLOT_LO;
    fp = slotfuncs;
    do {
	if (*isr & msk) {
	    args->a_dev = num;
	    (*fp)(args);
	}
	msk <<= 1;
	++num;
	++fp;
    } while (msk <= (*isr & OSS_IP_LEVEL3));
}

/* We initially come up with the SCC irq handed off to a "null" receiver
 * so nobody gets upset if the Mac left a pending SCC interrupt.  Either
 * sccio or seriop will readjust sccirq to go to the proper receiver.
 */

int scbypassint();

int (*sccirq)() = scbypassint;

Level4Int (args)
    struct args *args;
{
    register unsigned short *isr = &oss->oss_intpnd;

    if (*isr & OSS_IP_IOPSCC)
	(*sccirq)(args);
}

Level5Int (args)
    struct args *args;
{
    printf ("Level5!  Should not be here!\n");
}

Level6Int (args)
    struct args *args;
{
    printf ("Level6!  Should not be here!\n");
}


dopowerdown()
{
    int i;
    extern short machineID;

    SPLHI();

    switch (machineID) {
	case MACIIfx:
	    oss->oss_rcr = OSS_POWEROFF;
	    break;
	case MACIIci:
	    via2_addr->regb &= ~RBV_POWEROFF;
	    break;
	default:
	    via2_addr->regb &= ~VRB_POWEROFF;
	    via2_addr->ddrb |= 0x4;
	    break;
    }

    for (i = 0x50000; i > 0; i--)
	;	/* do nothing */
    
    dispshut ( );
    while (1)
	;	/* do nothing */
}

int debug = 0;

debugger_stop ( )
{
    if (!debug)
	return;

    printf ("\t*** Debugger Stop -- Press NMI Switch to Enter Debugger ***\n");
    asm ("	stop	&0x2700 ");
}
