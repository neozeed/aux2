#ifndef lint	/* .../sys/psn/io/debug_data.c */
#define _AC_NAME debug_data_c
#define _AC_NO_MAIN "@(#) Copyright (c) 1987 Apple Computer, Inc., All Rights Reserved.  {Apple version 2.1 89/10/13 16:00:16}"
#include <apple_notice.h>

#ifdef _AC_HISTORY
  static char *sccsid = "@(#)Copyright Apple Computer 1987\tVersion 2.1 of debug_data.c on 89/10/13 16:00:16";
#endif		/* _AC_HISTORY */
#endif		/* lint */

#define _AC_MODS

/*	@(#)debug_data.c	UniPlus VVV.2.1.4	*/
/*	@(#)debug.c	UniPlus V.2.1.18	*/
#ifdef	DEBUG


#include "sys/types.h"
#include "sys/param.h"
#include "sys/mmu.h"
#include "sys/sysmacros.h"
#include "svfs/fsdir.h"
#include "sys/buf.h"
#include "sys/iobuf.h"
#include "sys/file.h"
#include "sys/time.h"
#include "sys/vnode.h"
#include "svfs/inode.h"
#include "sys/map.h"

#include "sys/mmu.h"
#include "sys/page.h"
#include "sys/region.h"
#include "sys/pfdat.h"


#include "sys/proc.h"
#include "sys/reg.h"
#include "sys/signal.h"
#include "sys/systm.h"
#include "sys/ioctl.h"
#include "sys/tty.h"
#include "sys/user.h"
#include "sys/var.h"
#include "sys/cpuid.h"
#include "sys/vfs.h"
#include "sys/mbuf.h"
#include "sys/mount.h"

#ifdef	HOWFAR
int T_asa = 0;
int T_availmem = 0;
int T_carl = 0;
int T_clai = 0;
int T_dirlook = 0;
int T_dophys = 0;
int T_exec = 0;
int T_exit = 0;
int T_fault = 0;
int T_fdb = 0;
int T_fork = 0;
int T_fwd = 0;
int T_getpages = 0;
int T_grow = 0;
int T_hardflt = 0;
int T_hardsegflt = 0;
int T_iaccess = 0;
int T_ipc = 0;
int T_lookup = 0;
int T_machdep = 0;
int T_main = 0;
int T_meminit = 0;

extern int T_mmuinit;		/* put it in the pstart section */

int T_net = 0;
int T_page = 0;
int T_page2 = 0;
int T_paul = 0;
int T_pfault = 0;
int T_ram = 0;
int T_region = 0;
int T_rz = 0;
int T_sccio = 0;
int T_sendsig = 0;
int T_signal = 0;
int T_slp = 0;
int T_sony = 0;
int T_sony2 = 0;

extern int T_startup;		/* must be in pstart section */

extern int T_streamhead;
int T_subyte = 0;
int T_swap = 0;
int T_swapalloc = 0;
int T_swtch = 0;
int T_uconfig = 0;
int T_umachdep = 0;
int T_usyslocal = 0;
int T_utrap = 0;
int T_vm = 0;
int T_vt100 = 0;
int T_sdisk = 0;
int T_gdisk = 0;

struct Tflags {
	int *flag;
	char *name;
} Tflags[] = {
	&T_asa,		"T_asa",
	&T_availmem,		"T_availmem",
	&T_carl,	"T_carl",
	&T_clai,	"T_clai",
	&T_dirlook,	"T_dirlook",
	&T_dophys,	"T_dophys",
	&T_exec,	"T_exec",
	&T_exit,	"T_exit",
	&T_fault,	"T_fault",
	&T_fdb,		"T_fdb",
	&T_fork,	"T_fork",
	&T_fwd,		"T_fwd",
	&T_getpages,	"T_getpages",
	&T_grow,	"T_grow",
	&T_hardflt,	"T_hardflt",
	&T_hardsegflt,	"T_hardsegflt",
	&T_iaccess,	"T_iaccess",
	&T_ipc,		"T_ipc",
	&T_lookup,	"T_lookup",
	&T_machdep,	"T_machdep",
	&T_main,	"T_main",
	&T_mmuinit,	"T_mmuinit",
	&T_net,		"T_net",
	&T_page2,	"T_page2",
	&T_page,	"T_page",
	&T_paul,	"T_paul",
	&T_pfault,	"T_pfault",
	&T_ram,		"T_ram",
	&T_region,	"T_region",
	&T_rz,		"T_rz",
	&T_sccio,	"T_sccio",
	&T_sendsig,	"T_sendsig",
	&T_signal,	"T_signal",
	&T_slp,		"T_slp",
	&T_sony2,	"T_sony2",		/* before T_sony */
	&T_sony,	"T_sony",
	&T_startup,	"T_startup",
	&T_streamhead,	"T_streamhead",
	&T_subyte,	"T_subyte",
	&T_swapalloc,	"T_swapalloc",		/* must appear before T_swap */
	&T_swap,	"T_swap",
	&T_swtch,	"T_swtch",
	&T_uconfig,	"T_uconfig",
	&T_umachdep,	"T_umachdep",
	&T_usyslocal,	"T_usyslocal",
	&T_utrap,	"T_utrap",
	&T_vm,		"T_vm",
	&T_vt100,	"T_vt100",
	0, 0
};

int gettrace();
#endif HOWFAR

char tbuf[80];

#ifdef KDB
extern char *panicstr;
extern int kdb_debug_enter;
#endif KDB
extern int buf_usage();
extern int db_bufs();
extern int db_globals();
extern int setprintfstall();
extern int dbrunq();
extern int dbstreams();
extern int debuggtrace();
extern int dbswapbuf();
extern int syscalloff();
extern int syscallon();
extern int tracebuf();
extern int debugcore();
extern int debugdebug();
extern int debugfile();
extern int debuginode();
extern int debuglockedinodes();
extern int debugmdump();
extern int debugmbufclusters();
extern int debugmount();
extern int debugproc();
extern int debugregion();
extern int debugswap();
extern int debugvnode();
extern int debugkmem();
extern int kdb_entry();
extern int db0stack();
extern int dumpmm();
extern int nullsys();
extern int debugtune();

int debughelp();

extern char debuggetchar();

struct debug_cmd {
	char name;
	char *help;
	int (*funct)();
} debug_cmd[] = {
	'?', "help", debughelp,
	'A', "stack backtrace", db0stack,

#ifdef	ARG
	'a', "serial args", dbargs,
#endif ARG

#define BUFFERSUMMARY
#ifdef	BUFFERSUMMARY
	'B', "buffer usage summary", buf_usage,
#endif BUFFERSUMMARY

	'b', "buffers", db_bufs,
	'C', "mbuf clusters", debugmbufclusters,
	'c', "core map", debugcore,
	'D', "dump memory locations", debugmdump,
	'd', "read/write hex locations", debugdebug,
	'f', "file table", debugfile,
	'g', "global variables", db_globals,
	'i', "inode table (including vnodes)", debuginode,

#define KMEMALLOC
#ifdef	KMEMALLOC
	'K', "kmem alloc", debugkmem,
#endif KMEMALLOC
#ifdef KDB
	'k', "kdb", kdb_entry,
#endif KDB
	'l', "locked inode list", debuglockedinodes,
	'M', "mount table", debugmount,
	'm', "mmu dump", dumpmm,
	'P', "set printf stall", setprintfstall,
	'p', "proc table", debugproc,
	'q', "disk queue", nullsys,
	'r', "run queue", dbrunq,

	'R', "region info", debugregion,

	's', "swap map", debugswap,

	'S', "streams", dbstreams,

#ifdef HOWFAR
	'T', "set/reset tracing flags", gettrace,
#endif

	't', "alter debug trace", debuggtrace,
	'v', "vnodes (devtovp)", debugvnode,
	'w', "swap buffers", dbswapbuf,
	'x', "exit debug", nullsys,
#define EXIT_CMD 'x'
	'Y', "syscall trace restored", syscalloff,
	'y', "syscall trace on", syscallon,
	'z', "trace buffer", tracebuf,
	'Z', "dump tune_t struct", debugtune,
	 0,  (char *) 0, nullsys
};
	
#ifdef	ARG
static struct args *arg_ptr;
#endif

debug(ap)
struct args *ap;
{
	register int c;
	int spl;
	extern int printfstall;
	struct debug_cmd *cmdp;

	spl = spl7();
#ifdef KDB
	if (kdb_debug_enter && !panicstr) {
		kdb_entry(ap);
		splx(spl);
		return;
	}
#endif KDB

#ifdef ARG
	arg_ptr = ap;
#endif
	for (;;) {
		int SavePrintfstall = printfstall;

		printfstall = 0;
		cmdp = debug_cmd;
		printf("%c", cmdp->name);
		for (cmdp++; cmdp->name; cmdp++)
			printf(" %c", cmdp->name);
		printf(": ");

		printfstall = SavePrintfstall;

		c = debuggetchar();
		printf("%c\n", c);
		if (c == EXIT_CMD) {
			splx(spl);
			return;
		}

		for (cmdp = debug_cmd; cmdp->name && cmdp->name != c; cmdp++)
			;

		if ( ! cmdp->name) {
			printf("%c not implemented - try again (? for help)\n", c);
			continue;
		}
		(*cmdp->funct)(ap);
	}
}

debughelp()
{
	struct debug_cmd *cmdp;

	for (cmdp = debug_cmd; cmdp->name; cmdp++)
		printf("\t%c\t%s\n", cmdp->name, cmdp->help);
}

#ifdef	ARG
dbargs()
{
	register int c, j;
	char command[COMMSIZ+1];

	for (c=0; c<COMMSIZ; c++) {
		j = u.u_comm[c];
		if (j<=' ' || j>=0x7F)
			break;
		command[c] = j;
	}
	command[c] = 0;
	printf("pc = 0x%x sr = 0x%x u.u_procp = 0x%x dev=0x%x",
		arg_ptr->a_pc, arg_ptr->a_ps&0xFFFF, u.u_procp, arg_ptr->a_dev);
	printf(" pid = %d exec = '%s'\n", u.u_procp->p_pid, command);
	for (c = 0; c < 16; c++) {
		printf("0x%x ", arg_ptr->a_regs[c]);
		if (c == 7 || c == 15) printf("\n");
	}
}
#endif ARG

#ifdef	HOWFAR
gettrace()
{
	struct Tflags *tp;
	char *p;
	int i;

	for (;;) {
		printf("T_");
		gets();
		if (tbuf[0] == 0)
			return;
		for (tp = Tflags; tp->name; tp++) {
			i = strlen(tp->name);
			if (strncmp(tbuf, tp->name, i) == 0)
				break;
			i -= 2;
			if (strncmp(tbuf, &tp->name[2], i) == 0)
				break;
		}
		if (tp->name == 0) {
			for (tp = Tflags; tp->name; tp++) {
				printf("\t%s: %x", tp->name, *tp->flag);
				if ((tp - Tflags) & 1)
					printf("\t");
				else printf("\n");
			}
			if ( ! ((tp - Tflags) & 1))
				printf("\n");
			continue;
		}
		if (tbuf[i])
			i++;
		*tp->flag = atoi(&tbuf[i]);
	}
}
#endif HOWFAR

atoi(s)
register char *s;
{
	register int i = 0;

	while (s && *s && *s >= '0' && *s <= '9')
		i = i * 10 + (*s++ - '0');
	return(i);
}

gets()
{
	char *p;

	for (p = tbuf; p < &tbuf[80]; p++) {
		*p = debuggetchar();
		if (*p == '\r')
			*p = '\n';
		if (*p == '\b') {
			if (p > tbuf) {
				printf("\b \b");
				p--;
			}
			p--;
		} else
			printf("%c", *p);
		if (*p == '\n')
			break;
	}
	*p = 0;
}
#endif DEBUG
