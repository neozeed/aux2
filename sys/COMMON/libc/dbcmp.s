	#	@(#)Copyright Apple Computer 1987	Version 1.3 of dbcmp.s on 87/11/11 21:16:12
	#	@(#)dbcmp.s	UniPlus 2.1.1
	#
	#	M68000 IEEE Format Double Precision Routines
	#
	#	(C) Copyright 1983 by Motorola Inc.
	#
	#	Written by: Steve Sheahan
	#
	# dbcmp: compares two double precision numbers: "x" and "y"
	file	"dbcmp.s"

	# Input: %d0, %d1 first double precision number ("x")
	#	 4(%sp)   most significant long word of 2nd argument  ("y")
	#	 8(%sp)   least significant long word of 2nd argument
	#
	# Output: %d0 =  0 if x == y
	#	      =  1 if x > y
	#	      = -1 if x < y
	#
	# Function:
	#	This function compares two double precision numbers and returns
	#	the result of this comparison in %d0
	#
	# Register Usage
	#	%a0, %a1: temporary storage areas for %d2 and %d3
	#	%d0, %d1: "x"
	#	%d2     : holds a long word of "y"
	#	%d3     : result of comparison
	#
	# Execeptions
	#	unnormalized numbers and zero are equivalent
	#

	text	0
	global	dbcmp%%
dbcmp%%:
	mov.l	%d2,%a0		# save %d2 and %d3
	mov.l	%d3,%a1

	mov.l	%d0,%d2		# if x = 0 or unnormalized numbers
	and.l	&0x7ff00000,%d2
	bne.b	L%zeroy
	clr.l	%d0		# unnormalized numbers = 0
	clr.l	%d1
L%zeroy:
	mov.l	4(%sp),%d2	# if y = 0 or unnormalized numbers
	and.l	&0x7ff00000,%d2
	bne.b	L%begin
	clr.l	4(%sp)		# unnormalized numbers = 0
	clr.l	8(%sp)
L%begin:
	mov.l	4(%sp),%d2	# %d2: most significant long word of 2nd operand
	clr.w	%d3		# assume x == y so r = 0;
	cmp.l	%d0,%d2
	bge.b	L%great1
				# (x < y)
	mov.w	&-1,%d3		# 	r = -1;
	bra.b	L%signchk
L%great1:
	beq.b	L%second
				# (x > y)
	mov.w	&1,%d3		# 	r = 1;
	bra.b	L%signchk
L%second:			# first long word is equal check the second one
	mov.l	8(%sp),%d2	# %d2: least signicant long word of 2nd operand
				# 
	cmp.l	%d1,%d2
	bhs.b	L%great2	# (compare of 2nd word is unsigned!)
				# (x < y)
	mov.w	&-1,%d3		# 	r = -1;
	bra.b	L%signchk
L%great2:
	beq.b	L%signchk
				# (x > y)
	mov.w	&1,%d3		# 	r = 1;
L%signchk:
				# if (x < 0 && y < 0)
				# 	r = -r;
	tst.l	%d0
	bpl.b	L%return
	tst.l	4(%sp)
	bpl.b	L%return
	neg.w	%d3
L%return:
	mov.w	%d3,%d0		# return r
	mov.l	%a0,%d2		# restore %d2, %d3
	mov.l	%a1,%d3
	rts
