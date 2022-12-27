#ident	"@(#)pipe.c	2.1 10/13/89 19:27:39 "
/*
 *	
 *	Copyright (c) 1988 UNISOFT LIMITED 
 *	ALL RIGHTS RESERVED 
 *	
 *	THIS SOFTWARE PRODUCT CONTAINS THE UNPUBLISHED SOURCE
 *	CODE OF UNISOFT LIMITED
 *	
 *	The copyright notices above do not evidence any actual 
 *	or intended publication of such source code.
 *	
 *	UniSoft Ltd.
 */

#include "sys/param.h"
#include "sys/types.h"
#include "sys/time.h"
#include "sys/mmu.h"
#include "sys/page.h"
#include "sys/region.h"
#include "sys/systm.h"
#include "sys/signal.h"
#include "sys/buf.h"
#include "svfs/filsys.h"
#include "sys/proc.h"
#include "sys/user.h"
#include "sys/errno.h"
#include "sys/vnode.h"
#include "svfs/inode.h"
#include "sys/file.h"
#include "sys/vfs.h"
#include "sys/mount.h"
#include "sys/var.h"
#include "specfs/snode.h"
#include "specfs/fifonode.h"

extern  struct fileops vnodefops;

pipe()
{
        register struct file *rf, *wf;
        register struct mount *mp;
        register struct user *up;
        struct vnode *pvp, *vp, *newvp;
        int r, w;
        struct vnode *specvp();

        up = &u;
        if ((rf = falloc(0)) == NULL)
                return;
        r = up->u_rval1;
        /* If 'pipedev' isn't mounted just now we use 'rootdev'. */
        if ((mp = getmp(pipedev)) == NULL)
                mp = getmp(rootdev);
        if (VFS_ROOT(mp->m_vfsp, &vp)) {
                wf = NULL;
                goto errs;
        }
        if ((wf = falloc(0)) == NULL) {
                goto errs;
        }
        w = up->u_rval1;
        if ((pvp = VOP_MKFIFO(vp)) == NULL) {
                VN_RELE(vp);
                goto errs;
        }
        VN_RELE(vp);
        rf->f_flag = FREAD;
        rf->f_type = DTYPE_VNODE;
        rf->f_ops = &vnodefops;
        wf->f_flag = FWRITE | FAPPEND;
        wf->f_type = DTYPE_VNODE;
        wf->f_ops = &vnodefops;
        up->u_rval2 = w;
        up->u_rval1 = r;
        pvp->v_type = VFIFO;
        /*
         * Get special fifonode and then do fifo_open
         */
        newvp = specvp (pvp, NODEV);
        VTOF(newvp)->fn_flag |= FIFO_5PIPE;
        VN_RELE(pvp);
        rf->f_data = (caddr_t)newvp;
        wf->f_data = (caddr_t)newvp;
        fifo_open (&newvp, FREAD|FWRITE, NULL);
        newvp->v_count = 2;
        return;
errs:
        if(wf != NULL) {
                up->u_ofile[w] = NULL;
                crfree(wf->f_cred);
                wf->f_count = 0;
        }
        up->u_ofile[r] = NULL;
        crfree(rf->f_cred);
        rf->f_count = 0;
        return;
}
