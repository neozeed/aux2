/*
 * @(#)wave.prim.s  {Apple version 1.3 89/12/12 22:57:18}
 *
 * Copyright (c) 1987, 1988, 1989 by Apple Computer, Inc.
 * All Rights Reserved.
 *
 * THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF APPLE COMPUTER, INC.
 * The copyright notice above does not evidence any actual or
 * intended publication of such source code.
 */

/*
 *	wave.s - lowlevel routines for the wave synth
 */
	data
	comm	ShiftAmt,2

	text
	global	Interp
	global	SetShift
	global	GetShift
	global	ShiftAmt

/*
 * stack:
 *  8:	ptr to samples
 *  4:	frac
 *  0:	return address
 */
Interp:
	mov.l	%d2,%a1
	mov.l	8(%a7),%a0
	clr.l	%d0
	clr.w	%d1
	mov.b	(%a0)+,%d0	#a
	mov.b	(%a0),%d1	#b
	sub.w	%d0,%d1		#b-a
	mov.l	4(%a7),%d2	#frac, low part
	lsr.l	&1,%d2		#make positive
	muls	%d2,%d1		#do the multiply
	asr.l	&8,%d1
	asr.l	&7,%d1		#f(b-a)
	add.b	%d1,%d0		#a+f(b-a)
	mov.l	%a1,%d2
	rts

SetShift:
	lea	ShiftAmt,%a0
	mov.l	4(%sp),%d0
	mov.w	%d0,(%a0)
	rts

GetShift:
	lea	ShiftAmt,%a0
	mov.w	(%a0),%d0
	rts
