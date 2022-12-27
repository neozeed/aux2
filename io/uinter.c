#ifndef lint	/* .../sys/psn/io/uinter.c */
#define _AC_NAME uinter_c
#define _AC_NO_MAIN "@(#) Copyright (c) 1987 Apple Computer, Inc., All Rights Reserved.  {Apple version 2.45 90/05/01 01:20:06}"
#include <apple_notice.h>

#ifdef _AC_HISTORY
static char *sccsid = "@(#)Copyright Apple Computer 1989\tVersion 2.45 of uinter.c on 90/05/01 01:20:06";
#endif		/* _AC_HISTORY */
#endif		/* lint */

#define _AC_MODS
/*	@(#)uinter.c	*/
/*
 *	This is the user interface 'driver'
 */

#include <sys/param.h>
#include <sys/types.h>
#include <sys/sysmacros.h>
#include <sys/signal.h>
#include <sys/errno.h>
#include <sys/dir.h>
#include <sys/time.h>
#include <sys/uio.h>
#include <sys/ioctl.h>
#include <mac/types.h>
#include <mac/quickdraw.h>
#include <mac/segload.h>
#include <mac/events.h>
#include <mac/osutils.h>
#include <mac/files.h>
#include <mac/devices.h>
#include <sys/video.h>
#include <sys/uinter.h>
#include <sys/mouse.h>
#include <sys/key.h>
#include <sys/mmu.h>
#include <sys/page.h>
#include <sys/region.h>
#include <sys/pfdat.h>
#include <sys/proc.h>
#include <sys/user.h>
#include <sys/tuneable.h>
#include <sys/systm.h>
#include <sys/debug.h>
#include <sys/var.h>
#include <sys/module.h>
#include <sys/ipc.h>
#include <sys/via6522.h>		/* for SLOT_LO and SLOT_HI */
#include <sys/reboot.h>

/*
 *	This define helps the compiler to produce slightly better
 *		code for the case where the only possible device number is 0.
 *		For now, this is always true.
 */

#if NDEVICES == 1
#   define DEV	0
#else
#   define DEV	dev
#endif NDEVICES

#define BKSP_CHR 0x08
#define DELETE_CHR 0x7f

#define UI_PRI     (PINTR | 0)
#define KROMADR    0x40000000
#define MAXLOWADDR 0x2000
#define ABRTOFFSET 0x0fff
#define CURPIDOFF  0x0ff8
#define LOCKFLAGS  0x10                /* couldn't include buf.h */

extern union ptbl *segvptbl();
extern ui_exit();
extern ui_display();
extern ui_keyboard();
extern ui_mouse();
extern wakeup();
extern setrun();
extern lineAVector();
extern lineAFault();
extern int selwait;
extern unsigned char transData[][];	/* for KeyTrans() support */
extern unsigned char kmapData[NDEVICES][128];  /* for virtual keycode support */
extern int	key_trans_state[];	/* must be saved between calls to KeyTrans() */
extern int (*postDIroutine)();		/* func ptr to the floppy disk insert routine */
extern int noUIdriver();
extern struct kernel_info *kernelinfoptr;


/*
 *	bit masks used for encoding events
 */
static long ui_mask[] = {
	0x0001,	0x0002,	0x0004,	0x0008,	0x0010,	0x0020,	0x0040,	0x0080,
	0x0100,	0x0200,	0x0400,	0x0800,	0x1000,	0x2000,	0x4000,	0x8000,
	0x00010000, 0x00020000, 0x00040000, 0x00080000,
	0x00100000, 0x00200000, 0x00400000, 0x00800000,
	0x01000000, 0x02000000, 0x04000000, 0x08000000,
	0x10000000, 0x20000000, 0x40000000, 0x80000000
};

static Rect ui_bigrect = {-32000,-32000,32000,32000};
					/* For now, we are restricted to using */
					/* one video device.  We choose the one */
					/* in the lowest slot. */
static struct video *ui_vp;			/* pointer to our video device */

typedef int (*procedure_t)();
static procedure_t ui_vtrace[NDEVICES];		/* the old vtrace routine */
static unsigned char ui_cursor[NDEVICES];	/* sais cursor is 'on' */
static unsigned char ui_devices[NDEVICES];	/* sais devices are 'on' */
static unsigned char ui_key_open[NDEVICES];	/* the key state before we */
static unsigned long ui_key_mode[NDEVICES];	/* opened it */
static unsigned long ui_key_intr[NDEVICES];

static unsigned char ui_mouse_open[NDEVICES];	/* the mouse state before we */
static unsigned long ui_mouse_mode[NDEVICES];	/* opened it */
static unsigned long ui_mouse_intr[NDEVICES];

static char ui_inited[NDEVICES];		/* is this device is inited? */
static unsigned char ui_active[NDEVICES];	/* active layer for device */
static struct ui_interface *ui_addr[NDEVICES];	/* the user interface struct */
static char *ui_uaddr[NDEVICES];
static struct layer ui_layer[NDEVICES][NLAYERS];/* The layers */
static short ui_mx[NDEVICES];			/* For calc. deltas for mouse */
static short ui_my[NDEVICES];			/*    movement */

extern caddr_t ui_lowaddr;


ui_open(dev)
register dev_t dev;
{
    extern struct video *video_index[];
    register int slot;
    void ui_update();
    int  postDIevent();
    extern void (*ui_callout)();

    dev = minor(dev);
    if (dev >= NDEVICES || dev >= video_count)	/* make sure the */
	return EINVAL;				/*   'device' exists */
						/*   interface, fail */
    if (!ui_inited[DEV]) {			/* init device-sepecific stuff*/
	register int i;
	register struct layer *lp;

	if (ui_callout == NULL)
	    ui_callout = ui_update;

	ui_inited[DEV] = 1;
	ui_active[DEV] = NOLAYER;
	for (i = 0,lp = &ui_layer[DEV][0]; i < NLAYERS; i++,lp++)
	    lp->l_state.state = LS_EMPTY;
#ifndef ACFG_SELECT	/* if autoconfig can't handle special select routine */
	{
#include <sys/conf.h>
	extern int ui_select();
	register struct cdevsw *ptr;

	for (ptr = cdevsw; ptr->d_open != ui_open; ptr++)
	    ;
	ptr->d_select = ui_select;
	}
#endif ACFG_SELECT
	for (slot = 0; slot <= SLOT_HI; slot++) {
	    if (slot == 1)
		slot = SLOT_LO;
	    if (video_index[slot] != 0) {
		ui_vp = video_index[slot];
		break;
	    }
	}
    }
    if ( !(u.u_user[1] & UI_FLAG))		    /* if not already connected */
        u.u_user[1] = UI_FLAG|UI_DL(DEV)|NOLAYER;   /* label our process  as */
						    /* connected to this device */
    postDIroutine = postDIevent;		    /* enable the real thing /

    return 0;
}

ui_close(dev)
{
    postDIroutine = noUIdriver;		/* disable DI event postings */
    return 0;
}

/*
 *	reading is not supported
 */

ui_read(dev,uio)
struct uio *uio;
{
    return EINVAL;
}

/*
 *	writing is not supported
 */

ui_write(dev, uio)
struct uio *uio;
{
    return EINVAL;
}

/*
 *	All the user interface action is done via ioctls
 */

ui_ioctl(dev, cmd, addr, arg)
int dev;
register caddr_t addr;
{
    register int s, i;

#if NDEVICES != 0
    dev = minor(dev);
#endif NDEVICES


    if (cmd == UI_GETOSEVENT)
        {
	/*
	 * return the next event available from the event queue
	 */

        return ui_getevent((struct getosevent *) addr);
        }

    if (cmd == UI_TIMER)
        {
	register struct layer *lp;
	int ui_catch_timer();

	if (!ui_devices[DEV] || (i = UI_LAYER(u.u_user[1])) == NOLAYER)
	    return EINVAL;
	lp = &ui_layer[DEV][i];

	i = *(int *)addr;
	if (lp->l_tproc)
	    *(int *)addr = untimeout(ui_catch_timer, lp);
	else
	    *(int *)addr = lp->l_tremain;
	if (i == 0)
	    lp->l_tproc = NULL;
	else {
	    lp->l_tproc = u.u_procp;
	    timeout(ui_catch_timer, lp, i);
	}
	lp->l_tremain = 0;

	return(0);
        }

    if (cmd == UI_SWITCH)
        {
	register struct layer *lp;
	register struct attach *atp;
	register struct attach *catp;
	struct attach *ui_findtask();

	if ((i = UI_LAYER(u.u_user[1])) == NOLAYER)
	    return EINVAL;
	lp = &ui_layer[DEV][i];

	if ((s = *(int *)addr) < 0)
	    s = -s;

	if ((atp = ui_findtask(lp, s)) == NULL)
	    return EINVAL;
	if ((catp = lp->l_active) == atp)
	    return(0);
	lp->l_active = atp;
	if (ui_addr[DEV]) {
	    *(unsigned int *)((char *)ui_addr[DEV] + CURPIDOFF) = atp->pid;
	}
        ui_wakeup(atp);

	if (*(int *)addr < 0) {
	    if (u.u_procp->p_sigignore&(sigmask(SIGTERM)))
	        psignal(u.u_procp, SIGKILL);
	    else
	        psignal(u.u_procp, SIGTERM);
	    return(0);
	}
	return(ui_sleep(lp, catp));
        }

    if (cmd == UI_SLEEP)
        {
	register struct layer *lp;
	register struct attach *atp;

	if ((i = UI_LAYER(u.u_user[1])) == NOLAYER)
	    return EINVAL;
	lp = &ui_layer[DEV][i];
	atp = &lp->l_attached[UI_GETID(u.u_user[1])];

	if (atp == lp->l_active)
	    return(0);
	return(ui_sleep(lp, atp));
        }

    if (cmd == UI_SELECT)
        {
	register struct layer *lp;

	if ((i = UI_LAYER(u.u_user[1])) == NOLAYER)
	    return EINVAL;
	lp = &ui_layer[DEV][i];
	    
	lp->l_active->select = *(struct select *)addr;

	if ((unsigned int)(i = lp->l_active->select.nfd) > (unsigned int)NOFILE)
	    return EBADF;
	i = (1 << i) - 1;
	lp->l_active->select.mask[0] &= i;
	lp->l_active->select.mask[1] &= i;
	lp->l_active->select.mask[2] &= i;

	return(0);
        }

    if (cmd == UI_FIND_EVENT)
        {
	/*
	 * return the next event available from the event queue
	 */

	return(ui_findevent((struct findevent *) addr));
        }

    if (cmd == UI_POSTEVENT)
        {
	/*
	 * This posts an event to the layer associated with the
	 *	current process
	 */

	return(ui_postevent(DEV,UI_LAYER(u.u_user[1]),
		    ((struct postevent*)addr)->eventCode,
		    ((struct postevent*)addr)->eventMsg,
		    0, 0));
        }

    if (cmd == UI_POST_MOD)
        {
	/*
	 * This posts an event and it's modifiers to the layer
	 *	associated with the current process
	 */

	return(ui_postevent(DEV,UI_LAYER(u.u_user[1]),
		    ((EventRecord *)addr)->what,
		    ((EventRecord *)addr)->message,
		    1, ((EventRecord *)addr)->modifiers));
        }

    if (cmd == UI_SETEVENTMASK)
        {
	/*
	 * set the current layer's event mask
	 */

	if ((i = UI_LAYER(u.u_user[1])) == NOLAYER)
	    return EINVAL;
	ui_layer[DEV][i].l_mask = (*(short *)addr) | AUX_EVENT_MASK;

	return(0);
        }

    if (cmd == UI_FLUSHEVENTS)
        {
	/*
	 * This flushes events from the current processes
	 *	layer's queue
	 */

	return ui_flushevents(DEV,((struct flushevents *)addr)->eventMask,
				      ((struct flushevents *)addr)->stopMask);
        }


    switch(cmd)
	{

        case UI_PHYS:
	    {
	    register struct layer *lp;
	    register struct scrninfo *si;
	    register struct ui_phys *phys;
	    register char *beg;
	    register char *end;
	    register int slot;
	    extern struct video *video_index[];

	    if ((i = UI_LAYER(u.u_user[1])) == NOLAYER)
	        return EINVAL;
	    lp = &ui_layer[DEV][i];
	    
	    phys = (struct ui_phys *)addr;
	    beg = phys->paddr;
	    end = beg + phys->size;

	    if (end <= beg)
	        return(EINVAL);

	    if (lp->l_romaddr)
	        {
	        if (beg >= (char *)KROMADR && end <= ((char *)KROMADR + ui_sizerom()))
		      goto addrok;
		}
	    for (si = lp->l_screen; si < &lp->l_screen[NSCREENS]; si++)
	        {
	        if (si->vaddr)
		    {
			/* Compute requested slot, based on user's vaddr.  If
			 *  it matches the slot of a screen we have, and the
			 *  size is not too absurd, then try to phys it in.
			 */
		    if ((((((slot = 
			     ((unsigned long)phys->paddr & 0xf0000000)>>28) 
			    >= SLOT_LO &&  slot <= SLOT_HI) ||
			   ((slot = 
			     ((unsigned long)phys->paddr & 0x0f000000)>>24) 
			    >= SLOT_LO && slot <= SLOT_HI) || 
			   ((slot = 
			     ((unsigned long)phys->paddr & 0x00f00000)>>20) 
			    >= SLOT_LO && slot <= SLOT_HI)) && 
			  (video_index[slot] != 0))
			 || (video_index[0] != 0)) && 
			(phys->size <= 0x1000000))
		        {
			if (phys->paddr == 0)
			    beg = 0;
			else
			    beg = si->paddr;
			goto addrok;
		        }
		    }
	        }
	    return(EINVAL);
addrok:
	    if (ui_phys(phys->vaddr, phys->size, beg) == -1)
	        return(EINVAL);
	    break;
	    }

        case UI_ATTACHLAYER:
	    {
	    register struct layer *lp;
	    register struct attach *atp;
	    register struct attachInfo *ai;
	    register int n;
	    int ui_findfreetask();

	    if (UI_GETID(u.u_user[1]))
	        return EBUSY;
	    ai = (struct attachInfo *)addr;

	    if ((i = ai->layerid) >= NLAYERS || i < 0)
	        return EINVAL;
	    lp = &ui_layer[DEV][i];

	    if (lp->l_state.state != LS_INUSE)
	        return EAGAIN;

	    if ((lp->l_attached[1].procp->p_flag & SMAC24) != 
		              (u.u_procp->p_flag & SMAC24))
	        return EACCES;

	    if ((s = ui_findfreetask(lp)) == 0)
	        return EINVAL;
	    atp = &lp->l_attached[s];

	    if (lp->l_romaddr) {
	        if ((atp->romid = ui_phys(lp->l_romaddr,ui_sizerom(),KROMADR)) == -1)
		    return EINVAL;
	    }
	    for (n = 0; n < NSCREENS; n++) {
	        if (lp->l_screen[n].vaddr) {
		    atp->screenid[n] = ui_phys(lp->l_screen[n].vaddr,
					       lp->l_screen[n].size,
					       lp->l_screen[n].paddr);
		    if (atp->screenid[n] == -1) {
		          ui_ioctl(DEV, UI_UNROM, 0, 0);
		          ui_ioctl(DEV, UI_UNSCREEN, 0, 0);
		          return EINVAL;
		    }
		}
	    }
	    lp->l_refcnt++;
	    atp->procp = u.u_procp;
	    atp->pid = u.u_procp->p_pid;
	    atp->select.nfd = 0;

	    if (lp->l_gofptr) {
	        u.u_gofile  = lp->l_gofptr;
		u.u_gpofile = lp->l_gpfptr;
	    }
	    u.u_user[1] = UI_SETID(s)|UI_FLAG|UI_DL(DEV)|i;
	    u.u_procp->p_nice = 0;       /* really bump up our priority */

	    ui_postauxevent(DEV, lp, attachEvt, atp->pid, ai->size, ai->flags);
	    return(ui_sleep(lp, atp));
	    }

	case UI_SHMID:
	    if ((i = UI_LAYER(u.u_user[1])) == NOLAYER)
		return EINVAL;
	    ui_layer[DEV][i].l_shmid = *(int *)addr;
	    break;

	case UI_REBOOT:
	    sys_shutdown(RB_AUTOBOOT | RB_KILLALL| RB_UNMOUNT);
	    break;

	case UI_SHUTDOWN:
	    sys_shutdown(RB_HALT | RB_KILLALL| RB_UNMOUNT);
	    break;

	case UI_KILLMYLAYER:
	    {
	    register struct layer *lp;

	    if ((i = UI_LAYER(u.u_user[1])) == NOLAYER)
		return EINVAL;
	    lp = &ui_layer[DEV][i];

	    ui_terminate(lp, UI_GETID(u.u_user[1]));
	    break;
	    }

	case UI_SYNC:
	    {
	    int wakeup();

	    if ((i = *(int *)addr) >= NLAYERS || i < 0)
	        return EINVAL;
	    if (ui_active[DEV] != i)
	        {
		timeout(wakeup, &ui_active[DEV], 120 * HZ);
	        sleep(&ui_active[DEV], PZERO+1);
		untimeout(wakeup, &ui_active[DEV]);

		if (ui_active[DEV] != i)
		    return EAGAIN;
	        }
	    break;
	    }

	case UI_TEST:
	    {
	    register struct layer *lp;

	    if ((i = *(int *)addr) >= NLAYERS || i < 0)
	        return EINVAL;
	    lp = &ui_layer[DEV][i];

	    if (lp->l_state.state != LS_EMPTY)
	        return EEXIST;
	    break;
	    }

	case UI_ATTACHGFD:
	    {
	    register struct layer *lp;

	    if ((i = UI_LAYER(u.u_user[1])) == NOLAYER)
		return EINVAL;
	    lp = &ui_layer[DEV][i];

	    if (lp->l_gofptr == NULL) {
	        lp->l_gofptr = (struct file **)getmem(GNOFILE*sizeof(struct file *));
		lp->l_gpfptr = (char * )getmem(GNOFILE*sizeof(char *));
		bzero(lp->l_gofptr, GNOFILE * sizeof(struct file *));
		bzero(lp->l_gpfptr, GNOFILE * sizeof(char *));
	    }
	    u.u_gofile  = lp->l_gofptr;
	    u.u_gpofile = lp->l_gpfptr;
	    break;
	    }

	case UI_GETVERSION:

	   /*
	    *	Return driver version number
	    */

	    u.u_rval1 = UI_VERSION;
	    break;

	case UI_SET:
	    /*
	     *	Set up a lineA trap handler
	     */
	    
	    {
	    register struct layer *lp;

	    if ((i = UI_LAYER(u.u_user[1])) == NOLAYER)
		return EINVAL;
	    lp = &ui_layer[DEV][i];
	    
	    if (ui_lowaddr == (caddr_t)-1) {
	        if (useracc(0, MAXLOWADDR, LOCKFLAGS) == 0)
		    return EINVAL;
		if ((ui_lowaddr = (caddr_t)realvtop(FCUSERD, 0)) == (caddr_t)-1) {
		    undma(0, MAXLOWADDR, LOCKFLAGS);
		    return EINVAL;
		}
		lp->l_prevtime = time.tv_sec;
	    }
	    u.u_user[0] = (int)lineAVector;
	    *(int *)0x28 = (int)lineAVector;
	    break;
	    }
	    
	case UI_CLEAR:
	    
	    /*
	     *	Clear a lineA trap handler
	     */
	    
	    if (UI_LAYER(u.u_user[1]) == NOLAYER)
		return EINVAL;

	    u.u_user[0] = (int)lineAFault;
	    *(int *)0x28 = (int)lineAFault;

	    ui_lowaddr = (caddr_t)-1;
	    break;
	    
	case UI_SLOTSCREEN:
	    
	   /*
	    *	map in the screens associated with a 'device' to the
	    *		address space requested by the process (this
	    *		must be on a segment boundary)
	    *	TODO:
	    *		1) Try to restrict physed area to frame buffer
	    */
	   
	    {
	    register struct layer *lp;
	    register struct slot_screens *screens;
	    register struct video *vp;
	    register struct attach *atp;
	    register long address;
	    register long paddr;
	    register int slot;
	    extern struct video *video_index[];
	    extern int rbv_monitor;

	    if ((i = UI_LAYER(u.u_user[1])) == NOLAYER)
		return EINVAL;
	    if ((s = UI_GETID(u.u_user[1])) == 0)
	        return EINVAL;
	    lp = &ui_layer[DEV][i];
	    atp = &lp->l_attached[s];

	    screens = (struct slot_screens *) addr;

	    for (i = 0; i < NSCREENS; i++) {
		if (lp->l_screen[i].vaddr)
		    return EINVAL;		/* screens are already mapped */
		else
		    screens->screens[i].dCtlSlot = -1;
	    }
	    address = (long)screens->base_address;

	    for (i = 0, slot = 0; slot <= SLOT_HI; slot++) {
		if (slot == 1)
		    slot = SLOT_LO;
		if ((vp = video_index[slot]) != 0) {
		    address = ((long)screens->base_address
					 + ((slot ? slot : 0xB) << screens->slot_shift));

		    if (rbv_monitor && slot == 0) {
		        s = 320 * 1024;
			paddr = 0;
		    } else {
			if (u.u_procp->p_flag & SMAC24)
			    s = 1024 * 1024;
			else
			    s = 16 * 1024 * 1024;
			paddr = (long)vp->video_base;
		    }
		    if ((atp->screenid[i] = ui_phys(address, s, paddr)) != -1) {
		        lp->l_screen[i].vaddr = (caddr_t)address;
			lp->l_screen[i].paddr = (caddr_t)paddr;
			lp->l_screen[i].size  = s;
			screens->screens[i].dCtlDevBase = (long) address;
			screens->screens[i].dCtlSlot = slot;
			screens->screens[i].dCtlSlotId = vp->dce.dCtlSlotId;
			screens->screens[i].dCtlExtDev = vp->dce.dCtlExtDev;
		    }
		    i++;
		    break;			/* Only need one screen for now */
		}
	    }
	    break;
	    }

	case UI_UNSCREEN:
	   /*
	    *	Unmap the screen.
	    */

	    {
	    register struct layer *lp;
	    register struct attach *atp;

	    if ((i = UI_LAYER(u.u_user[1])) == NOLAYER)
		return EINVAL;
	    if ((s = UI_GETID(u.u_user[1])) == 0)
	        return EINVAL;
	    lp = &ui_layer[DEV][i];
	    atp = &lp->l_attached[s];

	    for (i = 0; i < NSCREENS; i++) {
		if (atp->screenid[i] != -1) {
		    dophys(atp->screenid[i], 0, 0, 0);
		    atp->screenid[i] = -1;
		}
	    }
	    for (i = 1; i < NATTACHES; i++) { 
	        for (s = 0; s < NSCREENS; s++) {
		    if (lp->l_attached[i].screenid[s] != -1)
		        break;
		}
		if (s >= NSCREENS)
		    break;
	    }
	    if (i >= NATTACHES) {
	        for (i = 0; i < NSCREENS; i++)
		    lp->l_screen[i].vaddr = NULL;
	    }
	    return u.u_error;
	    }

	case UI_ROM:
	   /*
	    *	map in the ROM to the address space requested by the
	    *		process (this must be on a segment boundary)
	    */

	    {
	    register struct layer *lp;

	    if ((i = UI_LAYER(u.u_user[1])) == NOLAYER)
		return EINVAL;
	    lp = &ui_layer[DEV][i];
	    if ((i = UI_GETID(u.u_user[1])) == 0)
	        return EINVAL;
	    if (lp->l_romaddr)
		return EINVAL;
	    lp->l_attached[i].romid = ui_phys(*(caddr_t *)addr,ui_sizerom(),KROMADR);
	    if (lp->l_attached[i].romid == -1)
		return EINVAL;
	    lp->l_romaddr = *(caddr_t *)addr;
	    break;
	    }

	case UI_UNROM:
	   /*
	    *	unmap the ROM
	    */

	    {
	    register struct layer *lp;
	    register struct attach *atp;

	    if ((i = UI_LAYER(u.u_user[1])) == NOLAYER)
		return EINVAL;
	    if ((s = UI_GETID(u.u_user[1])) == 0)
	        return EINVAL;
	    lp = &ui_layer[DEV][i];
	    atp = &lp->l_attached[s];

	    if (atp->romid == -1)
		return EINVAL;
	    dophys(atp->romid, 0, 0, 0);
	    atp->romid = -1;

	    for (i = 1; i < NATTACHES; i++) { 
	        if (lp->l_attached[i].romid != -1)
		    break;
	    }
	    if (i >= NATTACHES)
	        lp->l_romaddr = NULL;
	    return u.u_error;
	    }

	case UI_MAP:
	   /*
	    *	map in and lock down a page for the user interface
	    *		structure ... the address given must be a page
	    *		in a shared memory segment, and it must be on
	    *		a page boundary
	    */
	    return ui_map(DEV, *(caddr_t *)addr);

	case UI_UNMAP:
	   /*
	    *	unmap a shared memory page. Also undo any other ioctls
	    *		that have started actions (such as cursors or
	    *		keyboard events etc) that depend on this area.
	    *		Fails if there are live layers using the dev.
	    *		Does nothing if no page is mapped.
	    */

	    {
	    register struct layer *lp;

	    if (ui_devices[DEV])
		ui_remdevices(DEV);
	    if (ui_cursor[DEV])
		ui_remcursor(DEV);

	    if (ui_uaddr[DEV])
	        {
		undma(ui_uaddr[DEV], ptob(1), LOCKFLAGS);
		ui_uaddr[DEV] = NULL;
	        }
	    ui_addr[DEV] = NULL;
	    break;
	    }

	case UI_CURSOR:
	   /*
	    *	display a mouse cursor and enable cursor tracking
	    */

	    {
	    register struct video *vp;
	    register struct ui_interface *uip;

	    if ((i = UI_LAYER(u.u_user[1])) == NOLAYER)
		return EINVAL;
	    ui_layer[DEV][i].l_state.wanted |= C_WANTED;

	    if (ui_cursor[DEV])
		return 0;		/* already anabled */

					/* initialize cursor data */
	    uip = ui_addr[DEV];
	    uip->c_style = CUR_SMALL1;
	    uip->c_mlookup[0] = 4;
	    uip->c_mlookup[1] = 7;
	    uip->c_mlookup[2] = 10;
	    uip->c_mlookup[3] = 15;
	    uip->c_mlookup[4] = 20;
	    uip->c_mlookup[5] = 25;
	    uip->c_mlookup[6] = 30;
	    uip->c_mlookup[7] = 35;
	    uip->c_mlookup[8] = 39;
	    uip->c_mlookup[9] = 256;
	    uip->c_lock = 0;

	    ui_cursor[DEV] = 1;		/* mark it as on */
	    ui_mx[DEV] = mouse_x[DEV];	/* clear the deltas */
	    ui_my[DEV] = mouse_y[DEV];
	    vp = ui_vp;
	    s = spl1();			/* turn on interrupts */
	    ui_vtrace[DEV] = vp->video_intr;
	    vp->video_intr = ui_display;
	    (*vp->video_func)(vp, VF_ENABLE, 0);
	    ui_display(vp);		/* display it */
	    splx(s);
	    break;
	    }

	case UI_UNCURSOR:
	   /*
	    *	Stop the display of a cursor (this doesn't remove
	    *		the cursor from the screen, it just
	    *		stops its being updated)  We only actually
	    *		turn the cursor off if nobody else wants it.
	    */
	    {
	    register struct layer *lp;

	    if (!ui_cursor[DEV] || (i = UI_LAYER(u.u_user[1])) == NOLAYER)
		return EINVAL;
	    lp = &ui_layer[DEV][i];

	    if (!(lp->l_state.wanted & C_WANTED))
		return EINVAL;		/* this proc never did UI_CURSOR */
	    lp->l_state.wanted &= ~C_WANTED;

	    for (i = 0,lp = &ui_layer[DEV][0]; i < NLAYERS; i++,lp++)
		if (lp->l_state.state == LS_INUSE &&
		    (lp->l_state.wanted & C_WANTED))
		    return 0;
	    ui_remcursor(DEV);
	    break;
	    }

	case UI_DEVICES:
	   /*
	    *	This connects the devices to the mouse and keyboard
	    *		so that mouse and keyboard events can be
	    *		posted.
	    */

	    {
	    register struct ui_interface *uip;
	    register struct layer *lp;

	    if ((i = UI_LAYER(u.u_user[1])) == NOLAYER)
	        return EINVAL;

	    lp = &ui_layer[DEV][i];
	    lp->l_state.wanted |= A_WANTED;
	    if (ui_devices[DEV])
		return 0;	/* already connected */

				/* initialize mouse/keyboard data */
	    uip = ui_addr[DEV];
	    uip->c_keythres = 16;
	    uip->c_keyrate = 4;
	    uip->c_modifiers = btnState;
	    uip->c_button = 0;
	    for (i = 0; i < 128; i++)
		uip->c_key[i] = NOLAYER;

	    ui_devices[DEV] = 1;
	    if (ui_mouse_open[DEV] = mouse_op(DEV, MOUSE_OP_OPEN, 0))
		{
		ui_mouse_intr[DEV] = mouse_op(DEV, MOUSE_OP_INTR, ui_mouse);
		ui_mouse_mode[DEV] = mouse_op(DEV, MOUSE_OP_MODE, 1);
		}
	    else if (mouse_open(DEV, ui_mouse, 1) == 0)
		return EINVAL;
	    if (ui_key_open[DEV] = key_op(DEV, KEY_OP_OPEN, 0))
		{
		ui_key_intr[DEV] = key_op(DEV, KEY_OP_INTR,ui_keyboard);
		ui_key_mode[DEV] = key_op(DEV, KEY_OP_MODE, KEY_ARAW);
		}
	    else if (key_open(DEV, ui_keyboard, KEY_ARAW) == 0)
		return EINVAL;
	    break;
	    }

	case UI_UNDEVICES:
	   /*
	    *	This restores the mouse and keyboard to their previous
	    *		owners (or closes them if they were not
	    *		previously in use)  We only actually do this
	    *		if no other layer still wants mouse/keyboard events.
	    */

	    {
	    register struct layer *lp;

	    if (!ui_devices[DEV] || (i = UI_LAYER(u.u_user[1])) == NOLAYER)
		return EINVAL;
	    lp = &ui_layer[DEV][i];

	    if (!(lp->l_state.wanted & A_WANTED))
		return EINVAL;		/* this proc never did UI_DEVICES */
	    lp->l_state.wanted &= ~A_WANTED;

	    for (i = 0,lp = &ui_layer[DEV][0]; i < NLAYERS; i++,lp++)
		if (lp->l_state.state == LS_INUSE &&
			(lp->l_state.wanted & A_WANTED))
		    return 0;
	    ui_remdevices(DEV);
	    break;
	    }

	case UI_DELAY:
	   /*
	    *	delay waits until lbolt reaches *addr and then returns the
	    *		current value of lbolt.  If a process wants to sleep
	    *		for n clock ticks, it should call UI_TICKCOUNT to find
	    *		the current value of lbolt, then call UI_DELAY to sleep
	    *		until lbolt reaches that value plus n.  We can't do it
	    *		all in one call because we want to sleep at PZERO+1 so
	    *		we can deliver signals.  That causes this call to be
	    *		restarted.  If the parameter to this call were a delta
	    *		instead of an absolute, we would start the delay over
	    *		again.  If enough signals came in (via setitimer?), we
	    *		would never return!
	    */

	    {
	    register struct layer *lp;

	    if ((i = UI_LAYER(u.u_user[1])) == NOLAYER)
		return EINVAL;
	    lp = &ui_layer[DEV][i];

	    while ((unsigned long) lbolt < * (unsigned long *) addr)
		{	
		s = spl7();
		lp->l_sleep = waitMask;
		lp->l_mouse = ui_bigrect;
		timeout(setrun, u.u_procp, ui_convabs((*(unsigned long *)addr)));

		if (sleep((caddr_t) lp->l_active, PCATCH | UI_PRI))
		    {
		    untimeout(setrun, u.u_procp);
		    splx(s);
		    return(EINTR);
		    }
		untimeout(setrun, u.u_procp);
		splx(s);
		}

	    *(unsigned long *)addr = lbolt;
	    break;
	    }

	case UI_LPOSTEVENT:
	   /*
	    *	This posts an event to the layer identified  by the
	    *		field 'layer' in the parameter
	    */

	    if ((i = ((struct lpostevent*)addr)->layer) < 0 ||
			i >= NLAYERS ||
			ui_layer[DEV][i].l_state.state != LS_INUSE)
		return EINVAL;
	    i = ui_postevent(DEV,i,
		    ((struct lpostevent*)addr)->eventCode,
		    ((struct lpostevent*)addr)->eventMsg,
		    0, 0);
	    return i;

	case UI_CREATELAYER:
	   /*
	    *	Create a new layer on the current device, if one is
	    *		not available return EAGAIN.
	    */

	    if (ui_addr[DEV] == NULL || UI_LAYER(u.u_user[1]) != NOLAYER)
		return EINVAL;
	    if ((i = ui_createlayer(DEV, ui_addr[DEV])) < 0)
		return EAGAIN;
	    u.u_procp->p_nice = 0;       /* really bump up our priority */
	    u.u_user[1] = UI_SETID(1)|UI_FLAG|UI_DL(DEV)|i;
	    u.u_rval1 = i;
	    break;

	case UI_UNLAYER:
	   /*
	    *	Dispose of a process's layer
	    */

	    if ((i = UI_LAYER(u.u_user[1])) == NOLAYER)
		return EINVAL;
	    ui_killlayer(i, DEV);
	    u.u_user[1] = UI_FLAG|UI_DL(DEV)|NOLAYER;
	    break;

	case UI_SETLAYER:
	   /*
	    *	Set a named layer to be the current active one
	    *		for receiving new events
	    */

	    return (ui_setlayer(DEV, *(int *)addr));
		    
	    break;

	case UI_SETSELRECT:
	    /*
	     *	Set the mouse rect to use in future selects
	     */

            {
	    register struct layer *lp;

	    if ((i = UI_LAYER(u.u_user[1])) == NOLAYER)
		return EINVAL;
	    lp = &ui_layer[DEV][i];
	    lp->l_selmouse = * (Rect *) addr;
	    break;
	    }

	case UI_HASKIDS:
	    /*
	     *	Return 0 if a given process has children, ESRCH	otherwise.
	     */

	    {
	    register struct proc *p;

	    s = * (int *) addr;		/* s gets the pid passed in */
	    p = proc;			/* p points at start of proc table */
	    for (i = v.v_proc; --i >= 0; p++)
		if (p->p_stat != 0 && p->p_ppid == s)
		    return 0;
	    return ECHILD;
	    }

	case UI_GETDEPTH:
	    /*
	     *	Return the default pixel depth of the video device used
	     *	by the toolbox.  This goes away when multiple screens
	     *	arrive.  This is used only by the toolboxdaemon to decide
	     *	whether a screen refresh is appropriate.
	     */

	    * (int *) addr = ui_vp->video_data.v_pixelsize;
	    break;

	case UI_READPRAM:
	case UI_WRITEPRAM:
	    /*
	     *	Read or write some data from/to parameter RAM.
	     */

	    {
#define PRAM_SIZE 256
	    char pramBuffer[PRAM_SIZE];
	    register struct pram *p;
	    unsigned short offset,count;

	    p = (struct pram *) addr;
	    offset = (unsigned short) p->count;
	    count = (unsigned short) ((unsigned long) p->count >> 16);
	    if (offset + count > PRAM_SIZE)
		return EINVAL;
	    if (cmd == UI_READPRAM)
		{
		ReadXPRam(pramBuffer,p->count);
		if (copyout(pramBuffer,p->buffer,count))
		    return EINVAL;
		}
	    else
		{
		if (copyin(p->buffer,pramBuffer,count))
		    return EINVAL;
		else
		    WriteXPRam(pramBuffer,p->count);
		}
	    }
	    break;

	case UI_COPY_OUT:
	    /* copy low memory variables from the kernel to the user
	       version. */
	{
	    register struct lm_element *p = (struct lm_element *) addr;

	    if (p->start_address > (caddr_t)MAXLOWADDR ||
	       (p->start_address + p->byte_count) > (caddr_t)MAXLOWADDR)
	        return EINVAL;
	    if (copyout(p->start_address, p->start_address, p->byte_count))
		return EINVAL;
	    break;
	}

	case UI_PUSHLAYER:
	{
	    for (i = ui_active[DEV] + 1 ; i != ui_active[DEV] ; i++)
	    {
		if (i >= NLAYERS)
		    i = 0;

		if (ui_layer[DEV][i].l_state.state == LS_INUSE)
		{
		    ui_setlayer(DEV, i);
		    break;
		}
	    }
	    break;
	}
	    
	case UI_GETDQEL:
	    {
	    struct drive_queue *dqp;
	    register struct DrvQEl *inCoreQuePtr;
	    register int *ad_base, ad_offset, want;

	    /* Pass back the DrvQEl struct requested by the index
	     * passed in the dQDrive field. If the DrvQEl for that
	     * index does not exist, then return EINVAL
	     *
	     * Sash sets things up like this:
	     *
	     * *kernelinfoptr ->---------------------------
	     *                  |    struct auto_data     |
	     *                  ---------------------------
	     *                  |  (long sectinfo[3]) * 3 |
	     *                  ---------------------------
	     *                  |   short short_var       |
	     *                  ---------------------------
	     *                  |   long drvQueOffset     |---
	     *                  ---------------------------   |
	     *                                                |
	     *                                                |
	     *1st drvQue entry:	-------------------------     |
	     *                  |    4 byte flags       |     |
	     *                  -------------------------     |
	     *             -----|   offset to next el.  |<----
	     *            |     -------------------------
	     *            |                .
	     *            |                .
	     *            |                .
	     *            |
	     *             ------> next drvQue entry (last entry has 0)
	     */

	    ad_base = (int*)kernelinfoptr;
	    ad_offset = kernelinfoptr->drive_queue_offset;
	    inCoreQuePtr = (struct DrvQEl *)((int)ad_base + ad_offset);

	    dqp = (struct drive_queue *) addr;
	    
	    want = dqp->theQueue.dQDrive;	/* this the the one we want */

	    for (i = 0; ; i++)
	    {
		if (i == want)
		{
		    *dqp = *(struct drive_queue *)((int)inCoreQuePtr - 4);
		    return 0;
		}
		if (inCoreQuePtr->qLink == 0)
		    return EINVAL;           /* not found */

		/* pt to next DrvQEl */
	    	inCoreQuePtr = (struct DrvQEl *)((int)inCoreQuePtr +
						 (int)(inCoreQuePtr->qLink));
	    }
	    }
	    break;

	case UI_VIDEO_CONTROL:
	case UI_VIDEO_STATUS:
	    {
	    register struct CntrlParam *pb;
	    register struct video *vp;
	    extern struct video *video_index[];
	    pb = (struct CntrlParam *)addr;

	    if (rbv_monitor && pb->qType == MACIIci_XSLOT)
	        pb->qType = 0;
	    if ((unsigned short)pb->qType > SLOT_HI || (vp = video_index[pb->qType]) == 0)
	        pb->qType = -17;	/* controlErr */
	    else {
	        if (cmd == UI_VIDEO_CONTROL)
		    pb->qType = callControl(vp, pb->csCode, *(char **)pb->csParam);
		else
		    pb->qType = callStatus(vp, pb->csCode, *(char **)pb->csParam);
	    }
	    }
	    break;

	case UI_SET_KCHR:
	    {
	    register struct kybd_map *km;
	    extern int key_trans_state[];

	    km = (struct kybd_map *)addr;
	    if (km->byte_count > TRANSDATASIZE) {
		return EINVAL;
	    }
	    if (copyin(km->base_address, transData, km->byte_count)) {
		return EINVAL;
	    }
	    key_trans_state[0] = 0;
	    break;
	    }

	case UI_KEYBOARD:
	   /*
	    *	This puts a character into the input stream.
	    */
	    ui_keyboard(0, 0, *((unsigned char *)addr), 0);
	    break;

	default:
	    return EINVAL;
	}

    return 0;
}

/*
    This routine creates a new layer.  If it succeeds, it returns the layer
number.  Otherwise, it returns -1.
*/

static ui_createlayer(dev,uip)
int dev;
register struct ui_interface *uip;
{
    register int i,l,n;
    register struct layer *lp;
    register struct attach *atp;

			/* First, look for an unused layer */
    for (l = 0,lp = &ui_layer[DEV][0]; l < NLAYERS; l++,lp++) {
	if (lp->l_state.state == LS_EMPTY)
	    break;
    }
    if (l == NLAYERS)
	return -1;
			/* Found one, so initialize the new layer */
    lp->l_state.state = LS_INUSE;
    lp->l_state.wanted = NOTHING_WANTED;
    lp->l_refcnt = 1;
    lp->l_shmid = -1;
    lp->l_sleep = 0;
    lp->l_down = 0;
    lp->l_mask = (everyEvent & ~keyUpMask) | AUX_EVENT_MASK;
    lp->l_timeout = 0;
    lp->l_first = NULL;
    lp->l_last = NULL;
    lp->l_free = NULL;
    lp->l_selmouse = ui_bigrect;

    for (i = 0; i < NSCREENS; i++)
        lp->l_screen[i].vaddr = NULL;

    for (i = 0; i < NEVENTS; i++) {
	lp->l_events[i].next = lp->l_free;
	lp->l_free = &lp->l_events[i];
    }
    for (atp = &lp->l_attached[1]; atp < &lp->l_attached[NATTACHES]; atp++) {
	for (n = 0; n < NSCREENS; n++)
	    atp->screenid[n] = -1;
        atp->romid = -1;
	atp->pid = 0;
	atp->procp = NULL;
	atp->select.nfd = 0;
    }
    atp = &lp->l_attached[1];
    atp->procp = u.u_procp;
    atp->pid = u.u_procp->p_pid;

    lp->l_active = atp;
    if (ui_addr[DEV]) {
        *(unsigned int *)((char *)ui_addr[DEV] + CURPIDOFF) = atp->pid;
    }
    return(l);
}


/*
    This routine destroys a layer.
*/

static ui_killlayer(l,dev)
register int l;
{
    register struct layer *lp;

    lp = &ui_layer[DEV][l];
    if (ui_active[DEV] == l)
	ui_active[DEV] = NOLAYER;
    lp->l_state.state = LS_EMPTY;
}

/*
    This routine disconnects a process from a user interface device.  If the
kill_layer flag is set, it also removes the caller's layer if one exists.
*/

static ui_unconnect()
{
    register int l;
    register int i;
    register struct layer *lp;
    register struct attach *atp;
    register struct user *up;
#if NDEVICES != 1
    register int dev = UI_DEVICE(u.u_user[1]);
#endif NDEVICES
    struct attach *ui_findfirsttask();
    int ui_kill();
    
    up = &u;

    if (UI_FLAG & up->u_user[1]) {
	if ((l = UI_LAYER(up->u_user[1])) != NOLAYER) {
	    lp = &ui_layer[DEV][l];

	    if (i = UI_GETID(up->u_user[1])) {
	        atp = &lp->l_attached[i];

		if (--lp->l_refcnt == 0) {
		    lp->l_killed = 0;
		    untimeout(ui_kill, lp);

		    if (lp->l_shmid != -1) {
		        up->u_arg[0] = lp->l_shmid;
			up->u_arg[1] = IPC_RMID;
			up->u_ap = up->u_arg;
			shmctl();
		    }
		    if (lp->l_gofptr) {
		        for (i = NOFILE; i < GNOFILE; i++) {
			    if (lp->l_gofptr[i])
			        closef(lp->l_gofptr[i]);
			}
			releasemem(lp->l_gofptr,GNOFILE*sizeof(struct file *));
			releasemem(lp->l_gpfptr,GNOFILE*sizeof(char *));
			lp->l_gofptr = NULL;
			lp->l_gpfptr = NULL;
		    }
		    if (ui_lowaddr != (caddr_t)-1)
		        undma(0, MAXLOWADDR, LOCKFLAGS);
		    ui_ioctl(DEV, UI_CLEAR, (caddr_t) 0, 0);
		    ui_ioctl(DEV, UI_UNCURSOR, (caddr_t) 0, 0);
		    ui_ioctl(DEV, UI_UNDEVICES, (caddr_t) 0, 0);
		    ui_ioctl(DEV, UI_UNLAYER, (caddr_t) 0, 0);
		    ui_ioctl(DEV, UI_UNMAP, (caddr_t) 0, 0);

		    for (i = 0; i < NSCREENS; i++)
		        lp->l_screen[i].vaddr = NULL;
		    lp->l_romaddr = NULL;

		    wakeup(&lp->l_refcnt);
		} else {
		    if (i == 1) {
		        ui_terminate(lp, i);

			while (lp->l_refcnt)
			    sleep(&lp->l_refcnt, PZERO);
			    
 		    } else {
		        ui_postauxevent(DEV, lp, exitEvt, atp->pid, lbolt, 0);

			if (lp->l_active == atp) {
			    if (lp->l_active = ui_findfirsttask(lp)) {
			        if (ui_addr[DEV]) {
				    *(unsigned int *)((char *)ui_addr[DEV] + CURPIDOFF) =
				          lp->l_active->pid;
				}
			        ui_wakeup(lp->l_active);
			    }
			}
		    }
		}
		for (i = 0; i < NSCREENS; i++)
		    atp->screenid[i] = -1;
		atp->romid = -1;
		atp->pid = 0;
		atp->procp = NULL;
	    }
	    if (up->u_procp == lp->l_tproc) {
	        lp->l_tremain = untimeout(ui_catch_timer, lp);
		lp->l_tproc = NULL;
	    }
	}	    
	up->u_procp->p_nice = NZERO;
	up->u_user[1] = 0;
    }
}

/*
 *	This routine is called when a fork(2) occurs.
 */

ui_fork()
{
}

/*
 *	This routine is called when an exec occurs ... layers are not
 *		inherited across exec(2)s
 */

ui_exec()
{
    ui_unconnect();
    u.u_gofile = NULL;
    u.u_gpofile = NULL;
    u.u_procp->p_sr = 0;
    u.u_sr = 0;
}

/*
 *	This routine is called whenever a process exit(2)s.
 */

ui_exit()
{
    ui_unconnect();
    u.u_procp->p_sr = 0;
}


static ui_kill(lp)
register struct layer *lp;
{   register int n;

    if (lp->l_killed) {
        for (n = 1; n < NATTACHES; n++) {
	    if (lp->l_attached[n].procp)
	        psignal(lp->l_attached[n].procp, SIGKILL);
        }
    }
}


static ui_terminate(lp, ignore)
register struct layer *lp;
register int ignore;
{   register int n;
    register int snt;

    if (lp->l_killed)
        return;
    lp->l_killed = 1;

    for (snt = 0, n = 1; n < NATTACHES; n++) {
        if (lp->l_attached[n].procp && n != ignore) {
	    psignal(lp->l_attached[n].procp, SIGTERM);
	    snt++;
	}
    }
    if (snt)
        timeout(ui_kill, lp, 10 * HZ);   /* allow 10 seconds for processes */
}                                        /* to terminate */


static struct attach *ui_findfirsttask(lp)
register struct layer *lp;
{   register struct attach *atp;

    for (atp = &lp->l_attached[1]; atp < &lp->l_attached[NATTACHES]; atp++) {
        if (atp->pid)
	    return(atp);
    }
    return((struct attach *)NULL);
}


static int ui_findfreetask(lp)
register struct layer *lp;
{   register int n;

    for (n = 1; n < NATTACHES; n++) {
        if (lp->l_attached[n].pid == 0)
	    return(n);
    }
    return(0);
}


static struct attach *ui_findtask(lp, pid)
register struct layer *lp;
register short pid;
{   register struct attach *atp;

    if (pid == 0)
        return(ui_findfirsttask(lp));

    for (atp = &lp->l_attached[1]; atp < &lp->l_attached[NATTACHES]; atp++) {
        if (atp->pid == pid)
	    return(atp);
    }
    return((struct attach *)NULL);
}


/*
 *	This routine posts events to layer l in device dev.
 *	Special processing is done for key down so that autokey events are
 *	delivered to processes.
 *	Update events are not queued but flagged instead
 */

static ui_postevent(dev, l, what, message, mFlag, modifier)
int dev,l;
register int what, message;
int mFlag,modifier;
{
    register struct ui_interface *uip;
    register struct layer *lp;
    register struct event *ep;
    register struct event *el;
    int s;

    if ((uip = ui_addr[DEV]) == NULL || l == NOLAYER)
	return EINVAL;
    lp = &ui_layer[DEV][l];
    if (!(lp->l_mask&ui_mask[what]))
	return 0;

    s = spl1();

    if (what == updateEvt)
        {
	lp->l_update = 1;
	goto out;
        }
    if ((ep = lp->l_free) == NULL)
	{
	el = NULL;

	for (ep = lp->l_first; ep; ep = ep->next)
	    {
	    if (ep->event.what < attachEvt)
	        break;
	    el = ep;
	    }
	if (ep == NULL)
	    return(0);
	if (el == NULL)
	    lp->l_first = ep->next;
	else
	    el->next = ep->next;
	}
    else
	{
	lp->l_free = ep->next;
	}

    ui_seter(&ep->event,uip,what,message);
    if (mFlag)
	ep->event.modifiers = modifier;
    ep->next = NULL;
    if (lp->l_first == NULL)
	lp->l_first = ep;
    else
	lp->l_last->next = ep;
    lp->l_last = ep;
out:
    if (lp->l_sleep&ui_mask[what])
        {
        lp->l_sleep = 0;
	ui_wakeup(lp->l_active);
        }
    splx(s);
    return 0;
}


static ui_postauxevent(dev, lp, what, message, when, modifiers)
    register short what;
    register long  message;
    register struct layer *lp;
{
    register struct event *ep;
    register struct event *el;
    register struct ui_interface *uip;
    int s;

    if (!(lp->l_mask&ui_mask[what]))
	return 0;
    uip = ui_addr[DEV];
    s = spl1();

    for (ep = lp->l_first; ep; ep = ep->next)
	{
	if (ep->event.what == what && ep->event.message == message)
	    {
	    splx(s);
	    return 0;
	    }
        }
    if ((ep = lp->l_free) == NULL)
	{
	el = NULL;

	for (ep = lp->l_first; ep; ep = ep->next)
	    {
	    if (ep->event.what < attachEvt)
	        break;
	    el = ep;
	    }
	if (ep == NULL)
	    return(0);
	if (el == NULL)
	    lp->l_first = ep->next;
	else
	    el->next = ep->next;
	}
    else
	{
	lp->l_free = ep->next;
	}

    ep->event.what = what;
    ep->event.message = message;
    ep->event.when = when;
    ep->event.where.h = uip->c_mx;
    ep->event.where.v = uip->c_my;
    ep->event.modifiers = modifiers;
    ep->next = NULL;

    if (lp->l_first == NULL)
	lp->l_first = ep;
    else
	lp->l_last->next = ep;
    lp->l_last = ep;

    if (lp->l_sleep&ui_mask[what])
        {
        lp->l_sleep = 0;
	ui_wakeup(lp->l_active);
        }
    splx(s);
    return 0;
}

/*
 *	This returns events from the caller's layer
 *		update events are returned
 *			with out any message (and only one of
 *			these is 'queued' at any one time)
 *		the keyUp/keyDown events do not return an
 *			encoded character value in the low byte
 *			of the message, only a key number
 *			in the next byte.
 *		the blocking parameter can be one of
 *			NOBLOCK - return if no events are present
 *			AVAIL	- return if no events are present
 *				  but, don't remove event from queue
 *			BLOCK   - block until an event is available or
 *				  the timeout happens or
 *				  the mouse moves outside of mouseRect,
 *                                then remove it from the queue
 *                      AVBLOCK - block until an event is available or
 *				  the timeout happens or
 *				  the mouse moves outside of mouseRect,
 *                                but dont remove it from the queue
 *
 */

static ui_getevent(get)
register struct getosevent *get;
{
    register struct layer *lp;
    register struct event *ep, *last;
    register int mask;
    register int s;
    register int blocking;
    register unsigned long wakeme;
#if NDEVICES != 1
    int dev = UI_DEVICE(u.u_user[1]);
#endif NDEVICES

    if (ui_addr[DEV] == NULL)
	return EINVAL;
    if ((s = UI_LAYER(u.u_user[1])) == NOLAYER)
	return EINVAL;

    lp = &ui_layer[DEV][s];
    mask = (unsigned short) get->eventMask & lp->l_mask;
    if (get->auxevents)
        mask |= AUX_EVENT_MASK;
    blocking = get->blocking;
    s = spl7();

    for (;;)
	{

        {
	register struct event *last = 0;

	for (ep = lp->l_first; ep != 0; ep = ep->next)
	    {
	    if (ui_mask[ep->event.what] & mask)
		{
		get->theEvent = ep->event;	/* found one in the queue */
		if (blocking <= BLOCK)          /* BLOCK or NOBLOCK request */
		    {				/* remove it from the queue */
		    if (last == 0)
			lp->l_first = ep->next;
		    else
			last->next = ep->next;
		    if (lp->l_last == ep)
			lp->l_last = last;
		    ep->next = lp->l_free;
		    lp->l_free = ep;
		    }
		splx(s);
		return 0;
		}
	    last = ep;
	    }
        }

	{
	register struct ui_interface *uip = ui_addr[DEV];
						/* nothing in the queue */
	if (lp->l_down && lp->l_time <= lbolt && (mask&autoKeyMask))
	    {					/* time for an auto-key */
	    if (blocking <= BLOCK)
		lp->l_time = lbolt + uip->c_keyrate;
	    ui_seter(&get->theEvent,uip,autoKey,lp->l_char<<8);
	    break;
	    }

	if (lp->l_update && (mask&updateMask))
	    {					/* there's an update event */
	    if (blocking <= BLOCK)
		lp->l_update = 0;
	    ui_seter(&get->theEvent,uip,updateEvt,0);
	    break;
	    }
			/* There are no events available.  So, send him a */
			/* null event iff: */
			/* 1) We aren't being asked to block. */
			/* 2) The time out has expired. */
			/* 3) The mouse is outside of mouseRect. */
	if (blocking == NOBLOCK || blocking == AVAIL ||
		    (unsigned long) lbolt >= get->timeOut ||
		    (short) uip->c_mx <  get->mouseRect.left ||
		    (short) uip->c_mx >= get->mouseRect.right ||
		    (short) uip->c_my <  get->mouseRect.top ||
		    (short) uip->c_my >= get->mouseRect.bottom)
	    {
	    if ((blocking == NOBLOCK || blocking == AVAIL) &&
		 get->auxevents && lp->l_active->select.nfd)
	        {
		lp->l_active->brkselect = 1;

		splx(s);
		ui_selectit(lp, lp->l_active, wakeme);
		s = spl7();
	        }
	    ui_seter(&get->theEvent,uip,nullEvent,0);
	    break;
	    }
        }
	wakeme = get->timeOut;
	if (lp->l_down && (mask&autoKeyMask) && lp->l_time < wakeme)
	    wakeme = lp->l_time;
	lp->l_mouse = get->mouseRect;
	lp->l_sleep = waitMask | mask;
	
	if (lp->l_active->select.nfd)
	    {
	    lp->l_active->brkselect = 0;
	    splx(s);

	    if (ui_selectit(lp, lp->l_active, wakeme) == EINTR)
	        return(EINTR);

	    s = spl7();
	    }
	else
	    {
	    if (wakeme)
	        {
	        lp->l_timeout = 1;
	        timeout(setrun, u.u_procp, ui_convabs(wakeme));
	        }
	    if (sleep(lp->l_active, PCATCH | UI_PRI))
		{
	        if (wakeme)
		    {
		    lp->l_timeout = 0;
		    untimeout(setrun, u.u_procp);
		    }
		splx(s);
		return(EINTR);
	        }
	    if (wakeme)
	        {
		lp->l_timeout = 0;
		untimeout(setrun, u.u_procp);
	        }
	    }
      }
    splx(s);
    return 0;
}

/*
 * Search through the events in the caller's layer looking for one
 *	which meets the given requirements.  Since this emulates the
 *	action of thumbing through the event queue, we don't look for
 *	the artificial events like update and autoKey.
 */

static ui_findevent(find)
register struct findevent *find;
{
    register struct layer *lp;
    register struct event *ep;
    register int s;
#if NDEVICES != 1
    int dev = UI_DEVICE(u.u_user[1]);
#endif NDEVICES

    if (ui_addr[DEV] == NULL)
	return EINVAL;
    if ((s = UI_LAYER(u.u_user[1])) == NOLAYER)
	return EINVAL;

    lp = &ui_layer[DEV][s];
    s = spl7();

    for (ep = lp->l_first; ep != 0; ep = ep->next)
    {
	if (ui_maskcompare(&ep->event, &find->mask, &find->data,
			    sizeof(EventRecord)))
	{
	    find->data = ep->event;
	    splx(s);
	    u.u_rval1 = 1;
	    return 0;
	}
    }
    splx(s);
    find->data.what = nullEvent;
    u.u_rval1 = 0;
    return 0;
}

static ui_maskcompare(data, mask, value, size)
register unsigned short *data, *mask, *value;
register int size;
{
    for( ; size >= sizeof(short) ; size -= sizeof(short))
	if((*data++ ^ *value++) & *mask++)
	    return 0;

    if(size > 0)
	if((*(unsigned char *)data ^ *(unsigned char *)value)
	   & *(unsigned char *)mask)
	    return 0;
	
    return 1;
    }


static ui_flushevents(dev,eventMask,stopMask)
int dev;
register int eventMask, stopMask;
{
    register struct event *ep, *last, **parent;
    register struct layer *lp;
    register int i,s;

    if ((i = UI_LAYER(u.u_user[1])) == NOLAYER)
	return EINVAL;
    lp = &ui_layer[DEV][i];
    parent = &lp->l_first;
    last = NULL;

    s = spl1();

    while ((ep = *parent) != 0)
	{
	i = ui_mask[ep->event.what];
	if (i&stopMask)
	    break;
	if (i&eventMask)
	    {			/* delete this event */
	    *parent = ep->next;
	    if (lp->l_last == ep)
		lp->l_last = last;
	    ep->next = lp->l_free;
	    lp->l_free = ep;
	    }
	else
	    {			/* skip over this event */
	    last = ep;
	    parent = &ep->next;
	    }
	}
    splx(s);
    return 0;
}

/*
 *	This routine fills in the fields of an event record.  The what and
 *	message parameters are taken as arguments.  The other fields are
 *	taken from the current state of the machine.
 */

static ui_seter(erp,uip,what,message)
register EventRecord *erp;
register struct ui_interface *uip;
{
    erp->what = what;
    erp->message = message;
    erp->when = lbolt;
    erp->where.h = uip->c_mx;
    erp->where.v = uip->c_my;
    erp->modifiers = uip->c_modifiers;
}

/*
 *	This routine finds an empty phys id, and attempts to call dophys for
 *		that id.  Upon success, it returns the phys id.  Upon failure,
 *		-1 is returned.  u.u_error will not be affected by this routine.
 */

static int ui_phys(virtaddr,size,physaddr)
caddr_t virtaddr,physaddr;
unsigned int size;
{
    int u_error;
    register int i;
    register struct phys *p;

    u_error = u.u_error;
    u.u_error = 0;
    p = &u.u_phys[NPHYS - 1];
    for (i = NPHYS; --i >= 0; p--)
	if (p->u_phsize == 0)
	    {
	    dophys(i,virtaddr,size,physaddr);
	    if (u.u_error != 0)
		i = -1;
	    break;
	    }
    u.u_error = u_error;
    return i;
}

/*
 *	This routine is called from the keyboard driver in ARAW (ascii raw) mode
 *		dev 	device
 *		cmd 	(ignored)
 *		c	character code (bit 7 on means key up)
 *		next	(ignored)
 *	Basicly we locate the current layer and post the appropriate event. If
 *	a key up occurs, cancel any autokey events on any layers that have
 *	timeouts waiting for them.  Note that keyDown events are posted to the
 *	active layer, but keyUp events are posted to the layer that received
 *	the keyDown event for that key.
 */

/* 
 *  store key status (up/down) in lowmem array of 16 bytes (128 possible keycodes)
 *  storage is wierd (to correctly emulate the Mac) as an example, the keycode for 
 *  'B' is 1 and is stored as paddr+0 -> 0x02
 */
#define KEYON(KEY)	*(unsigned char *)(paddr+(KEY>>3)) |= (1 << (KEY & 0x7))
#define KEYOFF(KEY)	*(unsigned char *)(paddr+(KEY>>3)) &= ~(1 << (KEY & 0x7))
#define KEYOFFSET   0x174

static ui_keyboard(dev, cmd, c, next)
register int c;
{
    register struct ui_interface *uip;
    register struct layer *lp;
    register int l;
    register unsigned short keycode;
    register unsigned int result, char1, char2;
    register caddr_t paddr;

    if ((uip = ui_addr[DEV]) == NULL)
	return;

    if (ui_lowaddr != (caddr_t)-1)
            paddr = ui_lowaddr + KEYOFFSET;
    else
            paddr = (caddr_t)-1;

    if (c&0x80)
	{					/* The key went up */
	c = kmapData[DEV][c&0x7f];
	if (paddr != (caddr_t)-1)
	    KEYOFF(c);
	l = uip->c_key[c];
	if (l != NOLAYER)
	    {
	    uip->c_key[c] = NOLAYER;
	    lp = &ui_layer[DEV][l];
	    if (lp->l_state.state == LS_INUSE && lp->l_down && lp->l_char == c)
		{
		lp->l_down = 0;
		ui_wakeup(lp->l_active);
		}
	    }
	switch(c)
	    {
	    case 0x3b:
		uip->c_modifiers &= ~controlKey;
		break;
	    case 0x37:
		uip->c_modifiers &= ~cmdKey;
		break;
	    case 0x38:
		uip->c_modifiers &= ~shiftKey;
		break;
	    case 0x39:
		uip->c_modifiers &= ~alphaLock;
		break;
	    case 0x3a:
		uip->c_modifiers &= ~optionKey;
		break;
	    default:
		keycode = uip->c_modifiers;
		keycode &= ~0xff;
		keycode |= c&0x7f;
		result = KeyTrans(transData, keycode, &key_trans_state[DEV]);
		if (result & 0x00ff0000) {
			char1 = (result>>16) & 0xff;
			/* currently the transData table xlate's the del char (0x7f)
			   to a bksp char (0x8), this does not work too well with
			   Unix, so an adjustment was made in the key_mac() routine.
			   Here, we have to xlate back to original bksp char so Mac
			   apps don't break. (sigh) */
			if (char1 == DELETE_CHR)
				char1 = BKSP_CHR;
			ui_postevent(DEV, l, keyUp, char1 | (c<<8), 0, 0);
		}
		if (result & 0x000000ff) {
			char2 = result & 0xff;
			if (result == DELETE_CHR)
				result = BKSP_CHR;
			ui_postevent(DEV, l, keyUp, char2 | (c<<8), 0, 0);
		}
		break;
	    }
	}
    else if ((l = ui_active[DEV]) != NOLAYER)
	{						/* The key went down */
	c = kmapData[DEV][c];
	if (paddr != (caddr_t)-1)
	    KEYON(c);
	uip->c_key[c] = l;
	lp = &ui_layer[DEV][l];

	switch(c)
	    {
	    case 0x37:
		uip->c_modifiers |= cmdKey;
		return;
	    case 0x38:
		uip->c_modifiers |= shiftKey;
		return;
	    case 0x39:
		uip->c_modifiers |= alphaLock;
		return;
	    case 0x3a:
		uip->c_modifiers |= optionKey;
		return;
	    case 0x3b:
		uip->c_modifiers |= controlKey;
		return;
	    case 0x2f:
		if ((uip->c_modifiers & 0x1f00) == cmdKey)
		      *(((char *)uip)+ABRTOFFSET) = 1;
		break;
	    case 0x22:
		if ((uip->c_modifiers & 0x1f00) == (cmdKey | controlKey)) {
		      psignal(lp->l_active->procp, SIGQUIT);
		      return;
		}
		break;
	    case 0x0e:
		if ((uip->c_modifiers & 0x1f00) == (cmdKey | controlKey)) {
		      ui_terminate(lp, 0);
		      return;
		}
		break;
	    }
	keycode = uip->c_modifiers;
	keycode &= ~0xff;
	keycode |= c&0x7f;
	result = KeyTrans(transData, keycode, &key_trans_state[DEV]);
	if (result & 0x00ff0000) {
		char1 = (result>>16) & 0xff;
		if (char1 == DELETE_CHR)
			char1 = BKSP_CHR;
		ui_postevent(DEV, l, keyDown, char1 | ((c&0x7f)<<8), 0, 0);
	}
	if (result & 0x000000ff) {
		char2 = result & 0xff;
		if (char2 == DELETE_CHR)
			char2 = BKSP_CHR;
		ui_postevent(DEV, l, keyDown, char2 | ((c&0x7f)<<8), 0, 0);
	}
	if (!lp->l_down)
	    lp->l_down = 1;
	else if (lp->l_timeout)
	    ui_wakeup(lp->l_active);
	lp->l_time = lbolt+uip->c_keythres;
	lp->l_char = c;
        }
}

/*
 *	Mouse events are passed in to the appropriate layer. If the layer is
 *	in mouse blocking mode and the mouse has moved wake it.
 *		dev	device
 *		cmd	(ignore)
 *		b	raw mouse data from the device (ignore)
 *		change	bit 0 -> button change
 *			bit 1 -> displacement change
 */

#define MBSTATE    0x0172


static ui_mouse(dev, cmd, b, change)
int dev, cmd, b, change;
{
    register struct ui_interface *uip;
    register caddr_t paddr;
    register int l;

    if ((uip = ui_addr[DEV]) == NULL)
	return;
    uip->c_button = mouse_button[DEV];

    if ((paddr = ui_lowaddr) != (caddr_t)-1)
        *(char *)(paddr + MBSTATE) = uip->c_button?0x00:0x80;

    if ((l = ui_active[DEV]) == NOLAYER)
	return;

    if (change&1)
	{
	if (uip->c_button)
	    {
	    uip->c_modifiers &= ~btnState;
	    ui_postevent(DEV, l, mouseDown, 0, 0, 0);
	    }
	else
	    {
	    uip->c_modifiers |= btnState;
	    ui_postevent(DEV, l, mouseUp, 0, 0, 0);
	    }
	}
}

/*
 *	Page in and lock down a page in a shared memory section
 *		in a user's address space. Max of 1 page per 'device'
 */

static ui_map(dev, base)
int dev;
caddr_t base;
{
    register struct ui_interface *uip;
    register struct video *vp;

    if (ui_addr[DEV])
        return EINVAL;
    if (useracc(base, ptob(1), LOCKFLAGS) == 0)
        return EINVAL;
    if ((uip = (struct ui_interface *)realvtop(FCUSERD, base)) ==
	                             (struct ui_interface *)-1) {
        undma(base, ptob(1), LOCKFLAGS);
        return EINVAL;
    }
    uip->c_button = mouse_button[DEV];
    uip->c_mx = 0;
    uip->c_my = 0;
    uip->c_cx = 0;
    uip->c_cy = 0;

    vp = ui_vp;
    uip->c_ssx = vp->video_scr_x;
    uip->c_ssy = vp->video_scr_y;
    uip->c_smx = vp->video_mem_x;
    uip->c_smy = vp->video_mem_y;
    ui_addr[DEV] = uip;
    ui_uaddr[DEV] = base;

    return 0;
}



#define MTEMP       0x828
#define RAWMOUSE    0x82c
#define MOUSE       0x830
#define CRSRVIS     0x8cc
#define CRSRBUSY    0x8cd
#define CRSRNEW     0x8ce
#define CRSRCOUPLE  0x8cf
#define CRSRSTATE   0x8d0
#define CRSROBSCURE 0x8d2
#define MOUSEMASK   0x8d6
#define MOUSEOFFSET 0x8da

/*
 *	Called from the vertical retrace interrupt
 *	Update the current mouse position
 *	If required redraw the cursor
 *		1)	See if we need to (or are allowed to)
 *		2)	write back the old data
 *		3)	save the new data
 *		4)	write the cursor
 *		5)	update the cursor position
 */

static ui_display(vp)
register struct video *vp;
{
    register struct ui_interface *uip;
    register unsigned short *sp;
    register struct layer *lp;
    register short  h, v, mh, mv;
    register caddr_t paddr;
    int l;
#if NDEVICES != 1
    register int dev = vp->video_off;
#endif NDEVICES
    int ui_drawcursor();

    if (ui_vtrace[DEV])
	(*ui_vtrace[DEV])(vp);

    if ((uip = ui_addr[DEV]) == NULL || uip->c_lock)
	return;

    if (((paddr = ui_lowaddr) == (caddr_t)-1) ||
	*(char *)(paddr + CRSRNEW) == 0 ||
	*(char *)(paddr + CRSRBUSY))
        return;

    if ((l = ui_active[DEV]) == NOLAYER)
        return;
    lp = &ui_layer[DEV][l];

    if (*(char *)(paddr + CRSRCOUPLE)) {

        /* calc the movement deltas */
        h = ((Point *)(paddr + MTEMP))->h - ((Point *)(paddr + RAWMOUSE))->h;
	v = ((Point *)(paddr + MTEMP))->v - ((Point *)(paddr + RAWMOUSE))->v;

	if (h < 0)
	    mh = -h;
	else
	    mh = h;

	if (v < 0)
	    mv = -v;
	else
	    mv = v;

        {   register short n, i;

	    if (mh > mv)			/* calc. movement dist. */
	        i = mh + (mv>>1);
	    else
	        i = mv + (mh>>1);

	    if (i >= 256)
	        i = 255;
	    sp = &uip->c_mlookup[0];		/* <- do mouse acc. */

	    if ((n = *sp) && i > n) {
	        mh = h;
		mv = v;
		do {
		    h += mh;
		    v += mv;
		} while (i > *++sp);
	    }
	}

	{   register long loc, off;

	    mh = ((Point *)(paddr + RAWMOUSE))->h;
	    mv = ((Point *)(paddr + RAWMOUSE))->v;

	    if ((mh += h) >= uip->c_ssx) {	/* update the mouse position */
	        if (h < 0)
		    mh = 0;
		else
		    mh = uip->c_ssx - 1;
	    }
	    if ((mv += v) >= uip->c_ssy) {
	        if (v < 0)
		    mv = 0;
		else
		    mv = uip->c_ssy - 1;
	    }
	    loc = (mv << 16) | mh;

	    *(long *)(paddr + RAWMOUSE) = loc;
	    *(long *)(paddr + MTEMP) = loc;

	    loc &= *(long *)(paddr + MOUSEMASK);

	    if (off = *(long *)(paddr + MOUSEOFFSET)) {
	        loc += off;

		mv = loc >> 16;
		mh = loc;

		if (mh >= uip->c_ssx) {
		    if ((off & 0xffff) < 0)
		        mh = 0;
		    else
		        mh = uip->c_ssx - 1;
		}
		if (mv >= uip->c_ssy) {
		    if ((off >> 16) < 0)
		        mv = 0;
		    else
		        mv = uip->c_ssy - 1;
		}
		loc = (mv << 16) | mh;
	    }
	    *(long *)(paddr + MOUSE) = loc;
	}
    }
    uip->c_my = ((Point *)(paddr + MOUSE))->v;
    uip->c_mx = ((Point *)(paddr + MOUSE))->h;

    *(char  *)(paddr + CRSROBSCURE) = 0;

    /*
     *	try to draw the cursor
     */
    uip->c_lock = 1;

    if ((uip->c_ssy > 480 || uip->c_style > 1) && uip->c_my < (uip->c_ssy / 8))
        timeout(ui_drawcursor, vp, 1);
    else
        ui_drawcursor(vp);

    /*
     *	Look to see if a layer needs waking
     */
    if (lp->l_sleep &&
       (
       (short) uip->c_mx <  lp->l_mouse.left ||
       (short) uip->c_mx >= lp->l_mouse.right ||
       (short) uip->c_my <  lp->l_mouse.top ||
       (short) uip->c_my >= lp->l_mouse.bottom
       )) {
        lp->l_sleep = 0;
	ui_wakeup(lp->l_active);
    }
}


static ui_drawcursor(vp)
register struct video *vp;
{
    register struct ui_interface *uip;
    register caddr_t paddr;
    register short new, old;
#if NDEVICES != 1
    register int dev = vp->video_off;
#endif NDEVICES
    register int i;

    if ((uip = ui_addr[DEV]) == NULL)
	return;

    if (((paddr = ui_lowaddr) != (caddr_t)-1) &&
	*(char  *)(paddr + CRSRBUSY) == 0 &&
	*(char  *)(paddr + CRSROBSCURE) == 0 &&
	*(short *)(paddr + CRSRSTATE) == 0) {

                   /* i gets # scan lines to restore */
	if ((old = uip->c_smy - uip->c_cy + uip->c_hpy) > 16)
	    old = 16;
	           /* y gets # scan lines to draw */
	if ((new = uip->c_smy - uip->c_my + uip->c_hpy) > 16)
	    new = 16;
	
	i = *(unsigned char *)(paddr + CRSRVIS);

	switch (uip->c_style) {

	case CUR_SMALL1:
	    ui_small1(vp->video_addr, uip, old, new, i);
	    break;
	case CUR_SMALL2:
	    ui_small2(vp->video_addr, uip, old, new, i);
	    break;
	case CUR_SMALL4:
	    ui_small4(vp->video_addr, uip, old, new, i);
	    break;
	case CUR_SMALL8:
	    ui_small8(vp->video_addr, uip, old, new, i);
	    break;
	case CUR_SMALL16:
	    ui_small16(vp->video_addr, uip, old, new, i);
	    break;
	case CUR_SMALL32:
	    ui_small32(vp->video_addr, uip, old, new, i);
	    break;
	}
	/*
	 *	update the cursor position
	 */
	uip->c_cx = uip->c_mx;
	uip->c_cy = uip->c_my;

	*(char *)(paddr + CRSRVIS) = 1;
	*(char *)(paddr + CRSRNEW) = 0;
    }
    uip->c_lock = 0;
}


/*
 *	remove the cursor display routine from the VTRACE interrupt
 */

static ui_remcursor(dev)
int dev;
{
    register int s;
    register struct video *vp;

    ui_cursor[DEV] = 0;
    vp = ui_vp;
    s = spl1();
    vp->video_intr = ui_vtrace[DEV];
    if (vp->video_intr == NULL)
	(*vp->video_func)(vp, VF_DISABLE, 0);
    splx(s);
}

/*
 *	restore owership of the mouse/keyboard to their previous users
 *		(or close them if they were previously free)
 */

static ui_remdevices(dev)
int dev;
{
    register int s;

    ui_devices[DEV] = 0;
    s = spl1();
    if (!ui_mouse_open[DEV])
	mouse_close(DEV);
    else
	{
	mouse_op(DEV, MOUSE_OP_MODE, ui_mouse_mode[DEV]);
	mouse_op(DEV, MOUSE_OP_INTR, ui_mouse_intr[DEV]);
	}
    if (!ui_key_open[DEV])
	key_close(DEV);
    else
	{
	key_op(DEV, KEY_OP_MODE, ui_key_mode[DEV]);
	key_op(DEV, KEY_OP_INTR, ui_key_intr[DEV]);
	}
    splx(s);
}


/*
The when parameter is an absolute tickcount, rather than the delta
expected by timeout().  We force that delta to be in the range of 1
to 1 million.
*/

static ui_convabs(when)
register unsigned long when;
{
    register unsigned long ticks,now;

    now = (unsigned long) lbolt;
    if (when <= now)
	ticks = 1;
    else if ((ticks = when - now) > 1000000)
	ticks = 1000000;

    return(ticks);
}


/*
 *	If a process is sleeping waiting for an event in lp, this
 *		routine wakes it up.  It may be called
 *		for an auto-key event or if a WaitNextEvent times out.
 */

static ui_wakeup(at)
register struct attach *at;
{
    if (at->procp->p_wchan == (caddr_t)&selwait) {
	at->brkselect = 1;
	selwakeup(at->procp, 0);
	return;
    }
    if (at->procp->p_wchan == (caddr_t)at)
	wakeup(at);
}


static ui_sleep(lp, at)
register struct layer  *lp;
register struct attach *at;
{
    if (at->select.nfd) {
        at->brkselect = 0;
        if (ui_selectit(lp, at, 0) == EINTR)
	    return(EINTR);
    }
    while (at != lp->l_active) {
        if (sleep(at, PCATCH | UI_PRI))
	    return(EINTR);
    }
    return(0);
}



static ui_selectit(lp, atp, timer)
register struct layer *lp;
register struct attach *atp;
register int timer;
{
    register int s, ncoll;
    register struct proc *p;
    int obits[3];
#if NDEVICES != 1
    int dev = UI_DEVICE(u.u_user[1]);
#endif NDEVICES
    extern int nselcoll;
    extern int selscan();
    extern int unselect();

    p = u.u_procp;

    for (;;) {
	ncoll = nselcoll;
	p->p_flag |= SSEL;

	if (selscan(atp->select.mask, obits) || u.u_error) {
	    ui_postauxevent(DEV, lp, selEvt, atp->pid, lbolt, 0);
	    u.u_error = 0;
	    return(0);
	}
	s = spl6();

	if (atp->brkselect || (timer && lbolt >= timer)) {
		splx(s);
		return(0);
	}
	if ((p->p_flag & SSEL) == 0) {
		splx(s);
		continue;
	}
	p->p_flag &= ~SSEL;

	if (nselcoll != ncoll) {
		splx(s);
		continue;
	}
	if (timer) {
	    lp->l_timeout = 1;
	    timeout(unselect, (caddr_t)p, ui_convabs(timer));
	}
	if (sleep((caddr_t)&selwait, PCATCH | UI_PRI)) {
	    if (timer) {
	        lp->l_timeout = 0;
	        untimeout(unselect, (caddr_t)p);
	    }
	    splx(s);
	    return(EINTR);
	}
        if (timer) {
	    lp->l_timeout = 0;
	    untimeout(unselect, (caddr_t)p);
	}
	splx(s);
    }
}



static ui_catch_timer(lp)
register struct layer *lp;
{
    psignal(lp->l_tproc, SIGIOT);
    lp->l_tproc = NULL;
}



ui_seltimer(p)
register struct proc *p;
{
    if (p->p_wchan == (caddr_t)&selwait)
        selwakeup(p, 0);
}


/*
 *	Handle our part of a select system call.  Return 1 if there are
 *		events in this proc's queue.  Otherwise, arrange to have
 *		selwakeup called if events arrive and return 0.
 */

ui_select(dev,rw)
dev_t dev;
int rw;
{
    static struct getosevent get =
    {
	AVAIL,		/* blocking */
	0,              /* no special aux events */
	everyEvent,	/* eventMask */
    };
    register int i;
    register struct layer *lp;
    register struct ui_interface *uip;
#if NDEVICES != 1
    dev = minor(dev);
#endif NDEVICES

    if ((i = UI_LAYER(u.u_user[1])) == NOLAYER)
	return 0;
    lp = &ui_layer[DEV][i];

    untimeout(ui_seltimer, u.u_procp);

    if ((uip = ui_addr[DEV]) != NULL &&
	    (
	    (short) uip->c_mx <  lp->l_selmouse.left ||
	    (short) uip->c_mx >= lp->l_selmouse.right ||
	    (short) uip->c_my <  lp->l_selmouse.top ||
	    (short) uip->c_my >= lp->l_selmouse.bottom
	    ))
	return 1;

    ui_getevent(&get);
    if (get.theEvent.what != nullEvent)
	return 1;
    lp->l_sleep = lp->l_mask | waitMask;
    lp->l_mouse = lp->l_selmouse;
    if (lp->l_down && (lp->l_mask&autoKeyMask))
	timeout(ui_seltimer, u.u_procp, ui_convabs(lp->l_time));
    return 0;
}



#define TICKOFFSET       0x016a
#define SPALARMOFFSET    0x0200
#define SPVOLCTLOFFSET   0x0208
#define TIMEOFFSET       0x020c
#define ALARMSTATEOFFSET 0x021f


void ui_update()
{   register caddr_t paddr;
    register struct layer *lp;
    register int i;
    register unsigned long when;
    int dev;

    if ((u.u_user[0] == (int)lineAVector) && (u.u_procp->p_sr == 0)) {
        if ((i = UI_LAYER(u.u_user[1])) == NOLAYER)
	    return;
	if ((paddr = ui_lowaddr) == (caddr_t)-1)
	    return;
	dev = UI_DEVICE(u.u_user[1]);
	lp = &ui_layer[DEV][i];

	if (i = ((unsigned long)time.tv_sec - lp->l_prevtime)) {
	    *(long *)(paddr + TIMEOFFSET) += i;
	    lp->l_prevtime = time.tv_sec;

            /* turn off the blink bit */
	    *(char *)(paddr + ALARMSTATEOFFSET) &= ~0x20;

	    /* if the alarm is enabled, set and has expired */
	    /* set the ring bit */
	    if ((*(char *)(paddr + SPVOLCTLOFFSET) & 0x80) &&
		(when = *(unsigned long *)(paddr + SPALARMOFFSET))) {
	        if (when < *(unsigned long *)(paddr + TIMEOFFSET))
		    *(char *)(paddr + ALARMSTATEOFFSET) &= ~0x02;
	    }
	}
	*(long *)(paddr + TICKOFFSET) = lbolt;
    }
}



static long ui_sizerom()
{
    extern unsigned short RomVersion;

    if(RomVersion < 0x67c)
	return 256 * 1024;
    else
	return * (long *) (KROMADR + 0x40);
}


ui_setlayer(dev, layer)
register int dev;
register int layer;
{
    if (layer < 0 || layer >= NLAYERS ||
	ui_layer[DEV][layer].l_state.state != LS_INUSE)
    {
	return EINVAL;
    }
    ui_active[DEV] = layer;
    wakeup(&ui_active[DEV]);

    return(0);
}


/*
 *	This routine is called by the floppy driver when it detects
 *	that a disk has been inserted.
 */
postDIevent(fddev,stat)
register dev_t	fddev;
register int	stat;
{       register int i;
	register l;
	register int posted = 0;

	for (i = 0; i < NDEVICES; i++) {
	    if ((l = ui_active[i]) != NOLAYER) {
	        ui_postevent(i,l,diskEvt,(((fddev<<16)&0xffff0000)|(stat&0x0ffff)),0,0);
		posted = 1;
	    }
	}
	return(posted);
}
