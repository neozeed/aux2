/*
 * @(#)data.c  {Apple version 1.2 89/12/12 14:02:43}
 *
 * Copyright (c) 1987, 1988, 1989 by Apple Computer, Inc.
 * All Rights Reserved.
 *
 * THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF APPLE COMPUTER, INC.
 * The copyright notice above does not evidence any actual or
 * intended publication of such source code.
 */

#if !defined(lint) && !defined(NO_SCCS_IDS)
static char _sccsid[]="@(#)data.c  {Apple version 1.2 89/12/12 14:02:43}";
#endif

#include "fd.h"
#include <sys/fdioctl.h>
#include <sys/buf.h>
#include <sys/swimiop.h>

/* TUNABLES: adb and IOCTL adjustable, of course: */
struct fd_tune fd_tune = {
			    4,		/* getformat address tries */
			    3,		/* sector retries */
			    2,		/* rdadr retries */
			    1,		/* max recalibrates */
			    1,		/* max reseeks */
			    5		/* max revs/track */
			};
/* END TUNABLES */

/* METERING DATA, returned by ioctl.
    NB: blkcount cells are incremented based on request blockcount
    as follows: 1,2,4,8,16,...512,1024,2048,4096.  We use a simple shift
    to decide which range the count falls in.
 */
struct fd_meter fd_meter;

int fd_blkrange[13] = { 1,2,4,8,16,32,64,128,256,512,1024,2048,4096 };

int fd_motordelay = 3 * SECONDS;	/* motor off delay */

/* Values for verbose are as follows:
 *	0	NO TRACE OUTPUT
 *	1
 *	2	show errors which cause failed request (usually EIO)
 *	3	show transient errors
 *	4
 *	5
 *	6	show some actions (e.g. recal, reseek)
 *	7
 *	8	show buffer queueing activity in fd_start, motoroff
 *	9
 */
int fd_c_verbose = 0;

/* We set the drivestatus to point to the appropriate driveparams
 * based on what density we're currently using.
 */

struct drivestatus fd_status[MAXDRIVES];
struct drivestatus *fd_drive;		/* current drive in use */

struct buf fd_cmdbuf;

int fd_minor;				/* current drive minor dev */
int fd_revnum;				/* revision number */

struct chipparams *fd_chip;		/* current chip params */
struct interface *fd_int;		/* current interface type */

struct msg_insert fd_swimmsg;		/* incoming IOP SWIM message */
