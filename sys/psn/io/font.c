#ifndef lint	/* .../sys/psn/io/font.c */
#define _AC_NAME font_c
#define _AC_NO_MAIN "@(#) Copyright (c) 1987 Apple Computer Inc., All Rights Reserved.  {Apple version 1.6 89/09/27 10:45:37}"
#include <apple_notice.h>

#ifdef _AC_HISTORY
  static char *sccsid = "@(#)Copyright Apple Computer 1989\tVersion 1.6 of font.c on 89/09/27 10:45:37";
#endif		/* _AC_HISTORY */
#endif		/* lint */

#define _AC_MODS
/*	@(#)font.c	UniPlus VVV.2.1.2	*/
/*
 * (C) 1986 UniSoft Corp. of Berkeley CA
 *
 * UniPlus Source Code. This program is proprietary
 * with Unisoft Corporation and is not to be reproduced
 * or used in any manner except as authorized in
 * writing by Unisoft.
 */

#include "mac/types.h"
#include "mac/osutils.h"
#include "mac/segload.h"
#include "mac/files.h"
#include "mac/quickdraw.h"
#include "mac/devices.h"
#include "sys/video.h"
#include "sys/font.h"
extern font_clear();
extern font_scrollup();
extern font_scrolldown();
extern font_char();
extern font_erase();
extern font_invert();
extern font_delete();
extern font_insert();
extern font_invertall();

struct fpnt *
font_find(font, point)
char *font;
{
	extern struct fpnt fonts[];
	struct fpnt *fp;

	for (fp = fonts;fp->font;fp++) {
		if (point == fp->point && strcmp(font, fp->name) == 0)
			return(fp);
	}
	return(0);
}

font_set(f, fxp, vp, left, right, top, bottom)
register struct video *vp;
register struct font *f;
register struct fpnt *fxp;
unsigned left, right, top, bottom;
{
	register struct fontrec *fp;
	register int size;

	if (fxp == 0) {
		if (vp->video_scr_x <= 512) {
			size = 9;
		} else {
			size = 12;
		}
		fxp = font_find("TTY", size);
		if (fxp == 0)
			return;
	}
	fp = fxp->font;
	f->font_clear = font_clear;
	f->font_scrollup = font_scrollup;
	f->font_scrolldown = font_scrolldown;
	f->font_char = font_char;
	f->font_erase = font_erase;
	f->font_invert = font_invert;
	f->font_delete = font_delete;
	f->font_insert = font_insert;
	f->font_invertall = font_invertall;
	f->font_pnt = fp;
	f->font_screen = vp;
	f->font_height = fp->chHeight;
	f->font_theight = fp->chHeight+fp->leading;
	f->font_width = fp->fRectMax;
	if (f->font_width > fxp->width)
		f->font_width = fxp->width;
	f->font_leading = fp->leading;
	f->font_rowwords = ((unsigned int)fp->rowWords)<<4;
	f->font_bitimage = (unsigned char *)&fp->bitImage[0];
	f->font_loctable = &fp->bitImage[fp->rowWords*f->font_height];
	f->font_owtable = &fp->bitImage[fp->rowWords*f->font_height+
					 fp->lastChar + 2 - fp->firstChar];
	f->font_linewidth = (f->font_height+f->font_leading) * vp->video_mem_x;
	f->font_maxx = (vp->video_scr_x - left - right) / f->font_width;
	f->font_maxy = (vp->video_scr_y - top - bottom) /
		       (f->font_height+f->font_leading);
	f->font_offset = (top + vp->video_scr_y - bottom -
			 (f->font_height+f->font_leading) * f->font_maxy)/2 *
			 vp->video_mem_x +
			 (vp->video_scr_x - (f->font_width * f->font_maxx))/2;
}

#ifdef NOTDEF
font_debugd(i, v)
{
	printf("d%d = 0x%x\n", i&0xff, v);
}
font_debuga(i, v)
{
	printf("a%d = 0x%x\n", i&0xff, v);
}
#endif NOTDEF
