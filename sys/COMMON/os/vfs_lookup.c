#ifndef lint	/* .../sys/COMMON/os/vfs_lookup.c */
#define _AC_NAME vfs_lookup_c
#define _AC_NO_MAIN "@(#) Copyright (c) 1983-87 Sun Microsystems Inc., All Rights Reserved.  {Apple version 2.2 90/04/03 21:26:20}"
#include <apple_notice.h>

#ifdef _AC_HISTORY
  static char *sccsid = "@(#)Copyright Apple Computer 1989\tVersion 2.2 of vfs_lookup.c on 90/04/03 21:26:20";
#endif		/* _AC_HISTORY */
#endif		/* lint */

#define _AC_MODS
/*      @(#)vfs_lookup.c 1.1 86/02/03 SMI       */
/*      NFSSRC @(#)vfs_lookup.c 2.1 86/04/15 */

#ifdef HOWFAR
extern int T_clai;
#endif HOWFAR

#include "sys/types.h"
#include "sys/sysmacros.h"
#include "sys/param.h"
#include "sys/signal.h"
#include "sys/mmu.h"
#include "sys/time.h"
#include "sys/user.h"
#include "sys/uio.h"
#include "sys/vfs.h"
#include "sys/vnode.h"
#include "sys/pathname.h"
#include "sys/errno.h"

#ifdef	HOWFAR
extern int	T_lookup;
#endif

short lookuptrace = 0;	/* 1 => trace name lookups on console */

/*
 * lookup the user file name,
 * Handle allocation and freeing of pathname buffer, return error.
 */
lookupname(fnamep, seg, followlink, dirvpp, compvpp)
	char *fnamep;			/* user pathname */
	int seg;			/* addr space that name is in */
	enum symfollow followlink;	/* follow sym links */
	struct vnode **dirvpp;		/* ret for ptr to parent dir vnode */
	struct vnode **compvpp;		/* ret for ptr to component vnode */
{
	struct pathname lookpn;
	register int error;

	error = pn_get(fnamep, seg, &lookpn);
	if (error)
		return (error);
	if ( lookuptrace )
		printf(" %s",lookpn.pn_buf);
	error = lookuppn(&lookpn, followlink, dirvpp, compvpp);
	pn_free(&lookpn);
	return (error);
}

/*
 * Starting at current directory, translate pathname pnp to end.
 * Leave pathname of final component in pnp, return the vnode
 * for the final component in *compvpp, and return the vnode
 * for the parent of the final component in dirvpp.
 *
 * This is the central routine in pathname translation and handles
 * multiple components in pathnames, separating them at /'s.  It also
 * implements mounted file systems and processes symbolic links.
 */
lookuppn(pnp, followlink, dirvpp, compvpp)
	register struct pathname *pnp;		/* pathaname to lookup */
	enum symfollow followlink;		/* (don't) follow sym links */
	struct vnode **dirvpp;			/* ptr for parent vnode */
	struct vnode **compvpp;			/* ptr for entry vnode */
{
	register struct vnode *vp;		/* current directory vp */
	register struct vnode *cvp;		/* current component vp */
	register struct vfs *vfsp;		/* ptr to vfs for mount indir */
	register int error;
	register int nlink;
	struct vnode *tvp;			/* non-reg temp ptr */
	char component[MAXNAMLEN+1];		/* buffer for component */
	enum {TRUE = 1, FALSE = 0} wasroot;

	nlink = 0;
	cvp = (struct vnode *)0;

	/*
	 * start at current directory.
	 */
	vp = u.u_cdir;
	VN_HOLD(vp);

begin:
	/*
	 * Each time we begin a new name interpretation (e.g.
	 * when first called and after each symbolic link is
	 * substituted), we allow the search to start at the
	 * root directory if the name starts with a '/', otherwise
	 * continuing from the current directory.
	 */
	component[0] = 0;

	if (pn_peekchar(pnp) == '/') {
		VN_RELE(vp);
		pn_skipslash(pnp);
		if (pn_peekchar(pnp) == '\0')
			wasroot = TRUE;
		if (u.u_rdir)
			vp = u.u_rdir;
		else
			vp = rootdir;
		VN_HOLD(vp);
	}
	else {
	        if (pn_peekchar(pnp) == '\0') {
			error = ENOENT;
			goto bad;
	        }
	}
next:
	/*
	 * Make sure we have a directory.
	 */
	if (vp->v_type != VDIR) {
		error = ENOTDIR;
		goto bad;
	}
	/*
	 * Process the next component of the pathname.
	 */
	if (error = pn_getcomponent(pnp, component))
		goto bad;
#ifdef	HOWFAR
	if (T_lookup)
		if (component[0] == 0)
			printf("lookuppn about to scan for (null)\n");
		else
			printf("lookuppn about to scan for %s\n", component);
#endif

	/*
	 * Check for degenerate name (e.g. / or "")
	 * which is a way of talking about a directory,
	 * e.g. "/." or ".".
	 */
	if (component[0] == 0) {
		/*
		 * If the caller was interested in the parent then
		 * return an error since we don't have the real parent
		 */
		if (dirvpp != (struct vnode **)0) {
			if (wasroot == TRUE) {
				if (u.u_rdir)
					*dirvpp = u.u_rdir;
				else
					*dirvpp = rootdir;
				VN_HOLD(*dirvpp);
			} else {
				VN_RELE(vp);
				return(EINVAL);
			}
		}
		(void) pn_set(pnp, ".");
		if (compvpp != (struct vnode **)0) {
			*compvpp = vp;
		} else {
			VN_RELE(vp);
		}
		return(0);
	}

	/*
	 * Handle "..": two special cases.
	 * 1. If at root directory (e.g. after chroot)
	 *    then ignore it so can't get out.
	 * 2. If this vnode is the root of a mounted
	 *    file system, then replace it with the
	 *    vnode which was mounted on so we take the
	 *    .. in the other file system.
	 */
	if (component[0] == '.' && component[1] == '.' && component[2] == '\0') {
checkforroot:
		if ((vp == u.u_rdir) || (vp == rootdir)) {
			cvp = vp;
			VN_HOLD(cvp);
			goto skip;
		}
		if (vp->v_flag & VROOT) {
			cvp = vp;
			vp = vp->v_vfsp->vfs_vnodecovered;
			VN_HOLD(vp);
			VN_RELE(cvp);
			cvp = (struct vnode *)0;
			goto checkforroot;
		}
	}

	/*
	 * Perform a lookup in the current directory.
	 */
	if (error = VOP_LOOKUP(vp, component, &tvp, u.u_cred)) {
		cvp = (struct vnode *)0;
		/*
		 * On error, if more pathname or if caller was not interested
		 * in the parent directory then hard error.
		 */
		if (pn_pathleft(pnp) || dirvpp == (struct vnode **)0
			|| error == EACCES)
			goto bad;
		(void) pn_set(pnp, component);
		*dirvpp = vp;
		if (compvpp != (struct vnode **)0)
			*compvpp = (struct vnode *)0;
		return (0);
	}
	cvp = tvp;
	/*
	 * If we hit a symbolic link and there is more path to be
	 * translated or this operation does not wish to apply
	 * to a link, then place the contents of the link at the
	 * front of the remaining pathname.
	 */
	if (cvp->v_type == VLNK && (followlink == FOLLOW_LINK || pn_pathleft(pnp))) {
		struct pathname linkpath;

		if (++nlink > 20) {
			error = ELOOP;
			goto bad;
		}
		if (error = getsymlink(cvp, &linkpath))
			goto bad;
		if (pn_pathleft(&linkpath) == 0)
			(void) pn_set(&linkpath, ".");
		error = pn_combine(pnp, &linkpath);	/* linkpath before pn */
		pn_free(&linkpath);
		if (error)
			goto bad;
		VN_RELE(cvp);
		cvp = (struct vnode *)0;
		goto begin;
	}

	/*
	 * If this vnode is mounted on, then we
	 * transparently indirect to the vnode which 
	 * is the root of the mounted file system.
	 * Before we do this we must check that an unmount is not
	 * in progress on this vnode. This maintains the fs status
	 * quo while a possibly lengthy unmount is going on.
	 */
mloop:
	while (vfsp = cvp->v_vfsmountedhere) {
		while (vfsp->vfs_flag & VFS_MLOCK) {
			vfsp->vfs_flag |= VFS_MWAIT;
			if (sleep((caddr_t)vfsp, PVFS|PCATCH)) {
			        error = EINTR;
				goto bad;
			}
			goto mloop;
		}
		if (error = VFS_ROOT(cvp->v_vfsmountedhere, &tvp))
			goto bad;
		VN_RELE(cvp);
		cvp = tvp;
	}

skip:
	/*
	 * Skip to next component of the pathname.
	 * If no more components, return last directory (if wanted)  and
	 * last component (if wanted).
	 */
	if (pn_pathleft(pnp) == 0) {
		(void) pn_set(pnp, component);
		if (dirvpp != (struct vnode **)0) {
			/*
			 * check that we have the real parent and not
			 * an alias of the last component
			 */
			if (vp == cvp &&
			    (pnp->pn_buf[0] != '.' || pnp->pn_buf[1] != '\0')) {
				VN_RELE(vp);
				VN_RELE(cvp);
				return(EINVAL);
			}
			*dirvpp = vp;
		} else
			VN_RELE(vp);

		if (compvpp != (struct vnode **)0)
			*compvpp = cvp;
		else
			VN_RELE(cvp);
		return (0);
	}
	/*
	 * skip over slashes from end of last component
	 */
	pn_skipslash(pnp);

	/* directories may terminate with '/' */
	if (pn_pathleft(pnp) == 0 && cvp->v_type == VDIR)
		goto skip;

	/*
	 * Searched through another level of directory:
	 * release previous directory handle and save new (result
	 * of lookup) as current directory.
	 */
	VN_RELE(vp);
	vp = cvp;
	cvp = (struct vnode *)0;
	goto next;

bad:
	/*
	 * Error. Release vnodes and return.
	 */
	if (cvp)
		VN_RELE(cvp);
	VN_RELE(vp);
	return (error);
}

/*
 * Gets symbolic link into pathname.
 */
static int
getsymlink(vp, pnp)
	 struct vnode *vp;
register struct pathname *pnp;
{
	struct iovec aiov;
	struct uio auio;
	register int error;

	pn_alloc(pnp);
	aiov.iov_base = pnp->pn_buf;
	aiov.iov_len = MAXPATHLEN;
	auio.uio_iov = &aiov;
	auio.uio_iovcnt = 1;
	auio.uio_offset = 0;
	auio.uio_seg = UIOSEG_KERNEL;
	auio.uio_resid = MAXPATHLEN;
	if (error = VOP_READLINK(vp, &auio, u.u_cred))
		pn_free(pnp);
	else
		pnp->pn_pathlen = MAXPATHLEN - auio.uio_resid;
	return (error);
}
