	text
	global	db_init
	global	nmiloc
	global	bptloc
	global	sysdebug

db_init:
	mov.l	&sysdebug,nmiloc
	mov.l	&sysdebug,bptloc
	rts

#	text
#	global debug
#debug:	
#	mov.w	&0,-(%sp)		# fake type/vector offset
#	sub.l	&4,%sp			# spot for pc
#	sub.l	&2,%sp			# spot for sr
#	mov.l	&sysdebug,-(%sp)
#	mov.w	&2,-(%sp)
#	movm.l	&0xFFFF,-(%sp)		# save all registers
#	mov.l	%usp,%a0
#	mov.l	%a0,60(%sp)		# save usr stack ptr
#	mov.l	66(%sp),%a0		# fetch interrupt routine address
#	mov.l	%sp,%a1			# save argument list pointer
#	mov.l	%a1,-(%sp)		# push ap onto stack
#	jsr	(%a0)			# jump to actual interrupt handler
#	add.l	&4,%sp
#	mov.l	60(%sp),%a0
#	mov.l	%a0,%usp		# restore usr stack ptr
#	movm.l	(%sp)+,&0x7FFF		# restore all other registers
#	add.w	&10,%sp			# sp, pop fault pc, and alignment word
#	rts				# return from whence called

	text
	global dbgcflush

dbgcflush:
	short	0x4e7a,0x1002		# movec %CACR, %d1
	short	0x08c1,0x0003		# bset &3, %d1
	short	0x4e7b,0x1002		# movec %d1, %CACR
	rts
