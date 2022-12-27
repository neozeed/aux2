#ifndef lint	/* .../sys/NFS/sys/nfs_vfsops.c */
#define _AC_NAME nfs_vfsops_c
#define _AC_NO_MAIN "@(#) Copyright (c) 1983-87 Sun Microsystems Inc., All Rights Reserved.  {Apple version 2.2 90/02/19 11:24:55}"
#include <apple_notice.h>

#ifdef _AC_HISTORY
  static char *sccsid = "@(#)Copyright Apple Computer 1989\tVersion 2.2 of nfs_vfsops.c on 90/02/19 11:24:55";
#endif		/* _AC_HISTORY */
#endif		/* lint */

#define _AC_MODS
/* NFSSRC @(#)nfs_vfsops.c	2.2 86/05/15 */
/*      @(#)nfs_vfsops.c 1.1 86/02/03 SMI      */

/*
 * Copyright (c) 1985 by Sun Microsystems, Inc.
 */

#include "sys/param.h"
#include "sys/signal.h"
#include "sys/types.h"
#include "sys/time.h"
#include "sys/mmu.h"
#include "sys/page.h"
#include "sys/systm.h"
#include "sys/dir.h"
#include "sys/user.h"
#include "sys/vfs.h"
#include "sys/vnode.h"
#include "sys/pathname.h"
#include "sys/uio.h"
#include "sys/socket.h"
#include "sys/sysent.h"
#include "netinet/in.h"
#include "rpc/types.h"
#include "rpc/xdr.h"
#include "rpc/auth.h"
#include "rpc/clnt.h"
#include "sys/errno.h"
#include "nfs/nfs.h"
#include "nfs/nfs_clnt.h"
#include "nfs/rnode.h"
#include "sys/mount.h"
#include "nfs/mount.h"
#include "net/if.h"

#ifdef NFSDEBUG
extern int nfsdebug;
#endif

extern int (*lockctl)();
extern int klm_lockctl();
extern (*lockrel)();
extern vno_lockrelease();

struct vnode *nfsrootvp();
struct vnode *makenfsnode();
int nfsmntno;

/*
 * nfs vfs operations.
 */
int nfs_mount();
int nfs_unmount();
int nfs_root();
int nfs_statfs();
int nfs_sync();
extern int nfs_badop();
int nfs_testfs();
int nfs_mountable();


struct vfsops nfs_vfsops = {
	nfs_mount,
	nfs_unmount,
	nfs_root,
	nfs_statfs,
	nfs_sync,
	nfs_badop,		/* vfs_vget */
	nfs_badop,		/* vfs_mountroot */
	nfs_badop,		/* vfs_unmountroot */
	nfs_testfs,
	nfs_mountable
};

/*
 * Add nfs to vfssw, set up NFS system calls, and patch lockctl to use
 * kernel lock manager.  This routine must be called after any local
 * lock managers are initialized to that KLM will be used instead of
 * local locking.
 */
nfsinit()
{
	extern int async_daemon();
	extern int exportfs();
	extern int nfs_getfh();
	extern int nfs_svc();
	extern struct vfsops *vfssw[];

	vfssw[MOUNT_NFS] = &nfs_vfsops;

	sysent[119].sy_call = async_daemon;
	sysent[121].sy_call = nfs_svc;
	sysent[122].sy_call = nfs_getfh;
	sysent[140].sy_call = exportfs;

	/*
	 * Replace the current (local) lockctl routine with the NFS lockctl
	 * routine.
	 */
	lockctl = klm_lockctl;
	lockrel = vno_lockrelease;
}

/*
 * nfs mount vfsop
 * Set up mount info record and attach it to vfs struct.
 */
/*ARGSUSED*/
nfs_mount(vfsp, path, data)
	struct vfs *vfsp;
	char *path;
	caddr_t data;
{
	int error;
	struct vnode *rtvp = NULL;	/* the server's root */
	struct mntinfo *mi;		/* mount info, pointed at by vfs */
	fhandle_t fh;			/* root fhandle */
	struct sockaddr_in saddr;	/* server's address */
	char hostname[HOSTNAMESZ];	/* server's name */
	struct nfs_args args;		/* nfs mount arguments */

	/*
	 * get arguments
	 */
	error = copyin(data, (caddr_t)&args, sizeof (args));
	if (error) {
		goto errout;
	}

	/*
	 * Get server address
	 */
	error = copyin((caddr_t)args.addr, (caddr_t)&saddr,
	    sizeof(saddr));
	if (error) {
		goto errout;
	}
	/*
	 * For now we just support AF_INET
	 */
	if (saddr.sin_family != AF_INET) {
		error = EPFNOSUPPORT;
		goto errout;
	}

	/*
	 * Get the root fhandle
	 */
	error = copyin((caddr_t)args.fh, (caddr_t)&fh, sizeof(fh));
	if (error) {
		goto errout;
	}

	/*
	 * Get server's hostname
	 */
	if (args.flags & NFSMNT_HOSTNAME) {
		error = copyin((caddr_t)args.hostname, (caddr_t)hostname,
		    HOSTNAMESZ);
		if (error) {
			goto errout;
		}
	} else {
		addr_to_str(&saddr, hostname);
	}

	/*
	 * Get root vnode.
	 */
	rtvp = nfsrootvp(vfsp, &saddr, &fh, hostname);
	if (!rtvp) {
		error = EBUSY;
		goto errout;
	}

	/*
	 * Set option fields in mount info record
	 */
	mi = vtomi(rtvp);
	mi->mi_hard = ((args.flags & NFSMNT_SOFT) == 0);
	mi->mi_int = ((args.flags & NFSMNT_INT) == NFSMNT_INT);
	if (args.flags & NFSMNT_RETRANS) {
		mi->mi_retrans = args.retrans;
		if (args.retrans < 0) {
			error = EINVAL;
			goto errout;
		}
	}
	if (args.flags & NFSMNT_TIMEO) {
		mi->mi_timeo = args.timeo;
		if (args.timeo <= 0) {
			error = EINVAL;
			goto errout;
		}
	}
	if (args.flags & NFSMNT_RSIZE) {
		if (args.rsize <= 0) {
			error = EINVAL;
			goto errout;
		}
		mi->mi_tsize = MIN(mi->mi_tsize, args.rsize);
	}
	if (args.flags & NFSMNT_WSIZE) {
		if (args.wsize <= 0) {
			error = EINVAL;
			goto errout;
		}
		mi->mi_stsize = MIN(mi->mi_stsize, args.wsize);
	}

#ifdef NFSDEBUG
	dprint(nfsdebug, 1,
	    "nfs_mount: hard %d timeo %d retries %d wsize %d rsize %d\n",
	    mi->mi_hard, mi->mi_timeo, mi->mi_retrans, mi->mi_stsize,
	    mi->mi_tsize);
#endif
        vfsp->vfs_flag |= VFS_REMOTE;

errout:
	if (error) {
		if (rtvp) {
			VN_RELE(rtvp);
		}
	}
	return (error);
}

struct vnode *
nfsrootvp(vfsp, sin, fh, hostname)
	struct vfs *vfsp;		/* vfs of fs, if NULL amke one */
	struct sockaddr_in *sin;	/* server address */
	fhandle_t *fh;			/* swap file fhandle */
	char *hostname;			/* swap server name */
{
	struct vnode *rtvp = NULL;	/* the server's root */
	struct mntinfo *mi = NULL;	/* mount info, pointed at by vfs */
	struct vattr va;		/* root vnode attributes */
	struct nfsfattr na;		/* root vnode attributes in nfs form */
	struct statfs sb;		/* server's file system stats */
	extern struct ifnet loif;

	/*
	 * create a mount record and link it to the vfs struct
	 */
	mi = (struct mntinfo *)kmem_alloc((u_int)sizeof(*mi));
	bzero((caddr_t)mi, sizeof(*mi));
	mi->mi_hard = 1;
	mi->mi_addr = *sin;
	mi->mi_retrans = NFS_RETRIES;
	mi->mi_timeo = NFS_TIMEO;
	mi->mi_mntno = nfsmntno++;
	bcopy(hostname, mi->mi_hostname, HOSTNAMESZ);

	/*
	 * Make a vfs struct for nfs.  We do this here instead of below
	 * because rtvp needs a vfs before we can do a getattr on it.
	 */
	vfsp->vfs_fsid.val[0] = mi->mi_mntno;
	vfsp->vfs_fsid.val[1] = MOUNT_NFS;
	vfsp->vfs_data = (caddr_t)mi;

	/*
	 * Make the root vnode, use it to get attributes, then remake it
	 * with the attributes
	 */
	rtvp = makenfsnode(fh, (struct nfsfattr *) 0, vfsp, 0);
	if ((rtvp->v_flag & VROOT) != 0) {
		goto bad;
	}
	if (VOP_GETATTR(rtvp, &va, u.u_cred)) {
		goto bad;
	}
	VN_RELE(rtvp);
	vattr_to_nattr(&va, &na);
	rtvp = makenfsnode(fh, &na, vfsp, 0);
	rtvp->v_flag |= VROOT;
	mi->mi_rootvp = rtvp;

	/*
	 * Get server's filesystem stats.  Use these to set transfer
	 * sizes, filesystem block size, and read-only.
	 */
	if (VFS_STATFS(vfsp, &sb)) {
		goto bad;
	}
	mi->mi_tsize = min(NFS_MAXDATA, (u_int)nfstsize());

	/*
	 * Set filesystem block size to at least CLBYTES and at most MAXBSIZE
	 */
	mi->mi_bsize = MAX(va.va_blocksize, SBUFSIZE);
	mi->mi_bsize = MIN(mi->mi_bsize, MAXBSIZE);
	vfsp->vfs_bsize = mi->mi_bsize;

	/*
	 * Need credentials in the rtvp so do_bio can find them.
	 */
	crhold(u.u_cred);
	vtor(rtvp)->r_cred = u.u_cred;

	return (rtvp);
bad:
	if (mi) {
		kmem_free((caddr_t)mi, sizeof (*mi));
	}
	if (rtvp) {
		VN_RELE(rtvp);
	}
	return (NULL);
}

/*
 * vfs operations
 */

nfs_unmount(vfsp)
	register struct vfs *vfsp;
{
	register struct mntinfo *mi = (struct mntinfo *)vfsp->vfs_data;

#ifdef NFSDEBUG
        dprint(nfsdebug, 4, "nfs_unmount(%x) mi = %x\n", vfsp, mi);
#endif
	
	punmount(mi->mi_mntno);   /* flush all cached pages associated with this mount */
	rflush(vfsp);
	rinval(vfsp);

	if (mi->mi_refct != 1 || mi->mi_rootvp->v_count != 1) {
		return (EBUSY);
	}
	VN_RELE(mi->mi_rootvp);
	kmem_free((caddr_t)mi, (u_int)sizeof(*mi));
	return(0);
}

/*
 * find root of nfs
 */
int
nfs_root(vfsp, vpp)
	struct vfs *vfsp;
	struct vnode **vpp;
{

	*vpp = (struct vnode *)((struct mntinfo *)vfsp->vfs_data)->mi_rootvp;
	(*vpp)->v_count++;
#ifdef NFSDEBUG
        dprint(nfsdebug, 4, "nfs_root(0x%x) = %x\n", vfsp, *vpp);
#endif
	return(0);
}

/*
 * Get file system statistics.
 */
int
nfs_statfs(vfsp, sbp)
register struct vfs *vfsp;
struct statfs *sbp;
{
	struct nfsstatfs fs;
	struct mntinfo *mi;
	fhandle_t *fh;
	int error = 0;

	mi = vftomi(vfsp);
	fh = vtofh(mi->mi_rootvp);
#ifdef NFSDEBUG
        dprint(nfsdebug, 4, "nfs_statfs vfs %x\n", vfsp);
#endif
	error = rfscall(mi, RFS_STATFS, xdr_fhandle,
	    (caddr_t)fh, xdr_statfs, (caddr_t)&fs, u.u_cred);
	if (!error) {
		error = geterrno(fs.fs_status);
	}
	if (!error) {
		if (mi->mi_stsize) {
			mi->mi_stsize = MIN(mi->mi_stsize, fs.fs_tsize);
		} else {
			mi->mi_stsize = fs.fs_tsize;
		}
		sbp->f_bsize = fs.fs_bsize;
		sbp->f_blocks = fs.fs_blocks;
		sbp->f_bfree = fs.fs_bfree;
		sbp->f_bavail = fs.fs_bavail;

		/*  The Man page for statfs says that undefined fields are 
		 * set to -1.  Our df prints stupid numbers of free inodes
		 * on NFS systems if these are set to -1.  We set them to
		 * zero.
		 */
		sbp->f_files = 0;
		sbp->f_ffree = 0;
		/*
		 * XXX This is wrong - should be a real fsid
		 */
		bcopy((caddr_t)&vfsp->vfs_fsid,
		    (caddr_t)&sbp->f_fsid, sizeof (fsid_t));
	}
#ifdef NFSDEBUG
        dprint(nfsdebug, 5, "nfs_statfs returning %d\n", error);
#endif
	return (error);
}

/*
 * Flush any pending I/O.
 */
int
nfs_sync(vfsp)
	struct vfs * vfsp;
{

#ifdef NFSDEBUG
        dprint(nfsdebug, 5, "nfs_sync %x\n", vfsp);
#endif
	rflush(vfsp);
	return(0);
}

static char *
itoa(n, str)
	u_char n;
	char *str;
{
	char prbuf[11];
	register char *cp;

	cp = prbuf;
	do {
		*cp++ = "0123456789"[n%10];
		n /= 10;
	} while (n);
	do {
		*str++ = *--cp;
	} while (cp > prbuf);
	return (str);
}

/*
 * Convert a INET address into a string for printing
 */
static
addr_to_str(addr, str)
	struct sockaddr_in *addr;
	char *str;
{
	u_char *ucp = (u_char *)&addr->sin_addr.s_addr;

        str = itoa(ucp[0], str);
        *str++ = '.';
        str = itoa(ucp[1], str);
        *str++ = '.';
        str = itoa(ucp[2], str);
        *str++ = '.';
        str = itoa(ucp[3], str);
	*str = '\0';
}

/*
 * return MOUNT_NFS if dev contains an nfs file system, -1 otherwise.
 */
nfs_testfs(dev)
dev_t dev;
{
	return(-1);
}


nfs_mountable(data)
{
        return(0);
}



