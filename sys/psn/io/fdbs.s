	#	@(#)Copyright Apple Computer 1987	Version 1.3 of fdbs.s on 87/11/11 21:38:53
/*	@(#)fdbs.s	UniPlus VVV.2.1.8	*/
/*
 * (C) 1986 UniSoft Corp. of Berkeley CA
 *
 * UniPlus Source Code. This program is proprietary
 * with Unisoft Corporation and is not to be reproduced
 * or used in any manner except as authorized in
 * writing by Unisoft.
 */

	global fdb_intr
	global Xfdbint
	global fdb_command
	global fdb_timeout
	global fdb_select
	global fdb_pollflg
	global fdb_exb
	global fdb_state
	global fdb_error
	global fdb_cnt
	global idleflg
	global via1intr
	global via1_soft
	global via2intr
	global via2_soft
	global call%

	set VIA,	0x50000000
	set VIA_ORB,	0x0000
	set VIA_ORA,	0x1E00
	set VIA_DDRB,	0x0400
	set VIA_DDRA,	0x0600
	set VIA_T1CL,	0x0800
	set VIA_T1CH,	0x0A00
	set VIA_T1LL,	0x0C00
	set VIA_T1HL,	0x0E00
	set VIA_T2CL,	0x1000
	set VIA_T2CH,	0x1200
	set VIA_SR,	0x1400
	set VIA_ACR,	0x1600
	set VIA_PCR,	0x1800
	set VIA_IFR,	0x1A00
	set VIA_IER,	0x1C00

Xfdbint:
	btst	&2, VIA+VIA_IFR		# Check to see if it was a FDB (sr)
	bne	do_fdb			#	interrupt
do_intr:
	mov.w	&0x2100, %sr
	mov.l	&via1intr, (%sp)	# If it isn't do an indirect call of
	mov.w	idleflg, -(%sp)		#	via1intr
	jmp	call%
do_fdb:
	btst	&2, VIA+VIA_IER		# Check to see if it was a FDB (sr)
	beq	do_intr
	movm.l	&0xc0c0, -(%sp)		# Save A0-A1/D0-D1
	bsr	fdb_inthand
	movm.l	(%sp)+, &0x0303		# Recover the registers
	btst	&7, VIA+VIA_IFR	# do we have to do further interrupt
	bne	do_intr			# processing?
	btst	&2, via1_soft
	bne	do_intr
	addq.l	&4, %sp			# recover the stack
	rte

	global	fdb_inthand
fdb_inthand:
	mov.l	&VIA, %a1
	mov.b	&0x04, VIA_IFR(%a1)	# clear interrupt
	tst.b	fdb_state		# Did we just get an attention
					#	interrupt?
	bne	incc
	bset	&2, via1_soft		# say we have a real interrupt to be
					# serviced
	rts
incc:	mov.l	fdb_datap, %a0		# Get the data register pointer
	btst	&1, fdb_command		# Are we doing a Talk?
	bne	TalkInt
	btst	&0, fdb_command		# a listen?
	bne	ListInt
	btst	&2, fdb_command		# Checking for existance?
	bne	TalkInt
	btst	&3, fdb_command		# flushing?
	bne	FlushInt
ResetInt:
	btst	&0, fdb_state		# Do we now have to do a dummy S2?
	beq	S2Reset
	bclr	&0x04, VIA_ACR(%a1)	# change to shift in
	mov.b	VIA_SR(%a1), %d0	# start input
	br	SetS2
S2Reset:
	mov.b	VIA_SR(%a1), %d0	# Dummy read
	clr.l	%d0			# Flag OK
	br	SetS3			# Goto idle state

FlushInt:				
	btst	&0, fdb_state		# Do we now have to do a dummy S2?
	beq	S2Flush
	bclr	&0x04, VIA_ACR(%a1)	# change to shift in
	mov.b	VIA_SR(%a1), %d0	# start input
	btst	&3, VIA_ORB(%a1)	# Did a poll succeed?
	seq	fdb_pollflg
	bne	SetS2
	mov.b	&2, fdb_command		# Turn it into a talk
	mov.l	&fdb_exb, %a0		# Get last poll pointer .....
	br	SetS1			# Switch to state 1

S2Flush:
	btst	&3, VIA_ORB(%a1)	# Is there a service request?
	seq	fdb_select		
	mov.b	VIA_SR(%a1), %d0	# Dummy read
	clr.l	%d0			# Flag OK
	br	SetS3			# Goto idle state
	
	
TalkInt:
	btst	&0, fdb_state		# We are doing a talk, what state are
	beq	S1End			#	we in?

S0End:
	btst	&3, VIA_ORB(%a1)	# Did a poll succeed?
	seq	fdb_pollflg
	bne	S0x
	mov.l	&fdb_exb, %a0		# Get last poll pointer .....
S0x:	bclr	&0x04, VIA_ACR(%a1)	# change to shift in
	mov.b	VIA_SR(%a1), %d0	# start input
	br	SetS1			# Switch to state 1

S1End:
	btst	&1, fdb_state		# Are we in state 1
	beq	S2End
	btst	&3, VIA_ORB(%a1)	# Did a timeout occur??
	seq	fdb_timeout
	mov.b	VIA_SR(%a1), (%a0)+	# Save the read data
	br	SetS2			# Switch to state 2

StateErr:
	mov.b	VIA_SR(%a1), %d0	# An error occured, reset the SR
	mov.l	&0xff, %d0		# Flag the error
	br	SetS3			# Goto idle state

S2End:
	btst	&2, fdb_state		# Are we in state 2?
	beq	StateErr		# If not we are in trouble
	btst	&3, VIA_ORB(%a1)	# Is there a service request?
	seq	fdb_select		
	mov.b	VIA_SR(%a1), (%a0)+	# Save the data
	clr.l	%d0			# Flag OK
	br	SetS3			# Goto idle state

ListInt:
	btst	&0, fdb_state		# Are we in state 0?
	beq	List1
List0:
	btst	&3, VIA_ORB(%a1)	# Did a poll succeed?
	seq	fdb_pollflg
	bne	List0Cont
	bclr	&0x04, VIA_ACR(%a1)	# change to shift in
	mov.b	VIA_SR(%a1), %d0	# start input
	mov.l	&fdb_exb, %a0		# Get last poll pointer .....
	mov.b	&2, fdb_command		# Turn it into a talk
	br	SetS1			# Switch to state 1
List0Cont:
	tst.b	fdb_cnt			# Are there any characters
	beq	List3			# 	left to transfer?
	mov.b	(%a0)+, VIA_SR(%a1)	# Send the next one
	sub.b	&1, fdb_cnt		# Decrement the count
	br	SetS1			# Switch to state 1

List1:
	btst	&1, fdb_state		# Are we in state 1?
	beq	List2
	btst	&3, VIA_ORB(%a1)	# Did a timeout occur??
	seq	fdb_timeout
	tst.b	fdb_cnt			# Are there any characters left 
	beq	List3			#	to transfer?
	mov.b	(%a0)+, VIA_SR(%a1)	# Send the next one
	sub.b	&1, fdb_cnt		# Decrement the count
	br	SetS2			# Switch to state 2

List2:
	btst	&2, fdb_state		# Are we in state 2?
	beq	StateErr		# If not bugout
	btst	&3, VIA_ORB(%a1)	# Is there a service request?
	seq	fdb_select		
	tst.b	fdb_cnt			# Are there any characters
	beq	List3			#	left to transfer?
	mov.b	(%a0)+, VIA_SR(%a1)	# Send the next one
	sub.b	&1, fdb_cnt		# Decrement the count
	br	SetS1			# Switch to state 1

List3:
	clr.l	%d0			# Transfer completed OK
	mov.b	VIA_SR(%a1), %d0	# clear interrupt
	br	SetS3			# switch to state 3

SetS1:
	mov.l	&1, %d0			# Switching to state 1
	br	SetS

SetS2:
	mov.l	&2, %d0			# Switching to state 2
SetS:
	mov.l	%a0, fdb_datap		# Save the data pointer back again
	clr.b	fdb_state		# Move to the next state
	bset	%d0, fdb_state
	lsl.b	&4, %d0			# Set the state in the VIA
	mov.b	VIA_ORB(%a1), %d1
	and.b	&0xcf, %d1
	or.b	%d0, %d1
	mov.b	%d1, VIA_ORB(%a1)
	rts				

SetS3:
	mov.b	%d0, fdb_error		# Save the error status
	bset	&2, via1_soft		# say we have a real interrupt to be
					# serviced
	bclr	&0x04, VIA_ACR(%a1)	# change to shift in
	or.b	&0x30, VIA_ORB(%a1)	# Set the via to state 3
	rts
