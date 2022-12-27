 #	@(#)Copyright Apple Computer 1987	Version 1.6 of font1.s on 87/11/11 21:38:58
/*	@(#)font1.s	UniPlus VVV.2.1.4	*/
/*
 * (C) 1986 UniSoft Corp. of Berkeley CA
 *
 * UniPlus Source Code. This program is proprietary
 * with Unisoft Corporation and is not to be reproduced
 * or used in any manner except as authorized in
 * writing by Unisoft.
 */
 #
 #	Warning!!! ----> this only runs on 68020s ......
 #

#define	D(x, y)	global font_debugd ; \
		movm.l	&0xc0c0, -(%sp) ;\
		mov.l	y, -(%sp) ;\
		mov.l	&x, -(%sp) ;\
		jsr	font_debugd ;\
		addq.l	&8, %sp ;\
		movm.l	(%sp)+, &0x0303
	
#define	A(x, y)	global font_debuga ; \
		movm.l	&0xc0c0, -(%sp) ;\
		mov.l	y, -(%sp) ;\
		mov.l	&x, -(%sp) ;\
		jsr	font_debuga ;\
		addq.l	&8, %sp ;\
		movm.l	(%sp)+, &0x0303
	
#define	ffp	8(%fp)
#define	x	12(%fp)
#define	y	16(%fp)
#define	c	20(%fp)

#define firstChar	0x02
#define lastChar	0x04


#define	font_pnt 	0x00
#define	font_height 	0x04
#define	font_width 	0x08
#define	font_leading 	0x0c
#define	font_rowwords 	0x10
#define	font_bitimage 	0x14
#define	font_loctable 	0x18
#define	font_owtable 	0x1c
#define	font_linewidth 	0x20
#define	font_maxx 	0x24
#define	font_maxy 	0x28
#define	font_inverse 	0x2c
#define	font_screen 	0x30
#define	font_theight 	0x34
#define	font_offset 	0x38


#define	video_addr	0x00
#define	video_mem_x	0x04
#define	video_mem_y	0x08
#define	video_scr_x	0x0c
#define	video_scr_y	0x10

	global font_char
font_char:
	link	%fp, &0
	movm.l	&0x3ff8, -(%sp)
	mov.l	ffp, %a3		/* font pointer */
	mov.l	font_pnt(%a3), %a2	/* fontrec pointer */
	mov.l	c, %d0			/* character to output */
	cmp.w	%d0, firstChar(%a2)	/* make sure it is in range */
	bge	L1
bad:	clr.l	%d0			/* return a 0 on an error */
	movm.l	(%sp)+, &0x1ffc
	unlk	%fp
	rts
L1:	cmp.w	%d0, lastChar(%a2)
	bgt	bad
	mov.l	x, %d1			/* now test the x position for
					   validity */
	blt	bad
	cmp.l	%d1, font_maxx(%a3)
	bge	bad
	mov.l	y, %d2			/* and then the y */
	blt	bad
	cmp.l	%d2, font_maxy(%a3)
	bge	bad
	sub.w	firstChar(%a2), %d0	/* adjust the character index */
	mov.l	font_loctable(%a3), %a2	/* find the font offset */
	lea	(0,%a2,%d0.w*2), %a2
	clr.l	%d3
	mov.w	(%a2), %d3
	clr.l	%d4			/* get the width (if 0 return) */
	mov.w	2(%a2), %d4
	sub.l	%d3, %d4
	bgt	cnt			/* only valid 0 length is ' ' */
	blt	bad
	mov.l	c, %d0
	cmp.b	%d0, &0x20
	bne	bad
cnt:	mov.l	font_width(%a3), %d7	/* get the output width */
	mulu.w	%d7, %d1		/* get the screen index */
	mulu.l	font_linewidth(%a3), %d2
	add.l	%d1, %d2
	add.l	font_offset(%a1), %d2	/* get the pixel offset from the start*/
	mov.l	font_screen(%a3), %a4	/* get the screen base */
	mov.l	video_mem_x(%a4), %a2	/* get the line increment */
	mov.l	video_addr(%a4), %a4
	mov.l	font_rowwords(%a3), %a1	/* font row width */
	mov.l	font_bitimage(%a3), %a0	/* font info pointer */
	mov.l	font_inverse(%a3), %d5
	mov.l	%d7, %d6		/* get the center offset */
	sub.l	%d4, %d6
	blt	chk
	lsr.l	&1, %d6
	bra	cont
chk:	clr.l	%d6
cont:	mov.l	font_height(%a3), %d1	/* loop for every line */
	subq.l	&1, %d1
 	tst.l	%d4
	beq	blank			/* blank is a special case */
loop:
		bfextu	(%a0){%d3:%d4}, %d0 /* get the character row */
		add.l	%a1, %d3	/* increment the row index */
 		lsl.l	%d6, %d0	/* center the character */
		tst.b	%d5
		bne	inv
		not.l	%d0		/* invert it if required */
inv:
		bfins	%d0, (%a4){%d2:%d7} /* insert it in the bitmap */
		add.l	%a2, %d2	/* move to the next line */
	dbra	%d1, loop		/* loop through each row */
	mov.l	font_leading(%a3), %d1	/* do the last empty rows */
	beq	xit
blankit:
	subq.l	&1, %d1
	tst.b	%d5			/* get the fill value */
	bne	invL
	mov.l	&0xffffffff, %d0
	br	loopL
invL:
	clr.l	%d0
loopL:
		bfins	%d0, (%a4){%d2:%d7} /* insert it */
		add.l	%a2, %d2
	dbra	%d1, loopL
xit:	mov.l	&1, %d0			/* return 1 */
	movm.l	(%sp)+, &0x1ffc
	unlk	%fp
	rts
blank:	
	add.l	font_leading(%a3), %d1	/* just fill with background */
	addq.l	&1, %d1
	br	blankit

/*
 *	Code to do an upward scroll
 */
#define	ffq	8(%fp)
#define	top	12(%fp)
#define	bottom	16(%fp)
#define	ll	20(%fp)
	global	font_scrollup
font_scrollup:
	link	%fp, &0
	movm.l	&0x3f3c, -(%sp)
	mov.l	ffq, %a1			/* Font pointer */
	mov.l	bottom, %d0			/* Last line */
	blt	retn
	cmp.l	%d0, font_maxy(%a1)		/* Check it against the screen*/
	blt	nxt				/*	bounds */
	mov.l	font_maxy(%a1), %d0
	subq.l	&1, %d0
nxt:
	addq.l	&1, %d0				/* Add one to get next line */
	mov.l	%d0, %d3
	mov.l	ll, %d1				/* Line count */
	ble	retn
	sub.l	top, %d3			/* Get the scroll height */
	ble	retn
	cmp.l	%d1, %d3			/* Set the line count to a max*/
	ble	nxt2				/*	of the scroll height */
	mov.l	%d3, %d1
nxt2:

/* d1 = scroll count (ll) */
/* d0 = bottom line (bottom) */
/* d3 = window width (bottom - top) */

	mov.l	font_screen(%a1), %a0		/* Get the screen info */

	mov.l	video_addr(%a0), %a2		/* get the screen address */

	mov.l	%d3, %d4			/* get a count of the number of */
	sub.l	%d1, %d4			/* scanlines to be shifted up */
	mulu.l	font_theight(%a1), %d4

	mov.l	font_width(%a1), %d5		/* calc the number of pixels/line */
	mulu.l	font_maxx(%a1), %d5

	mov.l	font_offset(%a1), %d7		/* get the pixel offset from the start*/

	mov.l	%d7, %d6			/* figure it out in bytes */
	asr.l	&5, %d6	
	asl.l	&2, %d6

	add.l	%d6, %a2			/* adjust the screen pointer */

	and.l	&0x1f, %d7			/* get the inc in bits */

	mov.l	%d7, %d2			/* figure out how far to the first */
	sub.l	&32, %d2			/* longword boundary */
	neg.l	%d2
	mov.l	%d2, %a3

	mov.l	%d5, %d6			/* figure out how many longs to move */
	sub.l	%a3, %d6
	lsr.l	&5, %d6
	subq.l	&1, %d6

	mov.l	font_offset(%a1), %d0		/* see how much remainder is left */
	add.l	%d5, %d0
	and.l	&0x1f, %d0
	mov.l	%d0, %a5

	mov.l	video_mem_x(%a0), %d0		/* now see how many bytes to the next */
	sub.l	%d5, %d0			/* line */
	add.l	%a5, %d0
	sub.l	%d7, %d0
	asr.l	&3, %d0

	mov.l	top, %d2			/* calculate the byte address of the */
	mulu.l	font_linewidth(%a1), %d2	/* top of the screen */
	asr.l	&3, %d2
	mov.l	%d2, %a4
	add.l	%a2, %a4

	subq.l	&1, %d4				/* any lines to shift ? */
	ble	fill_up				/* nope, then proceed to filling */

	mov.l	%d1, %d2			/* get the byte address of the */
	add.l	top, %d2
	mulu.l	font_linewidth(%a1), %d2	/* topmost valid data */
	asr.l	&3, %d2
	add.l	%d2, %a2

loop90:						/* loop copying data up */

		mov.l	%a3, %d5		/* if there is less than a longword at */
		beq	skip91			/*    the beginning, copy it up */

			bfextu	(%a2){%d7:%d5}, %d2
			bfins	%d2, (%a4){%d7:%d5}

			addq.l	&4, %a4		/* increment the addresses */
			addq.l	&4, %a2
skip91:

		mov.l	%d6, %d5		/* loop copying out longs */
loop91:
			mov.l	(%a2)+, (%a4)+

		dbra	%d5, loop91

		mov.l	%a5, %d5		/* if there is some left - copy it out */
		beq	skip92

			bfextu	(%a2){&0:%d5}, %d2
			bfins	%d2, (%a4){&0:%d5}
skip92:

		add.l	%d0, %a2		/* move the byte address to the next */
		add.l	%d0, %a4		/* 	lines */

	dbra	%d4, loop90

fill_up:
	tst.l	font_inverse(%a1)		/* get the fill value */
	bne	skip93
	mov.l	&0xffffffff, %d2
	bra	skip94
skip93:	clr.l	%d2
skip94:

	mov.l	%d1, %d4			/* find out how many scan lines to fill*/
	mulu.l	font_theight(%a1), %d4
	subq.l	&1, %d4

loop95:						/* loop filling them */

		mov.l	%a3, %d5		/* do the first bit */
		beq	skip96

			bfins	%d2, (%a4){%d7:%d5}
			addq.l	&4, %a4
skip96:
		mov.l	%d6, %d5		/* loop moving longs */
loop97:
			mov.l	%d2, (%a4)+
		dbra	%d5, loop97

		mov.l	%a5, %d5		/* do the last bit */
		beq	skip98

			bfins	%d2, (%a4){&0:%d5}
skip98:
		add.l	%d0, %a4		/* increment the byte address */
	dbra	%d4, loop95


retn:	movm.l	(%sp)+, &0x3cfc
	unlk	%fp
	rts

/*
 *	Code to do a downward scroll
 */
	global	font_scrolldown
font_scrolldown:
	link	%fp, &0
	movm.l	&0x3f3c, -(%sp)
	mov.l	ffq, %a1		/* font pointer */
	mov.l	top, %d0		/* start line */
	blt	retn2
	mov.l	bottom, %d3		/* ending line */
	cmp.l	%d0, %d3
	bgt	retn2
	addq.l	&1, %d3			/* actaually make it the next one */
	mov.l	ll, %d1			/* line count */
	ble	retn2
	mov.l	%d3, bottom		/* save it for later */
	sub.l	%d0, %d3		/* check the length */
	cmp.l	%d1, %d3
	blt	nxt4
	mov.l	%d3, %d1		/* make it the max */
	beq	retn2
nxt4:
/* d1 = scroll count (ll) */
/* d0 = bottom line (bottom) */
/* d3 = window width (bottom - top) */

	mov.l	font_screen(%a1), %a0		/* Get the screen info */

	mov.l	video_addr(%a0), %a2		/* get the screen address */

	mov.l	%d3, %d4			/* get a count of the number of */
	sub.l	%d1, %d4			/* scanlines to be shifted down */
	mulu.l	font_theight(%a1), %d4

	mov.l	font_width(%a1), %d5		/* calc the number of pixels/line */
	mulu.l	font_maxx(%a1), %d5

	mov.l	font_offset(%a1), %d7		/* get the pixel offset from the start*/

	mov.l	%d7, %d6			/* figure it out in bytes */
	asr.l	&5, %d6	
	asl.l	&2, %d6

	add.l	%d6, %a2			/* adjust the screen pointer */

	and.l	&0x1f, %d7			/* get the inc in bits */

	mov.l	%d7, %d2			/* figure out how far to the first */
	sub.l	&32, %d2			/* longword boundary */
	neg.l	%d2
	mov.l	%d2, %a3

	mov.l	%d5, %d6			/* figure out how many longs to move */
	sub.l	%a3, %d6
	lsr.l	&5, %d6
	subq.l	&1, %d6

	mov.l	font_offset(%a1), %d0		/* see how much remainder is left */
	add.l	%d5, %d0
	and.l	&0x1f, %d0
	mov.l	%d0, %a5

	mov.l	video_mem_x(%a0), %d0 		/* now see how many bytes to the next */
	neg.l	%d0				/* line */
	sub.l	%d5, %d0
	add.l	%a5, %d0
	sub.l	%d7, %d0
	asr.l	&3, %d0

	mov.l	bottom, %d2			/* calculate the byte address of the */
	mulu.l	font_linewidth(%a1), %d2	/* bottom of the screen */
	sub.l	video_mem_x(%a0), %d2
	asr.l	&3, %d2
	mov.l	%d2, %a4
	add.l	%a2, %a4

	subq.l	&1, %d4				/* any lines to shift ? */
	ble	fill_down			/* nope, then proceed to filling */

	mov.l	bottom, %d2			/* get the byte address of the */
	sub.l	%d1, %d2
	mulu.l	font_linewidth(%a1), %d2	/* bottommost valid data */
	sub.l	video_mem_x(%a0), %d2
	asr.l	&3, %d2
	add.l	%d2, %a2

loop80:						/* loop copying data up */

		mov.l	%a3, %d5		/* if there is less than a longword at */
		beq	skip81			/*    the beginning, copy it up */

			bfextu	(%a2){%d7:%d5}, %d2
			bfins	%d2, (%a4){%d7:%d5}

			addq.l	&4, %a4		/* increment the addresses */
			addq.l	&4, %a2
skip81:

		mov.l	%d6, %d5		/* loop copying out longs */
loop81:
			mov.l	(%a2)+, (%a4)+

		dbra	%d5, loop81

		mov.l	%a5, %d5		/* if there is some left - copy it out */
		beq	skip82

			bfextu	(%a2){&0:%d5}, %d2
			bfins	%d2, (%a4){&0:%d5}
skip82:

		add.l	%d0, %a2		/* move the byte address to the next */
		add.l	%d0, %a4		/* 	lines */

	dbra	%d4, loop80

fill_down:
	tst.l	font_inverse(%a1)		/* get the fill value */
	bne	skip83
	mov.l	&0xffffffff, %d2
	bra	skip84
skip83:	clr.l	%d2
skip84:

	mov.l	%d1, %d4			/* find out how many scan lines to fill*/
	mulu.l	font_theight(%a1), %d4
	subq.l	&1, %d4

loop85:						/* loop filling them */

		mov.l	%a3, %d5		/* do the first bit */
		beq	skip86

			bfins	%d2, (%a4){%d7:%d5}
			addq.l	&4, %a4
skip86:
		mov.l	%d6, %d5		/* loop moving longs */
loop87:
			mov.l	%d2, (%a4)+
		dbra	%d5, loop87

		mov.l	%a5, %d5		/* do the last bit */
		beq	skip88

			bfins	%d2, (%a4){&0:%d5}
skip88:
		add.l	%d0, %a4		/* increment the byte address */
	dbra	%d4, loop85


retn2:	movm.l	(%sp)+, &0x3cfc
	unlk	%fp
	rts

/*
 *	erase the whole screen
 */
	global	font_clear
font_clear:
	link	%fp, &0
	movm.l	&0x3f3c, -(%sp)
	mov.l	ffq, %a1			/* Font pointer */

	mov.l	font_screen(%a1), %a0		/* Get the screen info */

	mov.l	video_addr(%a0), %a2		/* get the screen address */

	mov.l	font_width(%a1), %d5		/* calc the number of pixels/line */
	mulu.l	font_maxx(%a1), %d5

	mov.l	font_offset(%a1), %d7		/* get the pixel offset from the start*/

	mov.l	%d7, %d6			/* figure it out in bytes */
	asr.l	&5, %d6	
	asl.l	&2, %d6

	add.l	%d6, %a2			/* adjust the screen pointer */

	and.l	&0x1f, %d7			/* get the inc in bits */

	mov.l	%d7, %d2			/* figure out how far to the first */
	sub.l	&32, %d2			/* longword boundary */
	neg.l	%d2
	mov.l	%d2, %a3

	mov.l	%d5, %d6			/* figure out how many longs to move */
	sub.l	%a3, %d6
	lsr.l	&5, %d6
	subq.l	&1, %d6

	mov.l	font_offset(%a1), %d0		/* see how much remainder is left */
	add.l	%d5, %d0
	and.l	&0x1f, %d0
	mov.l	%d0, %a5

	mov.l	video_mem_x(%a0), %d0		/* now see how many bytes to the next */
	sub.l	%d5, %d0			/* line */
	add.l	%a5, %d0
	sub.l	%d7, %d0
	asr.l	&3, %d0

	mov.l	font_maxy(%a1), %d4		/* find out how many scan lines to fill*/

_font_clear:					
/*
 *	a1 = font structure pointer.
 *	a2 = pointer to current screen byte.
 *	d0 = bytes per line.
 *	d4 = scan lines to fill.	
 *	d7 = the inc in bits.	
 */
	tst.l	font_inverse(%a1)
	bne	inv5
	mov.l	&0xffffffff, %d2
	bra	lll
inv5:	clr.l	%d2
lll:
	mulu.l	font_theight(%a1), %d4

loop75:						/* loop filling them */

		mov.l	%a3, %d5		/* do the first bit */
		beq	skip76

			bfins	%d2, (%a2){%d7:%d5}
			addq.l	&4, %a2
skip76:
		mov.l	%d6, %d5		/* loop moving longs */
loop77:
			mov.l	%d2, (%a2)+
		dbra	%d5, loop77

		mov.l	%a5, %d5		/* do the last bit */
		beq	skip78

			bfins	%d2, (%a2){&0:%d5}
skip78:
		add.l	%d0, %a2		/* increment the byte address */
	dbra	%d4, loop75


	movm.l	(%sp)+, &0x3cfc
	unlk	%fp
	rts

/*
 *	invert the whole screen
 */
	global	font_invertall
font_invertall:
	link	%fp, &0
	movm.l	&0x3f3c, -(%sp)
	mov.l	ffq, %a1			/* Font pointer */

	mov.l	font_screen(%a1), %a0		/* Get the screen info */

	mov.l	video_addr(%a0), %a2		/* get the screen address */

	mov.l	font_width(%a1), %d5		/* calc the number of pixels/line */
	mulu.l	font_maxx(%a1), %d5

	mov.l	font_offset(%a1), %d7		/* get the pixel offset from the start*/

	mov.l	%d7, %d6			/* figure it out in bytes */
	asr.l	&5, %d6	
	asl.l	&2, %d6

	add.l	%d6, %a2			/* adjust the screen pointer */

	and.l	&0x1f, %d7			/* get the inc in bits */

	mov.l	%d7, %d2			/* figure out how far to the first */
	sub.l	&32, %d2			/* longword boundary */
	neg.l	%d2
	mov.l	%d2, %a3

	mov.l	%d5, %d6			/* figure out how many longs to move */
	sub.l	%a3, %d6
	lsr.l	&5, %d6
	subq.l	&1, %d6

	mov.l	font_offset(%a1), %d0		/* see how much remainder is left */
	add.l	%d5, %d0
	and.l	&0x1f, %d0
	mov.l	%d0, %a5

	mov.l	video_mem_x(%a0), %d0		/* now see how many bytes to the next */
	sub.l	%d5, %d0			/* line */
	add.l	%a5, %d0
	sub.l	%d7, %d0
	asr.l	&3, %d0

	mov.l	font_maxy(%a1), %d4		/* find out how many scan lines to fill*/
	mulu.l	font_theight(%a1), %d4

loop65:						/* loop filling them */

		mov.l	%a3, %d5		/* do the first bit */
		beq	skip66

			bfextu	(%a2){%d7:%d5}, %d2
			not.l	%d2
			bfins	%d2, (%a2){%d7:%d5}
			addq.l	&4, %a2
skip66:
		mov.l	%d6, %d5		/* loop moving longs */
loop67:
			mov.l	(%a2), %d2
			not.l	%d2
			mov.l	%d2, (%a2)+
		dbra	%d5, loop67

		mov.l	%a5, %d5		/* do the last bit */
		beq	skip68

			bfextu	(%a2){&0:%d5}, %d2
			not.l	%d2
			bfins	%d2, (%a2){&0:%d5}
skip68:
		add.l	%d0, %a2		/* increment the byte address */
	dbra	%d4, loop65


	movm.l	(%sp)+, &0x3cfc
	unlk	%fp
	rts
