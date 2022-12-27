/*
 * MMU Structure print routines
 *  @(#)mmuprt.c	2.4 90/03/02
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
extern struct user *altu;

#define DT_ADDR_MASK 0xfffffe00
#define PT_ADDR_MASK 0xffffff00

char *DP_Dtype();
char DP_Ktbl;
char *DP_dbtype[] = {
	"DBD_NONE", "DBD_SWAP", "DBD_FILE", "DBD_LFILE", "DBD_DZERO", "DBD_DFILL",
	"DBD_TYPE6", "DBD_TYPE7"
};

/*
 * Print a page table to a specified level
 */
DP_L1prt(dt, level)
register Dt_t *dt;
register int level;
{       register int i;
	register Dt_t *L1p;
	register unsigned int addr;
	int B24 = 0;		/* 1 if 24-bit root */

	if (level-- == 0)
		return;

	addr = 0;
	if (dt == K_L1tbl)
		DP_Ktbl = 1;
	else
		DP_Ktbl = 0;
	L1p = L1_ent(dt, 0);
	/* Determine (heuristically) if we're looking at a 24-bit table */
	if (L1p->Dtm.Dt_xxx == L1p[1].Dtm.Dt_xxx)
	{	B24++;
		dbgprintf("Assuming 24-bit page table\n");
	}
	for (i=0; i<NL1TBL; i++, L1p++)
	{       if (checkabort())
			break;

		if (L1p->Dtm.Dt_dt == DT4B || L1p->Dtm.Dt_dt == DTPD)
		{	dbgprintf("%s %d: ", DP_Dtype(L1p, TRUE), i);
			DP_dtstat(L1p);
			dbgprintf(" %x\n", L1p->Dti&DT_ADDR_MASK);
			if (L1p->Dtm.Dt_dt != 1)
				if (DP_L2prt(dt, level, addr))
					break;
		}
		if (B24)	/* Once around this loop */
			break;
		addr += L1tob(1);
	 }
}

DP_L2prt(dt, level, addr, B24)
register Dt_t *dt;
register int level;
int B24;			/* 1 if 24-bit root */
{       register int i;
	register Dt_t *L2p;
	register int limit;
	register int aborted;

	if (level-- == 0)
		return;

	limit = B24 ? NL2TBL/2 : NL2TBL;
	L2p = L2_ent(dt, addr);
	for (i=0, aborted=0; i<limit; i++, L2p++)
	{       if (checkabort())
		{	aborted++;
			break;
		}

		if (L2p->Dtm.Dt_dt == DT4B || L2p->Dtm.Dt_dt == DTPD)
		{	dbgprintf("  %s %d: ", DP_Dtype(L2p, TRUE), i);
			DP_dtstat(L2p);
			dbgprintf(" %x\n", L2p->Dti&PT_ADDR_MASK);
			if (L2p->Dtm.Dt_dt != 1)
				if (aborted = DP_Ptprt(dt, level, addr))
					break;
		}
		addr += L2tob(1);
	}
	return(aborted);
}

DP_Ptprt(dt, level, addr)
register Dt_t *dt;
register int level;
{       register int i, flag;
	register pte_t *Ptp;
	register dbd_t *dp;
	register int aborted;

	if (level == 0)
		return;

	Ptp = Pt_ent(dt, addr);
	flag = 0;
	for (i=0, aborted=0; i<NPGPT; i++, Ptp++)
	{       if (checkabort())
		{	aborted++;
			break;
		}

		dp = dbdget(Ptp);
		if (Ptp->pgm.pg_v == DTPD || (!DP_Ktbl && *(int *)dp))
		{	dbgprintf("    PTE %d: ", i);
			if (!DP_Ktbl)
			{	DP_dbstat(dp);
				dbgprintf("%s ", DP_dbtype[dp->dbd_type]);
				dbgprintf("%d (%d) ", dp->dbd_blkno,
					  dp->dbd_swpi);
			}
			if (Ptp->pgm.pg_v == DTPD)
			{	DP_ptstat(Ptp);
				dbgprintf("%x", Ptp->pgi.pg_pte&PG_ADDR);
			}
			dbgprintf("\n");
		}
	}
	return(aborted);
}

char *
DP_Dtype(Lxp, is_dt)
register Dt_t *Lxp;
{
	switch(Lxp->Dtm.Dt_dt)
	{	case 0:	/* Shouldn't happen */
			return("");
		case 1:
			if (is_dt) return("ET"); else return("PTE");
		case 2:
			return("Short DTE");
		case 3:
			return("Long DTE");
	}
}

/*
 * Print the status flags in an ste
 *  (Access level stuff not used; works for 020, 030)
 */
DP_dtstat(Lxp)
register Dt_t *Lxp;
{
	if (Lxp->Dtm.Dt_U) dbgprintf("USED ");
	if (Lxp->Dtm.Dt_W) dbgprintf("PROT ");
}

DP_ptstat(Ptp)
register pte_t *Ptp;
{
	if (Ptp->pgm.pg_cw) dbgprintf("CWRT ");
	if (Ptp->pgm.pg_gs) dbgprintf("GSHR ");
	if (Ptp->pgm.pg_cm) dbgprintf("CM(%x) ", Ptp->pgm.pg_cm);
	if (Ptp->pgm.pg_mod) dbgprintf("MOD ");
	if (Ptp->pgm.pg_ref) dbgprintf("REF ");
	if (Ptp->pgm.pg_prot) dbgprintf("PROT ");
}

DP_dbstat(dp)
register dbd_t *dp;
{
	if (dp->dbd_pg_lock) dbgprintf("LOCKED ");
	if (dp->dbd_pg_ndref) dbgprintf("NDREF ");
}
