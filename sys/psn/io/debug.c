#ifndef lint	/* .../sys/psn/io/debug.c */
#define _AC_NAME debug_c
#define _AC_NO_MAIN "@(#) Copyright (c) 1987, 1988, 1989 Apple Computer, Inc., All Rights Reserved.  {Apple version 2.1 89/10/13 16:32:33}"
#include <apple_notice.h>

#ifdef _AC_HISTORY
  static char *sccsid = "@(#)Copyright Apple Computer 1987, 1988, 1989\tVersion 2.1 of debug.c on 89/10/13 16:32:33";
#endif		/* _AC_HISTORY */
#endif		/* lint */

#define _AC_MODS
/*	@(#)debug.c	UniPlus V.2.1.25	*/
/*	Copyright 1985 UniSoft */

#ifdef	DEBUG
/*
 *	Slightly modified to make it easier to add debug proceedures
 *	and trace variables without waiting years for this module
 *	to recompile.  See psn/io/debug_data.c
 *
 *	If I missed your favorite routine, or the routines are out
 *	of date, the old debugger is in psn/io/odebug.c.  Grab the
 *	routine, add it here or in debug_data.c (or wherever) and
 *	add an entry into the function/command/name structure in
 *	debug_data.c
 *
 *	If you want a trace variable added, again add it to
 *	debug_data.c.
 */

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
#include "sys/tuneable.h"
#include "sys/getpages.h"

#define TRBUFSIZE	128		/* size of trace buffer 
					   (must be power of 2) */
#define	TRBUFMASK	(TRBUFSIZE-1)
static char trace_quit = 0;		/* quit when the buffer is full */
static char trace_print = 1;		/* printf when tracedebug is called */
static int trace_start = 0;		/* start of buffer pointer */
static int trace_end = 0;		/* end of buffer pointer */
static char *trace_a[TRBUFSIZE];	/* printf args */
static long trace_b[TRBUFSIZE];
static long trace_c[TRBUFSIZE];
static long trace_d[TRBUFSIZE];
static long trace_e[TRBUFSIZE];

static getint();

#include "sys/stream.h"
#include "sys/stropts.h"

#define FLAG(a, b, c)			\
	if ( (a) & (b) ) {		\
		if (i++)		\
			printf("|");	\
		printf(c);		\
	}

void	bufdump();

#define	DEBUGG_DNLC_OFF		0x01	/* turn off directory name cacheing */
int debugg = 0;				/* global debug flag set by case "T" */
extern int	doingcache;

#define NSYSENT	256
typedef enum {false,true} bool;
extern bool	SyscallTraceTable[NSYSENT];
bool	SaveSyscallTraceTable[NSYSENT];
int	sttinit;	/* SaveSyscallTraceTable has been initialized */
extern int printfstall;

struct mclstats {
	int	nmclget;
	int	nmclgetfailed;
	int	nmclgetx;
	int	nmclput;
	int	nmclputx;
	int	nmclrefdec;
	int	nmclrefinc;
} mclstats;

extern struct buf *sbuf;


#define BUFFERSUMMARY
#ifdef	BUFFERSUMMARY
buf_usage()
{
	struct buf *dp, *bp;
	int i, c, tc;

	printf("v_hbuf=%x v_hmask=%x\n", v.v_hbuf, v.v_hmask);
	printf("NBUF=%x\n", v.v_buf);
	printf("HASH ");
	tc = 0;
	for (i = 0; i < v.v_hbuf; i++) {
		dp = (struct buf *)&bufhash[i];
		c = 0;
		for (bp = dp->b_forw; bp != dp; bp = bp->b_forw)
			c++;
		printf("%x ", c);
		tc += c;
	}
	printf("= %x\n", tc);
	printf("FREE ");
	tc = 0;
	for (i = 0; i < 4; i++) {
		dp = &bfreelist[i];
		c = 0;
		for (bp = dp->av_forw; bp != dp; bp = bp->av_forw)
			c++;
		printf("%x ", c);
		tc += c;
	}
	printf("= %x\n", tc);
}
#endif BUFFERSUMMARY

db_bufs()
{
	struct buf *bp;

	for (bp = &sbuf[0]; bp < &sbuf[v.v_buf]; bp++) {
		printf("sbuf[%d]: ", bp-&sbuf[0]);
		debugbuf(bp);
	}
}

db_globals()
{
		printf("lbolt=%x time.tv_sec=%x runrun=%x runin=%x runout=%x\n",
			lbolt, time.tv_sec, runrun, runin, runout);
		printf("u.u_procp=proc[%d]\n", u.u_procp-&proc[0]);
}

setprintfstall()
{
	printf("printfstall = 0x%x", printfstall);
	printfstall = debugghex();
	printf("printfstall = 0x%x\n", printfstall);
}

dbrunq()
{
	struct proc *p;

	printf("RUNQ:");
	for(p=runq; p; p=p->p_link)
		printf(" %x", p-&proc[0]);
	printf("\n");
}

dbstreams()
{
	int c, i;
	int ndblock;

	c = '?';
	for(;c != 'x';) {
		printf("Streams - ? d f m q r s S x: ");
		c = debuggetchar();
		printf("%c\n", c);
		switch (c) {

		default:
			printf("\t?\tPrint streams help\n");
			printf("\td\tList data blocks\n");
			printf("\tf\tList message blocks\n");
			printf("\tm\thelp\n");
			printf("\tq\tDisplay queues\n");
			printf("\tr\tList the runable streams queues\n");
			printf("\ts\tDump streams\n");
			printf("\tS\tPrint statistics\n");
			printf("\tx\tReturn to main debug menu\n");
			break;
		case 'x':
			break;
		case 's':

	/*
	 * The stream table slots indicated in the argument list are
	 * printed.  If there are no arguments, then all allocated
	 * streams are listed.  The function prstream() can be found
	 * in the file stream.c.
	 */

			printf("\nSLOT  WRQ IOCB INODE  PGRP      IOCID IOCWT WOFF ERR FLAGS\n");
			for (i=0; i < v.v_nstream; i++) prstream(i,1);
			break;

		case 'q':

	/*
	 * The stream queue entries corresponding to the slot numbers 
	 * provided are printed.  If no argument is provided, then all 
	 * allocated queues are printed.  The function prqueue() can
	 * be found in the file stream.c.
	*/ 

			printf("\nSLOT   INFO   NEXT LINK   PTR     CNT HEAD TAIL MINP MAXP HIWT LOWT FLAGS\n");
			for (i=0; i < v.v_nqueue; i++) prqueue(i,1);
			break;

		case 'm':

	/*
	 * The message headers corresponding to the slot numbers 
	 * provided are printed.  If no argument is given, then all
	 * allocated message headers are printed.  If the argument
	 * 'all' is given, then all message blocks are printed,
	 * whether active or not.  The function prmess() can be
	 * found in the file stream.c.
	 */

			printf("\nSLOT NEXT CONT PREV   RPTR     WPTR   DATAB\n");
			for (i=0; i < nmblock; i++) prmess(i,1);
			break;


		case 'd':

	/*
	 * Print the header for the data blocks listed in the argument
	 * list, or all allocated data blocks, or all data blocks of
	 * a given set of classes, or all data blocks from all classes.
	 * The functions prdblk() and prdball() are located in the
	 * file stream.c.
	 */

			printf("\nSLOT CLASS SIZE  REF   TYPE     BASE     LIMIT  FREEP\n");
			ndblock = v.v_nblk4096 + v.v_nblk2048 + v.v_nblk1024 +
				v.v_nblk512 + v.v_nblk256 + v.v_nblk128 +
				v.v_nblk64 + v.v_nblk16 + v.v_nblk4;
			for (i=0; i < ndblock; i++) prdblk(i,1);
			break;


		case 'f':

	/* 
	 * List all of the message or data headers on the respective
	 * free lists.  If the argument is 'm', then only the message
	 * headers are listed.  If the argument is 'd', then the following
	 * arguments list the classes of data blocks for which the
	 * free lists are to be printed.  If no classes are explicitly
	 * provided, then the free lists for all data block classes
	 * are printed.  If no argument at all is given, an error 
	 * message is printed.  The functions prmfree() and prdfree()
	 * are located in the file stream.c.
	 */ 


			printf("\nSLOT NEXT CONT PREV   RPTR     WPTR   DATAB\n");
			prmfree();
			printf("\nSLOT CLASS SIZE  REF   TYPE     BASE     LIMIT  FREEP\n");
			for (i=0;i<9;i++) prdfree(i);
			break;


		case 'r':

	/*
	 * Print the slot numbers of each queue that has been enabled
	 * and placed on the qhead list.  This is done using the function
	 * prqrun() which is in the file stream.c.
	 */

			prqrun();
			break;

		case 'S':

	/*
	 * Print various statistics about streams, including current
	 * message block, data block, queue, and stream allocations
	 */

			prstrstat();
			break;
		}
	}
}

debuggtrace()
{
	int i;

	printf("debug=%x:", debugg);
	debugg = debugghex();
	if (debugg & DEBUGG_DNLC_OFF)
		doingcache = 0;
	else
		doingcache = 1;
	printf("debug=%x (", debugg);
	i = 0;
	FLAG(debugg, DEBUGG_DNLC_OFF, "DNLC_OFF");
	printf(")\n");
}

dbswapbuf()
{
	struct buf *bp;

	for (bp = &pbuf[0]; bp < &pbuf[v.v_pbuf]; bp++) {
		printf("pbuf[%d]: ", bp-&pbuf[0]);
		debugbuf(bp);
	}
}
syscalloff()
{
	int i;

	for (i = 0; i < NSYSENT; i++)
		SyscallTraceTable[i] = SaveSyscallTraceTable[i];
}
syscallon()
{
	int i;

	for (i = 0; i < NSYSENT; i++) {
		if (sttinit == 0)
			SaveSyscallTraceTable[i] = SyscallTraceTable[i];
		SyscallTraceTable[i] = true;
	}
	sttinit = 1;
}

char tbuf[80];

tracebuf()
{
	int c, i;
	int trace_p;

	c = '?';
	trace_p = (trace_end - trace_start - 44 + TRBUFSIZE)
					&TRBUFMASK;
	for(;c != 'x';) {
		printf("Trace buffer - ? - + . c d f g l n p s x: ");
		c = debuggetchar();
		printf("%c\n", c);
		switch (c) {
		default:
			printf("\t-\tPrevious page\n");
			printf("\t+\tNext page (also CR/LF)\n");
			printf("\t.\tCurrent page\n");
			printf("\tc\tClear trace buffer\n");
			printf("\td\tDump final page\n");
			printf("\tf\tBack to debug when buffer is full\n");
			printf("\tg\tDon't quit when buffer is full\n");
			printf("\tl\tList part of trace buffer\n");
			printf("\tn\tPrinting off\n");
			printf("\tp\tPrinting on\n");
			printf("\ts\tStatus\n");
			printf("\tx\tReturn to main debug menu\n");
			break;
		case 'x':
			break;
		case 's':
			printf("Printing = %d\n", trace_print);
			printf("Stop on full = %d\n", trace_quit);
			printf("Buffer size = %d\n", TRBUFSIZE);
			printf("Amount used = %d\n", (trace_end -
				trace_start + TRBUFSIZE)&TRBUFMASK);
			break;
		case 'c':
			trace_start = 0;
			trace_end = 0;
			break;
		case 'g':
			trace_quit = 0;
			break;
		case 'f':
			trace_quit = 1;
			break;
		case 'n':
			trace_print = 0;
			break;
		case 'p':
			trace_print = 1;
			break;
		case '-':
			trace_p -= 20;
			goto disp;
		case '\r':
		case '\n':
		case '+':
			trace_p += 20;
			goto disp;
		case 'd':
			trace_p = 0;
			goto disp;
		case 'l':
			printf("line: ");
			gets();
			i = getint(tbuf);
			if (i < 0) {
				printf("???\n");
				break;
			}
			trace_p = i;
		case '.':
		disp:
			if (trace_end == trace_start) {
				printf("	No Trace\n");
				break;
			}
			if (trace_p < 0)
				trace_p = 0;
			i = (trace_end - trace_start - 3 + TRBUFSIZE)
					&TRBUFMASK;
			if (trace_p > i)
				trace_p = i;
			i = (trace_start + trace_p)&TRBUFMASK;
			for (c = 0;c < 22;c++) {
				printf("%d:	",trace_p+c);
				printf(trace_a[i],trace_b[i],trace_c[i],
				       trace_d[i],trace_e[i]);
				i++;
				if (i >= TRBUFSIZE)
					i = 0;
				if (i == trace_end)
					break;
			}
			c = 'd';
			break;
		}
	}
}

debugbuf(bp)
struct buf *bp;
{
	register int i = 0;

	FLAG(bp->b_flags, B_READ, "READ");
	FLAG(bp->b_flags, B_DONE, "DONE");
	FLAG(bp->b_flags, B_ERROR, "ERROR");
	FLAG(bp->b_flags, B_BUSY, "BUSY");
	FLAG(bp->b_flags, B_PHYS, "PHYS");
	FLAG(bp->b_flags, B_MAP, "MAP");
	FLAG(bp->b_flags, B_WANTED, "WANTED");
	FLAG(bp->b_flags, B_AGE, "AGE");
	FLAG(bp->b_flags, B_ASYNC, "ASYNC");
	FLAG(bp->b_flags, B_DELWRI, "DELWRI");
	FLAG(bp->b_flags, B_OPEN, "OPEN");
	FLAG(bp->b_flags, B_STALE, "STALE");
#ifdef	B_UAREA
	FLAG(bp->b_flags, B_UAREA, "UAREA");
#endif B_UAREA
#ifdef	B_FORMAT
	FLAG(bp->b_flags, B_FORMAT, "FORMAT");
#endif B_FORMAT
#ifdef	B_CACHE
	FLAG(bp->b_flags, B_CACHE, "CACHE");
#endif B_CACHE
#ifdef	B_INVAL
	FLAG(bp->b_flags, B_INVAL, "INVAL");
#endif B_INVAL
#ifdef	B_LOCKED
	FLAG(bp->b_flags, B_LOCKED, "LOCKED");
#endif B_LOCKED
#ifdef	B_HEAD
	FLAG(bp->b_flags, B_HEAD, "HEAD");
#endif B_HEAD
#ifdef	B_BAD
	FLAG(bp->b_flags, B_BAD, "BAD");
#endif B_BAD
#ifdef	B_CALL
	FLAG(bp->b_flags, B_CALL, "CALL");
#endif B_CALL
#ifdef	B_NOCACHE
	FLAG(bp->b_flags, B_NOCACHE, "NOCACHE");
#endif B_NOCACHE
	printf(" dev=");
	if (bp->b_dev == NODEV)
		printf("NODEV");
	else
		printf("%x/%x", major(bp->b_dev), minor(bp->b_dev));
	printf("  bcount=%d", bp->b_bcount);
	printf(" addr=%x", bp->b_un.b_addr);
	printf(" blkno=%x", bp->b_blkno);
	if (bp->b_error)
		printf(" error=%x", bp->b_error);
	if (bp->b_resid)
		printf(" resid=%x", bp->b_resid);
	printf("\n");
}

debugpfdat(pfd)
register struct pfdat *pfd;
{
	int i = 0;

	printf("pfdat[%x]: ", pfd-pfdat);
	FLAG(pfd->pf_flags, P_QUEUE, "QUEUE");
	FLAG(pfd->pf_flags, P_BAD, "BAD");
	FLAG(pfd->pf_flags, P_HASH, "HASH");
	FLAG(pfd->pf_flags, P_DONE, "DONE");
	FLAG(pfd->pf_flags, P_SWAP, "SWAP");
	printf(" blkno=%x ", pfd->pf_blkno);
	printf("use=%x ", pfd->pf_use);
	printf("dev=%x ", pfd->pf_dev);
	printf("swpi=%x ", pfd->pf_swpi);
	printf("rawcnt=%x ", pfd->pf_rawcnt);
	printf("waitcnt=%x ", pfd->pf_waitcnt);
	printf("\n");
}

debugdebug()
{
	int o, l, a, v;

    for (;;) {
	printf("r w x:");
	o = debuggetchar();
	printf("%c\n", o);
	if (o == 'x')
		return;
	if (o != 'r' && o != 'w')
		continue;
	printf("b w l:");
	l = debuggetchar();
	printf("%c\n", l);
	if (l != 'b' && l != 'w' && l != 'l')
		continue;
	a = debugghex();
	printf("\n");
	switch (l) {
		case 'b':	printf("%x", *(char *)a & 0xFF);	break;
		case 'w':	printf("%x", *(short *)a & 0xFFFF);	break;
		case 'l':	printf("%x", *(long *)a);	break;
	}
	if (o == 'w') {
		printf("?");
		v = debugghex();
		switch (l) {
			case 'b':
				*(char *)a = v;
				printf("%x", *(char *)a & 0xFF);
				break;
			case 'w':
				*(short *)a = v;
				printf("%x", *(short *)a & 0xFFFF);
				break;
			case 'l':
				*(long *)a = v;
				printf("%x", *(long *)a);
				break;
		}
	}
	printf("\n");
    }
}

debugghex()
{
	int i, c;

	i = 0;
	for (;;) {
		c = debuggetchar();
		printf("%c", c);
		if (c >= '0' && c <= '9')
			i = (i << 4) | (c - '0');
		else if (c >= 'a' && c <= 'f')
			i = (i << 4) | (c - 'a' + 10);
		else if (c >= 'A' && c <= 'F')
			i = (i << 4) | (c - 'A' + 10);
		else
			break;
	}
	return(i);
}

debuggdec(i)
register int *i;
{
	int c, got = -1;

	*i = 0;
	for (;;) {
		c = getchar() & 0x7F;
		printf("%c", c);

		if (c >= '0' && c <= '9') {
			*i = (*i * 10) + (c - '0');
			got = 1;
		}
		else break;
	}
	return(got);
}

debugfile()
{
	register struct file *fp;
	register struct inode *ip;
	register struct vnode *vp;
	register int i;

	for (fp = &file[0]; fp < (struct file *)v.ve_file; fp++)
		if (fp->f_count) {
			printf("file[%d]: count=%d ",
				fp-&file[0], fp->f_count);
			i = 0;
			FLAG(fp->f_flag, FREAD, "READ");
			FLAG(fp->f_flag, FWRITE, "WRITE");
			FLAG(fp->f_flag, FNDELAY, "NDELAY");
#ifdef POSIX
			FLAG(fp->f_flag, FNONBLOCK, "NONBLOCK");
#endif POSIX
			FLAG(fp->f_flag, FAPPEND, "APPEND");
			FLAG(fp->f_flag, FCREAT, "CREAT");
			FLAG(fp->f_flag, FTRUNC, "TRUNC");
			FLAG(fp->f_flag, FEXCL, "EXCL");
			if (fp->f_type == DTYPE_SOCKET) {
				printf(" socket\n");
				continue;
			} else {
				vp = (struct vnode *) fp->f_data;
				ip = VTOI(vp);
				if ((ip != (struct inode *) NULL)
					&& (ip >= &inode[0])
					&& (ip < (struct inode *) v.ve_inode))
					printf(" vnode[%d] ", ip - &inode[0]);
				else
					printf(" vnode 0x%x", (int) vp);
				printf(" offset=%d\n", fp->f_offset);
			}
		}
}

debuginode()
{
	register struct inode *ip;
	register int i;

	for (ip = &inode[0]; ip < (struct inode *)v.ve_inode; ip++)
		if (ip->i_vnode.v_count) {
			printf("inode[%d]: ", ip-&inode[0]);
			i = 0;
			FLAG(ip->i_flag, ILOCKED, "LOCKED");
			FLAG(ip->i_flag, IUPD, "UPD");
			FLAG(ip->i_flag, IACC, "ACC");
			FLAG(ip->i_flag, IWANT, "WANT");
			FLAG(ip->i_flag, ITEXT, "TEXT");
			FLAG(ip->i_flag, ICHG, "CHG");
			printf(" count=%d dev=%x/%x inode=%d ",
				ip->i_vnode.v_count,
				major(ip->i_dev), minor(ip->i_dev),
				ip->i_number);
			switch (ip->i_mode & IFMT) {
				case IFDIR: printf("d"); break;
				case IFCHR: printf("c"); break;
				case IFBLK: printf("b"); break;
				case IFREG: printf("-"); break;
				case IFMPC: printf("mc"); break;
				case IFMPB: printf("mb"); break;
				case IFIFO: printf("p"); break;
				default: printf("?"); break;
			}
			if (ip->i_mode & 0400) printf("r"); else printf("-");
			if (ip->i_mode & 0200) printf("w"); else printf("-");
			if (ip->i_mode & ISUID)
				printf("s");
			else if (ip->i_mode & 0100)
				printf("x");
			else
				printf("-");
			if (ip->i_mode & 040) printf("r"); else printf("-");
			if (ip->i_mode & 020) printf("w"); else printf("-");
			if (ip->i_mode & ISGID)
				printf("s");
			else if (ip->i_mode & 010)
				printf("x");
			else
				printf("-");
			if (ip->i_mode & 04) printf("r"); else printf("-");
			if (ip->i_mode & 02) printf("w"); else printf("-");
			if (ip->i_mode & ISVTX)
				printf("t");
			else if (ip->i_mode & 01)
				printf("x");
			else
				printf("-");
			printf(" size=%d\n", ip->i_size);
			prvnode(&ip->i_vnode, 0);
		}
}

debuglockedinodes()
{
	register struct inode *ip;

	printf("Locked inodes:\n");
	for (ip = &inode[0]; ip < (struct inode *)v.ve_inode; ip++)
		if (ip->i_vnode.v_count && (ip->i_flag & ILOCKED)) {
			printf("\tinode[%d]:  locked at line %d of %s\n",
				ip - &inode[0],
				ip->i_lockedline,
				ip->i_lockedfile);
		}
}

static	gethex(), mdump_row(), mdump_byte();
debugmdump()

{
	register char *addr, *newaddr;

	addr = (char *)0;
	newaddr = (char *)0x100;
loop:
	printf("{addr, <CR>, x} ?");
	gets();
	if ((newaddr = (char *) gethex(tbuf)) == (char *) -1) {
		if(tbuf[0] == 'x')
			return;
		printf("huh?\n");
		goto loop;
	}
	else if(tbuf[0] != '\0')
		addr = newaddr;
	newaddr = addr + 0x100;
	while(addr < newaddr) {
		mdump_row(addr);
		addr += 16;
	}
	goto loop;
}

static
mdump_row(addr) 

register char *addr;
{
	register char *end = (char *)addr + 16;
	register char *start;

	start = addr;
	mdump_byte(((unsigned)addr >> 24) & 0xFF);
	mdump_byte(((unsigned)addr >> 16) & 0xFF);
	mdump_byte(((unsigned)addr >> 8) & 0xFF);
	mdump_byte((unsigned)addr & 0xFF);
	outchar(':');
	outchar(' ');
	while(addr < end) {
		mdump_byte(*addr);
		++addr;
		if(((int)(addr-start) & 0x3) == 0)
			outchar(' ');
	}
	outchar(' ');
	for(addr = start; addr < end; ++addr) {
		if(*addr >= ' ' && *addr < 0177)
			outchar(*addr);
		else
			outchar('.');
	}
	printf("\n");
}

static char 	mdump_digits[] = "0123456789ABCDEF";

static
mdump_byte(val)
	
register unsigned val;
{

	outchar(mdump_digits[(val>>4) & 0xF]);
	outchar(mdump_digits[val & 0xF]);
}


static
getint(cp)
register char *cp;
{
	register c, n;

	n = 0;
	while (c = *cp++) {
		if (c >= '0' && c <= '9')
			n = n * 10 + c - '0';
		else return(-1);
	}
	return(n);
}

static
gethex(cp)
register char *cp;
{
	register c, n;

	n = 0;
	while (c = *cp++) {
		if (c >= '0' && c <= '9')
			n = n * 16 + c - '0';
		else if (c >= 'a' && c <= 'f')
			n = n * 16 + c - 'a' + 10;
		else if (c >= 'A' && c <= 'F')
			n = n * 16 + c - 'A' + 10;
		else return(-1);
	}
	return(n);
}

debugmbufclusters()
{
	printf("# calls to mclget:\t%d\n", mclstats.nmclget);
	printf("# mclget failures:\t%d\n", mclstats.nmclgetfailed);
	printf("# calls to mclgetx:\t%d\n", mclstats.nmclgetx);
	printf("# calls to mclput:\t%d\n", mclstats.nmclput);
	printf("# calls to mclputx:\t%d\n", mclstats.nmclputx);
	printf("# mclref decrements:\t%d\n", mclstats.nmclrefdec);
	printf("# mclref increments:\t%d\n", mclstats.nmclrefinc);
}

debugmount()
{
	register int i;
	register struct mount *mp;
	register struct inode *ip;
	register struct vnode *vp;

	for (mp = &mounttab[0]; mp < (struct mount *)v.ve_mount; mp++)
		if (mp->m_bufp != NULL) {
			printf("mounttab[%d]:  ", mp-&mounttab[0]);
			printf(" dev=%x/%x",
				major(mp->m_dev), minor(mp->m_dev));
			printf("\n\tvfs:  flag ", mp->m_vfsp->vfs_flag);
			i = 0;
			FLAG(mp->m_vfsp->vfs_flag, VFS_RDONLY, "RDONLY");
			FLAG(mp->m_vfsp->vfs_flag, VFS_MLOCK, "MLOCK");
			FLAG(mp->m_vfsp->vfs_flag, VFS_MWAIT, "MWAIT");
			vp = mp->m_vfsp->vfs_vnodecovered;
			ip = VTOI(vp);
			printf(" vnodecovered=", vp);
			if ((ip != (struct inode *) NULL)
				&& (ip >= &inode[0])
				&& (ip < (struct inode *) v.ve_inode))
				printf(" vnode[%d]", ip - &inode[0]);
			else
				printf("%x", vp);
			printf(" bsize=%d", mp->m_vfsp->vfs_bsize);
			printf(" sb=sbuf[%d]", mp->m_bufp - &sbuf[0]);
			printf("\n");
		}
}

debugproc()
{
	register struct proc *p;
	register int i;
#define PSARGS
#if defined(COMMAND) | defined(PSARGS)
	char cbuf[MAX(COMMSIZ, PSARGSZ) + 1];
	struct user *up;
	int savepfn;
	extern pte_t *copypte;
	savepfn = copypte->pgm[0].pg_pfn;
#endif

	for (p = &proc[0]; p < (struct proc *)v.ve_proc; p++)
		if (p->p_stat) {
			printf("%d: ", p-&proc[0]);
			switch (p->p_stat) {
				case SSLEEP: printf("SLEEP "); break;
				case SRUN: printf("RUN "); break;
				case SZOMB: printf("ZOMB "); break;
				case SSTOP: printf("STOP "); break;
				case SIDL: printf("IDL "); break;
				case SONPROC: printf("ONPROC "); break;
				case SXBRK: printf("XBRK "); break;
				default: printf("stat=%x ", p->p_stat); break;
			}
			i = 0;
			FLAG(p->p_flag, SSYS, "SYS");
			FLAG(p->p_flag, STRC, "TRC");
			FLAG(p->p_flag, SWTED, "WTED");
			FLAG(p->p_flag, SNWAKE, "NWAKE");
			FLAG(p->p_flag, SLOAD, "LOAD");
			FLAG(p->p_flag, SLOCK, "LOCK");
			FLAG(p->p_flag, SCOFF, "COFF");
			FLAG(p->p_flag, SSEL, "SEL");
			FLAG(p->p_flag, STIMO, "TIMO");
			printf(" pid=%d", p->p_pid);
			printf(" pri=%d sig=%x", p->p_pri&0xFF, p->p_sig);
			debugwchan(p->p_wchan);
#if defined(COMMAND) | defined(PSARGS)
			i = pptop(p->p_uptbl[1].pgm[0].pg_pfn);
			up = (struct user *) (ptob(i) - sizeof(u.u_stbl));
#ifdef	COMMAND
			for (i = 0; i < COMMSIZ; i++) {
				cbuf[i] = up->u_comm[i];
				if (cbuf[i] < ' ' || cbuf[i] >= 0x7F)
					break;
			}
			cbuf[i] = 0;
			printf(" %s", cbuf);
#endif COMMAND
#ifdef	PSARGS
			for (i = 0; i < PSARGSZ; i++) {
				cbuf[i] = up->u_psargs[i];
				if (cbuf[i] < ' ' || cbuf[i] >= 0x7F)
					break;
			}
			cbuf[i] = 0;
			printf(" %s", cbuf);
#endif PSARGS
#endif
			printf("\n");
		}
}

debugregion()
{
	register reg_t *rp, *trp;
	register int i;

	printf("ACTIVE\n");
	for (rp = ractive.r_forw; rp != &ractive; rp = rp->r_forw) {
		printf("region[%d]: ", rp - region);
		i = 0;
		switch (rp->r_type) {
		case RT_UNUSED:		printf("RT_UNUSED ");	break;
		case RT_PRIVATE:	printf("RT_PRIVATE ");	break;
		case RT_STEXT:		printf("RT_STEXT ");	break;
		case RT_SHMEM:		printf("RT_SHMEM ");	break;
		case RT_PHYSCALL:	printf("RT_PHYSCALL ");	break;
		default:		printf("?????? ");	break;
		}
		if (rp->r_noswapcnt)
			printf("noswapcnt=%d ");
		FLAG(rp->r_flags, RG_NOFREE, "NOFREE");
		FLAG(rp->r_flags, RG_DONE, "DONE");
		FLAG(rp->r_flags, RG_NOSHARE, "NOSHARE");
		FLAG(rp->r_flags, RG_WAITING, "WAITING");
		printf(" listsz=%d ", rp->r_listsz);
		printf("pgsz=%d ", rp->r_pgsz);
		printf("stack=%d ", rp->r_stack);
		printf("nvalid=%d ", rp->r_nvalid);
		printf("refcnt=%d ", rp->r_refcnt);
		printf("filesz=%d ", rp->r_filesz);
		printf("lock=%d ", rp->r_lock);
		if (rp->r_vptr) {
			printf("vnode=%x ", rp->r_vptr);
			i = VTOI(rp->r_vptr) - inode;
			if (i >= 0 && i < v.v_inode)
				printf("inode[%d] ", i);
		}
		printf("\n");
	}

	i = 0;
	for (rp = rfree.r_forw; rp != &rfree; rp = rp->r_forw) {
		if (i++ == 0)
			printf("FREE:");
		printf(" %d", rp - region);
	}
	if (i)
		printf("\n");

	i = 0;
	for (trp = region; trp < &region[v.v_region]; trp++) {
		for (rp = ractive.r_forw; rp != &ractive; rp = rp->r_forw)
			if (rp == trp)
				goto OK;
		for (rp = rfree.r_forw; rp != &rfree; rp = rp->r_forw)
			if (rp == trp)
				goto OK;
		if (i++ == 0)
			printf("MISSING:");
		printf(" %d", rp - region);
	OK: ;
	}
	if (i)
		printf("\n");
}

debugvnode()
{
	struct dev_vnode {
		struct vnode dv_vnode;
		struct dev_vnode *dv_link;
	};
	extern struct dev_vnode *dev_vnode_headp;
	register struct dev_vnode *dvp;

	for (dvp = dev_vnode_headp; dvp; dvp = dvp->dv_link) {
		prvnode(&dvp->dv_vnode, 1);
	}
}

prvnode(vp, flag)
struct vnode *vp;
{
	register int i = 0;
	register struct mount *mp;
	register struct inode *ip = VTOI(vp);

	if (flag) {
		if ((ip != (struct inode *) NULL) && (ip >= &inode[0])
			&& (ip < (struct inode *) v.ve_inode))
			printf("vnode[%d] ", ip - &inode[0]);
		printf("(0x%x):  ", vp);
	}
	else
		printf("\t");

	printf("flag=", vp->v_flag);
	FLAG(vp->v_flag, VROOT, "ROOT");
	FLAG(vp->v_flag, VTEXT, "TEXT");
	FLAG(vp->v_flag, VEXLOCK, "EXLOCK");
	FLAG(vp->v_flag, VSHLOCK, "SHLOCK");
	FLAG(vp->v_flag, VLWAIT, "LWAIT");
	printf(" count=%x", vp->v_count);
	printf(" vfsmountedhere=");
	if (vp->v_vfsmountedhere) {
		mp = (struct mount *) vp->v_vfsmountedhere->vfs_data;
		if (mp >= &mounttab[0] && mp < (struct mount *) v.ve_mount) 
			printf("mounttab[%d]", mp - &mounttab[0]);
		else
			printf("%x", vp->v_vfsmountedhere);
	}
	else
		printf("0");
	printf(" vfsp=");
	if (vp->v_vfsp) {
		mp = (struct mount *) vp->v_vfsp->vfs_data;
		if (mp >= &mounttab[0] && mp < (struct mount *) v.ve_mount) 
			printf("mounttab[%d]", mp - &mounttab[0]);
		else
			printf("%x", vp->v_vfsp);
	}
	else
		printf("0");
	if (vp->v_type == VBLK || vp->v_type == VCHR)
		printf(" rdev=%x/%x", major(vp->v_rdev), minor(vp->v_rdev));
	printf("\n");
}

#define WCHAN(st, a, lim, mes)						\
	if ( (struct st *)w >= &a[0] && (struct st *)w < &a[lim]) {	\
		i = (struct st *)w - &a[0];				\
		o = w - (unsigned int)&a[i];				\
		printf(mes, i);						\
		if (o)							\
			printf("+%x", o);				\
		return;							\
	}

debugwchan(w)
register unsigned int w;
{
	int i, o;
	extern struct buf *sbuf;

	printf(" wchan=");
	if (w == (unsigned int)&bfreelist[BQ_LOCKED]) {
		printf("&bfreelist[BQ_LOCKED]"); return;
	}
	if (w == (unsigned int)&bfreelist[BQ_LRU]) {
		printf("&bfreelist[BQ_LRU]"); return;
	}
	if (w == (unsigned int)&bfreelist[BQ_AGE]) {
		printf("&bfreelist[BQ_AGE]"); return;
	}
	if (w == (unsigned int)&bfreelist[BQ_EMPTY]) {
		printf("&bfreelist[BQ_EMPTY]"); return;
	}
	if (w == (unsigned int)&cfreelist) { printf("cfreelist"); return; }
	if (w == (unsigned int)&runin) { printf("runin"); return; }
	if (w == (unsigned int)&runout) { printf("runout"); return; }
	if (w == (unsigned int)&u) { printf("u"); return; }
	WCHAN(proc, proc, v.v_proc, "proc[%d]");
	WCHAN(buf, sbuf, v.v_buf, "sbuf[%d]");
	WCHAN(buf, pbuf, v.v_pbuf, "pbuf[%d]");
	WCHAN(inode, inode, v.v_inode, "inode[%d]");
	WCHAN(file, file, v.v_file, "file[%d]");
	printf("%x", w);
}

#define	DUMP(what,how)	printf("what = how\n", (uint) bp->what)

void
bufdump(bp, dp)
register struct buf	*bp;
register struct bufhd	*dp;
{
	register struct buf *ep;

	printf("dp (0x%x) -> ", (uint) dp);
	for (ep = dp->b_forw; ep != (struct buf *) dp && ep != (struct buf *) NULL; ep = ep->b_forw)
		printf("0x%x -> ", ep);
	if (ep == (struct buf *) NULL)
		printf("NULL\n");
	else
		printf("dp");

	printf("bp = 0x%x\n", (uint) bp);
	DUMP(b_flags,0x%x);
	DUMP(b_dev,0x%x);
	DUMP(b_blkno,%d);
	DUMP(b_bcount,%d);
	DUMP(b_un.b_addr,0x%x);
}

void
bufvalidate(s)
char *s;
{
        register int i, pri;
	register struct bufhd *dp;
	register struct buf *bp;
	extern struct buf *sbuf;

#define	ASSERT(e)	{						\
				if (!(e)) {				\
					printf("%s:  ", s);		\
					printf("ASSERT failed:  e\n");	\
					bufdump(bp, dp);		\
					splx(pri);			\
					panic(s);			\
					/*NOTREACHED*/			\
				}					\
			}

	pri	= spl6();
        for (dp = bufhash, i = 0; i < v.v_hbuf; i++, dp++)
		for (bp = dp->b_forw; bp != (struct buf *) dp; bp = bp->b_forw){
			ASSERT(bp != (struct buf *) NULL);
			ASSERT(bp >= &sbuf[0]);
			ASSERT(bp < &sbuf[v.v_buf]);
		}
	splx(pri);
#undef	ASSERT
}

db0stack(dummy)
int dummy;
{
	int pid;
	struct user *up; 
	int i;

	printf("curproc = %d, proc ? ", u.u_procp - proc);
	if (debuggdec(&pid) == -1)
		pid = u.u_procp - proc;
	printf("\nproc %d stack trace\n", pid);
	if (pid != u.u_procp - proc) {
		i = pptop(proc[pid].p_uptbl[USIZE - 1].pgm[0].pg_pfn);
		up = (struct user *) (ptob(i) - (USIZE - 1) * sizeof(u.u_stbl));
		i = up->u_rsav[10]; /* A6 ?? */
	}
	else i =  (int) (&dummy - 2);

	dodbstack(i, &proc[pid].p_uptbl[0]);
}

dodbstack(a6, pt)
uint *a6;
pte_t *pt;
{
	uint *physa6;
	int i, j;

	int puoff = ptob(pptop(pt[USIZE - 1].pgm[0].pg_pfn))
				- (USIZE - 1) * PAGESIZE;
	extern uint netstak[];

	for (;;) {
		physa6 = (uint *) (puoff + ((int) a6 - (int) &u));

		printf("FP:%x", a6);
		if ((a6 >= (uint *) &netstak[0])
			&& a6 < (uint *) &netstak[3000/sizeof(uint)])
			i = (uint *) &netstak[3000/sizeof(uint)] - &a6[2];
		else {
			if (a6 < (uint *)&u)
				break;
			i = ((uint *)((uint)&u) + ptob(v.v_usize)) - &a6[2];
		}
		if (i < 0)
			break;
		printf(" RA:%x", physa6[1]);
		if (i > 6)
			i = 6;
		if (i > 0) {
			printf(" PARS:");
			for (j = 0; j < i; j++)
				printf(" %x", physa6[j+2]);
		}
		printf("\n");
		a6 = (uint *)physa6[0];
	}
	printf("\n");
}

#define	NEED_TO_FREE_SIZE	10
struct kmem_info {
	struct freehdr *free_root;
	struct freehdr *free_hdr_list;
	struct need_to_free {
		caddr_t addr;
		unsigned int	nbytes;
	} need_to_free[NEED_TO_FREE_SIZE];
};
extern struct kmem_info kmem_info;

struct 	freehdr	{
	struct freehdr *left;			/* Left tree pointer */
	struct freehdr *right;			/* Right tree pointer */
	char *block;			/* Ptr to the data block */
	unsigned int	size;			/* Size of the data block */
};

debugkmem()
{
	debug0kmem(kmem_info.free_root, 0);
}

debug0kmem(p, depth)
struct freehdr *p;
{
	int n;

	if (depth == 0) {
		printf("p 0x%x\n", p);
	} else {
		printf("p 0x%x depth %d\n", p, depth);
	}
	if (p){
		debug0kmem(p->left, depth+1);

		for (n = 0; n < depth; n++)
			printf("   ");
		printf(
		     "(0x%x): (left = 0x%x, right = 0x%x, block = 0x%x, size = %d)\n",
			p, p->left, p->right, p->block, p->size);

		debug0kmem(p->right, depth+1);
	}
}

debugit()
{
	struct proc *p;
	int j;
	int tot;
	int sleep=0, run=0, zomb=0, stop=0, idl=0, onproc=0, xbrk=0;
	int sys=0, trc=0, wted=0, nwake=0, load=0, lock=0, coff=0, sel=0, timo=0;
	int queue=0, bad=0, hash=0, done=0, swap=0;

	tot = 0;
	for (p = &proc[0]; p < (struct proc *)v.ve_proc; p++)
		if (p->p_stat) {
			tot++;
			switch (p->p_stat) {
				case SSLEEP: sleep++; break;
				case SRUN: run++; break;
				case SZOMB: zomb++; break;
				case SSTOP: stop++; break;
				case SIDL: idl++; break;
				case SONPROC: onproc++; break;
				case SXBRK: xbrk++; break;
			}
			if (p->p_flag & SSYS) sys++;
			if (p->p_flag & STRC) trc++;
			if (p->p_flag & SWTED) wted++;
			if (p->p_flag & SNWAKE) nwake++;
			if (p->p_flag & SLOAD) load++;
			if (p->p_flag & SLOCK) lock++;
			if (p->p_flag & SCOFF) coff++;
			if (p->p_flag & SSEL) sel++;
			if (p->p_flag & STIMO) timo++;
		}
	printf("S%d ", sleep);
	printf("R%d ", run);
	printf("Z%d ", zomb);
	printf("ST%d ", stop);
	printf("I%d ", idl);
	printf("P%d ", onproc);
	printf("X%d ", xbrk);
	printf("sys=%d ", sys);
	printf("trc=%d ", trc);
	printf("wted=%d ", wted);
	printf("nwake=%d ", nwake);
	printf("load=%d ", load);
	printf("lock=%d ", lock);
	printf("coff=%d ", coff);
	printf("sel=%d ", sel);
	printf("timo=%d ", timo);
	printf("tot=%d ", tot);
	printf("\n");
	tot = 0;
	for (j = 0; j < physmem; j++) {
		if (pfdat[j].pf_flags == 0)
			continue;
		if (pfdat[j].pf_flags & P_QUEUE) queue++;
		if (pfdat[j].pf_flags & P_BAD) bad++;
		if (pfdat[j].pf_flags & P_HASH) hash++;
		if (pfdat[j].pf_flags & P_DONE) done++;
		if (pfdat[j].pf_flags & P_SWAP) swap++;
		tot++;
	}
	printf("queue=%d ", queue);
	printf("bad=%d ", bad);
	printf("hash=%d ", hash);
	printf("done=%d ", done);
	printf("swap=%d ", swap);
	printf("tot=%d ", tot);
	printf("\n");
}

extern dblk_t *dbfreelist[];
extern mblk_t *mbfreelist;

/*
 * prstream() formats an stdata entry in the stream table for crash.  It
 * expects to receive 2 arguments - the slot number of the stream entry
 * to be formatted and a flag indicating if all entries are being printed.
 * If the latter is set then an unallocated slot will be ignored; if clear,
 * then an error message will be printed.  
 */

prstream(c, all)
{
	struct stdata* strm;			/* stream entry buffer */
	long str_off;				/* offset of stream in mem */
	int wrq_slot, ioc_slot, ino_slot;	/* computed slot numbers */

	if ((c >= v.v_nstream) || (c < 0)) {
		printf("%d out of range\n",c);	/* requested stream out of range */
		return;
	}
	strm = &streams[c];
	if (strm->sd_wrq == NULL) {
		if (!all) printf("Slot %d not active\n",c);
		return;
	}
	printf("%d ",c);
	wrq_slot = strm->sd_wrq - queue;
	printf("%d ",wrq_slot);
	ioc_slot = strm->sd_iocblk - mblock;
	if ( (ioc_slot>=0)&&(ioc_slot<nmblock) )
		printf("%d ",ioc_slot);
	else printf("   - ");
	ino_slot = VTOI(strm->sd_vnode) - inode;
	printf("%d ",ino_slot);
	printf("%d %d %d %d %o ", strm->sd_pgrp, strm->sd_iocid,
		strm->sd_iocwait, strm->sd_wroff, strm->sd_error);
	printf("%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s",
		((strm->sd_flag & IOCWAIT) ? "iocw " : ""),
		((strm->sd_flag & RSLEEP) ? "rslp " : ""),
		((strm->sd_flag & WSLEEP) ? "wslp " : ""),
		((strm->sd_flag & STRHUP) ? "hup " : ""),
		((strm->sd_flag & STWOPEN) ? "stwo " : ""),
		((strm->sd_flag & CTTYFLG) ? "ctty " : ""),
		((strm->sd_flag & RMSGDIS) ? "mdis " : ""),
		((strm->sd_flag & RMSGNODIS) ? "mnds " : ""),
		((strm->sd_flag & STRERR) ? "err " : ""),
		((strm->sd_flag & STRTIME) ? "sttm " : ""),
		((strm->sd_flag & STR2TIME) ? "s2tm " : ""),
		((strm->sd_flag & STR3TIME) ? "s3tm " : ""),
		((strm->sd_flag & ESLEEP) ? "eslp " : ""),
		((strm->sd_flag & RSEL) ? "rsel " : ""),
		((strm->sd_flag & WSEL) ? "wsel " : ""),
		((strm->sd_flag & ESEL) ? "esel " : ""));
	printf("%s%s%s%s%s%s%s%s\n",
		((strm->sd_flag & TIME_OUT) ? "tmot " : ""),
		((strm->sd_flag & TIME_WAIT) ? "tmwt " : ""),
		((strm->sd_flag & STR_RCOLL) ? "rcol " : ""),
		((strm->sd_flag & STR_WCOLL) ? "wcol " : ""),
		((strm->sd_flag & STR_NBIO) ? "nbio " : ""),
		((strm->sd_flag & STR_ASYNC) ? "async " : ""),
		((strm->sd_flag & STSOPEN) ? "open " : ""),
		((strm->sd_flag & STR_CLOSING) ? "clos " : ""));
}


/* 
 * prqueue() - this function prints the queue entry corresponding to the given
 * slot number.  If the queue is not active, then an error message is printed
 * if all is 0, and nothing is printed if all is non-zero.  
 */

prqueue(c, all)
{
	queue_t *que;
	mblk_t *m;
	long que_off, moff;
	int qn, ql;

	if ((c >= v.v_nqueue) || (c<0)) {
		printf("%d out of range\n",c);	/* requested queue out of range */
		return;
	}
	que = &queue[c];
	if (!(que->q_flag & QUSE)) {
		if (!all) printf("Queue slot %d not in use\n",c);
		return;
	}
        printf("%d ",c);
	printf("%x ",que->q_qinfo);
	qn = que->q_next - queue;
	ql = que->q_link - queue;
	if ((qn >= 0) && (qn < v.v_nqueue))
		printf("%d ",qn);
	else printf("   - ");
	if ((ql >= 0) && (ql < v.v_nqueue))
		printf("%d ",ql);
	else printf("   - ");
	printf("%x ",que->q_ptr);
	printf("%d ",que->q_count);
	m = que->q_first;
	if (m != NULL) {
		moff = m - mblock;
		printf("%d ", moff);
	}
	else printf("   - ");
	m = que->q_last;
	if (m != NULL) {
		moff = m - mblock;
		printf("%d ", moff);
	}
	else printf("   - ");
	printf("%d %d %d %d ",que->q_minpsz,que->q_maxpsz,que->q_hiwat,
		que->q_lowat);
	printf("%s%s%s%s%s%s%s\n",
		((que->q_flag & QENAB) ? "enab " : ""),
		((que->q_flag & QWANTR) ? "wantr " : ""),
		((que->q_flag & QWANTW) ? "wantw " : ""),
		((que->q_flag & QFULL) ? "full " : ""),
		((que->q_flag & QREADR) ? "readr " : ""),
		((que->q_flag & QUSE) ? "use " : ""),
		((que->q_flag & QNOENB) ? "noenb " : ""));
	
}


/* 
 * prmess() prints out a message block header given a particular slot number.
 * If the slot is not currently active, then the action done depends on the 
 * value of all.  If all is 0, then an error message is printed.  If all is
 * 1, then nothing is printed.  If all is 2 (or greater), then the contents
 * of the slot are printed anyway.  This function returns the slot number
 * of the next message block (obtained from the b_next field), mainly for
 * use by prmfree(), if the slot is valid.
 */

prmess(c, all)
{
	mblk_t *mblk;
	long  moff;
	int ndblock, mnext, mcont, datab, mprev;


	ndblock = v.v_nblk4096 + v.v_nblk2048 + v.v_nblk1024 + v.v_nblk512 +
	    v.v_nblk256 + v.v_nblk128 + v.v_nblk64 + v.v_nblk16 +
	    v.v_nblk4;
	if ((c >= nmblock) || (c < 0)) {
		printf("%d out of range\n",c);	/* requested message out of range */
		return(-1);
	}
	mblk = &mblock[c];
	if ((mblk->b_datap == NULL) && (all < 2)) {
	/* printf not reached in 2.0p implementation */
		if (!all) printf("Message slot %d not in use\n",c);
		return(-1);
	}
        printf("%d ",c);
	mnext = mblk->b_next - mblock;
	mcont = mblk->b_cont - mblock;
	mprev = mblk->b_prev - mblock;
	datab = mblk->b_datap - dblock;
	if ((mnext >= 0) && (mnext < nmblock))
		printf("%d ",mnext);
	else printf("   - ");
	if ((mcont >= 0) && (mcont < nmblock))
		printf("%d ",mcont);
	else printf("   - ");
	if ((mprev >= 0) && (mprev < nmblock))
		printf("%d ",mprev);
	else printf("   - ");
	printf("%x %x ",mblk->b_rptr, mblk->b_wptr);
	if ((datab >= 0) && (datab < ndblock))
		printf("%d\n",datab);
	else printf("   -\n");
	return(mnext);
	
}

/*
 * prmfree() prints out the contents of the free message block list.  This
 * list is headed in memory by the the variable mbfreelist.  Since the
 * message slot should not be allocated, prmess() is called with all=2.
 */

prmfree()
{
	mblk_t *m;
	long  moff;
	int  mnext;


	m = mbfreelist;
	mnext = m - mblock;
	while ((mnext >=0) && (mnext < nmblock)) mnext = prmess(mnext,2);
}

/*
 * prdblk() prints out the contents of the given data block header slot.  If the
 * slot is not active, then the action taken depends on the value of all.  If all
 * is zero, then an error message is printed.  If all is 1, then nothing is done.
 * If all is 2, then the slot is printed anyway.  If the slot is valid, then 
 * the slot pointed to by the db_freep field is returned by the function (mainly
 * for use by prdfree().  
 */

prdblk(c, all)
{
	dblk_t *dblk;
	long  doff;
	int ndblock, dfree;
	static int lastcls;


	ndblock = v.v_nblk4096 + v.v_nblk2048 + v.v_nblk1024 + v.v_nblk512 +
	    v.v_nblk256 + v.v_nblk128 + v.v_nblk64 + v.v_nblk16 +
	    v.v_nblk4;
	if ((c >= ndblock) || (c < 0)) {
		printf("%d out of range\n",c);	/* requested data block out of range */
		return(-1);
	}
	dblk = &dblock[c];
	if ((dblk->db_ref == 0) && (all < 2)) {
	/* printf not reached in 2.0p implementation */
		if (!all) printf("Data block slot %d not in use\n", c);
		return(-1);
	}
	if (dblk->db_class != lastcls) {
		printf("\n");
		lastcls=dblk->db_class;
	}
	printf("%d %d ",c, dblk->db_class);
	switch (dblk->db_class) {
		case 0: printf("   4 "); break;
		case 1: printf("  16 "); break;
		case 2: printf("  64 "); break;
		case 3: printf(" 128 "); break;
		case 4: printf(" 256 "); break;
		case 5: printf(" 512 "); break;
		case 6: printf("1024 "); break;
		case 7: printf("2048 "); break;
		case 8: printf("4096 "); break;
		default: printf("   - ");
	}
	printf("%d ", dblk->db_ref); 
	switch (dblk->db_type) {
		case M_DATA: printf("data     "); break;
		case M_PROTO: printf("proto    "); break;
		case M_SPROTO: printf("sproto   "); break;
		case M_BREAK: printf("break    "); break;
		case M_SIG: printf("sig      "); break;
		case M_DELAY: printf("delay    "); break;
		case M_CTL: printf("ctl      "); break;
		case M_IOCTL: printf("ioctl    "); break;
		case M_SETOPTS: printf("setopts  "); break;
		case M_ADMIN: printf("admin    "); break;
		case M_EXPROTO: printf("exproto  "); break;
		case M_EXDATA: printf("exdata   "); break;
		case M_EXSPROTO: printf("exspro   "); break;
		case M_EXSIG: printf("exsig    "); break;
		case M_IOCACK: printf("iocack   "); break;
		case M_IOCNAK: printf("iocnak   "); break;
		case M_PCSIG: printf("pcsig    "); break;
		case M_FLUSH: printf("flush    "); break;
		case M_STOP: printf("stop     "); break;
		case M_START: printf("start    "); break;
		case M_HANGUP: printf("hangup   "); break;
		case M_ERROR: printf("error    "); break;
		default: printf("       - ");
	}
	printf("%x %x ", dblk->db_base, dblk->db_lim);
	dfree = dblk->db_freep - dblock;
	if ((dfree >= 0) && (dfree < ndblock))
		printf("%d\n",dfree);
	else printf("   -\n");
	return(dfree);
	
}


/*
 * prdball() prints out all data block header entries for the given class.
 * The hacky looking code is done mainly to get the location of the first
 * block of the class and the number of blocks to print.  Since some of these
 * blocks may not be allocated (or even lost), prdblk() is called with all=2.
 */

prdball(c)
{
	int i,n, *p;
	int clasize[NCLASS];

	if ((c<0)||(c>NCLASS-1)) return;
	p = &(v.v_nblk4096);
	for (i=NCLASS-1; i>=0; i--) clasize[i] = *p++;  /* load size of each block class */
	n=0;
	for (i=NCLASS-1; i>c; i--) n+=clasize[i];	/* compute slot of first block */
	for (i=0; i<clasize[c]; i++) prdblk(n++,2);
}


/*
 * prdfree() prints out the free list for the given data block class.  
 * The list is headed by the c_th entry of the dbfreelist[] array.  Since
 * these blocks should not be allocated, prdblk() is called with all=2.
 */

prdfree(c)
{
	dblk_t *d;
	long  doff;
	int ndblock, dnext;


	ndblock = v.v_nblk4096 + v.v_nblk2048 + v.v_nblk1024 + v.v_nblk512 +
	    v.v_nblk256 + v.v_nblk128 + v.v_nblk64 + v.v_nblk16 +
	    v.v_nblk4;
	if ((c<0) || (c>NCLASS-1)) return;
	d = dbfreelist[c];
	dnext = d - dblock;
	while ((dnext >=0) && (dnext < ndblock)) dnext = prdblk(dnext,2);
}

/* 
 * prqrun() prints out all of the queue slots which have been placed on the
 * queue run list (by a qenable() operation).  Presumably all of these
 * queues are active.  Only the slot numbers are printed; the queue command
 * can then be used to print the individual queue slots.
 */

prqrun()
{
	queue_t *q;
	int  ql;

	q = qhead;
	printf("Queue slots scheduled for service: ");
	while (q != NULL) {
		ql = q - queue;
		printf("%d ",ql);
		q = q->q_link;
	}
	printf("\n");
}

/*
 * prstrstat() - print statistics about streams data structures
 */

prstrstat()
{
	long offset;
	queue_t *q;
	struct stdata *str;
	mblk_t *m;
	dblk_t *d;
	int qusecnt, susecnt, mfreecnt, musecnt,
	    dfreecnt, dusecnt, dfc[NCLASS], duc[NCLASS], dcc[NCLASS], qruncnt;
	int ndblock, i,j, *p;

	ndblock = v.v_nblk4096 + v.v_nblk2048 + v.v_nblk1024 + v.v_nblk512 +
	    v.v_nblk256 + v.v_nblk128 + v.v_nblk64 + v.v_nblk16 +
	    v.v_nblk4;
	qusecnt = susecnt = mfreecnt = 0;
	musecnt = dfreecnt = dusecnt = qruncnt = 0;
	for (i=0; i<NCLASS; i++) dfc[i] = duc[i] =0;
	p = &(v.v_nblk4096);
	for (i=NCLASS-1; i>=0; i--) dcc[i] = *p++;

	printf("ITEM             CONFIGURED   ALLOCATED     FREE\n");

	q = qhead;
	while (q != NULL) {
		qruncnt++;
		q = q->q_link;
	}
	
	q = queue;
	for (i=0; i<v.v_nqueue; i++) {
		if (q->q_flag & QUSE) qusecnt++;
		q++;
	}

	str = streams;
	for (i=0; i<v.v_nstream; i++) {
		if (str->sd_wrq != NULL) susecnt++;
		str++;
	}

	m = mbfreelist;
	while (m != NULL) {
		mfreecnt++;
		m = m->b_next;
	}

	m = mblock;
	for (i=0; i<nmblock; i++){
		if (m->b_datap != NULL) musecnt++;
		m++;
	}

	for (j=0; j<NCLASS; j++){
		d = dbfreelist[j];
		while (d != NULL) {
			dfc[j]++;
			dfreecnt++;
			d = d->db_freep;
		}
	}

	d = dblock;
	for (i=0; i<ndblock; i++) {
		if (d->db_ref) {
			dusecnt++;
			duc[d->db_class]++;
		}
		d++;
	}

	printf("streams             %d         %d       %d\n",
		v.v_nstream, susecnt, v.v_nstream - susecnt);
	printf("queues              %d         %d       %d\n",
		v.v_nqueue, qusecnt, v.v_nqueue - qusecnt);
	printf("message blocks      %d         %d       %d\n",
		nmblock, musecnt, mfreecnt);
	printf("data block totals   %d         %d       %d\n",
		ndblock, dusecnt, dfreecnt);
	for (i=0; i<NCLASS; i++) 
		printf("data block class %d  %d         %d       %d\n",
			i, dcc[i], duc[i], dfc[i]);
	printf("\nCount of scheduled queues:%d\n", qruncnt);


}

/*
 *	Get a character, strip off parity, and ignore ^S/^Q
 */
debuggetchar()
{
	register int c;

	do {
		c = getchar() & 0x7F;
	} while (c == '\021' || c == '\023');
	return (c);
}

/*
 *	Debug trace routine (called like printf)
 */

/* VARARGS */
debugtrace(a, b, c, d, e)
char *a;
long b,c,d,e;
{
	register long *up;

	up = (long *)(((char *)&u)+(NBPP*USIZE));
	if (((long *)&a) < up)
		trace_a[trace_end] = a; else 
		trace_a[trace_end] = "INVALID\n";
	if (&b < up)
		trace_b[trace_end] = b;
	if (&c < up)
		trace_c[trace_end] = c;
	if (&d < up)
		trace_d[trace_end] = d;
	if (&e < up)
		trace_e[trace_end] = e;
	if (trace_print)
		printf(trace_a[trace_end],
		       trace_b[trace_end],
		       trace_c[trace_end],
		       trace_d[trace_end],
		       trace_e[trace_end]);
	trace_end++;
	if (trace_end >= TRBUFSIZE)
		trace_end = 0;
	if (trace_end == trace_start) {
		trace_start++;
		if (trace_start >= TRBUFSIZE)
			trace_start = 0;
	}
	if (trace_quit && ((trace_end+1)&TRBUFMASK) == trace_start)
		debug(0);
}

debugcore()
{
	register struct pfdat_desc *pfdd = pfdat_desc;
	register pfd_t *pfd;
	register u_int pgcnt;
	int qcnt, badcnt, hashcnt, donecnt, swapcnt;
	extern int freemem;
	extern int maxmem;
	extern int availrmem, availsmem;
	int freepagecnt;
	static int phashcnt[10];
	int i;

	qcnt = badcnt = hashcnt = donecnt = swapcnt = 0;
	printf("pfdat debug:\n");
	for (; pfdd; pfdd = pfdd->pfd_next) {

		pfd = (pfd_t * )(((int) pfdd->pfd_head + 1) & ~1);
		pgcnt = pfdd->pfd_pfcnt;

		for ( ; pgcnt; pgcnt--, pfd++) {
			if (pfd->pf_flags & P_QUEUE)
				qcnt++;
			if (pfd->pf_flags & P_BAD)
				badcnt++;
			if (pfd->pf_flags & P_HASH)
				hashcnt++;
			if (pfd->pf_flags & P_DONE)
				donecnt++;
			if (pfd->pf_flags & P_SWAP)
				swapcnt++;
		}
	}
	printf("Total stats: 0x%x queue, 0x%x bad, 0x%x hash, 0x%x done, 0x%x swap\n",
		qcnt, badcnt, hashcnt, donecnt, swapcnt);
	qcnt = badcnt = hashcnt = donecnt = swapcnt = 0;
	for (pfd = phead.pf_next; pfd != &phead; pfd = pfd->pf_next) {
		freepagecnt++;
		if (pfd->pf_flags & P_QUEUE)
			qcnt++;
		if (pfd->pf_flags & P_BAD)
			badcnt++;
		if (pfd->pf_flags & P_HASH)
			hashcnt++;
		if (pfd->pf_flags & P_DONE)
			donecnt++;
		if (pfd->pf_flags & P_SWAP)
			swapcnt++;
	}
	printf("Free page : 0x%x queue, 0x%x bad, 0x%x hash, 0x%x done, 0x%x swap\n",
		qcnt, badcnt, hashcnt, donecnt, swapcnt);
	printf("\tmaxmem 0x%x, freemem 0x%x, availrmem 0x%x, availsmem 0x%x\n",
			maxmem, freemem, availrmem, availsmem);

	for (i = 0; i < sizeof(phashcnt)/sizeof(phashcnt[0]); i++)
		phashcnt[i] = 0;
	for (i = 0; i <= phashmask; i++) {
		hashcnt = 0;
		for (pfd = phash[i]; pfd; pfd = pfd->pf_hchain)
			hashcnt++;
		if (hashcnt >= sizeof(phashcnt)/sizeof(phashcnt[0]))
			phashcnt[sizeof(phashcnt)/sizeof(phashcnt[0]) - 1]++;
		else phashcnt[hashcnt]++;
	}
	printf("page hash collision info:\n");
	for (i = 0; i < sizeof(phashcnt)/sizeof(phashcnt[0]); i++) {
		if (phashcnt[i])
			printf("	0x%x phash buckets have 0x%x entries\n",
					phashcnt[i], i);
	}
}

#include	"sys/swap.h"
extern swpt_t	swaptab[];
extern int	nextswap;
extern int	availsmem;
debugswap()
{
	register int i;
	swpt_t	*swptr;

	printf("nextswap = 0x%x, availsmem = 0x%x\n",nextswap,availsmem);
	swptr = swaptab;
	for (i = 0; i < MSFILES; i++, swptr++) {
		if (swptr->st_npgs) {
			printf("swaptab[0x%x]: dev 0x%x flags 0x%x ucnt 0x%x next 0x%x\n"
					, i, swptr->st_dev, swptr->st_flags,
					swptr->st_ucnt, swptr->st_next);
			printf("\tswaplo 0x%x npgs 0x%x nfpgs 0x%x\n",
					swptr->st_swplo,swptr->st_npgs,swptr->st_nfpgs);
		}
	}

}

debugtune()
{
	printf("t_gpgslo 0x%x : ", tune.t_gpgslo);
	printf("If freemem < t_getpgslow, then start to steal pages from processes.\n");
	printf("t_gpgshi 0x%x : ", tune.t_gpgshi);
	printf("Once we start to steal pages, don't stop until freemem > t_getpgshi.\n");
	printf("t_gpgsmsk 0x%x : ", tune.t_gpgsmsk);
	printf("Is page stealable? 0 = any page, 0x%x = PG_REF, 0x%x = PG_NDREF\n",
			PG_REF, PG_NDREF);
	printf("t_vhandr 0x%x : ", tune.t_vhandr);
	printf("Run vhand once every t_vhandr seconds if freemem < t_vhandl.\n");
	printf("t_vhandl 0x%x\n", tune.t_vhandl);
	printf("t_maxsc 0x%x : ", tune.t_maxsc);
	printf("Max number of pages to swapped out in a single\n");
	printf("	operation.  Cannot be larger than MAXSPGLST 0x%x\n", MAXSPGLST);
	printf("t_maxfc 0x%x : ", tune.t_maxfc);
	printf("Max number of pages which will be saved up and freed\n");
	printf("	at once. Cannot be larger than MAXFPGLST 0x%x", MAXFPGLST);
	printf("t_maxumem 0x%x : ", tune.t_maxumem);
	printf("Max size of a user's virtual address space in pages.\n");

	printf("t_minarmem 0x%x : ", tune.t_minarmem);
	printf("Min available resident (not swapable) memory.  In pages.\n");
	printf("t_minasmem 0x%x : ", tune.t_minasmem);
	printf("Min available swapable memory to maintain.  In pages.\n");
}

#endif	DEBUG
