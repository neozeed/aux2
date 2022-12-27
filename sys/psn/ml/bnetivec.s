/* 
 * @(#)Copyright Apple Computer 1989	Version 2.6 of bnetivec.s on 90/03/13 12:31:47
 * @(#)bnetivec.s	UniPlus VVV.2.1.11
 */
    file "bnetivec.s"

#define LOCORE
#define __sys_signal_h
#include "mch.h"
#include "sys/param.h"
#include "sys/page.h"
#include "sys/uconfig.h"
#include "sys/sysmacros.h"
#include "sys/mmu.h"
#include "sys/oss.h"

/*
 * Copyright 1983 UniSoft Corporation
 *
 * Interrupt vector dispatch table
 * One entry per interrupt vector location
 */

	text
	global	fault%, buserr%, syscall0%, syscall1%, lpriv
	global	ivect, autovec
ivect:
	long	lzero%		# 0	Reset: Initial SP
	long	fault%		# 1	Reset: Initial PC
	long	buserr%		# 2	Bus Error
	long	fault%		# 3	Address Error
	long	fault%		# 4	Illegal Instruction
	long	fault%		# 5	Zero Divide
	long	fault%		# 6	CHK Instruction
	long	fault%		# 7	TRAPV Instruction
	long	lpriv		# 8	Privilege Violation
	long	tr0%		# 9	Trace
	long	fault%		# 10	Line 1010 Emulator
	long	fault%		# 11	Line 1111 Emulator
	long	fault%		# 12	(Unassigned, reserved)
	long	fault%		# 13	Coprocessor Protocol Violation
	long	fault%		# 14	Format Error
	long	fault%		# 15	Uninitialized Interrupt
	long	fault%		# 16	(Unassigned, reserved)
	long	fault%		# 17	(Unassigned, reserved)
	long	fault%		# 18	(Unassigned, reserved)
	long	fault%		# 19	(Unassigned, reserved)
	long	fault%		# 20	(Unassigned, reserved)
	long	fault%		# 21	(Unassigned, reserved)
	long	fault%		# 22	(Unassigned, reserved)
	long	fault%		# 23	(Unassigned, reserved)
	long	spur0%		# 24	Spurious Interrupt
autovec:long	via10%		# 25	Level 1 Interrupt Autovector
	long	via21%		# 26	Level 2 Interrupt Autovector
	long	fault%		# 27	Level 3 Interrupt Autovector
	long	sc0%		# 28	Level 4 Interrupt Autovector
	long	fault%		# 29	Level 5 Interrupt Autovector
	long	pw0%		# 30	Level 6 Interrupt Autovector
	long	AutoVecInt7	# 31	Level 7 Interrupt Autovector
	long	syscall0%	# 32	System call (TRAP #0)
	long	fault%		# 33	TRAP #1  Instruction Vector
	long	fault%		# 34	TRAP #2  Instruction Vector
	long	fault%		# 35	TRAP #3  Instruction Vector
	long	fault%		# 36	TRAP #4  Instruction Vector
	long	fault%		# 37	TRAP #5  Instruction Vector
	long	fault%		# 38	TRAP #6  Instruction Vector
	long	fault%		# 39	TRAP #7  Instruction Vector
	long	fault%		# 40	TRAP #8  Instruction Vector
	long	fault%		# 41	TRAP #9  Instruction Vector
	long	fault%		# 42	TRAP #10 Instruction Vector
	long	fault%		# 43	TRAP #11 Instruction Vector
	long	fault%		# 44	TRAP #12 Instruction Vector
	long	fault%		# 45	TRAP #13 Instruction Vector
	long	bp0%		# 46	TRAP #14 Instruction Vector
	long	syscall1%	# 47	System Call (TRAP #15)
	long	fault%		# 48	FPU Branch/Set on Unordered Condition
	long	fault%		# 49	FPU Inexact Result
	long	fault%		# 50	FPU Divide by Zero
	long	fault%		# 51	FPU Underflow
	long	fault%		# 52	FPU Operand Error
	long	fault%		# 53	FPU Overflow
	long	fault%		# 54	FPU Signaling NAN
	long	fault%		# 55	(Unassigned, reserved)
	long	fault%		# 56	MMU Configuration Error
	long	fault%		# 57	PMMU Illegal Operation
	long	fault%		# 58	PMMU Access Level Violation
	long	fault%		# 59	(Unassigned, reserved)
	long	fault%		# 60	(Unassigned, reserved)
	long	fault%		# 61	(Unassigned, reserved)
	long	fault%		# 62	(Unassigned, reserved)
	long	fault%		# 63	(Unassigned, reserved)

/*
 * location containing address of nmi routine
 */
	data
	global	nmiloc
	global	abintr
	global	bptloc
nmiloc:	long	abintr
bptloc:	long	fault%

	text
	global	call%, idleflg

/*
 * Put actual "C" routine name onto the stack
 * and call the system interrupt dispatcher
 */
	global	zerofunc
lzero%:	mov.l	&zerofunc,-(%sp)
	mov.w	idleflg,-(%sp)
	jmp	call%

	global	Xfdbint
via10%: sub.w	&4,%sp			# dummy return location
	mov.w	&0x2400, %sr		# Make sure we go thru call% if req.
	jmp	Xfdbint

 	global	via2intr
via21%:	mov.l	&via2intr,-(%sp)
 	mov.w	idleflg,-(%sp)
 	jmp	call%

	global	scintr
sc0%:	mov.l	&scintr,-(%sp)
	clr.w	-(%sp)
	jmp	call%

	global	powerintr
pw0%:	mov.l	&powerintr,-(%sp)
	clr.w	-(%sp)
	jmp	call%

	global	spurintr
spur0%:	mov.l	&spurintr,-(%sp)
	clr.w	-(%sp)
	jmp	call%

	global	AutoVecInt1,AutoVecInt2,AutoVecInt3
	global	AutoVecInt4,AutoVecInt5,AutoVecInt6
	global	Level1Int, Level2Int, Level3Int
	global	Level5Int, Level4Int, Level6Int
AutoVecInt1:
	mov.l	&Level1Int,-(%sp)
	mov.w	idleflg,-(%sp)
	jmp	call%

AutoVecInt2:
	mov.l	&Level2Int,-(%sp)
	mov.w	idleflg,-(%sp)
	jmp	call%

AutoVecInt3:
	mov.l	&Level3Int,-(%sp)
	mov.w	idleflg,-(%sp)
	jmp	call%

AutoVecInt4:
	mov.l	&Level4Int,-(%sp)
	mov.w	idleflg,-(%sp)
	jmp	call%

AutoVecInt5:
	mov.l	&Level5Int,-(%sp)
	mov.w	idleflg,-(%sp)
	jmp	call%

AutoVecInt6:
	mov.l	&Level6Int,-(%sp)
	mov.w	idleflg,-(%sp)
	jmp	call%

/*
 * General NMI handler.  Parity errors take highest priority.
 */
AutoVecInt7:
	tst.l	parity_enabled		# is parity enabled?
	beq.b	gonmi%			# ->no, can't be a parity error
	cmp.w	machineID,&MACIIci	# Mac IIci?
	beq.b	IIciparity%		# ->service Mac IIci parity error
	cmp.w	machineID,&MACIIfx	# Mac IIfx?
	beq.b	IIfxparity%		# ->service Mac IIfx parity error
gonmi%:
	mov.l	nmiloc,-(%sp)
	mov.w	&0,-(%sp)
	jmp	call%

IIciparity%:
	btst	&6,VIA1_ADDR		# parity enabled?
	bne.b	gonmi%			# ->no, can't be a parity error
	btst	&7,VIA1_ADDR		# check parity err status
	bne.b	gonmi%			# ->not a parity error
paroff1%:
	jsr	disable_caches		# disable and clear the caches
	bset	&6,VIA1_ADDR		# disable parity checking forever
	btst	&6,VIA1_ADDR		# did it take?
	beq.b	paroff1%
	jmp	fault%			# handle parity error like a trap

IIfxparity%:
	mov.l	oss,%a0			# get the oss address and
	mov.w	O_OSSINTPND%(%a0),%d0	#     get the interrupt pending flag
	btst	&OSS_PARITY,%d0		# was it a parity error?
	beq	gonmi%			# ...nope
	jsr	disable_caches		# disable and clear the caches
	mov.l	&RPU_REG_ADDR,%a0	# get the RPU address
	st.b	RPU_RESET(%a0)		# reset serial pointer register
	clr.b	(%a0)			# set correct parity
	st.b	(%a0)			# clear parity error
	jmp	fault%

tr0%:
	btst	&5,(%sp)		# did we come from user mode?
	bne	tr0a%			# no, check for sysdebugger
	jmp	fault%			# yes - proceed as usual
tr0a%:
	cmp.l	bptloc,&fault%		# is sysdebug loaded?
	bne	tr0b%			# yup
#ifdef KDB
	jmp	fault%
#else
	rte
#endif

tr0b%:
	mov.l	bptloc,-(%sp)
	mov.w	&3,-(%sp)
	jmp	dbgcom%
	

bp0%:	
	btst	&5,(%sp)		# did we come from user mode?
	bne	bp0a%			# no, check for sysdebugger
	jmp	fault%			# yes - proceed as usual
bp0a%:
	cmp.l	bptloc,&fault%		# is sysdebug loaded?
	bne	bp0b%			# yup
	jmp	fault%
bp0b%:
	mov.l	bptloc,-(%sp)
	mov.w	&1,-(%sp)
dbgcom%:
	movm.l	&0xFFFF,-(%sp)		# save all registers
	mov.l	%usp,%a0
	mov.l	%a0,60(%sp)		# save usr stack ptr
	mov.l	66(%sp),%a0		# fetch interrupt routine address
	mov.l	%sp,%a1			# save argument list pointer
	mov.l	%a1,-(%sp)		# push ap onto stack
	jsr	(%a0)			# jump to actual interrupt handler
	add.l	&4,%sp

	mov.l	60(%sp),%a0
	mov.l	%a0,%usp		# restore usr stack ptr
	movm.l	(%sp)+,&0x7FFF		# restore all other registers
	add.w	&10,%sp			# sp, pop fault pc, and alignment word
	rte				# return from whence called


	global	lineAVector, lineAFault, fprest
lineAVector:
	mov.l	%a0,-(%sp)
	mov.l	%d0,-(%sp)		# save a couple of scratch regs

	mov.l	%usp,%a0		# get pointer to user's stack 
	mov.l	12(%sp),%d0		# get vector + half of the pc
	movs.l	%d0,-(%a0)		# stuff them on the user stack

	mov.l	UDOT+U_SR%,%d0		# get virtual sr into high half of d0
	clr.w	%d0			# clear low half	
	or.l	8(%sp),%d0		# or in real sr + other half of the pc
	movs.l	%d0,-(%a0)		# stuff them on the user stack
	mov.l	%a0,%usp		# update user stack pointer
					# assembler wont generate (xxx).W
	short	0x0eb8,0x0000,0x0028	# movs.l  0x28.w,%d0
	mov.l	%d0,10(%sp)		# replace faulting pc with handler's pc

	tst.b	UDOT+U_FPSAVED%		# access of user address may cause
	bne.b	fpok1			# a context switch
	mov.l	(%sp)+,%d0		# restore scratch registers
	mov.l	(%sp)+,%a0
	rte
fpok1:
	mov.l	%a1,-(%sp)		# save rest of scratch regs used
	mov.l	%d1,-(%sp)		# by C
	jsr	fprest			# need to restore our fp state
	mov.l	(%sp)+,%d1		# restore scratch registers
	mov.l	(%sp)+,%a1
	mov.l	(%sp)+,%d0
	mov.l	(%sp)+,%a0
	rte
lineAFault:				# dont move this (the trap handler
	jmp	fault%			# checks for a pc in this range)

