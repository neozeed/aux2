	#	@(#)Copyright Apple Computer 1987	Version 1.3 of clear.s on 87/11/11 21:16:10
	file	"clear.s"
	#	@(#)clear.s	UniPlus 2.1.1
	#Clear memory: clear(p, n) writes n bytes of zeros, starting at p

	global	bzero, clear
	text	

bzero:
clear:	mov.l	4(%sp),%d1	#p
	mov.l	8(%sp),%d0	#n
	beq	LS%1		#nothing to do
	add.l	%d0,%d1		#&p[n]
	mov.l	%d1,%a0		#save it
	and.l	&1,%d1		#word aligned?
	beq	LS%2		#yes, potentially long moves
	clr.b	-(%a0)		#clear up to word boundry
	sub.l	&1,%d0		#one less byte to clear
	beq	LS%1		#nothing left

LS%2:	mov.l	%d0,%d1		#copy n
	and.l	&0xFFFFFF00,%d1	#m = number of 256 byte blocks left * 256
	beq	LS%3		#none

	sub.l	%d1,%d0		#we will do this many bytes in next loop
	asr.l	&8,%d1		#number of blocks left
	movm.l	&0xFF7E,-(%sp)	#save registers
	mov.l	%d1,-(%sp)	#number of blocks goes on top of stack
	mov.l	&zeros%,%a1
	movm.l	(%a1),&0x7CFF	#clear out a bunch of registers
	mov.l	%d0,%a1		#and this one too

LS%4:	movm.l	&0xFF7E,-(%a0)	#clear out 14 longs worth
	movm.l	&0xFF7E,-(%a0)	#clear out 14 longs worth
	movm.l	&0xFF7E,-(%a0)	#clear out 14 longs worth
	movm.l	&0xFF7E,-(%a0)	#clear out 14 longs worth
	movm.l	&0xFF00,-(%a0)	#clear out 8 longs worth, total of 256 bytes
	sub.l	&1,(%sp)	#one more block, any left?
	bgt	LS%4		#yes, do another pass

	mov.l	(%sp)+,%d1	#just pop stack
	movm.l	(%sp)+,&0x7EFF	#give me back the registers

LS%3:	mov.l	%d0,%d1		#copy n left
	and.l	&0xFFFFFFFC,%d1	#this many longs left
	beq	LS%5		#none
	sub.l	%d1,%d0		#do this many in next loop

LS%6:	clr.l	-(%a0)		#clear a long's worth
	sub.l	&4,%d1		#this many bytes in a long
	bgt	LS%6		#if there are more

LS%5:	tst.l	%d0		#anything left?
	beq	LS%1		#no, just stop here

LS%7:	clr.b	-(%a0)		#clear 1 byte's worth
	sub.l	&1,%d0		#one less byte to do
	bgt	LS%7		#if any more

LS%1:	rts			#that's it

zeros%:	long	0,0,0,0,0,0,0,0,0,0,0,0,0		#13 long  of zeros
