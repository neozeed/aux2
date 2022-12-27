	#	@(#)Copyright Apple Computer 1987	Version 1.3 of font2.s on 87/11/11 21:39:02
/*	@(#)font2.s	*/
/*
 *
 */
 #
 #	Warning!!! ----> this only runs on 68020s ......
 #

#define	D(x, y)	global dd ; \
		movm.l	&0xc0c0, -(%sp) ;\
		mov.l	y, -(%sp) ;\
		mov.l	&x, -(%sp) ;\
		jsr	dd ;\
		addq.l	&8, %sp ;\
		movm.l	(%sp)+, &0x0303
	
#define	A(x, y)	global aa ; \
		movm.l	&0xc0c0, -(%sp) ;\
		mov.l	y, -(%sp) ;\
		mov.l	&x, -(%sp) ;\
		jsr	aa ;\
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

/*
 *	font_erase(fp, xs, ys, xl, yl)
 */
#define	xs	12(%fp)
#define	ys	16(%fp)
#define	xl	20(%fp)
#define	yl	24(%fp)

	global font_erase
font_erase:
	link	%fp, &0
	movm.l	&0x3f30, -(%sp)
	mov.l	ffp, %a1		/* font pointer */
	mov.l	font_screen(%a1), %a2
	mov.l	video_addr(%a2), %a0
	tst.l	font_inverse(%a1)
	bne	inv
	mov.l	&0xffffffff, %d5
	bra	cont
inv:	clr.l	%d5
cont:	
	mov.l	yl, %d1
	bge	next
retn:
	movm.l	(%sp)+, &0x0cfc
	unlk	%fp
	rts
next:	mov.l	ys, %d0
	cmp.l	%d0, font_maxy(%a1)
	bgt	retn
	cmp.l	%d0, %d1
	bgt	retn
	blt	all
	mov.l	xs, %d1
	mov.l	xl, %d2
	jsr	partial
	bra	retn
all:	mov.l	xs, %d1
	mov.l	font_maxx(%a1), %d2 
	subq.l	&1, %d2
	jsr 	partial
	mov.l	ys, %d0
	addq.l	&1, %d0
	mov.l	yl, %d3
	addq.l	&1, %d3
	cmp.l	%d0, %d3
	bge	skip
	mov.l	xs, %d1
	mov.l	xl, %d2
	addq.l	&1, %d2
	jsr	total
skip:	mov.l	ys, %d0
	clr.l	%d1
	mov.l	xs, %d2
	jsr	partial
	bra	retn

/*
 *	do a partial line erase ... from %d1 to %d2 on line %d0
 */

partial:	
	mov.l	font_theight(%a1), %d4		/* get the line count */
	mov.l	font_width(%a1), %d6		/* get the char width */
	mulu.l	font_linewidth(%a1), %d0	/* calc the bit offset */
	sub.l	%d1, %d2
	addq.l	&1, %d2				/* get the bits left */
	mov.l	%d2, %d3
	mulu.l	%d6, %d3			/* turn chars into bits */
	sub.l	video_mem_x(%a2), %d3
	neg.l	%d3				/* amount left at end */
	mulu.l	%d6, %d1
	add.l	%d1, %d0			/* the last of the bit offset */
	add.l	font_offset(%a1), %d0
	subq.l	&1, %d2	
loop2:
		mov.l	%d2, %d1
loop1:
			bfins	%d5, (%a0){%d0:%d6}
			add.l	%d6, %d0
			dbra	%d1, loop1
		add.l	%d3, %d0
		dbra	%d4, loop2
	rts

/*
 *	do a line erase ... from %d0 to %d3, with a border before %d1 and after %d2
 */


	data	1
L%15:
	long	0
	even
L%16:
	long	0
	even
L%17:
	long	0
	even
L%18:
	long	0
	even
	text
total:
	mov.l	%d0, L%15
	mov.l	%d3, L%16
eloop:
	clr.l	%d1
	mov.l	font_maxx(%a1), %d2
	subq.l	&1, %d2
	jsr	partial
	addq.l	&1, L%15
	mov.l	L%15, %d0
	cmp.l	%d0, L%16
	bne	eloop
	rts

/*
 *	font_invert(fp, x, y)	invert a character
 */

	global	font_invert
font_invert:
	link	%fp, &0
	movm.l	&0x3000, -(%sp)
	mov.l	ffp, %a1
	mov.l	ys, %d0
	mulu.l	font_linewidth(%a1), %d0
	mov.l	xs, %d1
	mov.l	font_width(%a1), %d2
	mulu.l	%d2, %d1
	add.l	%d1, %d0
	add.l	font_offset(%a1), %d0
	mov.l	font_screen(%a1), %a0
	mov.l	video_mem_x(%a0), %d1
	mov.l	video_addr(%a0), %a0
	mov.l	font_theight(%a1), %d3
	subq.l	&1, %d3
loop5:
		bfchg	(%a0){%d0:%d2}
		add.l	%d1, %d0
		dbra	%d3, loop5
	movm.l	(%sp)+, &0x000c
	unlk	%fp
	rts
/*
 *	font_delete(fp, x, y, l)	delete a character(s)
 */
#define	len	20(%fp)

	global	font_delete
font_delete:
	link	%fp, &0
	movm.l	&0x3f30, -(%sp)
	mov.l	ffp, %a1
	mov.l	ys, %d0
	mulu.l	font_linewidth(%a1), %d0
	mov.l	font_maxx(%a1), %d4
	mov.l	xs, %d1
	sub.l	%d1, %d4
	ble	retn4
	mov.l	font_width(%a1), %d2
	mulu.l	%d2, %d1
	add.l	%d1, %d0
	mov.l	font_screen(%a1), %a2
	mov.l	video_addr(%a2), %a0
	mov.l	video_mem_x(%a2), %d1
	mov.l	font_theight(%a1), %d3
	subq.l	&1, %d3
	mov.l	len, %d6
	ble	retn4
	sub.l	%d6, %d4
	ble	del
	mulu.l	%d2, %d6
	mov.l	%d4, %d7
	mulu.l	%d2, %d7
	sub.l	%d7, %d1
	add.l	font_offset(%a1), %d0
	add.l	%d0, %d6
	subq.l 	&1, %d4
loop6:
		mov.l	%d4, %d7
loop7:
			bfextu	(%a0){%d6:%d2}, %d5
			bfins	%d5, (%a0){%d0:%d2}
			add.l	%d2, %d6
			add.l	%d2, %d0
			dbra	%d7, loop7
		add.l	%d1, %d6
		add.l	%d1, %d0
		dbra	%d3, loop6
del:
	tst.l	font_inverse(%a1)
	bne	inv2
	mov.l	&0xffffffff, %d5
	bra	cont2
inv2:	clr.l	%d5
cont2:	
	mov.l	ys, %d0
	mov.l	font_maxx(%a1), %d2
	mov.l	%d2, %d1
	sub.l	len, %d1
	cmp.l	%d1, xs
	bge	xxx
	mov.l	xs, %d1
xxx:	
	subq.l	&1, %d2
	jsr	partial
retn4:
	movm.l	(%sp)+, &0x0cfc
	unlk	%fp
	rts
/*
 *	font_insert(fp, x, y, l)	insert a character(s)
 */
#define	len	20(%fp)

	global	font_insert
font_insert:
	link	%fp, &0
	movm.l	&0x3f30, -(%sp)
	mov.l	ffp, %a1
	mov.l	ys, %d0
	mulu.l	font_linewidth(%a1), %d0
	mov.l	font_maxx(%a1), %d4
	mov.l	%d4, %d1
	sub.l	xs, %d4
	ble	retn5
	mov.l	font_width(%a1), %d2
	mulu.l	%d2, %d1
	add.l	%d1, %d0
	mov.l	font_screen(%a1), %a2
	mov.l	video_addr(%a2), %a0
	mov.l	video_mem_x(%a2), %d1
	mov.l	font_theight(%a1), %d3
	subq.l	&1, %d3
	mov.l	len, %d6
	ble	retn5
	sub.l	%d6, %d4
	ble	del2
	mulu.l	%d2, %d6
	neg.l	%d6
	mov.l	%d4, %d7
	mulu.l	%d2, %d7
	add.l	%d7, %d1
	add.l	font_offset(%a1), %d0
	add.l	%d0, %d6
	subq.l 	&1, %d4
loop8:
		mov.l	%d4, %d7
loop9:
			sub.l	%d2, %d6
			sub.l	%d2, %d0
			bfextu	(%a0){%d6:%d2}, %d5
			bfins	%d5, (%a0){%d0:%d2}
			dbra	%d7, loop9
		add.l	%d1, %d6
		add.l	%d1, %d0
		dbra	%d3, loop8
del2:
	tst.l	font_inverse(%a1)
	bne	inv3
	mov.l	&0xffffffff, %d5
	bra	cont3
inv3:	clr.l	%d5
cont3:	
	mov.l	ys, %d0
	mov.l	xs, %d1
	mov.l	%d1, %d2
	add.l	len, %d2
	cmp.l	%d2, font_maxx(%a1)
	ble	xxx2
	mov.l	font_maxx(%a1), %d2
xxx2:	
	subq.l	&1, %d2
	jsr	partial
retn5:
	movm.l	(%sp)+, &0x0cfc
	unlk	%fp
	rts
