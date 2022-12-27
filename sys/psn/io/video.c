#ifndef lint	/* .../sys/psn/io/video.c */
#define _AC_NAME video_c
#define _AC_NO_MAIN "@(#) Copyright (c) 1987 Apple Computer, Inc., All Rights Reserved.  {Apple version 2.4 90/01/09 12:17:45}"#
#include <apple_notice.h>

#ifdef _AC_HISTORY
  static char *sccsid = "@(#)Copyright Apple Computer 1989\tVersion 2.4 of video.c on 90/01/09 12:17:45";
#endif		/* _AC_HISTORY */
#endif		/* lint */

#define _AC_MODS
/*	@(#)video.c	UniPlus VVV.2.1.32	*/
/*
 * (C) 1986 UniSoft Corp. of Berkeley CA
 *
 * UniPlus Source Code. This program is proprietary
 * with Unisoft Corporation and is not to be reproduced
 * or used in any manner except as authorized in
 * writing by Unisoft.
 */

#include "sys/types.h"
#include "sys/errno.h"
#include "sys/param.h"
#include "sys/dir.h"
#include "sys/signal.h"
#include "sys/uconfig.h"
#include "sys/sysmacros.h"
#include "sys/reg.h"
#include "sys/mmu.h"
#include "sys/page.h"
#include "sys/systm.h"
#include "sys/time.h"
#include "sys/user.h"
#include "mac/types.h"
#include "mac/osutils.h"
#include "mac/segload.h"
#include "mac/files.h"
#include "mac/quickdraw.h"
#include "mac/devices.h"
#include "sys/video.h"
#include "setjmp.h"
#include "sys/stream.h"
#include "sys/via6522.h"
#include "sys/debug.h"
#include "sys/module.h"
#include "sys/key.h"
#include "sys/slotmgr.h"

extern struct key_data key_data;
extern struct mouse_data mouse_data;

/*
 *	CLUT table change requests
 */

#define CLUT_WRITE	0		/* just write it from the table */
#define CLUT_NOW	1		/* update the table and the clut */
#define CLUT_UPDATE	2		/* update the table and make the
					   clut change on vtrace */

/*
 *	Local storage
 */

struct video *video_desc[NSCREENS];	/* pointer to video board description */
struct video *video_index[16];		/* how to find it by slot number */
int video_count;			/* number of video boards */
int video_key_count;			/* the keyboard count */
static struct video video[NSCREENS];	/* their attributes */
					/* variables for video_alloc() below */
static caddr_t video_alloc_ptr;
static int video_alloc_length;
static caddr_t last_ptr;
static int last_length;
static caddr_t save_ptr;
static int save_length;


/*
 *	Local static defines
 */

static video_intr();
static video_ioctl();
static video_func();
int video_clear();
int video_gray();
static video_setcolor();

/*
 *	Default screen color
 */

static struct video_color black_white = {
	{ 0xffff, 0xffff, 0xffff},
	{ 0x0000, 0x0000, 0x0000},
};

/*
 *	video initialisation routine. First call video_find to probe the slots
 *	for video boards (we can't do this via autoconfig as we need the screen
 *	to come up and run autoconfig .... chicken and egg problem). Next we
 *	set up the video device descriptor and initialise the device. Finally
 *	we clear the screen.
 */

video_init(ubase)
long *ubase;
{
	register struct video *vp;
	register int i;

	video_alloc_ptr = (caddr_t)ptob(*ubase);
	initMacEnvironment();

#define NOSLOT	-1

	for (vp = video, i = 0; i < NSCREENS; vp++, i++) {
		vp->video_slot = NOSLOT;	/* init to be invalid slot */
		video_desc[i] = vp;
	}
	video_find();
	for (i = 0; i < NSCREENS; i++) {
		vp = video_desc[i];
		if (vp->video_slot != NOSLOT) {
			vp->video_off = i;
			vp->video_intr = 0;
			vp->video_color = black_white;
			vp->video_key = &key_data;
			vp->video_key_ind = video_key_count;
			vp->video_mouse = &mouse_data;
			vp->video_mouse_ind = video_key_count;
			video_key_count++;
			vp->video_ioctl = video_ioctl;
			vp->video_func = video_func;
			if (i != 0)
			     video_clear(vp, 1);
			viamkslotintr(vp->video_slot, video_intr, 1);
			vp->dcePtr = &vp->dce;
			callOpen(&vp->dcePtr);
		}
	}

	save_ptr = video_alloc_ptr;
	save_length = 10000;
	video_alloc_length += save_length;
	video_alloc_ptr += save_length;

	/*
	 *	Update the space 'stolen'
	 */

	if (video_alloc_length > 0)
		*ubase = btop(video_alloc_ptr);

	video_alloc_ptr = 0;
}

/*
 *	This routine allocates space from the ubase mechanism.  It may
 *	only be called during video_init()'s lifetime.  Currently, it only
 *	gets called from macdriver.c.  Note that video_alloc_ptr also gets
 *	used in video_find().
 */

caddr_t video_alloc(bytes)
int bytes;
{
	register caddr_t rval = 0;

	if ((unsigned int) bytes > 50000)
	    printf("video_alloc: %d bytes is out of range\n",bytes);
	else if (video_alloc_ptr == 0)
	    {
	    if (save_length < bytes)
		printf("Warning: video_alloc failed\n");
	    else
		{
		rval = save_ptr;
		save_ptr += bytes;
		save_length -= bytes;
		last_ptr = rval;
		last_length = bytes;
		}
	    }
	else
	    {
	    rval = video_alloc_ptr;
	    video_alloc_ptr += bytes;
	    video_alloc_length += bytes;
	    }
	return rval;
}

/*
    This routine frees a block that has been disposed by video_alloc.
It actually only does this if the block is the last block allocated.
Otherwise, the memory gets orphaned.
*/

video_free(ptr)
caddr_t ptr;
{
    if (ptr == last_ptr)
	{
	last_ptr = 0;
	save_ptr -= last_length;
	save_length += last_length;
	}
}

/*
 *	Clear the screen
 */

video_clear(vp, value)
struct video *vp;
{
	register int i, lines, width, inc;
	register long *cp;

	if (value)
		value = 0xffffffff;
	cp = (long *) vp->video_addr;
	lines = vp->video_scr_y - 1;
	width = (vp->video_scr_x*vp->video_data.v_pixelsize+(8*sizeof(long))-1)
		/(8*sizeof(long)) - 1;
	inc = (vp->video_mem_x/(8*sizeof(long)) - width - 1)*sizeof(long);
	do {
		i = width;
		do {
			*cp++ = value;
		} while(i--);
		cp = (long *) ((char *) cp + inc);
	} while (lines--);
}

video_gray(vp)
struct video *vp;
{
	register int i, lines, width, inc;
	register long *cp;
	long value1, value2;

	value1 = 0xaaaaaaaa;
	value2 = 0x55555555;

	cp = (long *) vp->video_addr;
	lines = vp->video_scr_y - 1;
	width = (vp->video_scr_x*vp->video_data.v_pixelsize+(8*sizeof(long))-1)
		/(8*sizeof(long)) - 1;
	inc = (vp->video_mem_x/(8*sizeof(long)) - width - 1)*sizeof(long);
	while (lines > 0)
	{
		i = width;
		do {
			*cp++ = value1;
		} while(i--);
		cp = (long *) ((char *) cp + inc);
		i = width;
		do {
			*cp++ = value2;
		} while(i--);
		cp = (long *) ((char *) cp + inc);
		lines -= 2;
	}
}


/*
 *	Fill the screen with a bit map.	NOTE: it is assumed that the screen size
 *		is in multiples of 8 bits
 */



video_bitmap(vp)
register struct video *vp;
{
	register int j,k,l;
	extern unsigned char screen_feature1[];
	extern unsigned char screen_feature2[];
	extern unsigned char screen_feature3[];
	extern unsigned char screen_feature4[];
	extern unsigned char screen_feature5[];
	extern unsigned char screen_feature6[];
	extern unsigned char screen_feature7[];
	extern unsigned char screen_feature8[];
	extern unsigned char screen_feature9[];

	video_clear(vp, 0);
	vwrite(vp, screen_feature1, 0, 0, 8, 22);	/* top LHS */
	l = vp->video_scr_x - 8;
	for (j = 8; j < l; j+=8) {
		vwrite(vp, screen_feature3, j, 0, 8, 22);/* top middle */
	}
	vwrite(vp, screen_feature2, l, 0, 8, 22);/* top RHS */
	vwrite(vp, screen_feature4, vp->video_scr_x/2-32, 6, 64, 12);/* Apple A/UX */
	l = vp->video_scr_y - 6;
	k = vp->video_scr_x - 8;
	for (j = 22; j < l; j+=2) {
		vwrite(vp, screen_feature5, 0, j, 8, 2);/* LHS */
		vwrite(vp, screen_feature6, k, j, 8, 2);/* RHS */
	}
	vwrite(vp, screen_feature7, 0, l, 8, 6);	/* bottom LHS */
	k = vp->video_scr_x - 8;
	for (j = 8; j < k; j+=8) {
		vwrite(vp, screen_feature8, j, l, 8, 6);/* bottom middle */
	}
	vwrite(vp, screen_feature9, k, l, 8, 6);/* bottom RHS */
}

launch_bitmap(vp)
register struct video *vp;
{
        register int j,k,l;
        extern unsigned char screen3_feature1[];
        extern unsigned char screen3_feature2[];
        extern unsigned char screen3_feature3[];
        extern unsigned char screen3_feature4[];
        extern unsigned char screen3_feature5[];
        extern unsigned char screen3_feature6[];
        extern unsigned char screen3_feature7[];
        extern unsigned char screen3_feature8[];
        extern unsigned char screen3_feature9[];
        extern unsigned char screen3_feature10[];
        extern unsigned char screen3_feature11[];
        extern unsigned char screen3_feature12[];
        extern unsigned char screen3_feature13[];
        int d_position_x;
        int d_position_y;

	video_gray(vp);
        for (k = 0; k < 20; k+=2)
            for (j=0; j < vp->video_scr_x; j+=224)
                vwrite(vp, screen3_feature7, j, k, 224, 2);

        d_position_x = (vp->video_scr_x - 448)/2;
        d_position_y = (vp->video_scr_y * 100)/ 534;
	
        vwrite(vp, screen3_feature1, 0, 0, 8, 5);               /* top LHS              */
        l = vp->video_scr_x - 8;
        vwrite(vp, screen3_feature2, l, 0, 8, 5);               /* top RHS              */
        vwrite(vp, screen3_feature3, 8, 3, 32, 11);             /* Apple logo.          */
        for (j = 0; j < vp->video_scr_x; j+=8)
            vwrite(vp, screen3_feature6, j, 20, 8, 1);          /* Draw a line.         */
        for (k = d_position_y; k < d_position_y + 126; k+=2)
            for (j=d_position_x; j < d_position_x + 448; j+=224)
                vwrite(vp, screen3_feature7, j, k, 224, 2);       /* Draw a white box.    */
        for (j=d_position_x + 8 ; j < d_position_x + 440; j+=8)
        {
            vwrite(vp, screen3_feature6, j, d_position_y, 8, 1);        /* Top Edge.    */
            vwrite(vp, screen3_feature6, j, d_position_y + 3, 8, 1);    /* Thick line   */
            vwrite(vp, screen3_feature6, j, d_position_y + 4, 8, 1);    /* below top edge.      */
            vwrite(vp, screen3_feature6, j, d_position_y + 125, 8, 1);  /* Bottom edge. */
            vwrite(vp, screen3_feature6, j, d_position_y+121, 8, 1);    /* Thick line above     */
            vwrite(vp, screen3_feature6, j, d_position_y+122, 8, 1);    /* bottom edge. */
        }
        vwrite(vp, screen3_feature8, d_position_x, d_position_y, 8, 126);       /* Left edge of box.    */
        vwrite(vp, screen3_feature9, d_position_x + 440, d_position_y, 8, 126);/* Rt. edge of box.     */
        vwrite(vp, screen3_feature10, d_position_x + 24, d_position_y + 24, 32, 32);  /* A/UX-Mac diagram. */
        vwrite(vp, screen3_feature11, d_position_x + 160, d_position_y + 33, 128, 9); /* Welcome to A/UX.  */
        vwrite(vp, screen3_feature12, d_position_x + 176, d_position_y + 49, 88, 12); /* Launching.     */
        vwrite(vp, screen3_feature13, d_position_x + 40, d_position_y + 81, 384, 20); /* Progress Bar/Cancel. */


        l = vp->video_scr_y - 6;
        vwrite(vp, screen3_feature4, 0, l, 8, 6);       /* bottom LHS */
        k = vp->video_scr_x - 8;
        vwrite(vp, screen3_feature5, k, l, 8, 6);       /* bottom RHS */
}

/*
 *	block blitter for writing screen features
 */

static
vwrite(vp, d, x, y, len, height)
struct video *vp;
register unsigned char *d;
int x, y, len, height;
{
	register int inc, i, j, width, w1;
	register unsigned char *cp;

	cp = (unsigned char *)vp->video_addr + ((vp->video_mem_x*y)>>3) + (x>>3);
	inc = (vp->video_mem_x - len)>>3;
	width = len/(8*sizeof(unsigned char));
	w1 = width&~(sizeof(long)-1);
	for (i = 0; i < height; i++) {
		for (j = 0; j < w1; j += sizeof(long)) {
			*(long *)cp = *(long *)d;
			cp += sizeof(long);
			d += sizeof(long);
		}
		for (; j < width; j++) {
			*cp++ = *d++;
		}
		cp += inc;
	}
}



/*
 *	Fill the screen with shutdown bitmap
 *		Note: it is assumed the screen is 512 by 342
 */


video_sbitmap(vp)
register struct video *vp;
{
	register int i;
	extern unsigned char screen2_feature1[];
	extern unsigned char screen2_feature2[];
	extern unsigned char screen2_feature3[];
	extern unsigned char screen2_feature4[];
	extern unsigned char screen2_feature5[];

	video_clear(vp, 1);	/* black background */
	for (i = 56; i < 58; i++) {
	    vwrite(vp, screen2_feature1, 40, i, 424, 1);
	}
	for (i = 58; i < 60; i++) {
	    vwrite(vp, screen2_feature2, 40, i, 424, 1);
	}
	for (i = 60; i < 131; i++) {
	    vwrite(vp, screen2_feature3, 40, i, 424, 1);
	}
	for (i = 131; i < 133; i++) {
	    vwrite(vp, screen2_feature2, 40, i, 424, 1);
	}
	for (i = 133; i < 135; i++) {
	    vwrite(vp, screen2_feature1, 40, i, 424, 1);
	}

	vwrite(vp, screen2_feature4, 64, 73, 32, 31);
	vwrite(vp, screen2_feature5, 112, 86, 320, 12);
}


/*
 *	perform device dependant functions to the screen (this is called via
 *	a pointer in the descriptor so that other people can write their own
 *	versions of this routine). Not all functions are supported by all
 *	devices.
 */

static 
video_func(vp, code, p1)
struct video *vp;
int code;
{
	register int s;
	register int ret;

	ret = 0;
	s = spl2();
	switch(code) {
	case VF_CLEAR:

		/*
		 *	clear the screen
		 */

		video_clear(vp, p1);
		break;

	case VF_ENABLE:

		/*
		 *	turn on vertical retrace interrupts
		 */
		vp->video_intstate = 1;
		break;

	case VF_ONCE:

		/*
		 *	turn on vertical retrace for at least one intr
		 */
		vp->video_intstate = 2;
		break;

	case VF_DISABLE:

		/*
		 *	turn off vertical retrace interrupts
		 */

		if (vp->video_request) {
			vp->video_intstate = 2;
		} else {
			vp->video_intstate = 0;
		}
		break;

	case VF_VINTR:

		/*
		 *	return 1 if we are currently doing a vertical retrace
		 */

		ret = 1;
		break;
	}
	splx(s);
	return(ret);
}

/*
 *	Autoconfig video cards (chicken/egg problem: needed because we have
 *		to come up with a console before we can autoconfig)
 *
 *	Ok, now a word about video drivers:
 *		We need some new comments here rpd!
 */

static
video_find()
{
	register int slot;
	register struct video *vp;
	register struct video_data *vdp;
	extern int rbv_monitor;
	extern int rbv_monid;

	vp = &video[0];

	/*
	 *	Search through the slots looking for video boards
	 */

	for (slot = 0; slot <= SLOT_HI && video_count < NSCREENS; slot++){
		/* 
		 * With rbv, slots are 0 and SLOT_LO to SLOT_HI.
		 */
	        if (slot == 1)
		       slot = SLOT_LO;
		if (slot == 0 && !rbv_monitor)
		       continue;
		/* 
		 * Slot 0 is the on board video slot.  However, it is addressed
		 * at 0xFBB08000, not 0x00000000, as would be expected.
		 */
                if (slot == 0)
		        vp->video_base = (char *)0xFBB08000;
		else
		        vp->video_base = (char *)(0xf0000000 | (slot<<24));

		/*
		 *	If it isn't a video card, ignore it.  Otherwise, get
		 *	the driver and video parameter block from the slot ROM.
		 */
		vp->dce.dCtlSlot = slot;
		vp->dce.dCtlDevBase = (long) vp->video_base;
		vp->dce.dCtlExtDev = 0;
		if (get_video_data(vp))
			continue;
		if (get_video_driver(vp)) {
			printf("Warning: no video driver for slot %d\n",slot);
			continue;
		}
		vdp = &vp->video_data;
		vp->video_mem_x = 8*vdp->v_rowbytes;
		vp->video_mem_y = vdp->v_bottom - vdp->v_top;
		vp->video_scr_x = vdp->v_right - vdp->v_left;
		vp->video_scr_y = vdp->v_bottom - vdp->v_top;
		vp->video_addr = vp->video_base + vdp->v_baseoffset;
		video_index[slot] = vp;
		vp->video_slot = slot;
		vp++;
		video_count++;
	}

	if (video_count == 0)
		printf("Warning: No Video Board Found\n");
	else
		check_video_depth();
}

/*
    This routine uses the Slot Manager to find a video devices default
mode and corresponding video parameter block.  It returns zero upon
success and a slot manager error code upon failure.  This code is
pretty much stolen from the Monitors Desk Accessory.
    We search through the list of video parameter blocks for the one
with the smallest bits/pixel.  For most devices, this will be the
first on in the list.
    This routine fills in the video_data and video_def_mode fields of the
video structure.  It also fills in the dCtlSlotId field of the dce which
is a magic number understood by only a few people on earth.  These people
have gained this knowledge only by promising to remove their tongues.
*/

static int get_video_data(vp)
register struct video *vp;
{
	int depth = 1000;	/* start with impossibly deep bits/pixel */
	int default_mode = 0x80;/* video modes normally default to 0x80 */
	int err;		/* last slot manager result */
	int success = 0;	/* assume failure */
	struct SpBlock pb;
	struct video_data *vd;
	caddr_t slotModesPointer;
	int nextMode;

	pb.spSlot = vp->dce.dCtlSlot;
	pb.spID = 0;
	pb.spCategory = 3;	/* catDisplay */
	pb.spCType = 1;		/* typeVideo */
	pb.spDrvrSW = 1;	/* drSwApple */
	pb.spTBMask = 1;

	err = slotmanager(_sNextTypesRsrc,&pb);
	if (err == 0 && pb.spSlot != vp->dce.dCtlSlot)
	    err = smNoMoresRsrcs;
	else if (err == 0) {
		vp->dce.dCtlSlotId = pb.spID;
		slotModesPointer = pb.spsPointer;
		for (nextMode = 0x80; depth != 1 && !err; nextMode++) {
			pb.spID = nextMode;
			pb.spsPointer = slotModesPointer;
			err = slotmanager(_sFindStruct,&pb);
			if (err == 0) {
				pb.spID = 1;	/* mVidParams */
				err = slotmanager(_sGetBlock,&pb);
				if (err == 0) {
					vd = (struct video_data *) pb.spResult;
					if (vd->v_pixelsize < depth) {
						depth = vd->v_pixelsize;
						default_mode = nextMode;
						vp->video_data = *vd;
						success = 1;
					}
					cDisposPtr(vd);
				}
			}
		}
	}
	vp->video_def_mode = default_mode;
	if (success && depth != 1)
	    printf(
	    "Warning: the video card in slot %d doesn't support 1 bit/pixel\n",
		    vp->dce.dCtlSlot);
	return success? 0: err;
}

/*
    This routine reads a Macintosh-style device driver from the slot ROM.  A
handle to the driver is stored in the dCtlDriver field of the video
structure.  If all goes well, zero gets returned.  Otherwise, a slot manager
error is returned.
*/

static int get_video_driver(vp)
register struct video *vp;
{
	int err;		/* last slot manager result */
	struct SpBlock pb;
	struct SEBlock se;

	pb.spSlot = vp->dce.dCtlSlot;
	pb.spID = vp->dce.dCtlSlotId;
	pb.spExtDev = 0;
	pb.spsExecPBlk = (caddr_t) &se;
	se.seSlot = pb.spSlot;
	se.sesRsrcId = pb.spID;

	err = slotmanager(_sGetDriver,&pb);
	if (err == 0)
	    vp->dce.dCtlDriver = (Ptr) pb.spResult;
	return err;
}

/*
 *	This routine makes sure that the first card in the video_desc array
 *	supports one bit/pixel, if any of the video cards do.  This is so that
 *	the console emulator will work.  Also, video_count will get set to
 *	zero if no cards support one bit/pixel.
 */

static check_video_depth()
{
	struct video *tmp;
	register int i;

	if (video_count > 0 && video_desc[0]->video_data.v_pixelsize != 1) {
		for (i = 1; i < video_count; i++) {
			if (video_desc[i]->video_data.v_pixelsize == 1) {
				tmp = video_desc[0];
				video_desc[0] = video_desc[i];
				video_desc[i] = tmp;
				return;
			}
		}
	printf("Warning: no video board supports one bit/pixel\n");
	video_count = 0;
	}
}

/*
 *	Video Interrupt Handler
 */

static
video_intr(args)
struct args *args;
{
	extern int  rbv_monitor;
	register struct video *vp;

	vp = video_index[args->a_dev];
	if (vp == NULL)
		panic("video interrupt");
	
	if (rbv_monitor && args->a_dev == 0) {
		extern struct via *via2_addr;
		register struct via *via = via2_addr;

		via->rbv_slotier = 0x40 | VIE_CLEAR;
		via = (struct via *)VIA1_ADDR;
		via->t2cl = 4000 & 0xff;
		via->t2ch = 4000 >> 8;
		via->ier = VIE_SET | VIE_TIM2;
	}

	/*
	 *	Clear the interrupt
	 *	If this is a one-shot turn it back off
	 */
	callSlotInt(args->a_dev);

	/*
	 *	If we have to update the CLUT then do it
	 */
	if (vp->video_request) {
		vp->video_request = 0;
		video_setcolor(vp, CLUT_WRITE, (struct video_color *) 0);
	}

	/*
	 *	If required call a service routine
	 */
	if (vp->video_intr)
		(*vp->video_intr)(vp);
}

/*
 *	This routine is called from below to do various ioctls (not all of
 *	which are supported on all cards
 */

static
video_ioctl(vp, iocbp, m)
struct video *vp;
struct iocblk *iocbp;
mblk_t *m;
{
	switch(iocbp->ioc_cmd) {

	case VIDEO_SETCOLOR:
		if (iocbp->ioc_count != sizeof(struct video_color))
			return(1);
		video_setcolor(vp, CLUT_UPDATE, m->b_cont->b_rptr);
		iocbp->ioc_count = 0;
		freemsg(unlinkb(m));
		return(0);

	case VIDEO_SETDEF:

		/*
		 *	return the board to its default settings
		 */

		freemsg(unlinkb(m));
		iocbp->ioc_count = 0;
		return(0);

	default:
		return(1);
	}
}

/*
 *	This routine sets up the device's colour table for the colous values passed in
 */

static
video_setcolor(vp, flag, color)
register struct video *vp;
struct video_color *color;
{
	if (flag != CLUT_WRITE)
		vp->video_color = *color;

	/*
	 *	zap it at the CLUT
	 */

	if (flag != CLUT_WRITE && flag != CLUT_NOW) {
		/*
		 *	wait for the next interrupt before writing
		 */

		vp->video_request = 1;
		video_func(vp, VF_ONCE, 0);
	}
	return(1);
}

