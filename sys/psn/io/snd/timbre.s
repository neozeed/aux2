/*
 * @(#)timbre.s  {Apple version 1.4 90/03/10 17:55:42}
 *
 * Copyright (c) 1987, 1988, 1989 by Apple Computer, Inc.
 * All Rights Reserved.
 *
 * THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF APPLE COMPUTER, INC.
 * The copyright notice above does not evidence any actual or
 * intended publication of such source code.
 */

/*
 *	setTimbre -
 *	Set a buffer to a timbre based on a value from zero to 255, where
 *	zero is somewhat pure, and 255 has lots of harmonics.  The buffer
 *	size is 512.
 *
 *	on entry -	
 *		4(SP): amp (0-255)	8(SP): timbre (0-255)	12(SP): buff Ptr
 *	in use -
 *		D0: amp		D1: timbre	D2: i		D3: midPt
 *		D4: phase	D5: inc		D6: temp
 *		A0: buff	A1: sine	A2: a		A3: b
 *
 */

#define BufferSize	512
#define BufferLn	9
#define TMax		256
#define TMaxLn		8
#define QtrSin		128

	text
	global setTimbre
setTimbre:
	mov.l	4(%sp),%d0		/* get amp in range (0 .. 255) */
	mov.l	&TMax,%d1		/* get timbre in range (TMax = pure, 0 = noisy) */
	sub.l	8(%sp),%d1
	mov.l	12(%sp),%a0		/* get pointer to buffer */
	movm.l	&0xfff0,-(%sp)
	
	lea	QuarterSine,%a1		/* point to sine table */
	
	mov.l	%d1,%d3 		/* calc midpoint = t/TMax * BufferSize / 2 */
			
	/* fill first half of buffer */
	clr.l	%d4			/* clear phase */
	mov.l	&0x1000000,%d5		/* calc inc = (Fixed)TMax/t */
	divu.l	%d3,%d5

	lea	(%a0),%a2		/* start at begining and midpoint */
	lea	0(%a0,%d3.l),%a3
			
loop1:	mov.l	%d4,%d6			/* get sine[phase] */
	swap	%d6
	mov.b	0(%a1,%d6.w),%d6
	ext.w	%d6
	
	mulu	%d0,%d6			/* amp adjust */
	asr.l	&8,%d6			/* put back in range (offset binary) */
	add.l	&0x80,%d6

	mov.b	%d6,(%a2)+		/* fill buffer ends */
	mov.b	%d6,-(%a3)

	add.l	%d5,%d4			/* bump phase */
	cmp.l	%a2,%a3
	blt.s	loop1
			
			
	/* fill second half of buffer */
	clr.l	%d4			/* clear phase */
	mov.l	&0x1000000,%d5	/* calc inc = (Fixed)TMax/(2*TMax - t) */
	mov.l	&BufferSize,%d6
	sub.l	%d3,%d6
	divu.l	%d6,%d5

	lea	0(%a0,%d3.l),%a2	/* start at midpoint and end */
	lea	BufferSize(%a0),%a3

loop2:	mov.l	%d4,%d6			/* get sine[phase] */
	swap	%d6
	mov.b	0(%a1,%d6.w),%d6
	ext.w	%d6

	mulu	%d0,%d6			/* amp adjust */
	asr.l	&8,%d6			/* put back in range */
	neg.l	%d6			/* and negate it */
	add.l	&0x80,%d6

	mov.b	%d6,(%a2)+		/* fill buffer ends */
	mov.b	%d6,-(%a3)

	add.l	%d5,%d4			/* bump phase */
	cmp.l	%a2,%a3
	blt.s	loop2

	movm.l	(%sp)+,&0x0fff
	rts
			
	data

QuarterSine:
	short	0x0103,0x0406,0x0709,0x0A0C
	short	0x0E0F,0x1112,0x1415,0x1718
	short	0x1A1B,0x1D1E,0x2022,0x2325
	short	0x2627,0x292A,0x2C2D,0x2F30
	short	0x3233,0x3536,0x3739,0x3A3C
	short	0x3D3E,0x4041,0x4244,0x4546
	short	0x4849,0x4A4B,0x4D4E,0x4F50
	short	0x5253,0x5455,0x5657,0x595A
	short	0x5B5C,0x5D5E,0x5F60,0x6162
	short	0x6364,0x6566,0x6768,0x696A
	short	0x6A6B,0x6C6D,0x6E6E,0x6F70
	short	0x7171,0x7273,0x7374,0x7575
	short	0x7676,0x7778,0x7879,0x797A
	short	0x7A7A,0x7B7B,0x7C7C,0x7C7D
	short	0x7D7D,0x7D7E,0x7E7E,0x7E7E
	short	0x7F7F,0x7F7F,0x7F7F,0x7F7F,0x7F7f

