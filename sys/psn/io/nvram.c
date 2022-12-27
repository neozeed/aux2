/*
 * @(#)nvram.c  {Apple version 2.4 90/01/09 14:41:51}
 *
 * Copyright (c) 1987, 1988, 1989 by Apple Computer, Inc.
 * All Rights Reserved.
 *
 * THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF APPLE COMPUTER, INC.
 * The copyright notice above does not evidence any actual or
 * intended publication of such source code.
 */

/*
 * Non-volitile RAM driver
 */

#ifdef lint
#include "sys/sysinclude.h"
#else lint
#include "sys/types.h"
#include "sys/errno.h"
#include "sys/uio.h"
#include "sys/nvram.h"
#endif lint

static int nvram_opened;		/* is the device opened? */

nvram_init ( )
{
    nvram_opened = 0;
}

/*
 * The open routine enforces the "only one process open at a time" rule. 
 */
nvram_open (dev, flag)
    dev_t dev;
    int flag;
{
    if (nvram_opened)
	return (EBUSY);

    nvram_opened = 1;
    return (0);
}

nvram_close(dev)
dev_t dev;
{
    nvram_opened = 0;
}

nvram_read (dev, uio)
    dev_t dev;
    register struct uio *uio;
{
    int val;
    register int err, space;
    register int bytes_allowed;
    char pram_buffer[NVRAM_LEN];

    if (uio->uio_offset >= NVRAM_LEN)
	    return (EIO);
    err = 0;
    space = NVRAM_LEN - uio->uio_offset;
    if (uio->uio_resid > space) 	/* bytes requested > space */
	bytes_allowed = space;
    else
	bytes_allowed = uio->uio_resid;

    val = (bytes_allowed << 16) | uio->uio_offset;
    ReadXPRam (pram_buffer, val);
    err = uiomove (pram_buffer, bytes_allowed, UIO_READ, uio);

    return (err);
}

nvram_write (dev, uio)
    dev_t dev;
    register struct uio *uio;
{
    int val;
    register int err, space;
    register int bytes_allowed;
    char pram_buffer[NVRAM_LEN];

    if (!suser ( ))
	return (EPERM);

    if (uio->uio_offset >= NVRAM_LEN)
	return (EIO);

    err = 0;
    space = NVRAM_LEN - uio->uio_offset;
    if (uio->uio_resid > space) 	/* bytes requested > space */
	bytes_allowed = space;
    else
	bytes_allowed = uio->uio_resid;

    /*
     * must compute val before the uiomove, since the uio_offset will
     * be changed.
     */
    val = (bytes_allowed << 16) | uio->uio_offset;
    err = uiomove (pram_buffer, bytes_allowed, UIO_WRITE, uio);
    WriteXPRam (pram_buffer, val);

    return (err);
}
