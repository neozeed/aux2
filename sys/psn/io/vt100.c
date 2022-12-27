#ifndef lint	/* .../sys/psn/io/vt100.c */
#define _AC_NAME vt100_c
#define _AC_NO_MAIN "@(#) Copyright (c) 1987 Apple Computer, Inc., All Rights Reserved.  {Apple version 1.7 89/09/27 10:45:42}"
#include <apple_notice.h>

#ifdef _AC_HISTORY
  static char *sccsid = "@(#)Copyright Apple Computer 1989\tVersion 1.7 of vt100.c on 89/09/27 10:45:42";
#endif		/* _AC_HISTORY */
#endif		/* lint */

#define _AC_MODS

/*	@(#)vt100.c	UniPlus VVV.2.1.9	*/
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
#include "sys/key.h"
#include "sys/debug.h"

extern char chorddata[32];
extern int T_vt100;

#define NVT100	1
#define NPAGES	4

#define	BELL	7
#define ESC	0x1b

typedef int (*procedure_t)();
static procedure_t vt100_proc[NVT100];
static int vt100_page[NVT100];
static int vt100_state[NVT100*NPAGES];
static int vt100_val1[NVT100*NPAGES];
static int vt100_val2[NVT100*NPAGES];
static int vt100_x[NVT100*NPAGES];
static int vt100_y[NVT100*NPAGES];
static int vt100_savx[NVT100*NPAGES];
static int vt100_savy[NVT100*NPAGES];
static int vt100_top[NVT100*NPAGES];
static char vt100_wrap[NVT100*NPAGES];
static int vt100_bottom[NVT100*NPAGES];
static char vt100_origin[NVT100*NPAGES];
static char vt100_reverse[NVT100*NPAGES];
static struct font *vt100_font[NVT100];

vt100_setup(dev, fp, proc)
struct font *fp;
procedure_t proc;
{
	int d, i;

	vt100_proc[dev] = proc;
	vt100_font[dev] = fp;
	d = dev*NPAGES;
	for (i = 0; i < NPAGES; i++) {
		vt100_x[d+i] = 0;
		vt100_y[d+i] = 0;
		vt100_top[d+i] = 0;
		vt100_wrap[d+i] = 0;
		vt100_bottom[d+i] = fp->font_maxy - 1;
		vt100_state[d+i] = 0;
		vt100_origin[d+i] = 0;
		vt100_reverse[d+i] = 0;
	}
	vt100_page[dev] = fp->font_screen->video_page;
	if (vt100_page[dev] >= NPAGES) {
		vt100_page[dev] = 0;
		(*fp->font_screen->video_func)(fp->font_screen, VF_SETPAGE, 0);
	}
	key_op(dev, KEY_OP_KEYPAD, 0);
	(*fp->font_clear)(fp);
	(*fp->font_invert)(fp, 0, 0);
}

vt100_char(dev, cp, l)
register int dev;
unsigned char *cp;
int l;
{
	register int x;
	register int y;
	register int state;
	register struct font *fp = vt100_font[dev];
	int changed = 0;
	int i;
	register unsigned char c;
	register int page;
	int maxx;
	int maxy;
	int bottom;

	page = dev*NPAGES + vt100_page[dev];
	maxx = fp->font_maxx;
	maxy = fp->font_maxy;
	bottom = vt100_bottom[page];
	x = vt100_x[page];
	y = vt100_y[page];
	state = vt100_state[page];
	(*fp->font_invert)(fp, x, y);
	while (l--) {
		if (vt100_wrap[page])
			vt100_wrap[page]--;
		c = *cp++;
		switch (state) {
		case 0:			/* normal character processing */
			switch(c) {
			case '\b':	
				if (x) {
					x--;
					changed = 1;
				}
				TRACE(T_vt100, ("bs %d %d\n", x, y));
				break;
			case '\r':
				if (vt100_wrap[page])
					vt100_wrap[page] = 2;
				if (x) {
					x = 0;
					changed = 1;
				}
				TRACE(T_vt100, ("cr %d %d\n", x, y));
				break;
			case '\n':
			line_feed:
				if (vt100_wrap[page])	/* ignore after a wrap*/
					break;
				y++;
				changed = 1;
				if (y > bottom) {
					y = bottom;
					(*fp->font_scrollup)(fp, vt100_top[page],
						vt100_bottom[page], 1);
					TRACE(T_vt100, ("SCRUP %d %d %d\n",
						vt100_top[page],
						vt100_bottom[page], 1));
				}
				TRACE(T_vt100, ("lf %d %d\n", x, y));
				break;
			case BELL:
				chord(chorddata);
				break;
			case '\t':
				x += (8-(x&0x7));
				if (x >= maxx) {
					x = 0;
					y++;
					if (y > bottom) {
						y = bottom;
						(*fp->font_scrollup)(fp,
							vt100_top[page],
							vt100_bottom[page], 1);
					}
					vt100_wrap[page] = 2;
				}
				TRACE(T_vt100, ("tab %d %d\n", x, y));
				changed = 1;
				break;
			case ESC:
				state = 1;
				break;
			default:
				if ((*fp->font_char)(fp, x, y, c)) {
					if (vt100_reverse[page]) 
						(*fp->font_invert)(fp, x, y);
					x++;
					if (x >= maxx) {
						x = 0;
						y++;
						if (y > bottom) {
							y = bottom;
							(*fp->font_scrollup)(fp,
							      vt100_top[page],
							      vt100_bottom[page],
							      1);
						}
						vt100_wrap[page] = 2;
					}
					changed = 1;
				}
				TRACE(T_vt100, ("char=0x%x %d %d\n",
					c&0xff, x, y));
				break;
			}
			break;
		case 1:
			state = 0;
			switch (c) {
			case '[':
				state = 2;
				vt100_val1[page] = 0;
				vt100_val2[page] = 0;
				break;
			case '=':
				TRACE(T_vt100, ("esc=\n"));
				key_op(dev, KEY_OP_KEYPAD, 1);
				break;
			case '>':
				key_op(dev, KEY_OP_KEYPAD, 0);
				TRACE(T_vt100, ("esc>\n"));
				break;
			case 'D':
				TRACE(T_vt100, ("escD\n"));
				goto line_feed;
			case 'M':
				y--;
				changed = 1;
				if (y < vt100_top[page]) {
					y = vt100_top[page];
					(*fp->font_scrolldown)(fp,
						vt100_top[page],
						vt100_bottom[page], 1);
				}
				TRACE(T_vt100, ("escM\n"));
				break;
			case '7':
				TRACE(T_vt100, ("esc7\n"));
				goto save_cur;
			case '8':
				TRACE(T_vt100, ("esc8 %d %d\n",vt100_savx[page],
						vt100_savy[page]));
				goto rest_cur;
			}
			break;
		case 2:
			state = 0;
			switch (c) {
			case '0': case '1': case '2':
			case '3': case '4': case '5':
			case '6': case '7': case '8':
			case '9':
				vt100_val1[page] = c - '0';
				state = 3;
				break;
			case ';':
				state = 4;
				break;
			case 's':
				TRACE(T_vt100, ("esc[s\n"));
			save_cur:
				vt100_savx[page] = x;
				vt100_savy[page] = y;
				break;
			case 'u':
				TRACE(T_vt100,("esc[u %d %d\n",vt100_savx[page],
						vt100_savy[page]));
			rest_cur:
				x = vt100_savx[page];
				y = vt100_savy[page];
				changed = 1;
				break;
			case '?':
				state = 5;
				break;
			case 'H':
			case 'f':
				if (vt100_origin[page]) {
					y = 0;
				} else {
					y = vt100_top[page];
				}
				x = 0;
				changed = 1;
				TRACE(T_vt100, ("esc[H %d %d\n",x, y));
				break;
			case 'J':
				goto erasepage;
				break;
			case 'K':
				goto eraseline;
				break;
			case 'A':
				vt100_val1[page] = 1;
				goto up;
			case 'B':
				vt100_val1[page] = 1;
				goto down;
			case 'D':
				vt100_val1[page] = 1;
				goto left;
			case 'C':
				vt100_val1[page] = 1;
				goto right;
			case 'm':
				vt100_reverse[page] = 0;
				break;
			}
			break;
		case 3:
			state = 0;
			switch (c) {
			case '0': case '1': case '2':
			case '3': case '4': case '5':
			case '6': case '7': case '8':
			case '9':
				vt100_val1[page] = vt100_val1[page]*10 + c - '0';
				state = 3;
				break;
			case ';':
				state = 4;
				break;
			case 'A':
			up:
				if (y) {
					if (vt100_origin[page]) {
						if (y > vt100_val1[page]+
							vt100_top[page]) {
							y -= vt100_val1[page];
						} else {
							y = vt100_top[page];
						}
						changed = 1;
					} else {
						if (y > vt100_val1[page]) {
							y -= vt100_val1[page];
						} else {
							y = 0;
						}
						changed = 1;
					}
				}
				TRACE(T_vt100, ("esc[%dA %d %d\n",
						vt100_val1[page], x, y));
				break;
			case 'B':
			down:
				if (y+1 < maxy) {
					if (vt100_origin[page]) {
						if (y + vt100_val1[page] <
							bottom) {
							y += vt100_val1[page];
						} else {
							y = bottom;
						}
						changed = 1;
					} else {
						if (y + vt100_val1[page] + 1 <
							maxy) {
							y += vt100_val1[page];
						} else {
							y = maxy-1;
						}
						changed = 1;
					}
				}
				TRACE(T_vt100, ("esc[%dB %d %d\n",
						vt100_val1[page], x, y));
				break;
			case 'D':
			left:
				if (x) {
					if (x >= vt100_val1[page]) {
						x -= vt100_val1[page];
					} else {
						x = 0;
					}
					changed = 1;
				}
				TRACE(T_vt100, ("esc[%dD %d %d\n",
					vt100_val1[page], x, y));
				break;
			case 'C':
			right:
				if (x+1 < maxx) {
					if (x + vt100_val1[page] + 1 <
							maxx) {
						x += vt100_val1[page];
					} else {
						x = maxx-1;
					}
					changed = 1;
				}
				TRACE(T_vt100, ("esc[%dC %d %d\n",
					vt100_val1[page], x, y));
				break;
			case 'J':
				switch (vt100_val1[page]) {
				case 0:
				erasepage:
					(*fp->font_erase)(fp, x, y,
							  maxx-1,
							  vt100_bottom[page]);
					break;
				case 1:
					(*fp->font_erase)(fp, 0, 0, x, y);
					break;
				case 2:
					(*fp->font_erase)(fp, 0, 0, maxx - 1,
							  vt100_bottom[page]);
					break;
				}
				TRACE(T_vt100, ("esc[%dJ %d %d\n",
					vt100_val1[page], x, y));
				break;
			case 'K':
				switch (vt100_val1[page]) {
				case 0:
				eraseline:
					i = maxx-(x%maxx)-1;
					if (i >= 0)
						(*fp->font_erase)(fp, x, y,
								  x+i, y);
					break;	
				case 1:
					if (x)
						(*fp->font_erase)(fp, 0, y,
								      x, y);
					break;
				case 2:
					(*fp->font_erase)(fp, 0, y,
							  maxx-1, y);
					break;
				}
				TRACE(T_vt100, ("esc[%dK %d %d\n",
						vt100_val1[page], x, y));
				break;
			case 'L':
				if(vt100_val1[page]) {
					(*fp->font_scrolldown)(fp,y,
						vt100_bottom[page],
						vt100_val1[page]);
					if (x) {
						x = 0;
						changed = 1;
					}
				}
				TRACE(T_vt100, ("esc[%dL %d %d\n",
						vt100_val1[page], x, y));
				break;
			case 'M':
				if(vt100_val1[page]) {
					(*fp->font_scrollup)(fp,y,
						vt100_bottom[page],
						vt100_val1[page]);
					if (x) {
						x = 0;
						changed = 1;
					}
				}
				TRACE(T_vt100, ("esc[%dM %d %d\n",
						vt100_val1[page], x, y));
				break;
			case '@':
				if (vt100_val1[page]) {
					(*fp->font_insert)(fp, x, y,
							   vt100_val1[page]);
				}
				TRACE(T_vt100, ("esc[%d@ %d %d\n",
						vt100_val1[page], x, y));
				break;
			case 'P':
				if (vt100_val1[page]) {
					(*fp->font_delete)(fp, x, y,
							   vt100_val1[page]);
				}
				TRACE(T_vt100, ("esc[%dP %d %d\n",
						vt100_val1[page], x, y));
				break;
			case 'm':
				vt100_reverse[page] = (vt100_val1[page] == 7?1:0);
				break;
			case 'n':
				if (vt100_val1[page] == 6) {
					(*vt100_proc[dev])(dev, KC_CHAR, ESC,1);
					(*vt100_proc[dev])(dev, KC_CHAR, '[',1);
					if (y > 10) {
						(*vt100_proc[dev])(dev, KC_CHAR,
							'0'+y/10,1);
					}
					(*vt100_proc[dev])(dev, KC_CHAR,
							'0'+y%10,1);
					(*vt100_proc[dev])(dev, KC_CHAR, ';',1);
					if (x > 10) {
						(*vt100_proc[dev])(dev, KC_CHAR,
							'0'+x/10,1);
					}
					(*vt100_proc[dev])(dev, KC_CHAR,
							'0'+x%10,1);
					(*vt100_proc[dev])(dev, KC_CHAR, 'R',0);
				}
				TRACE(T_vt100, ("esc[%dn %d %d\n",
						vt100_val1[page], x, y));
				break;
			case 'Z':
				if (x && vt100_val1[page]) {
					if ((x&7)) {
						x = x&~7;
						vt100_val1[page]--;
					}
					x -= vt100_val1[page]<<3;
					if (x < 0)
						x = 0;
					changed = 1;
				}
				TRACE(T_vt100, ("esc[%dZ %d %d\n",
						vt100_val1[page], x, y));
				break;
			}
			break;
		case 4:
			state = 0;
			switch (c) {
			case '0': case '1': case '2':
			case '3': case '4': case '5':
			case '6': case '7': case '8':
			case '9':
				vt100_val2[page] = vt100_val2[page]*10 + c - '0';
				state = 4;
				break;
			case 'r':
				vt100_val1[page]--;
				vt100_val2[page]--;
				if (vt100_val1[page] >= 0 &&
				    vt100_val2[page] < maxy &&
				    vt100_val1[page] < vt100_val2[page]) {
					vt100_top[page] = vt100_val1[page];
					vt100_bottom[page] = vt100_val2[page];
					bottom = vt100_val2[page];
					TRACE(T_vt100, ("'r' %d %d\n",
							vt100_top[page],
							vt100_bottom[page]));
					x = 0;
					y = vt100_top[page];
					changed = 1;
				}
				break;
				
			case 'f':
			case 'H':
				if (vt100_val1[page] == 0 &&
				    vt100_val2[page] == 0) {
					if (vt100_origin[page]) {
						y = 0;
					} else {
						y = vt100_top[page];
					}
					x = 0;
					changed = 1;
				} else {
					vt100_val1[page]--;
					vt100_val2[page]--;
					if (vt100_val2[page] >= 0 &&
					    vt100_val2[page] < maxx &&
					    vt100_val1[page] >= 0)
					if (vt100_origin[page]) {
						if (vt100_val1[page] <
					    	    vt100_bottom[page]-vt100_top[page]) {
							x = vt100_val2[page];
							y = vt100_val1[page] +
								vt100_top[page];
							changed = 1;
						}
					} else 
					if (vt100_val1[page] < maxy) {
						x = vt100_val2[page];
						y = vt100_val1[page];
						changed = 1;
					}
				}
				TRACE(T_vt100, ("esc[%d;%dH %d %d\n",
					vt100_val1[page], vt100_val2[page],
					x, y));
				break;
			}
			break;
		case 5:
			state = 0;
			switch(c) {
			case '0': case '1': case '2':
			case '3': case '4': case '5':
			case '6': case '7': case '8':
			case '9':
				vt100_val1[page] = vt100_val1[page]*10 + c - '0';
				state = 5;
				break;
			case 'h':
				switch(vt100_val1[page]) {
				case 5:
					if (fp->font_inverse == 0) {
						fp->font_inverse = 1;
						(*fp->font_invertall)(fp);
					}
					break;
				case 6:
					vt100_origin[page] = 1;
					break;
				}
				TRACE(T_vt100, ("esc?%dh\n", vt100_val1[page]));
				break;
			case 'l':
				switch(vt100_val1[page]) {
				case 5:
					if (fp->font_inverse == 1) {
						fp->font_inverse = 0;
						(*fp->font_invertall)(fp);
					}
					break;
				case 6:
					vt100_origin[page] = 0;
					break;
				}
				TRACE(T_vt100, ("esc?%dl\n", vt100_val1[page]));
				break;
			case 'u':
				if (vt100_val1[page] < NPAGES) {
					(*fp->font_screen->video_func)
						(fp->font_screen,
						 VF_SETPAGE,
						 vt100_val1[page]);
					if (fp->font_screen->video_page ==
						vt100_val1[page]) {
						vt100_page[dev] =
							vt100_val1[page];
						page = dev*NPAGES +
							vt100_val1[page];
						x = vt100_x[page];
						y = vt100_y[page];
						state = vt100_state[page];
					}
				}
				break;
			}
			break;
		}
	}
	if (changed) {
		vt100_x[page] = x;
		vt100_y[page] = y;
	}
	vt100_state[page] = state;
	(*fp->font_invert)(fp, x, y);
}

