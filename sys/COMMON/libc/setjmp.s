	#	@(#)Copyright Apple Computer 1987	Version 1.3 of setjmp.s on 87/11/11 21:16:17
	#	@(#)setjmp.s	UniPlus 2.1.1
	# C library -- setjmp, longjmp

	#	longjmp(a,v)
	# will	generate a "return(v)" from
	# the last call to
	#	setjmp(a)
	# by restoring a1-a7,d2-d7 from 'a'
	# and doing a return.
	#

	file	"setjmp.s"
	global	setjmp
	global	longjmp

setjmp:
	mov.l	(%sp)+,%a1	# pop return pc into %a1
	mov.l	(%sp),%a0	# a
	movm.l	&0xfefc,(%a0)	# save a1-a7, d2-d7 in a
	clr.l	%d0		# return 0
	jmp	(%a1)

longjmp:
	add.l	&4,%sp		# throw away this return pc
	mov.l	(%sp)+,%a0	# a
	mov.l	(%sp),%d0	# v
	bne.b	L0
	mov.l	&1,%d0		# force v non-zero
L0:
	movm.l	(%a0),&0xfefc	# restore a1-a7, d2-d7 from a
	jmp	(%a1)		# return v (but as if from setjmp)
