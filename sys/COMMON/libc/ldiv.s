	#	@(#)Copyright Apple Computer 1987	Version 1.3 of ldiv.s on 87/11/11 21:16:13
	file	"ldiv.s"
	#	@(#)ldiv.s	UniPlus 2.1.1

	# ldiv  - long signed division
	# lrem  - long signed remainder
	# uldiv - long unsigned division
	# ulrem - long unsigned remainder

	# enter with d0 == dividend; 4(sp) == divisor

	# return with result in d0.
	# d1, a0 & a1 are blasted.
	# if divisor == 0, one divide-by-zero trap will occur.

	text

ldiv%%:	global	ldiv%%		# quotient is negative if input signs differ
	mov.l	%d2,%a0		# save d2 in a0
	mov.l	4(%sp),%d2	# get divisor in d2
	bpl.b	L%ldiv1
	neg.l	%d2		# take absolute value
L%ldiv1:	mov.l	%d0,%d1	# get dividend in d1
	bpl.b	L%ldiv2
	neg.l	%d1		# take absolute value
L%ldiv2:	eor.l	%d0,4(%sp)	# blast divisor with signs-different flag
	bsr.b	L%uldiv1	# do unsigned division
	tst.b	4(%sp)
	bpl.b	L%ldiv3
	neg.l	%d0		# negative quotient if signs different
L%ldiv3:	rts

lrem%%:	global	lrem%%		# remainder is negative if dividend is
	mov.l	%d2,%a0		# save d2 in a0
	mov.l	4(%sp),%d2	# get divisor in d2
	bpl.b	L%lrem1
	neg.l	%d2		# take absolute value
L%lrem1:	mov.l	%d0,%d1	# get dividend in d1
	bpl.b	L%lrem2
	neg.l	%d1		# take absolute value
L%lrem2:	mov.l	%d0,4(%sp)	# blast divisor with dividend
	bsr.b	L%uldiv1	# do unsigned division
	mov.l	%d1,%d0		# result is remainder
	tst.b	4(%sp)
	bpl.b	L%lrem3
	neg.l	%d0		# negative dividend means negative remainder
L%lrem3:	rts

uldiv%%: global	uldiv%%
	mov.l	%d2,%a0		# save d2 in a0
	mov.l	4(%sp),%d2	# get divisor in d2
L%uldiv0:	mov.l	%d0,%d1		# get dividend in d1
L%uldiv1:	cmp.l	%d2,&65535	# check if software divide required
	bhi.b	L%uldiv3
	mov.l	&0,%d0
	#
	# see if instruction will work (divu quits quickly if it can't do it)
	#
	divu.w	%d2,%d1		# NOTE: we trap here if divide by zero
	bvc.b	L%good		# if no overflow, we're ok
	#
	# it's necessary to do it in parts
	#
	swap.w	%d1		# can use hardware divide
	mov.w	%d1,%d0
	divu.w	%d2,%d0
	swap.w	%d0
	mov.w	%d0,%d1
	swap.w	%d1
L%uldiv2:	divu.w	%d2,%d1
L%good:
	mov.w	%d1,%d0		# d0 = unsigned quotient
	clr.w	%d1
	swap.w	%d1		# d1 = unsigned remainder
	mov.l	%a0,%d2		# restore d2
	rts

	# The divisor is known to be >= 2^16 so only 16 cycles are needed.
L%uldiv3:	mov.l	%d1,%d0
	clr.w	%d1
	swap.w	%d1
	swap.w	%d0
	clr.w	%d0

		# %d1 now             0,,hi(dividend)
		# %d0 now  lo(dividend),,0
		#              this zero ^ shifts left 1 bit per cycle,
		#              becoming top half of quotient

	mov.l	%d3,%a1		# save d3 in a1 across loop
	mov.w	&16-1,%d3	# dbra counts down to -1
L%uldiv4:	add.l	%d0,%d0	# add is 2 cycles faster than shift or rotate
	addx.l	%d1,%d1
	cmp.l	%d2,%d1
	bhi.b	L%uldiv5
	sub.l	%d2,%d1
	add.w	&1,%d0		# bottom bit changes from 0 to 1 (no carry)
L%uldiv5:	dbra	%d3,L%uldiv4
	mov.l	%a1,%d3		# restore d3
	mov.l	%a0,%d2		# restore d2
	rts

ulrem%%: global	ulrem%%
	mov.l	%d2,%a0		# save d2 in a0
	mov.l	4(%sp),%d2	# get digisor in d2
	bsr.b	L%uldiv0	# do unsigned division
	mov.l	%d1,%d0		# result is remainder
	rts
