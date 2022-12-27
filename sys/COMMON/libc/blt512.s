	#	@(#)Copyright Apple Computer 1989	Version 1.4 of blt512.s on 89/05/22 14:25:34
	file	"blt512.s"
	#	@(#)blt512.s	UniPlus 2.1.1
	# Block transfer subroutine:
	#     blt512(destination, source, count)

	global	blt512
	text	

blt512:
	movm.l	4(%sp),&0x0301		# fetch paramaters into a0/a1/d0
	movm.l	&0x3f3e,-(%sp)		# save d2-d7/a2-a6
	exg	%d0,%a1			# get paramaters into proper registers

	lsl.l	&8,%d0
	lsl.l	&1,%d0			# multiply count by 512
	add.l	%d0,%a0			# point at end of buffers
	add.l	%d0,%a1			# so we can copy backwards
blt512copy:
	lea.l	-512(%a0),%a0		# is this the last
	cmp.l	%a0,52(%sp)		#   512 byte block to copy ?
					# the following moves dont affect condition codes
	movm.l	468(%a0),&0x7cfc
	movm.l	&0x3f3e,-(%a1)		# partial block move of 44 bytes

	movm.l	416(%a0),&0x7cff
	movm.l	&0xff3e,-(%a1)		# block move via various registers
	movm.l	364(%a0),&0x7cff
	movm.l	&0xff3e,-(%a1)		# block move via various registers
	movm.l	312(%a0),&0x7cff
	movm.l	&0xff3e,-(%a1)		# block move via various registers
	movm.l	260(%a0),&0x7cff
	movm.l	&0xff3e,-(%a1)		# block move via various registers
	movm.l	208(%a0),&0x7cff
	movm.l	&0xff3e,-(%a1)		# block move via various registers
	movm.l	156(%a0),&0x7cff
	movm.l	&0xff3e,-(%a1)		# block move via various registers
	movm.l	104(%a0),&0x7cff
	movm.l	&0xff3e,-(%a1)		# block move via various registers
	movm.l	52(%a0),&0x7cff
	movm.l	&0xff3e,-(%a1)		# block move via various registers
	movm.l	(%a0),&0x7cff
	movm.l	&0xff3e,-(%a1)		# block move via various registers
	bhi.b	blt512copy

	movm.l	(%sp)+,&0x7cfc		# restore registers
	rts	
