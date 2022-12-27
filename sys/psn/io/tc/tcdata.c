/*
 * @(#)tcdata.c  {Apple version 1.4 90/01/19 15:11:27}
 *
 * Copyright (c) 1987, 1988, 1989 by Apple Computer, Inc.
 * All Rights Reserved.
 *
 * THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF APPLE COMPUTER, INC.
 * The copyright notice above does not evidence any actual or
 * intended publication of such source code.
 */

#if !defined(lint) && !defined(NO_SCCS_IDS)
static char _sccsid[]="@(#)tcdata.c  {Apple version 1.4 90/01/19 15:11:27}";
#endif

/*
 *	tc -- Apple Tape Backup 40SC cartridge driver
 *
 *	Copyright Apple Computer Inc. 1987
 *
 */
#include "tc.h"
#include <sys/mtio.h>
#include <sys/param.h>

/* Various debugging flags and such: */
struct softc *tcsc;
int tc_d_verbose = 0;			/* be verbose about it! */
int tc_d_scsi = 0;			/* show tc_scsi calls */
int tc_d_strat = 0;			/* show tc_strategy calls */

/* Configuration flags: */

int tc_c_errs = 0;			/* show errors after I/O done */
int tc_c_retry = 0;			/* show retries */
int tc_c_noeofok = 0;			/* allow TC_NOEOF device */

int tc_c_intlv = 0;			/* interleave: 0 (default=2) - 7 */

int tc_c_maxtries = 5;			/* max retries */

/* Timeout values:
 *
 * 3M spec says rew = offl = 27 seconds
 * 3M spec says rw average 9 seconds, but we allow for seek time.
 *
 * HOWEVER, we've added a bunch to rw time because the 3M spec
 *  doesn't seem to include seek time in that figure...sigh...
 */
int tc_t_misc	= 5;			/* short ops */
int tc_t_rw	= 40;			/* read/write */
int tc_t_offl	= 35;			/* unload */
int tc_t_fmt	= 40*60;		/* format is 40 MINUTES */
int tc_t_ret	= 300;			/* retension time */

/* The following times are all specified in "ticks" for tc_timer. */

int tc_t_timer = (HZ / 10);		/* ticks per tc_timer() run */

int tc_t_again		= (18 * T_100MS); /* retry delay after SST_AGAIN */
int tc_t_timeout	=  T_100MS;	 /* retry delay after SST_TIMEOUT */
int tc_t_busy		= (4 * T_100MS); /* retry delay while device is busy */
int tc_t_loadpoll	= (1 * SECONDS); /* open: poll delay during autoload */
int tc_t_autoload	= (180 * SECONDS); /* open: total autoload timeout */

/* MODEL-SPECIFIC TABLES: */

int tc_readcap(), tc_iosetup();
int tc_srec(), tc_sfile(), tc_isfilemark(), tc_weof(), tc_iocget();

int ar_readcap(), ar_iosetup();
int ar_srec(), ar_sfile(), ar_isfilemark(), ar_weof(), ar_iseom();

static int falsefunc()
{
    return(0);
}

struct modelinfo tc_modelinfo[] = {
    {	tc_readcap,
	tc_iosetup,
	tc_srec,
	tc_sfile,
	tc_isfilemark,
	tc_weof,
	falsefunc,
	tc_iocget,
	"3M", "MCD-40/SCSI",
	35,
	MT_ISTC40,
	8192,				/* fixed at 8192 bytes */
	8192,				/* fixed at 8192 bytes */
	(M_FORCESIZE | M_WAITLOAD | M_DOFORMAT),
	28,				/* sense length */
	2,
    },
    {	ar_readcap,
	ar_iosetup,
	ar_srec,
	ar_sfile,
	ar_isfilemark,
	ar_weof,
	ar_iseom,
	falsefunc,
	"ARCHIVE", "VIPER",
	100,
	MT_ISAR,
	8192,				/* do in 8192 multiples for speed */
	512,				/* physical 512-byte blocks */
	(M_FMERROR | M_EOMERROR | M_SINGLEEOF),
	14,				/* sense length */
	3,
    },
};

short tc_model_max = 1;

/* Per-drive tables: */

struct softc	tc_softc[NTC];

/* Miscellaneous: */

short tc_revnum;			/* revision m.nn as (100*m) + nn */
