/*
 * Var structure print routines
 *  @(#)var.c	2.1 89/10/13
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
#include "sys/psl.h"
#include "sys/debug.h"
#include "sys/ttychars.h"
#include "sys/var.h"
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

/*
 * Formatted dump of the system var structure
 */
var()
{
	dbgprintf("NBUF    = %6.6d", v.v_buf);
	dbgprintf(" NCALL   = %6.6d", v.v_call);
	dbgprintf(" NINODE  = %6.6d", v.v_inode);
	dbgprintf(" NNFILE  = %6.6d\n", v.v_file);
	dbgprintf("NMOUNT  = %6.6d", v.v_mount);
	dbgprintf(" NPROC   = %6.6d", v.v_proc);
	dbgprintf(" NTEXT   = %6.6d", v.v_text);
	dbgprintf(" NPROC   = %6.6d\n", v.v_proc);
	dbgprintf("NCLIST  = %6.6d", v.v_clist);
	dbgprintf(" NSABUF  = %6.6d", v.v_sabuf);
	dbgprintf(" MAXUP   = %6.6d", v.v_maxup);
	dbgprintf(" CMAPSIZ = %6.6d\n", v.v_cmap);
	dbgprintf("SMAPSIZ = %6.6d", v.v_smap);
	dbgprintf(" NHBUF   = %6.6d", v.v_hbuf);
	dbgprintf(" NHBUF-1 = %6.6d", v.v_hmask);
	dbgprintf(" FLOCK   = %6.6d\n", v.v_flock);
	dbgprintf("NPHYS   = %6.6d", v.v_phys);
	dbgprintf(" NCLSIZE = %6.6d", v.v_clsize);
	dbgprintf(" TXTRND  = %6.6x", v.v_txtrnd);
	dbgprintf(" DEV_BSZ = %6.6d\n", v.v_bsize);
	dbgprintf("CXMAPSZ = %6.6d", v.v_cxmap);
	dbgprintf(" CLKTICK = %6.6d", v.v_clktick);
	dbgprintf(" HZ      = %6.6d", v.v_hz);
	dbgprintf(" USIZE   = %6.6d\n", v.v_usize);
	dbgprintf("PAGSHFT = %6.6d", v.v_pageshift);
	dbgprintf(" PGMASK  =%8.8x", v.v_pagemask);
	dbgprintf(" L2SHFT = %6.6d", v.v_l2shift);
	dbgprintf(" L2MASK  =%8.8x\n", v.v_l2mask);
	dbgprintf("L1SHFT = %6.6d", v.v_l1shift);
	dbgprintf(" L1MASK  =%8.8x\n", v.v_l1mask);
	dbgprintf("USTART  = %6.6d", v.v_ustart);
	dbgprintf(" UEND    =%8.8x", v.v_uend);
	dbgprintf(" STAKGAP= %6.6d", v.v_stkgap);
	dbgprintf(" CPUTYPE = %6.6d\n", v.v_cputype);
	dbgprintf("CPUVER  = %6.6d", v.v_cpuver);
	dbgprintf(" MMUTYPE = %6.6d", v.v_mmutype);
	dbgprintf(" DOFFSET = %6.6d", v.v_doffset);
	dbgprintf(" KVOFFST = %6.6d\n", v.v_kvoffset);
	dbgprintf("NSVTEXT = %6.6d", v.v_svtext);
	dbgprintf(" NPBUF   = %6.6d", v.v_pbuf);
	dbgprintf(" NSCATLD = %6.6d", v.v_nscatload);
	dbgprintf(" NREGION = %6.6d\n", v.v_region);
	dbgprintf("SPTMPSZ = %6.6d", v.v_sptmap);
	dbgprintf(" VHDNFRC = %6.6d", v.v_vhndfrac);
	dbgprintf(" MAXPMEM = %6.6d", v.v_maxpmem);
	dbgprintf(" NMBUFS  = %6.6d\n", v.v_nmbufs);
	dbgprintf("NPTY    = %6.6d", v.v_npty);
	dbgprintf(" MAXCORE = %6.6d", v.v_maxcore);
	dbgprintf(" MAXHDR  = %6.6d", v.v_maxheader);
	dbgprintf(" NSTREAM = %6.6d\n", v.v_nstream);
	dbgprintf("NQUEUE  = %6.6d", v.v_nqueue);
	dbgprintf(" NB4096  = %6.6d", v.v_nblk4096);
	dbgprintf(" NB2048  = %6.6d", v.v_nblk2048);
	dbgprintf(" NB1024  = %6.6d\n", v.v_nblk1024);
	dbgprintf("NB512   = %6.6d", v.v_nblk512);
	dbgprintf(" NB256   = %6.6d", v.v_nblk256);
	dbgprintf(" NB128   = %6.6d", v.v_nblk128);
	dbgprintf(" NB64    = %6.6d\n", v.v_nblk64);
	dbgprintf("NB16    = %6.6d", v.v_nblk16);
	dbgprintf(" NB4     = %6.6d", v.v_nblk4);
	dbgprintf(" SLICE   = %6.6d", v.v_slice);
	dbgprintf(" SBUFSZ  = %6.6d\n", v.v_sbufsz);
	dbgprintf("KERNINFO = %6.6d\n", v.v_kernel_info);
}
