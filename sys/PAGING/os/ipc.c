#ifndef lint	/* .../sys/PAGING/os/ipc.c */
#define _AC_NAME ipc_c
#define _AC_NO_MAIN "@(#) Copyright (c) 1984-85 AT&T-IS, All Rights Reserved.  {Apple version 2.1 89/10/13 12:03:44}"
#include <apple_notice.h>

#ifdef _AC_HISTORY
  static char *sccsid = "@(#)Copyright Apple Computer 1987\tVersion 2.1 of ipc.c on 89/10/13 12:03:44";
#endif		/* _AC_HISTORY */
#endif		/* lint */

#define _AC_MODS
/*	@(#)ipc.c	UniPlus VVV.2.1.1	*/

#ifdef lint
#include	"sys/sysinclude.h"
#else lint
#include	"sys/errno.h"
#include	"sys/types.h"
#include	"sys/mmu.h"
#include	"sys/param.h"
#include	"sys/signal.h"
#include	"sys/dir.h"
#include	"sys/time.h"
#include	"sys/user.h"
#include	"sys/page.h"
#include        "sys/region.h"
#include 	"sys/proc.h"
#include	"sys/ipc.h"
#endif lint

/*
**	Common IPC routines.
*/

/*
**	Check message, semaphore, or shared memory access permissions.
**
**	This routine verifies the requested access permission for the current
**	process.  Super-user is always given permission.  Otherwise, the
**	appropriate bits are checked corresponding to owner, group, or
**	everyone.  Zero is returned on success.  On failure, u.u_error is
**	set to EACCES and one is returned.
**	The arguments must be set up as follows:
**		p - Pointer to permission structure to verify
**		mode - Desired access permissions
*/

ipcaccess(p, mode)
register struct ipc_perm	*p;
register ushort			mode;
{
	register struct user *up;

	up = &u;
	if(up->u_uid == 0)
		return(0);
	if(up->u_uid != p->uid && up->u_uid != p->cuid) {
		mode >>= 3;
		if(up->u_gid != p->gid && up->u_gid != p->cgid)
			mode >>= 3;
	}
	if(mode & p->mode)
		return(0);
	up->u_error = EACCES;
	return(1);
}

/*
**	Get message, semaphore, or shared memory structure.
**
**	This routine searches for a matching key based on the given flags
**	and returns a pointer to the appropriate entry.  A structure is
**	allocated if the key doesn't exist and the flags call for it.
**	The arguments must be set up as follows:
**		key - Key to be used
**		flag - Creation flags and access modes
**		ipcfcn - function that returns ptrs to appropriate
			ipc id structure and associated semaphore
**		status - Pointer to status word: set on successful completion
**			only:	0 => existing entry found
**				1 => new entry created
**	Ipcget returns NULL with u.u_error set to an appropriate value if
**	it fails, or a pointer to the initialized and locked entry if
**	it succeeds.
*/

struct ipc_perm *
ipcget(key, flag, ipcfcn, status)
key_t key;
register int flag, (*ipcfcn)(), *status;
{
	struct ipc_perm	*base;		/* ptr to perm array entry */
	register struct ipc_perm *a;	/* ptr to available entry */
	register int i;			/* loop control */

	if(key == IPC_PRIVATE) {
		for(i=0; (*ipcfcn)(i,&base); i++)
		{
			if(base->mode & IPC_ALLOC)
				continue;
			goto init;
		}
		u.u_error = ENOSPC;
		return(NULL);
	} else {
		for(i=0, a=NULL; (*ipcfcn)(i,&base); i++)
		{
			if(base->mode & IPC_ALLOC) {
				if(base->key == key) {
					if((flag & (IPC_CREAT | IPC_EXCL)) ==
						(IPC_CREAT | IPC_EXCL)) {
						u.u_error = EEXIST;
						return(NULL);
					}
					if((flag & 0777) & ~base->mode) {
						u.u_error = EACCES;
						return(NULL);
					}
					*status = 0;
					return(base);
				}
				continue;
			}
			if(a == NULL)
				a = base;
		}
		if(!(flag & IPC_CREAT)) {
			u.u_error = ENOENT;
			return(NULL);
		}
		if(a == NULL) {
			u.u_error = ENOSPC;
			return(NULL);
		}
		base = a;
	}
init:
	*status = 1;
	base->mode = IPC_ALLOC | (flag & 0777);
	base->key = key;
	base->cuid = base->uid = u.u_uid;
	base->cgid = base->gid = u.u_gid;
	return(base);
}

/* <@(#)ipc.c	6.1> */
