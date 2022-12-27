#ifndef lint    /* .../sys/PAGING/os/exec.c */
#define _AC_NAME exec_c
#define _AC_NO_MAIN "@(#) Copyright (c) 1984-85 AT&T-IS, 1985-87 UniSoft Corporation, All Rights Reserved.  {Apple version 2.13 90/04/23 10:58:49}"
#include <apple_notice.h>

#ifdef _AC_HISTORY
  static char *sccsid = "@(#)Copyright Apple Computer 1989\tVersion 2.13 of exec.c on 90/04/23 10:58:49";
#endif		/* _AC_HISTORY */
#endif		/* lint */

#define _AC_MODS
/*	@(#)exec.c	UniPlus VVV.2.1.3	*/

#ifdef lint
#include "sys/sysinclude.h"
#include "a.out.h"
#else lint
#include "sys/types.h"
#include "sys/param.h"
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
#include "sys/file.h"
#include "sys/buf.h"
#include "sys/vnode.h"
#include "sys/vfs.h"
#include "sys/acct.h"
#include "sys/sysinfo.h"
#include "sys/reg.h"
#include "sys/var.h"
#include "sys/region.h"
#include "sys/proc.h"
#include "sys/tuneable.h"

#include "aouthdr.h"
#include "filehdr.h"
#include "scnhdr.h"

#include "sys/ipc.h"
#include "sys/shm.h"
#include "sys/wait.h"
#include "sys/pathname.h"
#include "sys/uio.h"
#endif lint

#include "sys/debug.h"


/*
 * Encapsulation of args to gethead(), execbld().
 * Needed to give a convenient place for stack base info before committing
 *  proc structure to the indicated shape.
 */
struct execinfo {
	unsigned int	e_stack;	/* Base for exec'd process stack */
	char	*e_cfarg;		/* Shell name array */
	long	e_libscn;		/* Number of shared libs needed */
	int	e_flags;		/* Flags - see below */
	struct	vattr e_vattr;		/* Exec'd file vattrs */
	struct	pathname e_pn;		/* Pathname struct for exec'd file */
};

#define EI_INDIR	0x01	/* Indirect execution of shell script */
#define EI_MAC24	0x02	/* Exec'd file is a 24-bit Mac/Toolbox App */
#define EI_MAC32	0x04	/* Exec'd file is a 32-bit Mac/Toolbox App */

struct execa {
	char	*fname;
	char	**argp;
	char	**envp;
};

#define SHSIZE	32
#define LPATHNAME 64


exece()
{	register struct vnode *vp;
	struct execinfo execinfo;
	struct vattr vattr;
	char cfarg[SHSIZE];
	struct vnode *gethead();

	execinfo.e_flags = 0;
	execinfo.e_cfarg = cfarg;
	cfarg[0] = '\0';

	if (vp = gethead(&execinfo))
		execbld(vp, &execinfo);

	sysinfo.sysexec++;
}


#define NCAPGS	btop(NCARGS + ((NCARGS + 2) * sizeof(int)))

#define GETB(V)					       \
{						       \
	if ((na = endcp - cp) <= 0) {		       \
		up->u_error = E2BIG;		       \
		goto out1;			       \
	}					       \
	if ((na = bcopyin(V, cp, na)) == 0) {	       \
		up->u_error = E2BIG;		       \
		goto out1;			       \
	}					       \
	if (na < 0) {				       \
		up->u_error = EFAULT;		       \
		goto out1;			       \
	}					       \
}
#define GETW(V)							  \
{								  \
	if ((na = wcopyin(V, cp, (NCARGS+1)*sizeof(int))) < 0) {  \
		up->u_error = EFAULT;				  \
		goto out1;					  \
	}							  \
}


execbld(vp, eip)
struct vnode	*vp;
struct execinfo *eip;
{
	register char *saveargs;
	register char **pp;
	register char *cp;
	register char *tp;
	register int na, ap;
	int uid, gid;
	register struct user  *up;
	struct execa *uap;
	register struct proc *p;
	char	     *endcp;
	char *fill_psargs();

	sysinfo.sysexec++;

	up = &u;
	availrmem -= NCAPGS;
	availsmem -= NCAPGS;

	if (availrmem < tune.t_minarmem || availsmem < tune.t_minasmem){
		up->u_error = EAGAIN;
		goto out2;
	}
	/* From here on, we are committed.  If we get an error, just kill
	 *  the process; can't recover the execing process.
	 */
	uid = up->u_uid;
	gid = up->u_gid;

	if ( (!(eip->e_flags&EI_INDIR)) &&
	    (vp->v_vfsp->vfs_flag & VFS_NOSUID) == 0) {
		if (eip->e_vattr.va_mode & VSUID)
			uid = eip->e_vattr.va_uid;
		if (eip->e_vattr.va_mode & VSGID)
			gid = eip->e_vattr.va_gid;
	}
	uap = (struct execa *)up->u_ap;

	while ((saveargs = (char *)kvalloc(NCAPGS, PG_ALL, -1)) == NULL) {
		mapwant(sptmap)++;
		sleep(sptmap, PMEM);
	}
	cp = saveargs;

	/* save room for argc */
	cp += sizeof(int);

	if (eip->e_flags&EI_INDIR) {
		if (*(eip->e_cfarg))	 /* if optional command line arg present */
			cp += sizeof(char *);	   /* reserve slot in argp list */
		uap->argp++;		 /* bump over original arg0 */
		cp += sizeof(char *);	 /* replace with simple name of indirect shell */
		
		pp = (char **)cp;	 /* insert name of indirect file */
		*pp = ((struct execa *)up->u_ap)->fname;
		cp += sizeof(char *);
	}

	/* fetch argv */
	GETW(uap->argp);
	cp += na;

	/* set up argc */
	*(int *)saveargs = (cp - saveargs) / sizeof(int) - 2;

	/* fetch envp */
	GETW(uap->envp);
	cp += na;
	endcp = cp + NCARGS;

	/* fetch args */
	pp = (char **)(saveargs + 4);
	tp = up->u_psargs;

	if (eip->e_flags&EI_INDIR) {
		na = eip->e_pn.pn_pathlen + 1;
		bcopy(eip->e_pn.pn_path, cp, na);
		*pp++ = cp;
		tp = fill_psargs(tp, cp, na);
		cp += na;

		if (*(eip->e_cfarg)) {
			na = strlen(eip->e_cfarg) + 1;
			bcopy(eip->e_cfarg, cp, na);
			*pp++ = cp;
			tp = fill_psargs(tp, cp, na);
			cp += na;
		}
	}
	while (*pp) {
		GETB(*pp);
		*pp++ = cp;
		tp = fill_psargs(tp, cp, na);
		cp += na;
	}
	while (tp < &up->u_psargs[PSARGSZ])
		*tp++ = 0;
	pp++;		    /* skip over NULL separating argp from envp */

	/* fetch env */
	while (*pp) {
		GETB(*pp);
		*pp++ = cp;
		cp += na;
	}

	/* leave int hole at end and round up to int size */
	tp = (char *)((int)(cp + 2 * sizeof(int) - 1) & ~(sizeof(int) - 1));

	while (cp < tp)
		*cp++ = 0;
	na = cp - saveargs;

	/* in case we're getting this across NFS, the open */
	/* will purge any cached info we have about this vnode */
	/* need to do it before the getxfile so we dont trash */
	/* the new info */
	VOP_OPEN(&vp, 0, 0);

	/* Transfer shape info from execinfo to the proc structure */
	p = up->u_procp;
	p->p_stack = eip->e_stack;
	/* In case we were 24-bit, remove all indications; the p_root* entries
	 * will retain current state for the execing process
	 */
	p->p_flag &= ~(SMAC24|SROOT32|SROOT24);
	if (eip->e_flags&EI_MAC24)
		p->p_flag |= SMAC24;

/* Check if we have any shared libraries to load.	*/
	if (up->u_nlibs > 0)
	     getxfile(vp, na, uid, gid, &eip->e_libscn);
	else
	     getxfile(vp, na, uid, gid, 0);

	if (up->u_error) {
		psignal(p, SIGKILL);
		goto out1;
	}
	ap = p->p_stack - na;

	/* reset argv pointers for new process */
	for (pp = (char **)saveargs + 1; *pp; pp++)
		*pp = *pp - saveargs + (char *)ap;

	/* reset envp pointers for new process */
	for (pp++; *pp; pp++)
		*pp = *pp - saveargs + (char *)ap;

	/* copy back arglist */
	up->u_ar0[SP] = ap;
	u.u_user[3] = ap;	/* Plug in for upcoming page fault */
	copyout(saveargs, ap, na);

	setregs(&eip->e_pn);

out1:
	kvfree((int)saveargs, NCAPGS, 1);
out2:
	availrmem += NCAPGS;
	availsmem += NCAPGS;
	pn_free(&eip->e_pn);
	VN_RELE(vp);
}


static char *
fill_psargs(psap, cp, na)
register char *psap;
register char *cp;
{	register char *tcp;
	register char *ucp;

	tcp = cp + na;
	ucp = &u.u_psargs[PSARGSZ-1];

	if (psap < ucp) {
	      do {
			*psap++ = *cp++;
	      } while (cp < tcp && psap < ucp);

	      psap[-1] = ' ';
	}
	return(psap);
}



struct vnode *
gethead(eip)
struct execinfo *eip;
{
	register struct vnode *vp;
	register struct user *up;
	register struct aouthdr *hdr;
	register unsigned tstart;
	register char  *cp;
	register struct proc *p;
	char	       *np;
	char	       *ep;
	union {
	  long	 ex_mac;
	  char	 ex_shell[SHSIZE];
	  struct execfile {
		struct filehdr filehdr;
		struct aouthdr aouthdr;
	  } execfile;
	} exdata;
	struct vnode *tvp;
	int	      resid;
	int	      indir;
	SCNHDR	      sbuf;
/* flags: used to ascertain presence of .text, .data and .bss sections */
/* txt_ptr : to store start of .text section */

	int	      indx = 0;
	int	      lindx;
	long	      flags = 0;
	long	      txt_ptr;

	up = &u;
	indir = 0;

	if (up->u_error = pn_get(((struct execa *)up->u_ap)->fname, UIOSEG_USER, &eip->e_pn))
		return((struct vnode *)NULL);
again:
	if (up->u_error = lookuppn(&eip->e_pn, FOLLOW_LINK, (struct vnode **)0, &tvp)) {
		pn_free(&eip->e_pn);
		return((struct vnode *)NULL);
	}
	vp = tvp;

	if (up->u_error = VOP_GETATTR(vp, &eip->e_vattr, up->u_cred))
		goto bad;
	/*
	 * XXX should change VOP_ACCESS to not let super user always have it
	 * for exec permission on regular files.
	 */
	if (up->u_error = VOP_ACCESS(vp, VEXEC, up->u_cred))
		goto bad;
	p = up->u_procp;
	if ((p->p_flag&STRC) &&
	    (up->u_error = VOP_ACCESS(vp, VREAD, up->u_cred)))
		goto bad;
	if (vp->v_type != VREG || (eip->e_vattr.va_mode & (VEXEC|(VEXEC>>3)|(VEXEC>>6))) == 0) {
		up->u_error = EACCES;
		goto bad;
	}
	/*
	 * read in first few bytes of file for segment sizes
	 * ux_mag = 407/410/411/520/570/575
	 *  407 is plain executable
	 *  410 is RO text
	 *  411 is separated ID
	 *  520 Motorola Common object
	 *  570 Common object
	 *  575 "
	 *  set ux_tstart to start of text portion
	 *
	 *  also handle #! files
	 *  the file name of the shell is specified along with an argument
	 *  these are prepended to the argument list given here
	 *
	 *  SHELL NAMES ARE LIMITED IN LENGTH
	 *  ONLY ONE ARGUMENT MAY BE PASSED TO THE SHELL FROM THE ASCII LINE
	 */
	exdata.ex_mac = 0;
	hdr = &exdata.execfile.aouthdr;
	up->u_error = vn_rdwr(UIO_READ, vp, (caddr_t)&exdata, sizeof(exdata),
			      0, UIOSEG_KERNEL, IO_UNIT, &resid);
	if (up->u_error)
		goto bad;

	if (exdata.ex_mac == 0x51600 || exdata.ex_mac == 0x51607 ||
	   (exdata.ex_shell[0] == '#' && exdata.ex_shell[1] == '!')) {
		if (indir) {
			up->u_error = ENOEXEC;
			goto bad;
		}
		indir = 1;

		if (exdata.ex_mac == 0x51600 || exdata.ex_mac == 0x51607)
		      np = "/mac/bin/launch";
		else {
		      for (cp = &exdata.ex_shell[2]; cp < &exdata.ex_shell[SHSIZE]; cp++){
			    if (*cp == '\t')
				  *cp = ' ';
			    else if (*cp == '\n') {
				  *cp = '\0';
				  break;
			    }
		      }
		      if (cp == &exdata.ex_shell[SHSIZE]) {
			    up->u_error = ENOEXEC;
			    goto bad;
		      }
		      ep = cp;

		      for (cp = &exdata.ex_shell[2]; *cp == ' '; cp++) ;

		      for (np = cp; *cp && *cp != ' '; cp++) ;

		      if (*cp) {
			    *cp = '\0';
			    
			    while (*++cp == ' ') ;

			    if (*cp)
				  bcopy(cp, eip->e_cfarg, (ep - cp) + 1);
		      }
		}
		if (vp->v_count == 1)
			vp->v_flag &= ~VTEXT;
		pn_free(&eip->e_pn);
		VN_RELE(vp);

		if (up->u_error = pn_get(np, UIOSEG_KERNEL, &eip->e_pn))
			return((struct vnode *)NULL);
		eip->e_flags |= EI_INDIR;
		goto again;
	}

	if (resid || exdata.execfile.filehdr.f_magic != 0520) {
		up->u_error = ENOEXEC;
		goto bad;
	}

	while (indx < exdata.execfile.filehdr.f_nscns)
	{
	    up->u_error = vn_rdwr(UIO_READ, vp, (caddr_t) &sbuf, SCNHSZ,
			      FILHSZ + sizeof(struct aouthdr) + indx*SCNHSZ,
			      UIOSEG_KERNEL, IO_UNIT, (int *) 0);
	    if (up->u_error)
		goto bad;


	    /*
	     * A DSECT DATA section may be a toolbox program.  If the section
	     * name is .lowmem, we assume a 32-bit toolbox process; if
	     * .low24, we assume a 24-bit process.  Note that this segment
	     * is only a place-holder (hence a DSECT).
	     */
	    switch(sbuf.s_flags)
	    {	case STYP_LIB:
/* If we have shared libraries to load, find out how many.	*/
			up->u_nlibs = sbuf.s_size / LPATHNAME;
			eip->e_libscn = sbuf.s_scnptr;
			break;

		case STYP_TEXT:
			flags |= STYP_TEXT;
			txt_ptr = sbuf.s_scnptr;
			break;

		case (STYP_DATA|STYP_DSECT):
			if (strncmp(sbuf.s_name, ".lowmem", 8) == 0)
				eip->e_flags |= EI_MAC32;
			else if (strncmp(sbuf.s_name, ".low24", 8) == 0)
				eip->e_flags |= EI_MAC24;
			break;

		case STYP_DATA:
			flags |= STYP_DATA;
			break;

		case STYP_BSS:
			flags |= STYP_BSS;
			break;
	    }
	    indx++;
	}


	if (flags != (STYP_TEXT|STYP_DATA|STYP_BSS))
	{
	    up->u_error = ENOEXEC;
	    goto bad;
	}

	p->p_flag |= SCOFF;

	if (tstart = hdr->text_start - (hdr->text_start & ~POFFMASK)) {
		hdr->tsize += tstart;
		hdr->text_start -= tstart;
		tstart = 0;
	} else 
		tstart = txt_ptr;


	/*	407 is RW nonshared text
	 *	410 is RO shared text
	 *	413 is demand fill RO shared text.
	 */

	if (hdr->magic == 0410	|| hdr->magic == 0413) {
		/*
		 * Check to make sure nobody is modifying the text right now
		 */
		if ((vp->v_flag & VTEXTMOD) != 0) {
			up->u_error  = ETXTBSY;
			goto bad;
		}
		if (((vp->v_flag & VTEXT) == 0) && vp->v_count != 1) {
			register struct file *fp;

			for (fp = file; fp < (struct file *)v.ve_file; fp++)
				if (fp->f_type == DTYPE_VNODE && fp->f_count > 0 &&
						    (struct vnode *)fp->f_data == vp &&
								 (fp->f_flag & FWRITE)) {
					up->u_error = ETXTBSY;
					goto bad;
				}
		}
		vp->v_flag |= VTEXT;
	} else if (hdr->magic == 0407) {
		hdr->dsize += hdr->tsize;
		hdr->tsize = 0;
		hdr->data_start = hdr->text_start;

		/*	The following is needed to prevent
		**	chksize from failing certain 407's.
		*/

		hdr->text_start = 0;
	} else {
		up->u_error = ENOEXEC;
		goto bad;
	}
	/* Check segment structure of hdr against the required stack */
	eip->e_stack = (eip->e_flags&EI_MAC24) ? MAC_24_BIT_STACK :
		       (eip->e_flags&EI_MAC32) ? MAC_32_BIT_STACK : 
		       AUX_USR_STACK;
	chksize(hdr, pttob(btotpt(eip->e_stack-1)));
bad:
	if (up->u_error) {
		if (vp->v_count == 1)
			vp->v_flag &= ~VTEXT;
		pn_free(&eip->e_pn);
		VN_RELE(vp);
		vp = NULL;
	} else {
		up->u_exdata = *((ufhd_t *)hdr);
		up->u_exdata.ux_tstart = tstart;
	}
	return(vp);
}

/* Load all the shared libraries. The names of these libraries are obtained from the .lib section.
   Shared libraries are loaded in the same manner as a normal executable. */

loadshlibs(lscnptr, lvp)
long *lscnptr;
register struct vnode *lvp;
{	register struct vnode *vp;
	register struct user *up;
	register struct proc *p;
	struct vnode *tvp;
	struct vattr vap;
	struct execfile {
		struct filehdr fhdr;
		struct aouthdr aouthdr;
	} execfile;
	int resid;
	int nscn;
	SCNHDR sbuf;
	long flags;
	struct pathname lpnp;
	register int data_start, dsize;
	int bsize;
	int doffset;
	int rgva;
	register int npgs;
	register reg_t *rp;
	register preg_t *prp;
	char libname[LPATHNAME];
	struct ltxtspcs ltxtspecs;
	long currlib;

	up = &u;
	currlib = *lscnptr;

	p = up->u_procp;
	while (up->u_nlibs >0)
	{	resid = 0;
		nscn = 0;
		flags = 0;
		if (up->u_error = vn_rdwr(UIO_READ, lvp, (caddr_t)libname,
				  LPATHNAME, currlib, UIOSEG_KERNEL, IO_UNIT,
				  (int *)0))
			return(1);


		if (up->u_error = pn_get((caddr_t)libname, UIOSEG_KERNEL,
					 &lpnp))
			return(1);

		if (up->u_error = lookuppn(&lpnp, FOLLOW_LINK, (struct vnode **)0, &tvp))
		{	pn_free(&lpnp);
			return(1);
		}

		vp = tvp;

		if (up->u_error = VOP_GETATTR(vp, &vap, up->u_cred))
			goto shout;

		if (up->u_error = VOP_ACCESS(vp, VEXEC, up->u_cred))
			goto shout;

		if ((p->p_flag&STRC) &&
		    (up->u_error = VOP_ACCESS(vp, VREAD, up->u_cred)))
			goto shout;

		if (vp->v_type != VREG ||
		    (vap.va_mode & (VEXEC|(VEXEC>>3)|(VEXEC>>6))) == 0) {
			up->u_error = EACCES;
			goto shout;
		}
		if (up->u_error = vn_rdwr(UIO_READ,vp, (caddr_t)&execfile,
					  sizeof(execfile), 0, UIOSEG_KERNEL, 
					  IO_UNIT, &resid))
			goto shout;

		if (resid || execfile.fhdr.f_magic != 0520)
		{	up->u_error = ENOEXEC;
			goto shout;
		}

		ltxtspecs.text_start = execfile.aouthdr.text_start;
		ltxtspecs.tsize = execfile.aouthdr.tsize;
		ltxtspecs.magic = execfile.aouthdr.magic;

		while (nscn < execfile.fhdr.f_nscns)
		{	up->u_error = vn_rdwr(UIO_READ, vp, (caddr_t) &sbuf,
			      SCNHSZ,
			      FILHSZ + nscn*SCNHSZ + sizeof(struct aouthdr),
			      UIOSEG_KERNEL, IO_UNIT, (int *) 0);
			if (up->u_error)
				goto shout;

			switch (sbuf.s_flags)
			{	case STYP_TEXT:
					flags |= STYP_TEXT;
					ltxtspecs.tstart = sbuf.s_scnptr;
					break;

				case STYP_DATA:
					flags |= STYP_DATA;
					data_start = sbuf.s_vaddr;
					dsize = sbuf.s_size;
					doffset = sbuf.s_scnptr;
					break;

				case STYP_BSS:
					flags |= STYP_BSS;
					bsize = sbuf.s_size;
					break;
			}
			nscn++;
		}
		if (flags != (STYP_TEXT|STYP_DATA|STYP_BSS))
		{	up->u_error = ENOEXEC;
			goto shout;
		}

		if (execfile.aouthdr.magic == 0410 ||
		    execfile.aouthdr.magic == 0413)
		{	if ((vp->v_flag & VTEXTMOD) != 0)
			{	up->u_error	= ETXTBSY;
				goto shout;
			}

			if (((vp->v_flag & VTEXT) == 0) && vp->v_count != 1)
			{	register struct file *fp;

				for (fp = file; fp < (struct file *)v.ve_file;
				     fp++)
					if (fp->f_type == DTYPE_VNODE &&
					    fp->f_count > 0 &&
					    (struct vnode *)fp->f_data == vp &&
					    (fp->f_flag & FWRITE))
					{	up->u_error = ETXTBSY;
						goto shout;
					}
			}
			vp->v_flag |= VTEXT;
		} else
		{	up->u_error = ENOEXEC;
			goto shout;
		}

		if (btop(ltxtspecs.tsize) + btop(dsize + bsize) + NCAPGS >
		    tune.t_maxumem)
		{	up->u_error = ENOMEM;
			goto shout;
		}

		rgva = data_start & ~L2OFFMASK;

		if (up->u_error = xalloc(vp,&ltxtspecs))
		{	up->u_error = ENOEXEC;
			goto shout;
		}

		if ((rp = allocreg(vp, RT_PRIVATE)) == NULL)
		{	up->u_error = ENOEXEC;
			goto shout;
		}

		if ((prp = attachreg(rp, p, rgva, PT_LIBDATA, Lx_RW)) == NULL)
		{	freereg(rp);
			up->u_error = ENOEXEC;
			goto shout;
		}

		if (dsize)
		{	if (execfile.aouthdr.magic == 0413)
			{	if (mapreg(prp, (caddr_t)data_start, vp,
					   doffset, dsize) < 0)
				{	detachreg(prp, p);
					up->u_error = ENOEXEC;
					goto shout;
				}
			} else if (loadreg(prp, (caddr_t)data_start, vp,
					   doffset, dsize) < 0)
			{	detachreg(prp, p);
				up->u_error = ENOEXEC;
				goto shout;
			}
		}

		if (npgs =
		    btop(data_start+dsize+bsize) - btop(data_start+dsize))
			if (growreg(prp, npgs, DBD_DZERO) < 0)
			{	detachreg(prp, p);
				up->u_error = ENOEXEC;
				goto shout;
			}

		regrele(rp);
		pn_free(&lpnp);
		VN_RELE(vp);
		up->u_nlibs--;
		currlib += LPATHNAME;
	}
	return(0);

shout:

	if (vp->v_count == 1)
		vp->v_flag &= ~VTEXT;
	VN_RELE(vp);
	pn_free(&lpnp);
	return(1);
}

chksize(hdr, stack)
register struct aouthdr *hdr;
register unsigned int stack;
{

	/*	Check that the text, data, and stack segments
	 *	are all non-overlapping.
	 */

	if ((hdr->text_start + hdr->tsize) > hdr->data_start  ||
	    (hdr->data_start + hdr->dsize + hdr->bsize) > stack) {
		u.u_error = ENOMEM;
		return;
	}
	if (btop(hdr->tsize) + btop(hdr->dsize + hdr->bsize) + NCAPGS > tune.t_maxumem)
		u.u_error = ENOMEM;
}


/*
 * Read in and set up memory for executed file.
 */
getxfile(vp, nargc, uid, gid, libscn)
register struct vnode *vp;
int nargc, uid, gid;
long *libscn;
{
	register int	size, npgs, base;
	register int	execself;
	register reg_t	*rp;
	register preg_t	*prp;
	register struct user *up;
	register int	flag;
	struct proc	*p;
	int		rgva;
	int		offset;
	register preg_t *textprp;
	int		(**fptr)();
	extern int	(*execfunc[])();

	up = &u;
	p = up->u_procp;

	for (fptr = execfunc; *fptr; fptr++)
		(**fptr)(up);
	up->u_prof.pr_scale = 0;

	/*	We must check for the special case of a process
	**	exec'ing itself.  In this case, we skip
	**	detaching the regions and then attaching them
	**	again because it causes deadlock problems.
	*/

	prp = findpreg(p, PT_TEXT);

	if (prp && prp->p_reg->r_vptr == vp)
	{	if (p->p_flag&SMAC24)
		{	up->u_error = EINVAL;
			return;
		}
		execself = 1;
	} else {
		execself = 0;

		/*	We must unlock the vnode for the
		**	file we are about to execute before
		**	detaching the regions of the current
		**	file.  If we don't, we could get an
		**	a-b, b-a deadlock problem.
		*/

		vp->v_flag |= VTEXT;
	}

	/*
	 * shred_24bit() puts us into the 32-bit world (leave the TEXT,
	 * DATA alone).  Call only if currently a 24-bit process, which
	 *  we infer by the use of the p_rootxx entries.
	 */
	if (p->p_root24)
		shred_24bit(p, execself);

	/*	Loop through all of the regions but handle the text and data
	**	specially if we are exec'ing ourselves.
	*/
	prp = p->p_region;

	/* Note that detachreg() moves the regions down */
	while (rp = prp->p_reg) {
		register unsigned int i, j, sp;
		register Dt_t *L2p;

		flag = prp->p_flags&(PF_MAC24|PF_MAC32);
		if (execself) {
			if (prp->p_type == PT_TEXT) {
				/*	Just skip the text region if
				 **	we are exec'ing ourselves.
				 */
				prp->p_flags &= ~(PF_MAC24|PF_MAC32);
				textprp = prp;
				prp++;
				continue;
			}
			if (prp->p_type == PT_DATA) {
				/*	Give up all of the data space since
				 **	it may have been modified.  We cannot
				 **	detach from the data region because
				 **	this will unlock the vnode.
				 **	Invalidate the L2 pointers here.
				 */

				/* Get table shape info before zapping
				 *  the region.
				 */
				j = ptoL2(rp->r_pgsz);
				sp = (unsigned int)prp->p_regva;

				reglock(rp);
				(void) growreg(prp, - rp->r_pgsz, DBD_DFILL);
				regrele(rp);

				/*
				 * For 24-bit DATA, this region is shared with
				 *  the 32-bit world.  We need to invalidate
				 *  L2 ptrs in both worlds (we know we are in
				 *  32-bit world).
				 */
				for (i = 0; i < j; i++, sp += L2tob(1))
				{	L2p = L2_ent(p->p_root, sp);
					Dt_invalidate(L2p);
					if (flag)
					{	L2p = L2_ent(p->p_root24, sp);
						Dt_invalidate(L2p);
						Dt_invalidate(L2p+NL2TBL/2);
					}
				}
				prp->p_flags &= ~(PF_MAC24|PF_MAC32);
				prp++;
				continue;
			}
		}
		reglock(rp);
		detachreg(prp, p);
	}

	/*
	 * The 32-bit tree now becomes the basis for the new page table
	 */
	if (p->p_root32)
	{	if (!execself)
			shred_32bit(p, 0);
		p->p_flag &= ~SROOT32;
		p->p_root32 = 0;
		MMU_sync();
	}

	bzero((caddr_t) up->u_phys, v.v_phys * sizeof(struct phys));
	
	if (!execself) {
		if (up->u_exdata.ux_mag == 0407)
			vp->v_flag &= ~VTEXT;
	}
	clratb(USRATB);

	offset = up->u_exdata.ux_tstart + up->u_exdata.ux_tsize;

/* Load shared libraries if any. */
	if ((up->u_nlibs > 0) &&
	    (loadshlibs(libscn,vp)))
		goto out;

	/*	Load text region.  Note that if xalloc
	**	returns an error, then it has already
	**	done an pn_free.
	**/

	if (!execself)
		if (xalloc(vp,NULL))
		{	up->u_error = ENOEXEC;
			goto out;
		}

	/*	Allocate the data region.
	 */

	base = up->u_exdata.ux_datorg;
	size = up->u_exdata.ux_dsize;
	/* Align (somewhat arbitrarily) the data on an L2 boundary */
	rgva = base & ~L2OFFMASK;

	if (execself) {
		prp = findpreg(p, PT_DATA);
		rp = prp->p_reg;
		reglock(rp);
	} else if ((rp = allocreg(vp, RT_PRIVATE)) == NULL)
	{	up->u_error = ENOEXEC;
		goto out;
	}

	/*	Attach the data region to this process.
	 */
	
	if (!execself && (prp = attachreg(rp, p, rgva, PT_DATA, Lx_RW)) == NULL) {
		freereg(rp);
		up->u_error = ENOEXEC;
		goto out;
	}

	/*
	 * Load data region
	 */

	if (size) {
		if (up->u_exdata.ux_mag == 0413) {
			if (mapreg(prp, (caddr_t)base, vp, offset, size) < 0) {
				detachreg(prp, p);
				up->u_error = ENOEXEC;
				goto out;
			}
		} else {
			if (loadreg(prp, (caddr_t)base, vp, offset, size) < 0) {
				detachreg(prp, p);
				up->u_error = ENOEXEC;
				goto out;
			}
		}
	}

	/*
	 * Allocate bss as demand zero
	 */
	if (npgs = btop(base + size + up->u_exdata.ux_bsize) - btop(base + size)) {
		if (growreg(prp, npgs, DBD_DZERO) < 0) {
			detachreg(prp, p);
			up->u_error = ENOEXEC;
			goto out;
		}
	}
	regrele(rp);

	/*	Allocate a region for the stack and attach it to
	 *	the process.
	 */

	if ((rp = allocreg((struct vnode *)NULL, RT_PRIVATE)) == NULL)
	{	up->u_error = ENOEXEC;
		goto out;
	}

	if ((prp = attachreg(rp, p, p->p_stack, PT_STACK, Lx_RW)) == NULL) {
		freereg(rp);
		up->u_error = ENOEXEC;
		goto out;
	}
	
	/*	Grow the stack but don't actually allocate
	 *	any pages.
	 */
	
	npgs = SSIZE + btop(nargc);
	if (growreg(prp, npgs, DBD_DZERO) < 0) {
		detachreg(prp, p);
		up->u_error = ENOEXEC;
		goto out;
	}
	regrele(rp);

	if ((p->p_flag&SMAC24) && (up->u_error = check_shape(p)))
		goto out;

	/*
	 * set SUID/SGID protections, if no tracing
	 */
	if ((p->p_flag&STRC)==0) {
		if (uid != up->u_uid || gid != up->u_gid)
			up->u_cred = crcopy(up->u_cred);
		up->u_uid = uid;
		up->u_gid = gid;
		p->p_suid = up->u_uid;
		p->p_sgid = up->u_gid;
	} else
		psignal(p, SIGTRAP);

	return;

out:
	/*	We get here only for an error.	The vnode
	**	ip is unlocked.	 We may have regions attached
	**	which we must detach.  Note that we again
	**	rely on the compacting of detachreg.
	**/

	prp = p->p_region;
	while (rp = prp->p_reg) {
		reglock(rp);
		detachreg(prp, p);
	}
}

/*
 * This is a 24-bit Mac/Toolbox ap; assure that we have set up a tree (page
 *  table) that is 24-bit pure.  If all is kosher, craft a 32-bit tree from
 *  the 24-bit one and save it away.
 * Note: at this point, we only have text, data+bss, and stack (p)regions.
 *  We share these with the 32-bit world implicitly, so they are marked as
 *  both 24- and 32-bit pregions (at attach
 */
check_shape(p)
register struct proc *p;
{	register int i;
	register struct pregion *prp;

	p->p_root24 = p->p_root;
	p->p_flag |= SROOT24;

	/*
	 * First, mark all the current regions as 24-bit.  They are already
	 *  32-bit, so they will be seen as shared.
	 */
	for (prp = p->p_region; prp->p_reg; prp++)
		prp->p_flags |= PF_MAC24;

	/*
	 * Now, replicate the 0th L1 entry.  If any other is valid,
	 *  be a nice guy and free it.  It can't be used, and presumably,
	 *  all attached L2 pointers have been invalidated.
	 */
	{	register long *lp;
		register Dt_t *L1p, *L2p;

		L1p = p->p_root;
		lp = (long *)L1p++;
		for (i = 1; i++ < NL1TBL; L1p++)
		{	if (Lx_valid(L1p))
				ptblfree(DT_addr(L1p), 2);
			*(long *)L1p = *lp;
		}
	}
	/* Next, look at the 0th L2 table; this must be folded in half (the
	 *  higher half duplicating the lower).  This time, grouse if
	 *  anything in that top half is valid.
	 */
	{	register Dt_t *L2p, *L2q;

		L2p = DT_addr(p->p_root);
		/* Point to middle of table */
		L2q = (Dt_t *)((long *)L2p + NL2TBL/2);
		for (i=0; i++ < NL2TBL/2;)
		{	if (Lx_valid(L2q))
				return(ENOEXEC);
			*(long *)L2q++ = *(long *)L2p++;
		}
	}
	/* Finally, set up a 32-bit table for SwapMMUMode() action.  This
	 *  must duplicate the address space set up for the 24 bit version,
	 *  but provide access to the full 32-bit address space for the
	 *  larger slot spaces (16, 256 MB).  It is the process's
	 *  responsibility to keep the two spaces in sync after this.
	 */
	{	register Dt_t *L1p, *L2p, *L2q;

		L1p = (Dt_t *)ptblalloc(2);
		p->p_root32 = L1p;
		L2p = (Dt_t *)ptblalloc(2);
		wtl1e(L1p, L2p, Lx_RW);		/* Install new L2 table */
		L2q = DT_addr(p->p_root24);
		/* Duplicate low half of 24-bit L2 table */
		for (i=0; i++ < NL2TBL/2;)
			*(long *)L2p++ = *(long *)L2q++;
	}
	return(0);
}

/* <@(#)exec.c	1.5> */
