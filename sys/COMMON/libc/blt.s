	#	@(#)Copyright Apple Computer 1987	Version 1.3 of blt.s on 87/11/11 21:16:05
	file	"blt.s"
	#	@(#)blt.s	UniPlus 2.1.1

	# Block transfer subroutine: blt(destination, source, count)
	# returns count

	global	blt
	text	

blt:	mov.l	4(%sp),%a0		# destination
	mov.l	8(%sp),%a1		# source
	mov.l	%d2,-(%sp)		# save d2
	mov.l	16(%sp),%d2		# count
	mov.l	%a0,%d0		# destination
	mov.l	%a1,%d1		# source
	cmp.l	%d2,&0x8000		# can we do everything as shorts ?
	bge	LS%1		# no. do things the hard way
	and.w	&0x1,%d0		# see if word aligned
	and.w	&0x1,%d1		# see if word aligned?
	cmp.w	%d1,%d0		# do they agree?
	beq	LS%2		# yes, can do something interesting
	mov.w	%d2,%d1		# count
	bra	LS%3		# ho, hum, just do byte moves
LS%2:	mov.w	%d2,%d1		# count
	tst.w	%d0		# are we on a word boundary?
	beq	LS%4		# yes, don't worry about fudge
	mov.b	(%a1)+,(%a0)+		# move byte to get to word boundary
	sub.w	&1,%d1		# adjust for 1 odd byte
	beq	LS%5		# done
LS%4:	mov.w	%d1,%d0		# copy remaining count
	and.w	&511,%d0		# can we do it really quick ?
	beq	LS%6		# yes.
	mov.w	%d1,%d0		# copy remaining count
	and.w	&0xFFFFFFFC,%d0		# count mod 4 is number of long words
	beq	LS%3		# hmm, must not be any
	sub.w	%d0,%d1		# long words moved * 4 = bytes moved
	asr.w	&2,%d0		# number of long words
	cmp.w	%d0,&97		# do we have a bunch to do (remember prefetch)?
	blt	LS%7		# no, just do normal moves
	movm.l	&0x7F3E,-(%sp)		# save some registers
	sub.w	&49,%d0		# start with a pre decrement
LS%8:	movm.l	(%a1)+,&0x7CFE		# block move via various registers
	movm.l	&0x7CFE,(%a0)
	movm.l	(%a1)+,&0x7CFE		# block move via various registers
	movm.l	&0x7CFE,48(%a0)
	movm.l	(%a1)+,&0x7CFE		# block move via various registers
	movm.l	&0x7CFE,96(%a0)
	movm.l	(%a1)+,&0x7CFE		# block move via various registers
	movm.l	&0x7CFE,144(%a0)
	add.w	&192,%a0		# moveml won't let me auto inc a destination
	sub.w	&48,%d0		# we moved twelve longs worth
	bge	LS%8		# yes, keep at it
	movm.l	(%sp)+,&0x7CFE		# restore registers
	add.w	&49,%d0		# restore our count
	beq	LS%3		# no, nothing but a few random bytes
LS%7:	cmp.w	%d0,&8		# do we have a bunch to do ?
	blt	LS%9		# no, just do normal moves
	sub.w	&8,%d0		# start with a pre decrement
LS%10:	mov.l	(%a1)+,(%a0)+		# copy
	mov.l	(%a1)+,(%a0)+		# copy
	mov.l	(%a1)+,(%a0)+		# copy
	mov.l	(%a1)+,(%a0)+		# copy
	mov.l	(%a1)+,(%a0)+		# copy
	mov.l	(%a1)+,(%a0)+		# copy
	mov.l	(%a1)+,(%a0)+		# copy
	mov.l	(%a1)+,(%a0)+		# copy
	sub.w	&8,%d0		# we moved eight longs worth
	bge	LS%10		# yes, keep at it
	add.w	&8,%d0
	beq	LS%3
LS%9:	sub.w	&1,%d0
LS%11:	mov.l	(%a1)+,(%a0)+		# copy as many long words as possible
	dbra	%d0,LS%11		# while alignment count
LS%3:	tst.w	%d1		# anything left to do?
	beq	LS%5		# nothing left
	sub.w	&1,%d1
LS%12:	mov.b	(%a1)+,(%a0)+		# copy any residual bytes
	dbra	%d1,LS%12		# while alignment count
LS%5:	mov.l	%d2,%d0		# just return the count
	mov.l	(%sp)+,%d2		# restore d2
	rts	

LS%1:	and.l	&0x1,%d0		# see if word aligned
	and.l	&0x1,%d1		# see if word aligned?
	cmp.w	%d1,%d0		# do they agree?
	beq	LS%13		# yes, can do something interesting
	mov.l	%d2,%d1		# count
	bra	LS%14		# ho, hum, just do byte moves
LS%13:	mov.l	%d2,%d1		# count
	tst.w	%d0		# are we on a long boundary?
	beq	LS%15		# yes, don't worry about fudge
	neg.l	%d0		# complement
	add.l	&2,%d0		# 2 - adjustment = fudge
	cmp.l	%d0,%d1		# is count bigger than fudge
	bge	LS%14		# no, must be 3 bytes or less
	sub.l	%d0,%d1		# shrink remaining count by this much
	sub.l	&1,%d0
LS%16:	mov.b	(%a1)+,(%a0)+		# move bytes to get to long boundary
	sub.l	&1,%d0		# while alignment count
	bge	LS%16
LS%15:	mov.l	%d1,%d0		# copy remaining count
	and.l	&0xFFFFFFFC,%d0		# count mod 4 is number of long words
	beq	LS%14		# hmm, must not be any
	sub.l	%d0,%d1		# long words moved * 4 = bytes moved
	asr.l	&2,%d0		# number of long words
	cmp.l	%d0,&13		# do we have a bunch to do (remember prefetch)?
	blt	LS%17		# no, just do normal moves
	movm.l	&0x7F3E,-(%sp)		# save some registers
LS%18:	movm.l	(%a1)+,&0x7CFE		# block move via various registers
	movm.l	&0x7CFE,(%a0)
	add.w	&48,%a0		# moveml won't let me auto inc a destination
	sub.l	&12,%d0		# we moved twelve longs worth
	cmp.l	%d0,&13		# do we have another 13 to go
	bge	LS%18		# yes, keep at it
	movm.l	(%sp)+,&0x7CFE		# restore registers
	tst.l	%d0		# any long's left
	beq	LS%14		# no, nothing but a few random bytes
LS%17:	sub.l	&1,%d0
LS%19:	mov.l	(%a1)+,(%a0)+		# copy as many long words as possible
	sub.l	&1,%d0
	bge	LS%19
LS%14:	tst.l	%d1		# anything left to do?
	beq	LS%5		# nothing left
	sub.l	&1,%d1
LS%20:	mov.b	(%a1)+,(%a0)+		# copy any residual bytes
	sub.l	&1,%d1
	bge	LS%20
	bra	LS%5

LS%6:	mov.w	%d1,%d0		# count divisor
	beq	LS%5		# are we really all done?
	mov.l	&9,%d1		# count divisor
	asr.w	%d1,%d0		# calculate number of 512 byte moves
	sub.w	&1,%d0		# pre decrement
	movm.l	&0x7F3E,-(%sp)		# save some registers
LS%21:	movm.l	(%a1)+,&0x7CFE		# block move via various registers
	movm.l	&0x7CFE,(%a0)
	movm.l	(%a1)+,&0x7CFE		# block move via various registers
	movm.l	&0x7CFE,48(%a0)
	movm.l	(%a1)+,&0x7CFE		# block move via various registers
	movm.l	&0x7CFE,96(%a0)
	movm.l	(%a1)+,&0x7CFE		# block move via various registers
	movm.l	&0x7CFE,144(%a0)
	movm.l	(%a1)+,&0x7CFE		# block move via various registers
	movm.l	&0x7CFE,192(%a0)
	movm.l	(%a1)+,&0x7CFE		# block move via various registers
	movm.l	&0x7CFE,240(%a0)
	movm.l	(%a1)+,&0x7CFE		# block move via various registers
	movm.l	&0x7CFE,288(%a0)
	movm.l	(%a1)+,&0x7CFE		# block move via various registers
	movm.l	&0x7CFE,336(%a0)
	movm.l	(%a1)+,&0x7CFE		# block move via various registers
	movm.l	&0x7CFE,384(%a0)
	movm.l	(%a1)+,&0x7CFE		# block move via various registers
	movm.l	&0x7CFE,432(%a0)
	add.w	&480,%a0		# moveml won't let me auto inc a destination
	mov.l	(%a1)+,(%a0)+		# copy
	mov.l	(%a1)+,(%a0)+		# copy
	mov.l	(%a1)+,(%a0)+		# copy
	mov.l	(%a1)+,(%a0)+		# copy
	mov.l	(%a1)+,(%a0)+		# copy
	mov.l	(%a1)+,(%a0)+		# copy
	mov.l	(%a1)+,(%a0)+		# copy
	mov.l	(%a1)+,(%a0)+		# copy
	dbra	%d0,LS%21		# while alignment count
	movm.l	(%sp)+,&0x7CFE		# restore registers
	mov.l	%d2,%d0		# just return the count
	mov.l	(%sp)+,%d2		# restore d2
	rts	
