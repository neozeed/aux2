/*
 * @(#)sysdebug.c  {Apple version 2.3 90/02/28 14:40:25}
 *
 * Copyright (c) 1987, 1988, 1989 by Apple Computer, Inc.
 * All Rights Reserved.
 *
 * THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF APPLE COMPUTER, INC.
 * The copyright notice above does not evidence any actual or
 * intended publication of such source code.
 */

#if !defined(lint) && !defined(NO_SCCS_IDS)
static char _sccsid[]="@(#)sysdebug.c  {Apple version 2.3 90/02/28 14:40:25}";
#endif

/*
 * A/UX Kernel debugger - main command loop and support routines
 *  @(#)sysdebug.c	2.1 89/10/13
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
#include "sys/sysinfo.h"
#include "sys/var.h"
#include "sys/ipc.h"
#include "sys/shm.h"
#include "sys/termio.h"
#include "sys/utsname.h"
#include "sys/psl.h"
#include "sys/debug.h"
#include "sys/ttychars.h"
#endif lint
#include "sys/via6522.h"
#include "sys/module.h"
#include "sys/gdisk.h"
#include "sysdebug.h"
#include "ctype.h"
#include "varargs.h"
#include "values.h"
#include "filehdr.h"
#include "syms.h"
#include "nan.h"

/* By rights, the 0xfffffff and 0x10000000 should be computed from sectinfo */
#define dbgvtop(x) (((x) < sectinfo[0].vstart) ? (x): (((x) & 0xffffff) + sectinfo[0].pstart))
#define dbgptov(x) (((x) < sectinfo[0].pstart) ? (x) \
					 : (((x) - sectinfo[0].pstart) | 0x10000000))

#define SYS_BKPT 0x4e4e /* trap 14 */
#define MAXBP	20

static struct bpstruct {
	unsigned int bpp;		 /* Saved 'proc' value */
	unsigned short *adx;
	unsigned short val;
	char flags;
} dbgbp[MAXBP];
/*
 * Single-step used to step past a break-pointed location.
 * If this bit set, the break-point will be re-installed when we continue.
 * If single-stepping at this point, just knock the bit down and let the user
 *  decide.  If not, continue right away.
 */
#define BPSINGLE 0x1
#define BPLINKW	0x2
#define BPLINKL	0x4

int DB_Flags = 0;		/* Global flags for debugger */
#define DB_TRACE	0x1	/* Tracing underway */

char *dbgdefaultprompt = "+ ";
char *dbgprompt = "+ ";
int dbg_stepcount, dbg_savestep;
/* Set to `1' to display registers on single step */
short dbg_dr = 0;

extern reg_t *region;
extern dbgtxtoffset;

static char dbgcmd[80];
static char *dbgcmdptr;
static int dbglastval;
static caddr_t dbgtop;
struct user *altu;
unsigned long rp[2];

extern pte_t *svtopte();
char *formatsym();

sysdebug(args)
	struct args *args;
{
	extern short scc_iop_on;
	extern int (*sccirq)();
	extern int scbypassint();
	static int iopkilled = 0;
	register int i,adx,len,offs,oadx,cnt;
	int s;
	struct proc *p;
	reg_t *rp;

	s = spl7();

	/* Blast the SCC IOP back to normal state so we can
	 * do something useful.  We install a bypass-interrupt
	 * handler to get the pending SCC IRQ cleared, to avoid
	 * nasty problems with IOP manager.
	 *
	 * NB: If the SCC IOP is in use, pressing NMI to come
	 * here will kill all serial IOP operations, and disable
	 * SCC interrupts.  We can still use the debugger, but
	 * serial will be dead.
	 */

	if (scc_iop_on && !iopkilled) {
	    sccirq = scbypassint; /* prevent iopmgr from getting SCC IRQ */
	    iop_into_bypass();
    dbgprintf("\n************* Serial IOP killed by debugger. ***********\n");
	    iopkilled = 1;
	}

	removebps();
	if (args->a_dev == 3)
	{	for (i=0; i<MAXBP; i++)
		{	if (dbgbp[i].flags & BPSINGLE) {
				dbgbp[i].flags &= ~BPSINGLE;
				break;
			}
		}
		if (DB_Flags&DB_TRACE)
		{	if (--dbg_stepcount == 0 && dbg_dr)
				dbgshowregs(args);
			disassm(args->a_pc, 1);
			/* May have lost TRACE bit (e.g., from mov to sr) */
			args->a_ps |= PS_T;
			if (dbg_stepcount)
				goto out;
		} else
		{	args->a_ps &= ~PS_T;
			goto out;
		}
	}
	switch (args->a_dev) {
	case 0:
		dbgprintf("\n***** sysdebug interrupt");
		break;
	case 1:
		adx = dbgvtop((int)args->a_pc-2);
		if ((i=findbp(adx)) != -1)
		{	dbgbp[i].flags |= BPSINGLE;
			args->a_pc -= 2;
			args->a_ps |= PS_T;
			if (dbgbp[i].bpp && u.u_procp != (struct proc *)dbgbp[i].bpp)
			{	/* A small assumption here */
				goto out;
			}
			dbgprintf("\n***** sysdebug breakpoint");
		}
		break;
	case 2:
		dbgprintf("\n***** sysdebug call");
		break;
	case 3:
		break;
	default:
		dbgprintf("\n***** sysdebug for unknown reason");
		break;
	}
	if (args->a_dev != 3)
	{	dbgprintf(" at %s *****\n",formatsym(args->a_pc));
		if (args->a_dev == 1)
			disassm(args->a_pc, 1);
	}
	while (1) {
		getcmd();
		if (dbgcmd[0] && (DB_Flags&DB_TRACE))
		{	dbgprompt = dbgdefaultprompt;	/* Switch modes */
			args->a_ps &= ~PS_T;		/* Disable tracing */
			DB_Flags &= ~DB_TRACE;
		}
		switch (dbgcmd[0]) {
			case 'h':
			case '?':
				usage();
				break;

			case '=':
			case ' ':
				if (!eol()) {
					dbglastval = getparm();
				}
				dbgprintf("%s  %#lx  %ld  %#lo\n",
					formatsym(dbglastval),dbglastval,
						dbglastval, dbglastval);
				break;

			case 'B':
			case 'b':
				/* please excuse hacks below */
				if (eol()) {
					for (i=0; i < MAXBP; i++) {
						if (adx = (int)dbgbp[i].adx) {
							dbgprintf("%s",formatsym(dbgptov(adx)));
							if (adx = (int)dbgbp[i].bpp)
								dbgprintf(" (%x)",adx);
							dbgprintf("\n");
						}
					}
					break;
				}
				adx = getparm();
				oadx = getparm();
				if (adx > 0x10ffffff) {
					dbgprintf("bad addr\n");
					break;
				}
				adx = dbgvtop(adx);
				if (findbp(adx) == -1) {
					i = setbp(adx, dbgcmd[0]);
					dbgbp[i].bpp = oadx;
				}
				break;
	
			case 'd':
				if (eol()) {
					for (i=0; i < MAXBP; i++) {
						dbgbp[i].adx = 0;
						dbgbp[i].flags = 0;
						dbgbp[i].bpp = 0;
					}
				} else {
					adx = getparm();
					if (adx > 0x10ffffff) {
						dbgprintf("bad addr\n");
						break;
					}
					delbp(dbgvtop(adx));
				}
				break;
		
			case 'e':
				goto out;
				break;

			case 'f':
				adx = args->a_regs[14];
				offs = 0;
				oadx = 0;
				len = sizeof(int)*16;
				while (adx > oadx && adx&0x7fffffff) {
					if (badaddr(adx)) {
						break;
					}
					if (checkabort()) {
						break;
					}
					dbgprintf(" %s\n", formatsym(((uint *)adx)[1]));
					if (xdisplay(adx + offs, len)) {
						break;
					}
					dbgprintf("\n");
					oadx = adx;
					adx = *(unsigned *)adx;
				}
				dbgprintf("\n");
				break;
	
			case 'i':
				if (eol()) {
					adx = dbglastval;
					len = 0;
				} else {
					adx = getparm();
					len = getparm();
				}
				disassm(adx, len);
				break;

			case 'k':
			case 'K':
				if (dbgcmd[0] == 'K')
					adx = (int)K_L1tbl;
				else
				{	if (eol())
						adx = (int)u.u_procp->p_root;
					else
						adx = getparm();
					if (!adx)
					{	dbgprintf("Null root\n");
						break;
					}
				}
				if (eol())
					cnt = 3;
				else
					cnt = getparm();
				DP_L1prt(adx, cnt);
				break;

			case 'l':
				adx = getparm();
				offs = getparm();
				len = getparm();
				oadx = adx;
				while (adx&0x7fffffff) {
					if (checkabort()) {
						break;
					}
					if (xdisplay(adx, len)) {
						break;
					}
					dbgprintf("\n");
					adx = *(unsigned *)((int)adx + offs);
					if (adx == oadx)
						break;
				}
				break;

			case 'P':
				{	register preg_t *prp;
					if (eol())
						p = u.u_procp;
					else
						p = (struct proc *)getparm();
					prp = p->p_region;
					for (; prp->p_reg; prp++)
						if (prp->p_reg)
						{	dbgprintpreg(prp);
							dbgprintreg(prp->p_reg);
						}
				}
				break;

			case 'p':
				/* more ps like */
				dbgprintf(
	"    PROC    ST	  FLG	PID  PPID    SZ	  SIG	PTROOT	  PREG	  WCHAN\n");
				for (p=proc; p < &proc[v.v_proc]; p++) {
					if (checkabort()) {
						break;
					}
					if ( !p->p_stat )
						continue;
					printsptb(p);
				}
				break;

			case 'Q':
				adx = getparm();
				dbgprintreg(adx);
				break;
			
			case 'R':
				rp = region;
				for (i=0;i<v.v_region;i++) {
					if (checkabort()) {
						break;
					}
					if (rp->r_refcnt)
						dbgprintreg(rp++);
				}
				break;

			case 'r':
				adx = getparm();
				len = getparm();
				xdisplay(adx, len);
				break;

			case 's':
				i = getparm();
				adx = getparm();
				for (p=proc; p < &proc[v.v_proc]; p++) {
					if (checkabort()) {
						break;
					}
					if ( !p->p_stat )
						continue;
					if ( p->p_pid == i )
					{
						psignal(p, adx);
						break;
					}
				}
				if ( p >= &proc[v.v_proc] )
					dbgprintf("process not found\n");
				break;

			case 'T':
				dbg_stepcount = 1;
				cnt = getparm();
				if (cnt)
				{	dbg_stepcount = cnt;
					if (eol())
						dbg_savestep = 1;
					else 
						dbg_savestep = cnt;
				} else 
					dbg_savestep = 1;
				args->a_ps |= PS_T;
				DB_Flags |= DB_TRACE;
				dbgprompt = "+> ";
				goto out;

			case '\0':	/* Implicitly, continue tracing */
				dbg_stepcount = dbg_savestep;
				goto out;

			case 't':
				adx = getparm();
				len = getparm();
				cnt = getparm();
				while (cnt--) {
					if (checkabort()) {
						break;
					}
					if (xdisplay(adx, len)) {
						break;
					}
					dbgprintf("\n");
					adx += len;
				}
				break;

			case 'U':
				if (eol()) {
					adx = (int)u.u_procp;
					offs = args->a_regs[14];
				} else {
					adx = getparm();
					offs = getparm();
				}
				dbgstbu(adx, offs);
				break;

			case 'u':
				adx = getparm();
				if (adx)
					printuarea(adx);
				else
					printuarea(u.u_procp);
				break;

			case 'v':
				var();
				break;

			case 'w':
				adx = getparm();
				for(; ; adx += 2) {
					if (notwriteable(adx)) break;
					dbgprintf("%08x (%04x): ", adx, *(ushort *)adx);
					gets(dbgcmd);
					if (dbgcmd[0] == '\003') {
						break;
					} else if (dbgcmd[0] == '\0') {
						continue;
					}
					dbgcmdptr = dbgcmd;
					*(ushort *)adx = getparm();
				}
				break;

			case 'x':
				dbgshowregs(args);
				break;

			case 'Z':
				{	char *name;

					if (eol() || *dbgcmdptr == 's')
					{	name = "srp";
						get_srp(rp);
					} else if (*dbgcmdptr == 'c')
					{	name = "crp";
						get_crp(rp);
					} else if (*dbgcmdptr == 't')
					{	get_tc(rp);
						dbgprintf("tc = %x\n", rp[0]);
						break;
					} else
						break;
					dbgprintf("%s = %x/%x\n", name, rp[0],
						  rp[1]);
				}
				break;

			case 'z':
				if (eol()) {
					adx = (int)u.u_procp;
				} else {
					adx = getparm();
				}
				printproc(adx);
				break;

			default:
				dbgprintf("Unknown cmd: %s\n", dbgcmd);
				break;
		}
	}
	out:
	installbps();
	splx(s);
	return(0);
}

dbgshowregs(args)
register struct args *args;
{	register int i;
	register unsigned int j;
	register unsigned int *p;
	register unsigned char *tt;	/* Locate the "trap type" */

	/* Show the SSP as the SP at the time of the interrupt/trace trap */
	tt = (unsigned char *)(args + 1);		/* Point just after the arg block */
	j = (*tt & 0xf0) >> 4;		/* Pick out the trap type */
	switch (j)		/* Switch on frame format */
	{	case 0x0:
		case 0x1: i = 0; break;	/* 4-word stack */
		case 0x2: i = 4; break;	/* 6-word stack */
		case 0x9: i = 12;break;	/* 10-word stack */
		case 0xa: i = 24;break;	/* 16-word stack */
		case 0xb: i = 84;break;	/* 46-word stack */
		default:  i = 0; break; /* Unknown format type */
	}
	tt += i+2;			/* This should do as the SSP */
	dbgprintf("sr = %04x, pc = %s, ssp = %08x\n", (unsigned short)args->a_ps, formatsym(args->a_pc), tt);
	dbgprintf("d0 - d7 ");
	for (i = 0; i < 8; i++)
		dbgprintf("%08x ", args->a_regs[i]);
	dbgprintf("\na0 - a7 ");
	for (; i < 16; i++)
		dbgprintf("%08x ", args->a_regs[i]);
	dbgprintf("\n");
}

/*
 * Print out selected items from the u struct for process 'p'.
 */
printuarea(p)
struct proc *p;
{
	register int i;
	dbgmapupage(p);	/* map in user area */
	dbgprintf("%s\n", altu->u_psargs);
	dbgprintf("&u %x, &u_rsav %x, &u_qsav %x\n", 
		  altu, altu->u_rsav, altu->u_qsav);
	dbgprintf("u_error %x, u_uid %x, u_gid %x, u_ruid %x, u_rgid %x\n",
		  altu->u_error, altu->u_uid,
		  altu->u_gid, altu->u_ruid, altu->u_rgid);
	dbgprintf("&u_dirp %x, &u_pbsize %x, &u_tsize %x\n", &altu->u_dirp, &altu->u_pbsize, &altu->u_tsize);
	dbgprintf("u_procp %x, u_rval1 %x, u_rval2 %x\n",
		  altu->u_procp, altu->u_r.r_reg.r_val1,
		  altu->u_r.r_reg.r_val2);
	dbgprintf("u_ar0 %x, &u_signal %x\n",
		  altu->u_ar0, altu->u_signal);
	dbgprintf("&u_ofile %x, &u_pofile %x\n",
		  altu->u_ofile, altu->u_pofile);

	if (altu->u_gofile)
	      dbgprintf("u_gofile %x, u_gpofile %x\n",
			altu->u_gofile, altu->u_gpofile);

	dbgprintf("&u_prof %x, &u_arg %x, &u_comm %x\n",
		  &altu->u_prof, altu->u_arg, altu->u_comm);
	dbgprintf("u_ttyd %x\n",altu->u_ttyd);
	dbgprintf("&u_exdata %x, &u_psargs %x, &u_iow %x\n\n",
		  &altu->u_exdata, altu->u_psargs, &altu->u_iow);
	dbgprintf("\n");
}

dbgprintpreg(p)
preg_t *p;
{

	dbgprintf("preg %x: p_reg %x, p_regva %x, p_flags %x, p_type %x\n\n",
		 p, p->p_reg, p->p_regva, p->p_flags, (unsigned short)p->p_type);
}

dbgprintreg(r)
reg_t *r;
{

	dbgprintf("r %x: r_flags %x, r_pgsz %x, r_noswapcnt %x, r_stack %x, r_plist %x\n",
		  r,r->r_flags,r->r_pgsz,r->r_noswapcnt,r->r_stack,r->r_plist);
	dbgprintf("	   r_ptcount %x, r_plistsz %x, r_nvalid %x, r_refcnt %x, r_type %x\n",
		  r->r_ptcount,r->r_plistsz,r->r_nvalid,r->r_refcnt,
		  (unsigned short)r->r_type);
	dbgprintf("	   r_filesz %x, r_vptr %x, r_forw %x, r_back %x, r_lock %x\n\n",
		  r->r_filesz, r->r_vptr, r->r_forw, r->r_back, r->r_lock);
}
/*
 * map the upage of process p thru the altu virtual window
 *  For this version of the kernel, we can just point, since the U block is
 *  a single page (hence no need to make contiguous).
 */
dbgmapupage(p)
struct proc * p;
{
	altu = (struct user *)p->p_addr;
}

/*
 * Do a stack trace back for process 'p'.
 */
dbgstbu(p, offs)
struct proc *p;
uint offs;
{
	uint adx;
	uint oadx;

	dbgmapupage(p);	 /* map in the user area */
	if (badaddr(altu)) {
		dbgprintf("bad u addr\n");
		return;
	}
	if (offs == 0) {
		offs = altu->u_rsav[10];
	}
	adx = offs - (uint)&u + (uint)altu;
	oadx = 0;
	while (adx && adx > oadx ) {
		if (badaddr(adx)) {
			break;
		}
		dbgprintf(" %s\n", formatsym(((uint *)adx)[1]));
		if (xdisplay(adx, sizeof(int)*8)) {
			break;
		}
		dbgprintf("\n");
		oadx = adx;
		adx = (*(uint *)adx) - (uint)&u + (uint)altu;
	}
}

/*
 * returns true if the address passed in points to an invalid page
 *  or is not writeable.
 * svtopte() sets up the pmmu status register, which is fetched by get_psr()
 */
notwriteable(svaddr)
register unsigned svaddr;
{
	int i;
	if (svaddr >= 0x50000000) return(1);
	svtopte(svaddr);
	i = get_psr();
	if (i & 0xdc00) return(1);
	return(0);
}

/*
 * returns true if the address passed in points to
 * an invalid page
 * svtopte() sets up the pmmu status register, which is fetched by get_psr()
 */
badaddr(svaddr)
register unsigned svaddr;
{
	int i;
	if (svaddr >= 0x50000000) return(1);
	svtopte(svaddr);
	i = get_psr();
	if (i & 0xd400) return(1);
	return(0);
}

gets(bufp)
register char *bufp;
{
	register char *lp;
	register int c;

	lp = bufp;
	while (1) {
		c = dbggetc() & 0177;
		switch(c) {

		case '\n':
		case '\r':
			c = '\n';
			*lp++ = '\0';
			return;
			break;

		case '\003':
			*lp++ = '\003';
			*lp++ = '\0';
			dbgputc('\n');
			return;
			break;

		case '\b':
			lp--;
			if(lp < bufp) {
				lp = bufp;
			}
			break;
			
		case 0x11:
		case 0x13:
			break;

		case '\030':
			lp = bufp;
			dbgputc('\n');
			break;

		default:
			*lp++ = c;
			break;
		}
	}
}


/*
 * prints an integer value in hex, leading digits 0 pad.
 */

puthex(value)
unsigned value;
{
	static char ptab[] = "0123456789abcdef";
	char lebuf[5];
	register int i;

	lebuf[4] = '\0';
	for (i = 3; i >= 0; i--) {
		lebuf[i] = ptab[value & 0xf];
		value = value >> 4;
	}
	dbgprintf("%s", lebuf + i + 1);
}


/*
 * get the next parameter from dbgcmd starting at dbgcmdptr. Ignores leading
 * blanks.
 */
getparm()
{
	int value;

	value = 0;
	if (!eol()) {
		expression(&dbgcmdptr, &value, 1);
	}
	return(value);
}


/*
 * returns non zero if there are no more parameters in the dbgcmd string.
 */

eol()
{
	while (*dbgcmdptr == ' ') {
		dbgcmdptr++;
	}
	if (*dbgcmdptr == '\0') {
		return(1);
	} else {
		return(0);
	}
}


/*
 * displays memory starting at adx for len.
 * If len is zero, we default to 128 bytes.
 */

xdisplay(ad, le)
unsigned ad;
unsigned le;
{	register int j, i;
	register long inx;
	register unsigned char *cadx;
	register long adx;
	register long len;
	register int cnt;

	inx = 0;
	adx = (long) ad;
	cadx = (unsigned char *)ad;
	len = (le == 0) ? 0x80 : (long) le;
	while (inx < len) {
		if (checkabort()) {
			dbgprintf("\n");
			return(1);
		}
		if (badaddr(adx)) return(1);
		if ((inx % 8) == 0) {
			dbgprintf(" ");
			cnt++;
		}
		if ((inx % 16) == 0) {
			dbgprintf("%08x:", adx);
			cnt = 0;
		}
		dbgprintf(" %02x", *(unsigned char *)adx);
		cnt += 3;

		/* Now, interpret the bytes, if at end of line */
		if (inx >= (len - 1) || (inx % 16) == 15) {
			unsigned char ch;

			/* First, space out to the end of the line */
			while (cnt++ < 49)
				dbgprintf(" ");
			dbgprintf(" ");
			for (i=0, j=inx%16; i<=j; i++) {
				ch = *(unsigned char *)cadx++;
				if ((ch < ' ') || (ch > '~'))
					ch = '.';
				dbgputc(ch);
				if (i == 7) {
					dbgputc(' ');
				}
			}
			for (;i<16;i++)
			{	dbgputc(' ');
				if (i == 7)
					dbgputc(' ');
			}
			dbgprintf("\n");
		}
		inx += 1;
		adx += 1;
	}
	return(0);
}

checkabort()
{
	register int c;

	if (!dbgwaitc()) {
		return(0);
	}
	c = dbggetc();
	if (c == '\023') {
		c = dbggetc();
	}
	if (c == '\003') {
		return(1);
	} else {
		return(0);
	}
}

char *
memchr(s, c, n)
char *s;
int c, n;
{

	while ((n--) > 0) {
		if (*s == c) {
			return(s);
		}
		s++;
	}
	return(0);
}

char *
memcpy(s1, s2, n)
register char *s1, *s2;
register int n;
{
	register char *p;

	p = s1;
	while ((n--) > 0) {
		*s1++ = *s2++;
	}
	return(p);
}

int
dbglen(str)
register char *str;
{
	register int cnt;

	cnt = 0;
	while (*str++ != '\0') {
		cnt++;
	}
	return(cnt);
}

int
dbgcmp(s1, s2)
register char *s1, *s2;
{

	if(s1 == s2)
		return(0);
	while(*s1 == *s2++)
		if(*s1++ == '\0')
			return(0);
	return(*s1 - *--s2);
}

int
tolower(c)
register int c;
{
	if(c >= 'A' && c <= 'Z')
		c -= 'A' - 'a';
	return(c);
}

char *
dbgcat(s1, s2)
register char *s1, *s2;
{
	register char *os1;

	os1 = s1;
	while(*s1++)
		;
	--s1;
	while(*s1++ = *s2++)
		;
	return(os1);
}

char *
dbgcpy(s1, s2)
register char *s1, *s2;
{
	register char *os1;

	os1 = s1;
	while(*s1++ = *s2++)
		;
	return(os1);
}

int
dbgncmp(s1, s2, n)
register char *s1, *s2;
register n;
{
	if(s1 == s2)
		return(0);
	while(--n >= 0 && *s1 == *s2++)
		if(*s1++ == '\0')
			return(0);
	return((n < 0)? 0: (*s1 - *--s2));
}

findbp(addr)
unsigned short *addr;
{	register struct bpstruct *bp;
	register i;

	for (i=0, bp = dbgbp; i < MAXBP; i++, bp++) {
		if (((bp->flags&BPLINKW) &&
		    bp->adx == (unsigned short *)((int)addr+4)) ||
		   ((bp->flags&BPLINKL) &&
		    bp->adx == (unsigned short *)((int)addr+6)) ||
		   (bp->adx == addr))
			break;
	}
	if (i == MAXBP) {
		return(-1);
	}
	return(i);
}

findbpslot()
{
	return(findbp(0));
}

/* Templates for MC68K link instructions */
#define LINK_W 0x4E50		/* link.w %reg */
#define LINK_L 0x4808		/* link.l %reg */

setbp(addr, flag)
unsigned short *addr;
{	register unsigned int val;
	register i;

	i = findbpslot();
	if (i == -1) {
		dbgprintf("all breakpoints in use\n");
		return(i);
	}
	val = *addr;
	if (flag == 'B')
	{	if ((val&0xFFF8) == LINK_W)
		{	addr = (unsigned short *)((int)addr + 4);
			dbgbp[i].flags |= BPLINKW;
		} else if ((val&0xFFF8) == LINK_L)
		{	addr = (unsigned short *)((int)addr + 6);
			dbgbp[i].flags |= BPLINKL;
		}
	}
	dbgbp[i].adx = addr;
	return(i);
}

delbp(addr)
unsigned short *addr;
{
	register i;
	i = findbp(addr);
	if (i == -1) {
		dbgprintf("no such breakpoint\n");
		return;
	}
	dbgbp[i].adx = 0;
	dbgbp[i].flags = 0;
	dbgbp[i].bpp = 0;
}

installbps()
{	register int i;
	register unsigned short *p;

	if (!(DB_Flags&DB_TRACE))
		for (i=0; i < MAXBP; i++)
		{	if ((p = dbgbp[i].adx) && !(dbgbp[i].flags & BPSINGLE))
			{	dbgbp[i].val = *p;
				*p = SYS_BKPT;
			}
		}
	dbgcflush();
}

removebps()
{	register unsigned short *p;
	register unsigned short v;
	register i;

	for (i=0; i < MAXBP; i++)
	{	if ((p = dbgbp[i].adx) && (v = dbgbp[i].val) &&
		    !(dbgbp[i].flags & BPSINGLE) )
			*p = v;
	}
	dbgcflush();
}

printsptb(p)
register struct proc *p;
{
	dbgprintf("%8x %5x %5x %5x %5x %5x %5x %8x %8x %s\n", p, 
		p->p_stat, p->p_flag, p->p_pid, p->p_ppid, 
		p->p_size, p->p_sig, p->p_root, p->p_region,
		formatsym(p->p_wchan));
}

usage()
{
	dbgprintf("= <exp> print value\n");
	dbgprintf("b [<exp> [<proc>]] show-all/set-a bp\n");
	dbgprintf("B [<exp> [<proc>]] show-all/set-a bp after link\n");
	dbgprintf("d [<exp>] delete [all] bp\n");
	dbgprintf("e exit\n");
	dbgprintf("f kstk trace\n");
	dbgprintf("i <exp> <cnt> inst disasm\n");
	dbgprintf("k <exp> [<lev>] user page table\n");
	dbgprintf("K [<lev>] kernel page table\n");
	dbgprintf("l <exp> <offs> <cnt> display linked list chain\n");
	dbgprintf("p ps-like display\n");
	dbgprintf("P [<proc>] print proc's p_region list\n");
	dbgprintf("Q <reg> print region\n");
	dbgprintf("r <exp> <cnt> display mem\n");
	dbgprintf("R print region tbl\n");
	dbgprintf("s <pid signo> send signo to pid\n");
	dbgprintf("t <exp> <len> <cnt> print len*cnt bytes from mem in cnt blocks\n");
	dbgprintf("T [<cnt> [-]] Trace (single-step) from current location\n");
	dbgprintf("u <proc> print ublk\n");
	dbgprintf("U <proc> kstk trace for proc\n");
	dbgprintf("v print var structure\n");
	dbgprintf("w <exp> modify memory\n");
	dbgprintf("x print registers\n");
	dbgprintf("z <proc> print proc entry\n");
	dbgprintf("Z [s|c|t] print SRP/CRP/TC register\n");
}

printproc(p)
register struct proc *p;
{
	register preg_t *prp;
	dbgprintf("proc %d: p_flag=%x p_stat=%x p_pri=%x p_cpu=%x\n",p->p_pid,
		p->p_flag,p->p_stat,p->p_pri,p->p_cpu);
	dbgprintf("\tp_nice=%x p_time=%x p_wchan=%s p_cursig=%x\n",
		p->p_nice,p->p_time,formatsym(p->p_wchan),p->p_cursig);
	dbgprintf("\tp_sig=%x p_sigmask=%x p_sigignore=%x p_sigcatch=%x\n",
		p->p_sig,p->p_sigmask,p->p_sigignore,p->p_sigcatch);
	dbgprintf("\tp_uid=%x p_suid=%x p_pgrp=%x p_ppid=%x p_region=%x\n",
		p->p_uid,p->p_suid,p->p_pgrp,p->p_ppid,p->p_region);
	dbgprintf("\tp_link=%x p_parent=%x p_child=%x p_sibling=%x\n",
		p->p_link,p->p_parent,p->p_child,p->p_sibling);
	dbgprintf("\tp_sbr=%x p_size=%x p_addr=%x p_root=%x p_stack=%x\n",
		p->p_sbr,p->p_size,p->p_addr,p->p_root, p->p_stack);
	dbgprintf("\tp_utime=%x p_stime=%x p_xstat=%x p_clktim=%x\n",
		p->p_utime,p->p_stime,p->p_xstat,p->p_clktim);
	dbgprintf("\tp_compatflags=%x p_ttyp=%x\n",p->p_compatflags,p->p_ttyp);
	for (prp = p->p_region; prp->p_reg; prp++)
	{	dbgprintpreg(prp);
		dbgprintreg(prp->p_reg);
	}
	
}

/*
 * Fetch the current Supervisor/CPU Root Pointer
 *  As a side effect, it may invalidate existing ATC/TLB entries for this
 *  root pointer: always on 851 and 030 (as coded), unknown for 040.
 */
get_srp(rp)
register unsigned long *rp;	/* !! Known to be a2 !! */
{
	/* asm ("PMOVE	SRP,(a2%)"); works for 020 (851), 030, 040 */
	asm("short 0xf012");
	asm("short 0x4a00");
}

get_crp(rp)
register unsigned long *rp;	/* !! Known to be a2 !! */
{
	/* asm ("PMOVE	CRP,(a2%)"); works for 020 (851), 030, 040 */
	asm("short 0xf012");
	asm("short 0x4e00");
}

get_tc(lp)
register unsigned long *lp;	/* !! Known to be a2 !! */
{
	/* asm ("PMOVE	TC,(a2%)"); works for 020 (851), 030, 040 */
	asm("short 0xf012");
	asm("short 0x4200");
}

#if 0
#define	BSR2	0x6100		/* bsr offset	*/
#define	CALL2	0x4E90		/* jsr a0@	*/
#define	CALL4	0x4EBA		/* jsr offset	*/
#define CALL6	0x4EB9		/* jsr <adr>	*/
#define ADDQL	0x5080		/* addql #x,	*/
#define ADDL	0xD1FC		/* addl #X,	*/
#define ADDW	0xDEFC		/* addw #X,	*/
#define BRAW	0x6000		/* bra.w L	*/
#define ISYM	2

backtr(link, stack, cnt)
{
	register long	rtn, p, inst;
	register int	n = 1, i, argn;
	long		calladr, entadr;
	int		tinst, taddr, indir;
	int		wasc4;

	while(cnt--)
	{
		calladr = -1; entadr = -1; indir = -1;
		wasc4 = 0;
		if (badaddr(stack)) break;
		rtn = fetch(stack, DSP, 4);
		/* If no link instruction, return is on top of stack */
		if (findcall(rtn, &entadr, &calladr, &indir, &wasc4)) {
			p = stack;
			stack += 4;
		} else {
		/* Didn't find call; there must be link instruction */
			if (badaddr(link)) break;
			p = link;
			link = fetch(p, DSP, 4);
			p += 4;
			if (badaddr(p)) break;
			rtn = fetch(p, DSP, 4);
			if (!findcall(rtn, &entadr, &calladr, &indir, &wasc4)){
				break;
			}
		}

		inst = instfetch(rtn, ISP, 2);
		if ((inst & 0xF1C0) == ADDQL) {
			argn = (inst>>9) & 07;
			if (argn == 0) argn = 8;
			argn += 4;
		} else if ((inst & 0xF1FC) == ADDL) {
			argn = 4 + instfetch(rtn + 2, ISP, 4);
		} else if ((inst & 0xFFFF) == ADDW) {
			argn = 4 + instfetch(rtn + 2, ISP, 2);
		} else if ((inst & 0xFFFF) == BRAW) {
			/* one level indirect */
			taddr = instfetch(rtn + 2, ISP, 2);
			if (taddr & 0x8000) taddr |= 0xFFFF0000;
			taddr += rtn + 2;
			tinst = instfetch(taddr, ISP, 2);
			if ((tinst & 0xF1C0) == ADDQL) {
				argn = (tinst>>9) & 07;
				if (argn == 0) argn = 8;
				argn += 4;
			} else if ((tinst & 0xF1FC) == ADDL) {
				argn = 4 + instfetch(taddr + 2, ISP, 4);
			} else if ((inst & 0xFFFF) == ADDW) {
				argn = 4 + instfetch(rtn + 2, ISP, 2);
			} else argn = 0;
		} else {
			argn = 0;
			if (wasc4 && (instfetch(rtn-6,ISP,2) & 0xFFC0) == 0x2E80) {
				argn = 4;	/* catch some single arg fcns */
			}
		}
		if (argn && (argn % 4)) argn = (argn/4) + 1;
		else argn /= 4;

		if (calladr != -1) psymoff(calladr, ISYM, ":");
		else break;
		if (entadr != -1) {
			if (indir != -1) dbgprintf("(a%d@)", indir);
			else psymoff(entadr, ISYM, "");
		} else
			dbgprintf("???");
		dbgputc('(');
		if (argn) dbgprintf("%x", fetch(p += 4, DSP, 4)); 
		for(i = 1; i < argn; i++)
			dbgprintf(", %X", fetch(p += 4, DSP, 4));
		dbgprintf(")\n");
	}
}

fetch(adr, space, size)
{
	register long data = 0;

	if (badaddr(adr)) return(-1);
	if (size == 4) data = *(long *)adr;
	else data = *(short *)adr;
	return(data);
}

findcall(rtn, pent, pcall, pind, pwc4)
register long rtn;
register long *pent, *pcall, *pind, *pwc4;
{
	register long inst;
	register int i;

	if (instfetch(rtn - 6, ISP, 2) == CALL6) {
		*pent = instfetch(rtn - 4, ISP, 4);
		*pcall = rtn - 6;
		return 1;
	} else if ((instfetch(rtn - 4, ISP, 2) == BSR2) ||
		   (instfetch(rtn - 4, ISP, 2) == CALL4))  {
		if (instfetch(rtn - 4, ISP, 2) == CALL4) *pwc4 = 1;
		*pent = rtn - 2 + (short)instfetch(rtn - 2, ISP, 2);
		*pcall = rtn - 4;
		return 1;
	} else {
		inst = instfetch(rtn - 2, ISP, 2);
		for (i=0; i<8; i++) {
			if (inst == CALL2+i) {
				*pent = 0;
				*pind = i;
				*pcall = rtn - 2;
				return 1;
			}
		}
	}
	return 0;
}
#endif


/*
 * Baroque command line scarfer - got to get rid of unfriendly stuff.
 * Look at global state to determine behavior.
 */
getcmd()
{	register char *p;

	do
	{	dbgprintf(dbgprompt);
		p = dbgcmd;
		gets(p);
		/* Ignore spurious garbage */
		while (*p <= ' ' || *p > 0x7f)
		{	if (!(dbgcmd[0] = *p))
				break;
			*p++ = ' ';
		}
	} while (!dbgcmd[0] && !(DB_Flags&DB_TRACE));
	dbgcmdptr = p + 1;
}
