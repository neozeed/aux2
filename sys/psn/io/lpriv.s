 #
 # Copyright 1987, 1988, 1989 Apple Computer, Inc.
 # All Rights Reserved.
 #
 # THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF APPLE COMPUTER, INC.
 # The copyright notice above does not evidence any actual or
 # intended publication of such source code.
 #
 #	@(#)Copyright Apple Computer 1989	Version 1.3 of lpriv.s on 90/04/27
 #	file	"lpriv.s"

#define LOCORE
#define __sys_signal_h
#include "mch.h"
#include "sys/param.h"

	set	_ORI,    0x007c
	set	_ANDI,   0x027c
	set	_EORI,   0x0a7c
	set	_MOVES,  0x0e00
	set	_MFRSR,  0x40c0
	set	_MTOSR,  0x46c0
	set	_RTE,	 0x4e73
	set	_MTOUSP, 0x4e60
	set	_MFRUSP, 0x4e68	
	set	_MFRCTRL,0x4e7a
	set	_MTOCTRL,0x4e7b
	set	_FSAVE,  0xf300
	set	_FRESTOR,0xf340

	set	_PC, 76
	set	_SR, 74
	set	_USP,64
	set	_A0, 36
	set	_D0,  4

	set	FRAMESIZE,60

	global	fprest, fault2%
	global	lpriv
lpriv:
	sub.w	&10,%sp			# fake ra + align ps + reserve usp
	movm.l	&0xFFFE,-(%sp)		# save all registers but a7
	mov.l	%usp,%a0
	mov.l	%a0,60(%sp)		# save usr stack ptr

	pea.l	lpriv_fault		# go here on a bad user address
	mov.l	%sp,UDOT+U_FLTSP%

	mov.l	_PC(%sp),%a0
	movs.l	(%a0),%d2
	mov.l	%d2,%d3
	swap	%d3

	cmp.w	%d3,&_MFRCTRL
	beq	L%MFRCTRL
	cmp.w	%d3,&_MTOCTRL
	beq.b	L%MTOCTRL
	cmp.w	%d3,&_ORI
	beq	L%ORI
	cmp.w	%d3,&_ANDI
	beq	L%ANDI
	cmp.w	%d3,&_EORI
	beq	L%EORI
	cmp.w	%d3,&_RTE
	bne	L%others
L%RTE:
	mov.l	_USP(%sp),%a0
	movs.w	6(%a0),%d4
	and.w	&0xf000,%d4
	beq.b	L%73
	cmp.w	%d4,&0x2000
	bne	lpriv_failed
L%73:
	movs.l	2(%a0),%d0
	mov.l	%d0,_PC(%sp)
	movs.w	(%a0),%d6
	tst.w	%d4
	bne.b	L%74
	add.l	&8,_USP(%sp)
	bra	L%setupsr
L%74:
	add.l	&12,_USP(%sp)
	bra	L%setupsr


L%MTOCTRL:
	mov.w	%d2,%d0
	lsr.w	&12,%d0
	lea.l	(%sp,%d0.w*4,_D0),%a2
	and.w	&0x0fff,%d2

	cmp.w	%d2,&2
	bne.b	L%tousp
	mov.l	(%a2),%d0
	mov.l	%d0,%cacr
	mov.l	%d0,UDOT+U_USER%+8
	add.l	&4,_PC(%sp)
	bra	lpriv_success
L%MFRCTRL:
	mov.w	%d2,%d0
	lsr.w	&12,%d0
	lea.l	(%sp,%d0.w*4,_D0),%a2
	and.w	&0x0fff,%d2

	cmp.w	%d2,&2
	bne.b	L%frusp
	mov.l	%cacr,%d0
	mov.l	%d0,(%a2)
	add.l	&4,_PC(%sp)
	bra	lpriv_success
L%tousp:
	cmp.w	%d2,0x0800
	bne.b	L%dummyctrl
	mov.l	(%a2),_USP(%sp)
	add.l	&4,_PC(%sp)
	bra	lpriv_success
L%frusp:
	cmp.w	%d2,0x0800
	bne.b	L%dummyctrl
	mov.l	_USP(%sp),(%a2)
	add.l	&4,_PC(%sp)
	bra	lpriv_success
L%dummyctrl:
	tst.w	%d2
	beq.b	L%dummy
	cmp.w	%d2,&0x0001
	beq.b	L%dummy
	cmp.w	%d2,&0x0801
	beq.b	L%dummy
	cmp.w	%d2,&0x0802
	beq.b	L%dummy
	cmp.w	%d2,&0x0803
	beq.b	L%dummy
	cmp.w	%d2,&0x0804
	bne	lpriv_failed
L%dummy:
	cmp.w	%d3,&_MFRCTRL
	bne.b	L%69
	clr.l	(%a2)
L%69:
	add.l	&4,_PC(%sp)
	bra	lpriv_success


L%ANDI:
	mov.w	_SR(%sp),%d6
	or.w	UDOT+U_SR%,%d6
	and.w	%d2,%d6
	add.l	&4,_PC(%sp)
	br.b	L%setupsr
L%EORI:
	mov.w	_SR(%sp),%d6
	or.w	UDOT+U_SR%,%d6
	eor.w	%d2,%d6
	add.l	&4,_PC(%sp)
	br.b	L%setupsr
L%ORI:
	mov.w	_SR(%sp),%d6
	or.w	UDOT+U_SR%,%d6
	or.w	%d2,%d6
	add.l	&4,_PC(%sp)
L%setupsr:
	mov.w	%d6,%d0
	and.w	&0xc700,%d0
	mov.w	%d0,UDOT+U_SR%
	lsr.w	&8,%d0
	mov.b	%d0,([UDOT+U_PROCP%],P_SR%)
	and.w	&0x3f00,_SR(%sp)
	and.w	&0xc0ff,%d6
	or.w	%d6,_SR(%sp)
lpriv_success:
	clr.l	UDOT+U_FLTSP%
	add.w	&4,%sp			# get rid of fault handler address
	tst.b	UDOT+U_FPSAVED%
	bne.b	lpriv_fp
lpriv_rte:
	mov.l	60(%sp),%a0		# no, just return normally
	mov.l	%a0,%usp		# restore usr stack ptr
	movm.l	(%sp)+,&0x7FFF		# restore all other registers
	add.w	&10,%sp			# sp, pop fault pc, and alignment word
	rte				# return from whence called
lpriv_fp:
	jsr	fprest
	br.b	lpriv_rte



L%others:
	mov.l	&7,%d5
	and.w	%d3,%d5
	lea.l	(%sp,%d5.l*4,_A0),%a2

	mov.w	%d3,%d0
	and.w	&0xffc0,%d0
	cmp.w	%d0,&_MFRSR
	beq	L%MFRSR
	cmp.w	%d0,&_MTOSR
	beq	L%MTOSR
	cmp.w	%d0,&_FSAVE
	beq	L%FSAVE
	cmp.w	%d0,&_FRESTOR
	beq	L%FRESTOR

	mov.w	%d3,%d0
	and.w	&0xfff8,%d0
	cmp.w	%d0,&_MTOUSP
	beq.b	L%MTOUSP
	cmp.w	%d0,&_MFRUSP
	beq.b	L%MFRUSP
	and.w	&0xff00,%d0
	cmp.w	%d0,&_MOVES
	beq.w	L%MOVES
lpriv_failed:
	clr.l	UDOT+U_FLTSP%
	add.w	&4,%sp			# get rid of fault handler address
	jmp	fault2%	
lpriv_fault:
	mov.w	&0x0008,76(%sp)		# a bus fault occurred
	jmp	fault2%			# when referencing a user supplied address
					# need to fake a funny bus fault vector

L%MFRUSP:
	mov.l	_USP(%sp),(%a2)
	add.l	&2,_PC(%sp)
	bra	lpriv_success
L%MTOUSP:
	mov.l	(%a2),_USP(%sp)
	add.l	&2,_PC(%sp)
	bra	lpriv_success


L%FRESTOR:			# in case we're doing a (d16,An)
	ext.l	%d2		# sign extexnd the 16 bit displacement
	mov.w	%d3,%d4
	lsr.w	&3,%d4
	and.l	&7,%d4

	sub.l	&FRAMESIZE,%sp	# need room for the FP context frame
	mov.l	%sp,%a3		# keep pointer to it
	cmp.b	%d4,&5
	beq	L%150
	cmp.b	%d4,&2
	beq	L%148
	cmp.b	%d4,&3
	bne	L%failed
L%148:
	mov.l	&0,%d2
L%150:
	mov.l	&4,%d7
	add.l	(%a2),%d2
	mov.l	%d7,-(%sp)
	mov.l	%a3,-(%sp)
	mov.l	%d2,-(%sp)
	jsr	copyin
	add.l	&12,%sp
	tst.l	%d0
	bne.b	L%fault
	tst.b	(%a3)
	beq.b	L%153
	mov.w	fpversion,%d0
	cmp.w	%d0,(%a3)
	bne	L%failed
L%154:
	mov.l	&0,%d0
	mov.b	1(%a3),%d0
	mov.l	%d0,-(%sp)
	pea.l	4(%a3)
	add.l	&4,%d2
	mov.l	%d2,-(%sp)
	jsr	copyin
	add.l	&12,%sp
	tst.l	%d0
	bne	L%fault
	mov.l	&0,%d0
	mov.b	1(%a3),%d0
	add.l	%d0,%d7
L%153:
	frestore (%a3)
	add.l	&FRAMESIZE,%sp	# get rid of reserved space
	clr.b	UDOT+U_FPSAVED%
	cmp.b	%d4,&3
	bne	L%155
	add.l	%d7,(%a2)
L%156:
	add.l	&2,_PC(%sp)
	bra	lpriv_success
L%failed:
	add.l	&FRAMESIZE,%sp	# get rid of reserved space
	bra	lpriv_failed
L%fault:
	add.l	&FRAMESIZE,%sp	# get rid of reserved space
	clr.l	UDOT+U_FLTSP%
	bra	lpriv_fault


L%FSAVE:			# in case we're doing a (d16,An)
	ext.l	%d2		# sign extexnd the 16 bit displacement
	mov.w	%d3,%d4
	lsr.w	&3,%d4
	and.l	&7,%d4

	sub.l	&FRAMESIZE,%sp	# reserve space for FSAVE
	mov.l	%sp,%a3		# keep pointer to it
	fnop
	fsave	(%a3)
	mov.l	&4,%d7
	tst.b	(%a3)
	beq.b	L%135
	mov.l	&0,%d0
	mov.b	1(%a3),%d0
	add.l	%d0,%d7
L%135:
	cmp.b	%d4,&2
	beq.b	L%139
	cmp.b	%d4,&5
	beq.b	L%140
	cmp.b	%d4,&4
	bne.b	L%failed
L%138:
	sub.l	%d7,(%a2)
L%139:
	mov.l	&0,%d2
L%140:
	mov.l	%d7,-(%sp)
	mov.l	(%a2),%d0
	add.l	%d2,%d0
	mov.l	%d0,-(%sp)
	mov.l	%a3,-(%sp)
	jsr	copyout
	add.l	&12,%sp
	tst.l	%d0
	bne	L%fault
	add.l	&FRAMESIZE,%sp	# get rid of reserved space
L%155:
	cmp.b	%d4,&5
	bne	L%156
	add.l	&4,_PC(%sp)
	bra	lpriv_success


L%MFRSR:
	mov.w	%d3,%d4
	lsr.w	&3,%d4
	and.l	&7,%d4
	mov.w	_SR(%sp),%d6
	or.w	UDOT+U_SR%,%d6

	tst.b	%d4			# mov	%sr,Dn
	beq.b	L%90
	cmp.b	%d4,&4			# mov	%sr,-(An)
	beq	L%94
	cmp.b	%d4,&2			# mov	%sr,(An)
	beq.b	L%92
	cmp.b	%d4,&5			# mov	%sr,(d16,An)
	beq	L%95
	cmp.b	%d4,&3			# mov	%sr,(An)+
	beq.b	L%93
	cmp.b	%d4,&7			# mov	%sr,(xxx).?
	bne	lpriv_failed
L%97:
	tst.b	%d5
	beq.b	L%970
	cmp.b	%d5,&1
	bne	lpriv_failed
L%971:
	mov.l	_PC(%sp),%a0		# mov	%sr,(xxx).L
	movs.l	2(%a0),%a0
	movs.w	%d6,(%a0)
	add.l	&6,_PC(%sp)
	bra	lpriv_success
L%970:
	mov.l	&0,%d0
	mov.w	%d2,%d0
	movs.w	%d6,(%d0.l)		# mov	%sr,(xxx).W
	add.l	&4,_PC(%sp)
	bra	lpriv_success
L%90:					# mov   %sr,Dn
	mov.w	%d6,(%sp,%d5.l*4,_D0+2)
	add.l	&2,_PC(%sp)
	bra	lpriv_success
L%92:					# mov   %sr,(An)
	mov.l	(%a2),%a0
	movs.w	%d6,(%a0)
	add.l	&2,_PC(%sp)
	bra	lpriv_success
L%93:					# mov   %sr,(An)+
	mov.l	(%a2),%a0
	movs.w	%d6,(%a0)
	add.l	&2,(%a2)
	add.l	&2,_PC(%sp)
	bra	lpriv_success
L%94:					# mov	%sr,-(An)
	sub.l	&2,(%a2)
	mov.l	(%a2),%a0
	movs.w	%d6,(%a0)
	add.l	&2,_PC(%sp)
	bra	lpriv_success
L%95:					# mov	%sr,(d16,An)
	ext.l	%d2
	movs.w	%d6,([%a2],%d2.l)
	add.l	&4,_PC(%sp)
	bra	lpriv_success


L%MTOSR:
	mov.w	%d3,%d4
	lsr.w	&3,%d4
	and.l	&7,%d4

	tst.b	%d4			# mov	Dn,%sr
	beq.b	L%100
	cmp.b	%d4,&3			# mov	(An)+,%sr
	beq.b	L%103
	cmp.b	%d4,&2			# mov	(An),%sr
	beq.b	L%102
	cmp.b	%d4,&5			# mov	(d16,An),%sr
	beq	L%105
	cmp.b	%d4,&4			# mov	-(An),%sr
	beq	L%104
	cmp.b	%d4,&7			# mov	(xxx).?,%sr
	bne	lpriv_failed
L%107:
	tst.b	%d5
	beq.b	L%1070
	cmp.b	%d5,&1
	beq.b	L%1071
	cmp.b	%d5,&4
	bne	lpriv_failed
L%1074:					# mov	#<data>,%sr
	mov.w	%d2,%d6
	add.l	&4,_PC(%sp)
	bra	L%setupsr
L%1070:
	mov.l	&0,%d0			# mov	(xxx).W,%sr
	mov.w	%d2,%d0
	movs.w	(%d0.l),%d6
	add.l	&4,_PC(%sp)
	bra	L%setupsr
L%1071:
	mov.l	_PC(%sp),%a0		# mov	(xxx).L,%sr
	movs.l	2(%a0),%a0
	movs.w	(%a0),%d6
	add.l	&6,_PC(%sp)
	bra	L%setupsr	
L%100:					# mov	Dn,%sr
	mov.w	(%sp,%d5.l*4,_D0+2),%d6
	add.l	&2,_PC(%sp)
	bra	L%setupsr
L%102:					# mov	(An),%sr
	mov.l	(%a2),%a0
	movs.w	(%a0),%d6
	add.l	&2,_PC(%sp)
	bra	L%setupsr
L%103:					# mov	(An)+,%sr
	mov.l	(%a2),%a0
	movs.w	(%a0),%d6
	add.l	&2,(%a2)
	add.l	&2,_PC(%sp)
	bra	L%setupsr
L%104:					# mov	-(An),%sr
	sub.l	&2,(%a2)
	mov.l	(%a2),%a0
	movs.w	(%a0),%d6
	add.l	&2,_PC(%sp)
	bra	L%setupsr
L%105:					# mov	(d16,An),%sr
	ext.l	%d2
	movs.w	([%a2],%d2.l),%d6
	add.l	&4,_PC(%sp)
	bra	L%setupsr



L%MOVES:
	btst	&11,%d2			# cant handle writes
	bne	lpriv_failed
	lsr.w	&12,%d2
	lea.l	(%sp,%d2.w*4,_D0),%a3	# fetch pointer to target register

	mov.w	%d3,%d4
	lsr.w	&3,%d4			# fetch mode
	and.b	&7,%d4
	lsr.w	&6,%d3			# fetch size
	and.b	&3,%d3

	cmp.b	%d4,&1
	bne.b	L%201	
	mov.l	(%a2),%a0		# movs	  (An),Rn
	mov.l	&4,%d6
L%189:
	tst.b	%d3
	bne.b	L%190
	mov.l	&7,%d0
	mov.l	%d0,%sfc
	movs.b	(%a0),%d1
	mov.b	%d1,3(%a3)
	mov.l	&1,%d0
	mov.l	%d0,%sfc
	add.l	%d6,_PC(%sp)
	bra	lpriv_success
L%190:
	cmp.b	%d3,&1
	bne.b	L%191
	mov.l	&7,%d0
	mov.l	%d0,%sfc
	movs.w	(%a0),%d1
	mov.w	%d1,2(%a3)
	mov.l	&1,%d0
	mov.l	%d0,%sfc
	add.l	%d6,_PC(%sp)
	bra	lpriv_success
L%191:
	cmp.b	%d3,&2
	bne	lpriv_failed
	mov.l	&7,%d0
	mov.l	%d0,%sfc
	movs.l	(%a0),%d1
	mov.l	%d1,(%a3)
	mov.l	&1,%d0
	mov.l	%d0,%sfc
	add.l	%d6,_PC(%sp)
	bra	lpriv_success
	
L%201:
	cmp.b	%d4,&7
	bne	lpriv_failed
	tst.b	%d5
	bne.b	L%202
	mov.l	_PC(%sp),%a0		# movs.w  (xxx).W,Rn
	mov.l	&0,%d0
	movs.w	4(%a0),%d0
	mov.l	%d0,%a0
	mov.l	&6,%d6
	bra	L%189
L%202:
	cmp.b	%d5,&1
	bne	lpriv_failed
	mov.l	_PC(%sp),%a0		# movs.l  (xxx).L,Rn
	movs.l	4(%a0),%a0
	mov.l	&8,%d6
	bra	L%189

