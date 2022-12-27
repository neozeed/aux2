/*
 * @(#)params.c  {Apple version 1.2 89/12/12 14:03:44}
 *
 * Copyright (c) 1987, 1988, 1989 by Apple Computer, Inc.
 * All Rights Reserved.
 *
 * THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF APPLE COMPUTER, INC.
 * The copyright notice above does not evidence any actual or
 * intended publication of such source code.
 */

#if !defined(lint) && !defined(NO_SCCS_IDS)
static char _sccsid[]="@(#)params.c  {Apple version 1.2 89/12/12 14:03:44}";
#endif

#include "fd.h"

static
int nullfunc()
{
    return(0);
}

static
int true()
{
    return(1);
}
static
int false()
{
    return(0);
}


    int fd_iop_beginopen();
    int fd_iop_eject();
    int fd_iop_enable();
    int fd_iop_format();
    int fd_iop_getformat();
    int fd_iop_init();
    int fd_iop_rw();
    int fd_iop_status();
    int fd_iop_seldrive();
    int fd_iop_setdensity();

    int fd_mb_beginopen();
    int fd_mb_disable();
    int fd_mb_eject();
    int fd_mb_enable();
    int fd_mb_format();
    int fd_mb_getformat();
    int fd_mb_init();
    int fd_mb_loadfloppy();
    int fd_mb_pollinsert();
    int fd_mb_rw();
    int fd_mb_seldrive();
    int fd_mb_setdensity();
    int fd_mb_status();

struct interface fd_interface[2] = {

    {					/* IOP */
	nullfunc,			/* no beginopen stuff to do */
	nullfunc,			/* no disable stuff to do */
	fd_iop_eject,
	fd_iop_enable,
	fd_iop_format,
	fd_iop_getformat,
	fd_iop_init,
	nullfunc,			/* no loadfloppy stuff to do */
	nullfunc,			/* no pollinsert needed */
	fd_iop_rw,
	fd_iop_seldrive,
	fd_iop_setdensity,
	fd_iop_status,
    },

    {					/* MOTHERBOARD */
	fd_mb_beginopen,
	fd_mb_disable,
	fd_mb_eject,
	fd_mb_enable,
	fd_mb_format,
	fd_mb_getformat,
	fd_mb_init,
	fd_mb_loadfloppy,
	fd_mb_pollinsert,
	fd_mb_rw,
	fd_mb_seldrive,
	fd_mb_setdensity,
	fd_mb_status,
    },
};

    int fd_gcr_motoron();
    int fd_mfm_motoron();
    int fd_setgcrmode();
    int fd_setmfmmode();
    int ism_setparams();

    int gcr_erasetrack();
    int gcr_fmttrack();
    int gcr_optseek();
    int gcr_rdadr();
    int gcr_read();
    int gcr_sectortime();
    int gcr_write();

    int gcr400_geometry();

    int gcr800_geometry();

    int mfm_erasetrack();
    int mfm_fmttrack();
    int mfm_optseek();
    int mfm_rdadr();
    int mfm_sectortime();
    int mfm_read();
    int mfm_write();

    int mfm720_geometry();
    int mfm720_rdadr();

    int mfm1440_geometry();
    int mfm1440_rdadr();

struct driveparams fd_d_params[MAXDTYPES] = {

    {
    	gcr_erasetrack,
	gcr_fmttrack,
	gcr400_geometry,
	fd_gcr_motoron,
	gcr_optseek,
	gcr_rdadr,
	gcr_read,
	gcr_sectortime,
	fd_setgcrmode,
	nullfunc,			/* no params to set */
	gcr_write,
	false,				/* can't write 1:1 */
    },

    {
	gcr_erasetrack,
	gcr_fmttrack,
	gcr800_geometry,
	fd_gcr_motoron,
	gcr_optseek,
	gcr_rdadr,
	gcr_read,
	gcr_sectortime,
	fd_setgcrmode,
	nullfunc,			/* no params to set */
	gcr_write,
	false,				/* can't write 1:1 */
    },

    {
	mfm_erasetrack,
	mfm_fmttrack,
	mfm720_geometry,
	fd_mfm_motoron,
	mfm_optseek,
	mfm_rdadr,
	mfm_read,
	mfm_sectortime,
	fd_setmfmmode,
	ism_setparams,
	mfm_write,
	true,				/* we can write 1:1 */
    },

    {
	mfm_erasetrack,
	mfm_fmttrack,
	mfm1440_geometry,
	fd_mfm_motoron,
	mfm_optseek,
	mfm_rdadr,
	mfm_read,
	mfm_sectortime,
	fd_setmfmmode,
	ism_setparams,
	mfm_write,
	true,				/* we can write 1:1 */
    },
};


    int woz_command();
    int woz_disable();
    int woz_enable();
    int woz_init();
    int woz_seltrack();
    int woz_setmfm();
    int woz_status();

    int ism_command();
    int ism_disable();
    int ism_enable();
    int ism_init();
    int ism_seltrack();
    int ism_setgcr();
    int ism_status();

struct chipparams fd_c_params[2] = {
    {
	woz_command,
	woz_disable,
	woz_enable,
	woz_init,
	woz_seltrack,
	nullfunc,			/* GCR? already there. */
	woz_setmfm,
	woz_status
    },

    {
	ism_command,
	ism_disable,
	ism_enable,
	ism_init,
	ism_seltrack,
	ism_setgcr,
	nullfunc,			/* MFM? already there. */
	ism_status
    },
};
