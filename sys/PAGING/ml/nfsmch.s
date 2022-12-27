 #
 # Copyright 1987, 1988, 1989 Apple Computer, Inc.
 # All Rights Reserved.
 #
 # THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF APPLE COMPUTER, INC.
 # The copyright notice above does not evidence any actual or
 # intended publication of such source code.
 #
 #	@(#)Copyright Apple Computer 1989	Version 2.13 of nfsmch.s on 90/04/19
 #	file	"nfsmch.s"
 #	@(#)nfsmch.s	UniPlus VVV.2.1.18

#define LOCORE
#define __sys_signal_h
#include	"mch.h"
#include "sys/param.h"
#include "sys/page.h"
#include "sys/uconfig.h"
#include "sys/sysmacros.h"
#include "sys/mmu.h"
#include "sys/errno.h"
#include "sys/cpuid.h"
	# USIZE dependencies - assembler does not understand '<<'
#if PAGESHIFT==PS4K
#define	NBPP		4096
#endif
#if PAGESHIFT==PS8K
#define	NBPP		8192
#endif


	set	USER_DSPACE,1
	set	USER_ISPACE,2

	set	TRAPREST,3		# see user.h
	set	UDOTSIZE%,NBPP*USIZE	# Size of U area (bytes)

	# Configuration dependencies
	set	HIGH%,0x2700		# High priority supervisor mode (spl 7)
	set	LOW%,0x2000		# Low priority, supervisor mode (spl 0)

	global	u
	data	
	set	utblstk,UDOT+UDOTSIZE%

	global kstack%, splimit%, m20cache, mmu_on, ivecstart, kstart, mmuaddr
	global fp881, cputype, idleflg, kernelinfoptr, machineID, fpversion
	global wakeup

	even
m20cache:	long	0
mmu_on:         long    0               # Is the mmu enabled yet
splimit%: 	long	0		# For generic m68k stack probe mechanism
kstack%:	long	0		# temporary stack pointer
ivec%:		long	0		# temp area
fvec%:		long	0		# temp area
tt0%:		long	0		# temp area for checking if '030
ivecstart:	long	VECBASE		# interrupt vector base address
kstart:		long	KSTART		# starting kernel virtual address
mmuaddr:	long	MMUADDR		# CPU address of mmu
fp881:		short	FPU_NONE	# 1='881, 2='882, else no fpu
fpversion:	short	0
cputype:	long	VER_MC68000	# for 680X0 family id
idleflg:	short   0	        # if set, indicate we're in idle loop
kernelinfoptr:	long	0
machineID:	short	4		# machine ID defaults to MACII

	# MC68881 Floating Point Coprocessor Data
fpnull%:	long	0		# Format word for MC68881 Null State

	text	
	global	_start, main
	global	symtabsize, symtabaddr
	global	sectinfo, rbv_exists

_start:
	mov.w	&HIGH%,%sr		# spl7

	cmp.l	%d0,&0x536d7201
	beq.b	launchver1
	mov.l	&0x400,kernelinfoptr
	br.b	oldlaunch
launchver1:
	mov.l	%a0,kernelinfoptr	# save pointer to kernel info
	lea.l	140(%a0),%a0		# point at sectinfo passed to us
	mov.l	&sectinfo,%a1
	mov.w	&8,%d0			# copy 9 longs
scopy%:
	mov.l	(%a0)+,(%a1)+
	dbra	%d0,scopy%		# new versions of launch pass along the
	mov.w	(%a0),machineID		# real machine id
oldlaunch:
	mov.l	sectinfo+28,%d7		# start of bss
	add.l	sectinfo+32,%d7		# + size = end of bss
	mov.l	sectinfo+28,%a0		# Start clearing here (start of bss)
clrbss%:
	mov.l	&0,(%a0)+		# Clear bss
	cmp.l	%a0,%d7
	bcs	clrbss%
	mov.l	sectinfo+28,%d7		# start of bss
	add.l	sectinfo+32,%d7		# + size = end of bss
	add.l	&NBPP-1,%d7
	and.l	&-NBPP,%d7		# Round to nearest click
	mov.l	%d7,%a0
	mov.w	(%a0),%d0
	cmp.w	%d0,&0520
	bne	clrbs2%			# branch if symbols not loaded
	mov.l	%a0,symtabaddr		# save addr of symtab
	mov.l	&0x14,%d1		# running symtab size
	add.l	%d1,%d7			# size of filehdr
	mov.l	12(%a0),%d0		# number of syms
	muls.l	&18,%d0			# bytes of syms
	add.l	%d0,%d7
	add.l	%d0,%d1			# running size
	mov.l	%d7,%a0
	mov.l	(%a0),%d0		# str tab size
	add.l 	%d0,%d7
	add.l	%d0,%d1			# total sym+str tab size
	mov.l	%d1,symtabsize
clrbs2%:
	mov.l	%d7,%a0
	add.l	&UDOTSIZE%+NBPP-1,%d7	# End of unix, allocate udot
	and.l	&-NBPP,%d7		# Round to nearest click
clrbs1%:mov.l	&0,(%a0)+		# Clear udot
	cmp.l	%a0,%d7
	bcs	clrbs1%
 	mov.l	%d7,%sp			# Move off the booter's stack

	mov.l	&CACHEOFF,%d0		# turn off on-chip cache
	mov.l	%d0,%cacr
 # Determine cpu type
	mov.l	KSTART+16,ivec%		# Illegal instruction vector
	mov.l	KSTART+44,fvec%		# F-line Emulator vector
	mov.l	&LS%1,KSTART+16		# Illegal instruction vector
	mov.l	&KSTART,%d0		# Load interrupt vector base register
	mov.l	%d0,%vbr
	mov.l	&VER_MC68010,cputype	# No trap. Must be 68010 or 68020
	mov.w	&0x3700,%sr		# Set master state
	mov.w	%sr,%d0
	mov.w	&0x2700,%sr		# Restore interrupt state
	btst	&12,%d0			# Master state set in 68020 only
	beq	LS%1			# Not set - it's a 68010
	mov.l	&VER_MC68020,cputype	# Must be 68020
	mov.l	&LS%2,KSTART+44		# dodge F-line trap
	lea	tt0%,%a0		# set up dest for pmove
	short	0xF010			# pmove  tt0%,(%a0)
	short	0x0A00
	mov.l	&VER_MC68030,cputype	# Must be 68030
LS%2:
	# Determine if MC68881 Floating Point Coprocessor is present, by
	#	attempting to issue a reset (restore null state)
	# Note: this only makes sense if we know there's a 68020
	mov.l	&LS%1,KSTART+44		# F-line Emulator vector
	mov.l	&fpnull%,%a0		# a0 points to null state format word
	frestore (%a0)
	mov.w	&FPU_MC68881,fp881	# No trap, mc68881 found as CP-ID 1
	mov.l	%d7,%sp			# set up space for fsave
 	sub.l	&0x100,%sp		#   need a lot of space
	short	0xF200			# fmovecr.x	<pi>,%fp0
	short	0x5C00
	fsave	(%sp)
	mov.w	(%sp),fpversion		# save version number for sanity checks
	cmp.b	1(%sp),&0x18		# check state frame size
	beq	LS%3			# it's not an '882
	mov.w	&FPU_MC68882,fp881
LS%3:
	frestore (%a0)			#	frestore (a0)
LS%1:	
	mov.l	ivec%,KSTART+16		# restore Illegal instruction vector
	mov.l	fvec%,KSTART+44		# F-line Emulator vector
	mov.l	%d7,%sp			# Stack for C calls
	jsr	boardinit		# figure out motherboard specifics
	mov.l	%d7,-(%sp)
	mov.l	&memerror%,KSTART+8	# set up for memory buserr
 	mov.l	%d7,-(%sp)		# save a copy of %d7 on the stack
 	mov.l	%sp, kstack%		# 	save the stack
	mov.l	%d7,-(%sp)		# arg to memsize - where to start
	jsr	memsize			# memory sizer (physical memory)
					#  leave result in 'physmem'
memerror%:
 	mov.l	kstack%, %sp		# restore our stack/%d7 state
 	mov.l	(%sp)+, %d7		# in case memsize used %d7 and
 					# then trapped to memerror%
	mov.l	%d7, %sp		# Stack for mmusetup call
	mov.l	%d7, -(%sp)
	jsr	mmusetup		# Allocate space for mmu tables,
	add.l	&4, %sp			#  set up kernel tables
	mov.l	%d0, %d7		# Save end of tables (tblend)
 	mov.l	%sp, %a0		# push the address of the current udot,
 	sub.l	&UDOTSIZE%, %a0		# 	it will become the idle udot
 	mov.l	%a0, -(%sp)		#	eventually
	mov.l	%d7,-(%sp)		# tblend where to allocate tables
	jsr	vadrspace		# **MMU enabled on return**
	mov.l	&utblstk,%sp		# Set stack at end of U area
	mov.l	%sp,kstack%		# vaddrof stackend (resched exit)
	sub.l	&4,kstack%		# in case used before decrement

	mov.l	&USER_DSPACE,%d1	# set up user data space for future use
	mov.l	%d1,%dfc		# must always be restored to these values
	mov.l	%d1,%sfc		# if set to any other space
	jsr	main			# Call main() to complete startup.
					#  On return, either enter init(1M)
	tst.l	%d0			#  or (d0 != 0) jump to specified
	beq	enter_init		#  "process".
	mov.l	%d0,%a0
	jmp	(%a0)
enter_init:				# Prepare to invoke 1st user process:
	clr.l	-(%sp)			# Indicate short 4 byte stack format
	mov.l	&USTART,-(%sp)		# Starting program address
	clr.w	-(%sp)			# New sr value
	rte				# Invoke init


	# save and restore of register sets

	global	save,resume,qsave,uptbl
save:	mov.l	(%sp)+,%a1		# return address
	mov.l	(%sp),%a0		# ptr to label_t
	movm.l	&0xFCFC,(%a0)		# save d2-d7, a2-a7
	mov.l	%a1,48(%a0)		# save return address
	mov.l	&0,%d0
	jmp	(%a1)			# return

qsave:	mov.l	(%sp)+,%a1		# return address
	mov.l	(%sp),%a0		# ptr to label_t
	add.w	&40,%a0
	mov.l	%fp,(%a0)+		# save a6
	mov.l	%sp,(%a0)+		# save a7
	mov.l	%a1,(%a0)+		# save return address
	mov.l	&0,%d0
	jmp	(%a1)		# return

	# resume(save area, proto page table)
resume:	
 	jsr	fpsave			# save the state of fp

	mov.l	4(%sp),%a2		# ptr to udot's label_t
	mov.l	8(%sp),%a0		# local udot pg tbl entry
	mov.w	&HIGH%,%sr		# spl 7

	mov.l	uptbl,%a1		# Ptr to home for new pte
	mov.l	%a0,(%a1)		# copy in the pte for the new UBLOCK
	lea.l	UDOT,%a0		# set up address of the UBLOCK

	cmp.l	cputype,&VER_MC68030	# on 68030 cant use pflushs
	beq	resume030
	#	pflushs &5,&7,(%a0)	# the UBLOCK address has a different
	short	0xF010			#  physical location
	short	0x3CF5
	br.b	resume020
resume030:
	#	pflush &5,&7,(%a0)	# the UBLOCK address has a different
	short	0xF010			#  physical location
	short	0x38F5
resume020:
	mov.l	&CACHECLR,%d0		# clear the virtual on-chip cache
	mov.l	%d0,%cacr		# so that we see the new UBlock
#ifdef CLR_CACHE
	mov.l	&1,-(%sp)
	jsr	clr_cache
	add.w	&4,%sp
#endif CLR_CACHE

	lea.l	cpu_rp,%a0		# update pointer to root table
	mov.l	([UDOT+U_PROCP%],P_ROOT%),4(%a0)
	#	pmove	(%a0),%crp	# load the user's root pointer
	short	0xF010
	short	0x4c00

	mov.l	UDOT+U_USER%+0,0x28	# set handling of Aline traps
	mov.l	UDOT+U_USER%+8,%d0	# set this process's idea
	mov.l	%d0,%cacr		# of the cache control register
	mov.l	48(%a2),%a1		# fetch the original pc
	movm.l	(%a2),&0xFCFC		# restore the registers
	mov.w	&LOW%,%sr		# set spl0
	mov.l	&1,%d0			# return 1
	jmp	(%a1)			# return




	# Enable on-chip cache and external cache card
	global	enable_caches
enable_caches:
	mov.l	&CACHEON,%d0		# enable on on-chip cache entries
	cmp.w	machineID,&MACIIci	# enable burst for Mac IIci machines
	beq.b	burst%
	cmp.w	machineID,&MACIIfx	# ...same for Mac IIfx machines
	bne.b	noburst%
burst%:
	or.l	&CACHEBURST,%d0
noburst%:
	mov.l	%d0,m20cache		# save if cache is disabled/enabled
	or.l	&CACHECLR,%d0
	mov.l	%d0,%cacr

	tst.l	rbv_exists		# no rbv --> no external cache card
	bne.b	enable_card
	rts

	# Disable on-chip cache and external cache card
	global	disable_caches
disable_caches:
	tst.l	rbv_exists	# no rbv --> no external cache card
	beq.b	disable_onchip
	mov.l	via2_addr,%a0	# get address of via data register
	mov.b	(%a0),%d0
	or.b	&1,%d0
	mov.b	%d0,(%a0)
disable_onchip:
	mov.l	&CACHEOFF,%d0
	mov.l	%d0,%cacr
	rts


	# Enable external card cache
	# PLEASE NOTE -- the CDIS~ signal is opposite that stated in the
	# documentation (i.e., 1=cache disabled, 0=cache enabled, and would
	# thus be more appropriately titled CENABLE~).

	# The enable code must be cached in the 030's on-board cache before 
	# enabling the external cache.  Interrupts must be disabled to ensure
	# no new code is encached.  This code is tricky.  If you don't 
	# understand it, don't mess with it!
enable_card:
	mov.l	via2_addr,%a0		# get address of via data register

	btst	&0,(%a0)		# flush cache (only if disabled)
	beq.s	noflush
	bclr	&3,(%a0)		# set flush line low
	bset	&3,(%a0)		# and high again
noflush:
	move.b	(%a0),%d0		# ready to enable
	and.b	&0xFE,%d0
	bra.s	skip			# jump around, forcing the instructions
					# that do the actual enable to burst.
	# NOTE: To force code to be burst in, you must have
	# LESS THAN 4 longwords of code between here and skip.

enable: move.b	%d0,(%a0)		# write the register to enable cache,
	move.b	(%a0),%d0		# and read it back to delay
	rts
skip:	bra.s	enable



	# Reset the MC68881 Floating-point Coprocessor
	# by restoring the null state.
	#
	global	fpreset
fpreset:
	frestore fpnull%
	rts	



	global	fp_status
fp_status:
	fmove	%status,%d0
	rts



	global	myfsave
myfsave:
	mov.l	4(%sp),%a0
	fnop
	fsave	(%a0)
	rts


	global	myfrestore
myfrestore:
	mov.l	4(%sp),%a0
	frestore (%a0)
	rts


	global	fpsave
fpsave:
	tst.w	fp881
	beq.b	fpdontsave
	lea.l	UDOT+U_FPSAVED%,%a1
	tst.b	(%a1)
	bne.b	fpdontsave
	lea.l	UDOT+U_FPSTATE%,%a0
	fsave	(%a0)
	tst.w	(%a0)
	beq.b	fpsdone
	lea.l	UDOT+U_FPSYSREG%,%a0
	short	0xf210			# fmovem (a0)
	short	0xbc00			# save system registers
	lea.l	UDOT+U_FPDREG%,%a0
	short	0xf210			# fmovem (a0)
	short	0xf0ff			# save all the data registers
fpsdone:
	mov.b	&1,(%a1)		# u.u_fpsaved = 1
fpdontsave:
	rts


	global	fprest
fprest:
	lea.l	UDOT+U_FPSTATE%,%a1
	tst.w	(%a1)
	beq.b	fprdone
	lea.l	UDOT+U_FPSYSREG%,%a0
	short	0xf210			# fmovem (a0)
	short	0x9c00			# restore system registers
	lea.l	UDOT+U_FPDREG%,%a0
	short	0xf210			# fmovem (a0)
	short	0xd0ff			# restore all the data registers
fprdone:
	frestore (%a1)
	clr.b	UDOT+U_FPSAVED%
	rts



	# spl routines
	#
	global	splhi, splx
	global	splclock
	global	splimp, splnet
	global	spl7, spl6, spl5, spl4, spl3, spl2, spl1, spl0

splhi:
spl7:	mov.w	%sr,%d0			# fetch current CPU priority
	mov.w	&0x2700,%sr		# set priority 7
	rts	
splclock:
splimp:
splnet:
spl6:	mov.w	%sr,%d0			# fetch current CPU priority
	mov.w	&0x2600,%sr		# set priority 6
	rts	
spl5:	mov.w	%sr,%d0			# fetch current CPU priority
	mov.w	&0x2500,%sr		# set priority 5
	rts	
spl4:	mov.w	%sr,%d0			# fetch current CPU priority
	mov.w	&0x2400,%sr		# set priority 4
	rts	
spl3:	mov.w	%sr,%d0			# fetch current CPU priority
	mov.w	&0x2300,%sr		# set priority 3
	rts	
spl2:	mov.w	%sr,%d0			# fetch current CPU priority
	mov.w	&0x2200,%sr		# set priority 2
	rts	
spl1:	mov.w	%sr,%d0			# fetch current CPU priority
	mov.w	&0x2100,%sr		# set priority 1
	rts	
spl0:	mov.w	%sr,%d0			# fetch current CPU priority
	mov.w	&0x2000,%sr		# set priority 0
	rts	
splx:	mov.w	6(%sp),%sr		# set priority
	rts	


	global	qrunflag, queueflag, stream_run
	global	idle
idle1%:
	mov.w	&1,idleflg
	stop	&0x2000			# Set priority zero
idle:
	cmp.b	sir%,&0x01		# soft interrupt pending + not busy ?
	bne.b	idle3%			# no: skip it
	bsr	dosir%			# go handle software interrupt requests
idle3%:
	tst.b	qrunflag		# Are there streams to do?
	beq.b	idle2%
	mov.b	&1,queueflag		# OK. Do them
	mov.l	stream_run,%a0		
	jsr	(%a0)			# Call streams scheduler
	clr.b	queueflag		# clear lockout flag
idle2%:
	mov.w	&0x2700,%sr
	tst.l	runrun			# if runrun not set
	beq.b	idle1%			# reenter the idle loop
	clr.w	idleflg
	rts	



	global	buserr%, fault%, call%, busaddr, fault2%
	global	syscall1%, syscall1, syscall0%, syscall0
	global	runrun, trap, reschedule
fault%:
	btst	&5,(%sp)		# did we come from user mode?
	bne	sup_fault%		# no, don't move stack
	sub.w	&10,%sp			# fake ra + align ps + reserve usp
	movm.l	&0xFFFE,-(%sp)		# save all registers but a7
	mov.l	%usp,%a0
	mov.l	%a0,UDOT+U_USER%+0xC	# Needed for fault evaluation
	mov.l	%a0,60(%sp)		# save usr stack ptr
fault2%:
	bsr	userhandler		# if userhandler returns, then kernel
					# must handle the fault
	mov.w	&0x2000,%sr
	mov.w	76(%sp),%d0		# fetch vector offset
	and.l	&0x0fff,%d0		# only want the offset, not the type
	asr.l	&2,%d0			# calculate vector number
	mov.l	%d0,-(%sp)		# argument to trap
	jsr	trap			# C handler for traps and faults
	add.w	&4,%sp

	mov.w	76(%sp),%d0
	and.w	&0xf000,%d0
	beq	ret_user2		# type 0 and
	cmp.w	%d0,&0x2000		# type 2 frames are OK to rte from
	beq	ret_user2
	and.w	&0x0fff,76(%sp)		# change to type0 stack frame
	mov.l	&utblstk,%a1
	lea.l	80(%sp),%a0		# copy saved registers + small frame
fault3%:
	mov.l	-(%a0),-(%a1)		# to the top of the stack
	cmp.l	%a0,%sp
	bne	fault3%
	mov.l	%a1,%sp			# set up new kernel stack pointer
	br	ret_user2
sup_fault%:
	sub.w	&6,%sp			# align ps
	movm.l	&0xFFFF,-(%sp)		# save all registers
sup_fault2%:
	mov.w	76(%sp),%d0		# fetch vector offset
	and.l	&0x0fff,%d0		# only want the offset, not the type
	asr.l	&2,%d0			# calculate vector number
	mov.l	%d0,-(%sp)		# argument to trap
	jsr	trap			# C handler for traps and faults
	add.w	&4,%sp
	tst.l	%d0			# see if fault due to bad user address
	bne	user_errret		# by checking return from 'trap'
	movm.l	(%sp)+,&0x7FFF		# restore all other registers
	add.w	&10,%sp			# sp, pop fault pc, and alignment word
	rte	


	global	framelock, framewant, framedata
syscall1%:
	btst	&5,(%sp)		# did we come from user mode?
	bne	sup_fault%		# no, error !!!
	sub.w	&10,%sp			# fake ra + align ps + reserve usp
	movm.l	&0xFFFE,-(%sp)		# save all registers but a7
	mov.l	%usp,%a0
	mov.l	%a0,UDOT+U_USER%+0xC	# Needed for fault evaluation
	mov.l	%a0,60(%sp)		# save usr stack ptr
	jsr	syscall1		# Process system call

	btst	&TRAPREST,UDOT+U_TRAPTYPE%+1	# access low order byte
	bne	replaceframe
	btst	&7,70(%sp)		# see if we entered syscall with
	beq	ret_user2		# with the trace bit set
faketrace%:
	mov.w	&0x24,76(%sp)		# fake a trace trap vector
	bra	fault2%


syscall0%:
	btst	&5,(%sp)		# did we come from user mode?
	bne	sup_fault%		# no, error !!!
	sub.w	&10,%sp			# fake ra + align ps + reserve usp
	movm.l	&0xFFFE,-(%sp)		# save all registers but a7
	mov.l	%usp,%a0
	mov.l	%a0,UDOT+U_USER%+0xC	# Needed for fault evaluation
	mov.l	%a0,60(%sp)		# save usr stack ptr
	jsr	syscall0		# Process system call

	btst	&TRAPREST,UDOT+U_TRAPTYPE%+1	# access low order byte
	bne	replaceframe
	btst	&7,70(%sp)		# see if we entered syscall with
	bne	faketrace%		# with the trace bit set
ret_user2:
	cmp.b	sir%,&0x01		# soft interrupt pending + not busy ?
	bne.b	LQR%8			# no: skip it
	bsr	dosir%			# go handle software interrupt requests
LQR%8:
	tst.b	qrunflag		# Are there streams to do?
	beq.b	LQR%9
	mov.b	&1,queueflag		# OK. Do them
	mov.l	stream_run,%a0		
	jsr	(%a0)			# Call streams scheduler
	clr.b	queueflag		# Tidy up on the way out
LQR%9:
	mov.w	&0x2700,%sr	
	tst.l	runrun			# should we reschedule?
	beq.b	LQR%10
	mov.w	76(%sp),%d0
	and.l	&0xf000,%d0		# just want the frame type
	mov.l	%d0,-(%sp)		# if non-zero, we dont want to
	jsr	reschedule		# handle pending signals at this time
	add.w	&4,%sp
LQR%10:
	tst.b	UDOT+U_FPSAVED%
	beq.b	ret_super
	jsr	fprest
ret_super:
	mov.l	60(%sp),%a0		# no, just return normally
	mov.l	%a0,%usp		# restore usr stack ptr
	movm.l	(%sp)+,&0x7FFF		# restore all other registers
	add.w	&10,%sp			# sp, pop fault pc, and alignment word
	rte				# return from whence called


replaceframe:				# u.u_traptype is cleared on next syscall entry
	mov.w	framedata+4,%d0		# get vector out of replacement frame
	and.w	&0xf000,%d0		# just want the frame type
	lsr.w	&8,%d0
	lsr.w	&3,%d0			# need index into table of shorts
	mov.w	(framesize,%d0.w),%d0
	sub.w	&2,%d0			# sr not included in dummy frame
	mov.w	%d0,%d1
	sub.w	&6,%d1			# current pc and vect to be overwritten
	mov.l	%sp,%a0			# pointer to current top of stack
	sub.w	%d1,%sp			# make room for the frame to be copied
	mov.l	%sp,%a1
	mov.w	&17,%d1			# need to copy 18 longs	
rloop1:
	mov.l	(%a0)+,(%a1)+	
	dbra	%d1,rloop1

	lsr.w	&1,%d0			# turn into count of shorts
	sub.w	&1,%d0			# turn into dbra counter
	lea.l	framedata,%a0		# get pointer to replacement frame
rloop2:
	mov.w	(%a0)+,(%a1)+
	dbra	%d0,rloop2		# copy in the replacement frame

	clr.l	framelock		# clear the lock that we're holding
	tst.l	framewant
	beq	ret_user2
	pea.l	framelock		# wakeup anyone waiting to use framedata
	jsr	wakeup
	add.w	&4,%sp
	br	ret_user2



	# common interrupt dispatch
	#
call%:	
	movm.l	&0xFFFF,-(%sp)		# save all registers
	add.l	&1,sysinfo+V_INTR%	# count interrupts
	mov.l	66(%sp),%a0		# fetch interrupt routine address
	mov.l	%sp,%a1			# save argument list pointer
	mov.l	%a1,-(%sp)		# push ap onto stack
	jsr	(%a0)			# jump to actual interrupt handler
	add.w	&4,%sp
	btst	&5,70(%sp)		# did we come from user mode?
	bne	ret_call%		# no, just continue

	mov.w	&0x2000,%sr
	cmp.b	sir%,&0x01		# soft interrupt pending + not busy ?
	bne	LQC%8			# no: skip it
	bsr	dosir%			# go handle software interrupt requests
LQC%8:
	tst.b	qrunflag		# Are there streams to do?
	beq	LQC%9
	mov.b	&1,queueflag		# OK. Do them
	mov.l	stream_run,%a0		
	jsr	(%a0)			# Call streams scheduler
	clr.b	queueflag		# Tidy up on the way out
LQC%9:
	mov.w	&0x2700,%sr
	tst.l	runrun			# should we reschedule?
	beq	ret_call%

	mov.l	%usp,%a0
	mov.l	%a0,UDOT+U_USER%+0xC	# Needed for fault evaluation
	mov.l	%a0,60(%sp)		# save usr stack ptr
	mov.w	76(%sp),%d0
	and.l	&0xf000,%d0		# just want the frame type
	mov.l	%d0,-(%sp)		# if non-zero, we dont want to
	jsr	reschedule		# handle pending signals at this time
	add.w	&4,%sp
	mov.l	60(%sp),%a0
	mov.l	%a0,%usp		# restore usr stack ptr

	tst.b	UDOT+U_FPSAVED%
	beq	ret_call%
	jsr	fprest			# restore floating point state
ret_call%:
	movm.l	(%sp)+,&0x7FFF		# restore all other registers
	add.w	&10,%sp			# sp, pop fault pc, and alignment word
	rte				# return from whence called



	# Bus error entry, this has its stack somewhat different.  We will
	# call a C routine to save the info then fix the stack to look like
	# a trap.  These entries will be called directly from interrupt vector.
	# NB: We save the USP for later reference only for user bus errors.
	# This may be mild paranoia.

buserr%:
	# hardflt(): 	if successful vfault() or pfault()
	#			return 0
	#		else
	#			return -1
	#
	sub.w	&10,%sp			# fake ra + align ps + reserve usp
	movm.l	&0xFFFE,-(%sp)		# save all registers but a7
	mov.l	%usp,%a0
	mov.l	%a0,60(%sp)		# save user stack pointer

	btst	&5,70(%sp)
	bne	super_buserr
user_buserr:
	mov.l	%a0,UDOT+U_USER%+0xC	# Needed for fault evaluation
	jsr	hardflt			# hardflt(vaddr, &lbuserr)
	tst.l	%d0			# returns non-zero on failure
	beq	ret_user2
	br	fault2%
super_buserr:
	jsr	hardflt			# hardflt(vaddr, &lbuserr)
	tst.l	%d0			# returns non-zero on failure
	beq	ret_super
	br	sup_fault2%



userhandler:
	cmp.l	0x28,&lineAFault	# if we're handling our own Aline traps
	beq	nouserhandler 		# then we probably want the other traps also
	mov.l	%sp,UDOT+U_FLTSP%	# protect against wacky user virtual address

	mov.w	80(%sp),%d3		# fetch type/vector
	mov.w	%d3,%d4			# need both type + vector
	and.l	&0x0fff,%d3		# isolate vector offset
	cmp.l	%d3,&0x24
	beq	usertrace
	and.l	&0xf000,%d4		# isolate stk type
	lsr.l	&8,%d4	
	lsr.l	&3,%d4			# need index into table of shorts
	mov.l	%d3,%a0			# we'll use the vector offset as a pointer

	movs.l	(%a0),%d5		# fetch vector contents from user space
	tst.l	%d5			# movs doesn't set the condition codes
	beq	nouserhandler		# if fault handler address == 0 or
	btst	&0,%d5			# it's odd, user not prepared to handle it
	bne	nouserhandler
	mov.w	(framesize,%d4),%d0
	beq	nouserhandler		# unknown frame type
	mov.w	%d0,%d6			# save for later use
	mov.w	UDOT+U_SR%,%d1		# fetch virtual status register

	mov.l	64(%sp),%a0		# in case we got here after executing
					# a system call (trace bit set)
	lea.l	74(%sp),%a1		# pointer to top of fault frame
	or.w	%d1,(%a1)		# pass up the virtual sr
	add.w	%d0,%a1			# get pointer to base of fault frame
	lsr.w	&2,%d0			# need count of longs
	sub.w	&1,%d0			# dbra counter
frameloop:
	mov.l	-(%a1),%d1
	movs.l	%d1,-(%a0)
	dbra	%d0,frameloop
	mov.l	%a0,%usp

	clr.l	UDOT+U_FLTSP%		# protect against wacky user virtual address
	tst.b	UDOT+U_FPSAVED%
	beq.b	fpok1
	jsr	fprest
fpok1:
	mov.l	%d5,saveUsraddr
	mov.w	%d6,saveFrmsize
	add.w	&4,%sp			# get rid of caller's return address
	movm.l	(%sp)+,&0x7fff		# restore all registers
	add.w	&10,%sp			# pop ksp,dummy word,bsr
	add.w	saveFrmsize,%sp		# get rid of fault frame
	clr.w	-(%sp)			# 'vect' = 0
	mov.l	saveUsraddr,-(%sp)	# 'pc' of user exception handler
	clr.w	-(%sp)			# 'sr' = 0
	rte

usertrace:
	movs.l	0x24,%d2		# fetch trace vector contents from user space
	tst.l	%d2			# movs doesn't set the condition codes
	beq	nouserhandler		# if fault handler address == 0 or
	btst	&0,%d2			# it's odd, user not prepared to handle it
	bne	nouserhandler
	btst	&2,([UDOT+U_PROCP%],P_SIG%+3)
	bne	userquit

	mov.w	UDOT+U_SR%,%d0		# or the virtual status register
	or.w	%d0,74(%sp)		# into the current user's version
	mov.l	64(%sp),%a0		# in case we got here after executing
					# a system call (trace bit set)
	mov.l	78(%sp),%d1
	mov.w	&0x24,%d1		# make sure we look like a type 0 frame
	movs.l	%d1,-(%a0)		# generated via a trace trap
	mov.l	74(%sp),%d1
	movs.l	%d1,-(%a0)
	mov.l	%a0,%usp		# update new user sp

	mov.l	%d2,76(%sp)		# set up return to user handler
	clr.w	74(%sp)			# need to clear the TraceTrap bit in the sr

	clr.l	UDOT+U_FLTSP%		# protect against wacky user virtual address
	tst.b	UDOT+U_FPSAVED%
	beq.b	fpok2	
	jsr	fprest
fpok2:
	add.w	&4,%sp			# get rid of caller's return address
	movm.l	(%sp)+,&0x7fff		# restore all registers
	add.w	&10,%sp			# pop ksp,dummy word,bsr
	rte

userquit:
	clr.l	UDOT+U_FLTSP%		# protect against wacky user virtual address
	add.w	&4,%sp			# get rid of caller's return address
	br	ret_user2

nouserhandler:
	clr.l	UDOT+U_FLTSP%		# protect against wacky user virtual address
	rts



	data
framesize:
	short	8,  8, 12,  0, 0, 0, 0, 0
	short	0, 20, 32, 92, 0, 0, 0, 0 
saveUsraddr:
	long	0
saveFrmsize:
	short	0



	text
	# General purpose code

	global	getusp,getsr
getusp:
	mov.l	%usp,%a0		# get the user stack pointer
	mov.l	%a0,%d0
	rts	

getsr:
	mov.l	&0,%d0			# get the sr
	mov.w	%sr,%d0
	rts	



	global	getcacr, setcacr
getcacr:
	mov.l	%cacr,%d0
	rts
setcacr:
	mov.l	4(%sp),%d0
	mov.l	%d0,%cacr
	rts



	global	ffs_sig
ffs_sig:
	mov.l	4(%sp),%d0
	bne	ffs2%
	mov.l	%d0,%a0
	rts
ffs2%:
	bfffo	4(%sp){&0:&31},%d0
	neg.l	%d0
	add.l	&32,%d0
	cmp.l	%d0,&3			# see if we're looking at a SIGQUIT
	bne	ffs3%

	cmp.l	0x28,&lineAFault	# if we're handling our own Aline traps
	beq	ffs3%	 		# then dont process SIGQUITS if NMIflag set
	movs.b	0xC2C,%d1
	btst	&7,%d1
	beq	ffs3%
	clr.l	%d0
ffs3%:
	rts



	global	ffs
ffs:
	mov.l	4(%sp),%d0
	bne	ffs1%
	mov.l	%d0,%a0
	rts
ffs1%:
	bfffo	4(%sp){&0:&31},%d0
	neg.l	%d0
	add.l	&32,%d0
	mov.l	%d0,%a0
	rts


	#	Software interrupt request handler
	data
sir%:	byte	0


	text

	global	siron
siron:
	bset	&0,sir%
	rts


	global	softint			# enter with interrupts masked
dosir%:
	mov.b	&0x02,sir%		# clear posted bit + set busy flag
	mov.w	&0x2000,%sr
	jsr	softint			# do tasks
	mov.w	&0x2700,%sr
	btst	&0,sir%			# was another soft interrupt posted
	bne	dosir%			# while we were busy?
	clr.b	sir%			# clear flags
	mov.w	&0x2000,%sr
	rts	



	# MMU Primitives
	# Turn on MMU (write to root pointer and translation control register)
	global	turnon_mmu
turnon_mmu:
	lea     cpu_rp,%a0
	#       pmove   a0@,crp
	short   0xF010
	short   0x4C00

	lea     sup_rp,%a0
	#       pmove   a0@,srp
	short   0xF010
	short   0x4800

	move.l	&0x82C07760,-(%sp)	# Enable, SRE, 4K, 7/7/6 table segmentation
	#	pmove	(%sp),tc
	short   0xF017
	short   0x4000
	add.w	&4,%sp

	#       pflusha                 # flush all atc entries
	short   0xF000
	short   0x2400

	mov.l	&0,%d0			# interrupt vector base register -> virtual 0
 	mov.l	%d0,%vbr

	mov.l   &1,mmu_on
	rts	


	global	fl_atc, fl_sysatc, fl_usratc, fl_userpg, ld_userpg
fl_usratc:
	cmp.l	cputype,&VER_MC68030	# on 68030 must use pflusha
	beq	fl_atc
	lea	cpu_rp,%a0
	#	pflushr	(%a0)		# flush current user's atc entries
	short	0xF010
	short	0xA000
	rts

fl_sysatc:
	cmp.l	cputype,&VER_MC68030
	beq	fl_atc
	#	pflushs	&4,&4		# flush all supervisor atc entries
	short	0xF000
	short	0x3494
	rts

fl_atc:
	#       pflusha                 # flush all atc entries
	short   0xF000
	short   0x2400
	rts	

fl_userpg:
	mov.l	4(%sp),%a0
	#	pflush &0,&4,(%a0)
	short	0xF010
	short	0x3893
	rts


ld_userpg:
	mov.l	4(%sp),%a0
	mov.l	8(%sp),%d0
	#	ploadw %d0,(%a0)
	short	0xF010
	short	0x2008
	rts




	global	realvtopte, get_psr, vtopte_sr
realvtopte:
	mov.l	4(%sp),%d0		# fetch fcode
	mov.l	8(%sp),%a1		# fetch vaddr
	mov.l	&-1,%a0			# in case of instant failure

	#	ptest (%a1),&7,%a0
	short	0xF011
	short	0x9D08
	mov.l	%a0,%d0
	rts

get_psr:
	sub.l	&2,%sp			# reserve slot on top of stack
	#	pmove %psr,(%sp)
	short	0xF017
	short	0x6200
	mov.w	(%sp)+,%d0
	rts

vtopte_sr:
	mov.l	4(%sp),%d0		# fetch fcode
	mov.l	8(%sp),%a0		# fetch vaddr
	mov.l	12(%sp),%a1		# fetch &psr

	mov.w	%sr,%d1			# save current interrupt mask
	mov.w	&0x2700,%sr		# mask all interrupts

	#	ptest (%a0),&7,%a0	# get pte into a0
	short	0xF010
	short	0x9D08
	#	pmove %psr,(%a1)	# get psr into &psr
	short	0xF011
	short	0x6200

	mov.w	%d1,%sr			# restore interrupt state
	mov.l	%a0,%d0			# return pte in both a0 and d0
	rts


	global	caller
caller:
	link	%fp,&0
	mov.l	(%fp),%a0
	mov.l	4(%a0),%d0
	mov.l	%d0,%a0
	unlk	%fp
	rts




	global	user_errret
user_errret:
	mov.l	&USER_DSPACE,%d0	# make sure we leave these pointing
	mov.l	%d0,%dfc		# at user space
	mov.l	%d0,%sfc		# if set to any other space
	mov.l	UDOT+U_FLTSP%,%sp	# set back to stack of caller
	mov.l	UDOT+U_FLTRV%,%d0	# set up return value
	bne.b	L%errret
	mov.l	&-1,%d0
L%errret:
	clr.l	UDOT+U_FLTSP%
	clr.l	UDOT+U_FLTRV%
	rts


	# fuword(uaddr)
	#
	global	fuword
fuword:
	mov.l	4(%sp),%a0
	mov.l	%sp,UDOT+U_FLTSP%	# in case we have an illegal address
	movs.l	(%a0),%d0		# fetch long from user space
	clr.l	UDOT+U_FLTSP%
	rts


	# fushort(uaddr)
	#
	global	fushort
fushort:
	mov.l	4(%sp),%a0
	mov.l	%sp,UDOT+U_FLTSP%	# in case we have an illegal address
	mov.l	&0,%d0
	movs.w	(%a0),%d0		# fetch long from user space
	clr.l	UDOT+U_FLTSP%
	rts


	# fubyte(uaddr)
	#
	global	fubyte
fubyte:
	mov.l	4(%sp),%a0
	mov.l	%sp,UDOT+U_FLTSP%	# in case we have an illegal address
	mov.l	&0,%d0
	movs.b	(%a0),%d0		# fetch byte from user space
	clr.l	UDOT+U_FLTSP%
	rts


	# suword(uaddr, val)
	#
	global	suword
suword:
	mov.l	4(%sp),%a0
	mov.l	8(%sp),%d0
	mov.l	%sp,UDOT+U_FLTSP%	# in case we have an illegal address
	movs.l	%d0,(%a0)		# move long to user space
	clr.l	UDOT+U_FLTSP%
	clr.l	%d0			# indicate success
	rts


	# sushort(uaddr, val)
	#
	global	sushort
sushort:
	mov.l	4(%sp),%a0
	mov.l	8(%sp),%d0
	mov.l	%sp,UDOT+U_FLTSP%	# in case we have an illegal address
	movs.w	%d0,(%a0)		# move long to user space
	clr.l	UDOT+U_FLTSP%
	clr.l	%d0			# indicate success
	rts


	# subyte(uaddr, val)
	#
	global	subyte
subyte:
	mov.l	4(%sp),%a0
	mov.l	8(%sp),%d0
	mov.l	%sp,UDOT+U_FLTSP%	# in case we have an illegal address
	movs.b	%d0,(%a0)		# move byte to user space
	clr.l	UDOT+U_FLTSP%
	clr.l	%d0			# indicate success
	rts


	# bcopyin(uaddr, kaddr, nbytes)
	#
	global	bcopyin
bcopyin:
	mov.l	 4(%sp),%a0
	mov.l	 8(%sp),%a1
	mov.l	12(%sp),%d0
	beq.b	L%bcretz
	sub.l	&1,%d0			# set up for dbra

	mov.l	%sp,UDOT+U_FLTSP%	# in case we have an illegal address
L%bcloop:
	movs.b	(%a0)+,%d1
	mov.b	%d1,(%a1)+
	dbeq	%d0,L%bcloop
	bne.b	L%bcretz
L%bcretc:
	clr.l	UDOT+U_FLTSP%
	mov.l	%a0,%d0			# compute count of chars xferred
	sub.l	4(%sp),%d0
	rts
L%bcretz:
	mov.l	&0,%d0			# indicate success
	mov.l	%d0,UDOT+U_FLTSP%
	rts


	# wcopyin(uaddr, kaddr, nbytes)
	#
	global	wcopyin
wcopyin:
	mov.l	 4(%sp),%a0
	mov.l	 8(%sp),%a1
	mov.l	12(%sp),%d0
	lsr.l	&2,%d0			# turn into count of longs
	beq.b	L%wcretz
	sub.l	&1,%d0			# set up for dbra

	mov.l	%sp,UDOT+U_FLTSP%	# in case we have an illegal address
L%wcloop:
	movs.l	(%a0)+,%d1
	mov.l	%d1,(%a1)+
	dbeq	%d0,L%wcloop
	bne.b	L%wcretz
L%wcretc:
	clr.l	UDOT+U_FLTSP%
	mov.l	%a0,%d0			# compute count of chars xferred
	sub.l	4(%sp),%d0
	rts
L%wcretz:
	mov.l	&0,%d0			# indicate success
	mov.l	%d0,UDOT+U_FLTSP%
	rts




set	sizeOfByte,	0x1
set	sizeOfShort,	sizeOfByte*2
set	sizeOfLong,	sizeOfShort*2
set	theParameters,	sizeOfLong

set	d0a0a1,		0x0301
set	a0a1,		0x0300



 # void copyin(fromAddr, toAddr, nBytes)
 #
 # char		*fromAddr, *toAddr;
 # unsigned	nBytes;
 #
 # {

	global	copyin
copyin:
	movem.l	theParameters(%sp),&d0a0a1
	move.l	%sp,UDOT+U_FLTSP%	# Signal an EFAULT
	move.l	&EFAULT,%d1		#    if there's a problem
	move.l	%d1,UDOT+U_FLTRV%	#       in copying the data

	add.l	%a1,%a0			# Adjust pointers to copy backwards
	cmp.l	%a1,&sizeOfByte*64
	exg	%d0,%a1			# Are we copying
	add.l	%d0,%a1			#     enough bytes to justify
	blo.s	lilCopyIn		#          block moves + long aligning the data ?

	move.w	%a1,%d1			# Is the copy
	and.w	&0x3,%d1		#    already long aligned ?
	beq.s	bigCopyIn
alignCopyIn:
	moves.l	-(%a1),%d1
	move.l	%d1,-(%a0)
	move.w	%a1,%d1			# Is the copy
	and.l	&0x3,%d1
	sub.l	%d1,%d0			# adjust count
	subq.w	&4,%d1
	sub.w	%d1,%a1
	sub.w	%d1,%a0
	move.l	&sizeOfByte*64,%d1	# Are we copying
	cmp.l	%d0,%d1			#     enough data to justify
	blo.s	lilCopyIn		#          using big block moves ?
bigCopyIn:
	ror.l	&6,%d0			# Adjust our count
	subq.w	&1,%d0			#    to decrement by 64
copyInBy64:
	moves.l	-(%a1),%d1		# Copyin
	move.l	%d1,-(%a0)		#    64 bytes
	moves.l	-(%a1),%d1		#        at a time!
	move.l	%d1,-(%a0)
	moves.l	-(%a1),%d1		#    etc...
	move.l	%d1,-(%a0)
	moves.l	-(%a1),%d1
	move.l	%d1,-(%a0)
	moves.l	-(%a1),%d1
	move.l	%d1,-(%a0)
	moves.l	-(%a1),%d1
	move.l	%d1,-(%a0)
	moves.l	-(%a1),%d1
	move.l	%d1,-(%a0)
	moves.l	-(%a1),%d1
	move.l	%d1,-(%a0)
	moves.l	-(%a1),%d1
	move.l	%d1,-(%a0)
	moves.l	-(%a1),%d1
	move.l	%d1,-(%a0)
	moves.l	-(%a1),%d1
	move.l	%d1,-(%a0)
	moves.l	-(%a1),%d1
	move.l	%d1,-(%a0)
	moves.l	-(%a1),%d1
	move.l	%d1,-(%a0)
	moves.l	-(%a1),%d1
	move.l	%d1,-(%a0)
	moves.l	-(%a1),%d1
	move.l	%d1,-(%a0)
	moves.l	-(%a1),%d1
	move.l	%d1,-(%a0)
	dbra	%d0,copyInBy64		# Can we still copy 64 more ?

	clr.w	%d0			# Get number of long
	rol.l	&4,%d0			#     words remaining to copyin
	bne.s	testCopyInBy4
	move.l	&0x0,%d0		# Copy was successful,
	move.l	%d0,UDOT+U_FLTSP%	#    reset our EFAULT handler
	move.l	%d0,UDOT+U_FLTRV%
	rts				# Return to caller
lilCopyIn:
	ror.l	&2,%d0			# Get number of long words to copyin
	bra.s	testCopyInBy4
copyInBy4:
	moves.l	-(%a1),%d1		# Copyin
	move.l	%d1,-(%a0)		#    4 bytes
testCopyInBy4:
	dbra	%d0,copyInBy4		#       at a time!

	clr.w	%d0			# Get number of bytes
	rol.l	&2,%d0			#   remaining to be copied
	bra.s	testCopyInBy1
copyInBy1:
	moves.b	-(%a1),%d1		# Copyin
	move.b	%d1,-(%a0)		#   1 byte
testCopyInBy1:
	dbra	%d0,copyInBy1		#     at a time!

	move.l	&0x0,%d0		# Copy was successful,
	move.l	%d0,UDOT+U_FLTSP%	#    reset our EFAULT handler
	move.l	%d0,UDOT+U_FLTRV%
	rts				# Return to caller
 # }




 # void copyout(fromAddr, toAddr, nBytes)
 #
 # char		*fromAddr, *toAddr;
 # unsigned	nBytes;
 #
 # {

	global	copyout
copyout:
	movem.l	theParameters(%sp),&d0a0a1
	move.l	%sp,UDOT+U_FLTSP%	# Signal an EFAULT
	move.l	&EFAULT,%d1		#    if there's a problem
	move.l	%d1,UDOT+U_FLTRV%	#       in copying the data

	add.l	%a1,%a0			# Adjust pointers to copy backwards
	cmp.l	%a1,&sizeOfByte*64
	exg	%d0,%a1			# Are we copying
	add.l	%d0,%a1			#    enough data to justify
	blo.s	lilCopyOut		#        long aligning the copy ?

	move.w	%a1,%d1
	and.w	&3,%d1			# Is our data
	beq.s	bigCopyOut		#      long aligned ?
alignCopyOut:
	move.l	-(%a1),%d1		# Assume we need
	moves.l	%d1,-(%a0)		#    to move some bytes to
	move.w	%a1,%d1			# Is the copy
	and.l	&0x3,%d1
	sub.l	%d1,%d0			# adjust count
	subq.w	&4,%d1
	sub.w	%d1,%a1
	sub.w	%d1,%a0
	move.l	&sizeOfByte*64,%d1	# Are we copying
	cmp.l	%d0,%d1			#     enough data to justify
	blo.s	lilCopyOut		#         a big block move ?
bigCopyOut:
	ror.l	&6,%d0			# Adjust our count
	subq.w	&1,%d0			#    to decrement by 64
copyOutBy64:
	move.l	-(%a1),%d1		# Copyout
	moves.l	%d1,-(%a0)		#    64 bytes
	move.l	-(%a1),%d1		#       at a time!
	moves.l	%d1,-(%a0)
	move.l	-(%a1),%d1		#    etc...
	moves.l	%d1,-(%a0)
	move.l	-(%a1),%d1
	moves.l	%d1,-(%a0)
	move.l	-(%a1),%d1
	moves.l	%d1,-(%a0)
	move.l	-(%a1),%d1
	moves.l	%d1,-(%a0)
	move.l	-(%a1),%d1
	moves.l	%d1,-(%a0)
	move.l	-(%a1),%d1
	moves.l	%d1,-(%a0)
	move.l	-(%a1),%d1
	moves.l	%d1,-(%a0)
	move.l	-(%a1),%d1
	moves.l	%d1,-(%a0)
	move.l	-(%a1),%d1
	moves.l	%d1,-(%a0)
	move.l	-(%a1),%d1
	moves.l	%d1,-(%a0)
	move.l	-(%a1),%d1
	moves.l	%d1,-(%a0)
	move.l	-(%a1),%d1
	moves.l	%d1,-(%a0)
	move.l	-(%a1),%d1
	moves.l	%d1,-(%a0)
	move.l	-(%a1),%d1
	moves.l	%d1,-(%a0)
	dbra	%d0,copyOutBy64		# Can we still copy 64 more ?

	clr.w	%d0
	rol.l	&4,%d0			#    words remaining to copyout
	bne.s	testCopyOutBy4
	move.l	&0x0,%d0		# Copy was successful,
	move.l	%d0,UDOT+U_FLTSP%	#    reset our EFAULT handler
	move.l	%d0,UDOT+U_FLTRV%
	rts				# Return to caller
lilCopyOut:
	ror.l	&2,%d0			# Get number of long words to copyout
	bra.s	testCopyOutBy4
copyOutBy4:
	move.l	-(%a1),%d1		# Copyout
	moves.l	%d1,-(%a0)		#    4 bytes
testCopyOutBy4:
	dbra	%d0,copyOutBy4		#       at a time!

	clr.w	%d0			# Get number of bytes
	rol.l	&2,%d0			#      remaining to copyout
	bra.s	testCopyOutBy1
copyOutBy1:
	move.b	-(%a1),%d1		# Copyout
	moves.b	%d1,-(%a0)		#    1 byte
testCopyOutBy1:
	dbra	%d0,copyOutBy1		#      at a time!

	move.l	&0x0,%d0		# Copy was successful,
	move.l	%d0,UDOT+U_FLTSP%	#    reset our EFAULT handler
	move.l	%d0,UDOT+U_FLTRV%
	rts				# Return to caller

 # }




 # void uclear(toAddr, nBytes)
 #
 # char		*toAddr;
 # unsigned	nBytes;
 #
 # {

	global	uclear
uclear:
	movem.l	theParameters(%sp),&a0a1
	move.l	%sp,UDOT+U_FLTSP%	# Recover gracefully if error on clearing

	move.l	&0x0,%d0		# Are we clearing enough data
	cmp.l	%a1,&sizeOfByte*64	#     to justify long aligning pointers ?
	exg	%d0,%a1
	add.l	%d0,%a0			# Adjust pointers to clear backwards
	blo.s	lilUClear

	move.w	%a0,%d1
	and.l	&0x3,%d1		# Is the data already
	beq.s	bigUClear		#     long word aligned ?
alignUClear:
	moves.l	%a1,-(%a0)		# Assume some bytes
	sub.l	%d1,%d0
	subq.w	&4,%d1			# If not,
	sub.w	%d1,%a0			#    adjust the
	move.l	&sizeOfByte*64,%d1	# Are we clearing
	cmp.l	%d0,%d1			#    enough data to justify
	blo.s	lilUClear		#          a big block clear ?
bigUClear:
	ror.l	&6,%d0			# Adjust our count
	subq.w	&1,%d0			#     to decrement by 64
uClearBy64:
	moves.l	%a1,-(%a0)
	moves.l	%a1,-(%a0)		# uClear
	moves.l	%a1,-(%a0)		#   64 bytes
	moves.l	%a1,-(%a0)		#      at a time!
	moves.l	%a1,-(%a0)
	moves.l	%a1,-(%a0)		#   etc...
	moves.l	%a1,-(%a0)
	moves.l	%a1,-(%a0)
	moves.l	%a1,-(%a0)
	moves.l	%a1,-(%a0)
	moves.l	%a1,-(%a0)
	moves.l	%a1,-(%a0)
	moves.l	%a1,-(%a0)
	moves.l	%a1,-(%a0)
	moves.l	%a1,-(%a0)
	moves.l	%a1,-(%a0)
	dbra	%d0,uClearBy64		# can we clear another 64 ?

	clr.w	%d0			# get number of
	rol.l	&4,%d0			#     words remaining to clear
	bne.s	testUClearBy4
	move.l	&0x0,%d0		# uClear was sucessfull,
	move.l	%d0,UDOT+U_FLTSP%	#     reset error recovery mechanism
	rts				# Return to caller
lilUClear:
	ror.l	&2,%d0			# Get number of long words to clear
	bra.s	testUClearBy4
uClearBy4:
	moves.l	%a1,-(%a0)		# uClear 4 bytes
testUClearBy4:
	dbra	%d0,uClearBy4		#     at a time !

	clr.w	%d0			# Get number of bytes
	rol.l	&2,%d0			#      remaining to clear
	bra.s	testUClearBy1
uClearBy1:
	moves.b	%a1,-(%a0)		# uClear 1 byte
testUClearBy1:
	dbra	%d0,uClearBy1		#     at a time !

	move.l	&0x0,%d0		# uClear was sucessfull,
	move.l	%d0,UDOT+U_FLTSP%	#     reset error recovery mechanism
	rts				# Return to caller
 # }

	# fuargs(uaddr, kaddr, nargs)
	#
	global	fuargs
fuargs:
	mov.l	 4(%sp),%a0
	mov.l	 8(%sp),%a1
	mov.l	12(%sp),%d0
	mov.l	%sp,UDOT+U_FLTSP%	# in case we have an illegal address
	sub.l	&1,%d0			# set up for dbra
L%faloop:
	movs.l	(%a0)+,%d1
	mov.l	%d1,(%a1)+
	dbra	%d0,L%faloop

	mov.l	&0,%d0			# indicate success
	mov.l	%d0,UDOT+U_FLTSP%
	rts




	global	bcmp
bcmp:
	mov.l	4(%sp),%a0
	mov.l	8(%sp),%a1
	mov.l	12(%sp),%d0
	beq.b	bcmp_done
	sub.w	&1,%d0			# allow for dbra 
bcmp_loop:
	cmp.b	(%a0)+,(%a1)+
	dbne	%d0,bcmp_loop
	add.w	&1,%d0			# dbra terminates at -1
bcmp_done:				# we want to return 0
	rts				# for success




 #########################################################################################
 #
 #   getc(clist)
 #
 #
	global	cfreelist
	global	getc
getc:
	movm.l	&0x3000,-(%sp)		# save d2/d3
	mov.l	12(%sp),%a0		# p = clist
	mov.w	%sr,%d2
	mov.w	&0x2600,%sr

	mov.l	4(%a0),%a1		# cp = p->c_cf
	sub.l	&1,(%a0)
	blt.b	getc_empty
	mov.l	&0,%d3
	mov.b	4(%a1),%d3
	add.b	&1,4(%a1)
	mov.b	(%a1,%d3.l,6.b),%d3	# c = cp->c_data[cp->c_first++];
	mov.b	4(%a1),%d0
	cmp.b	%d0,5(%a1)
	beq	getc_free
getc_ret:
	mov.w	%d2,%sr
	mov.l	%d3,%d0			# return(c)
	movm.l	(%sp)+,&0x000c		# restore d2/d3
	rts
getc_empty:
	add.l	&1,(%a0)
	mov.l	&-1,%d3			# c = -1
	mov.l	%a1,%d0
	beq.b	getc_ret
getc_free:
	mov.l	(%a1),4(%a0)
	bne.b	L%30
	clr.l	8(%a0)
L%30:	mov.l	cfreelist,(%a1)
	mov.l	%a1,cfreelist
	tst.l	cfreelist+8
	beq.b	getc_ret
	clr.l	cfreelist+8
	pea.l	cfreelist
	jsr	wakeup
	add.w	&4,%sp
	br	getc_ret


	global	putc
putc:
	mov.l	8(%sp),%a0		# p = clist
	mov.w	%sr,%d1
	mov.w	&0x2600,%sr

	mov.l	8(%a0),%a1		# cp = p->c_c1
	mov.l	%a1,%d0			# if (cp == NULL)
	beq.b	putc_full		# ||
	mov.l	&0,%d0
	mov.b	5(%a1),%d0		# cp->c_last == cfreelist.c_size
	cmp.l	%d0,cfreelist+4
	beq.b	putc_full
putc_char:
	add.b	&1,5(%a1)
	mov.b	7(%sp),(%a1,%d0.l,6.b)  # cp->c_data[cp->c_last++] = c;
	add.l	&1,(%a0)		# p->c_cc++;
	mov.w	%d1,%sr
	mov.l	&0,%d0
	rts
putc_full:
	tst.l	cfreelist
	beq	putc_bad
	mov.l	%a2,-(%sp)		# need another address register
	mov.l	%a1,%a2			# ocp = cp
	mov.l	cfreelist,%a1
	mov.l	(%a1),cfreelist
	clr.l	(%a1)
	mov.l	%a2,%d0
	bne.b	L%38
	mov.l	%a1,4(%a0)
	br.b	L%39
L%38:	mov.l	%a1,(%a2)
L%39:	mov.l	%a1,8(%a0)
	mov.l	&0,%d0
	mov.b	%d0,5(%a1)
	mov.b	%d0,4(%a1)
	mov.l	(%sp)+,%a2
	br	putc_char
putc_bad:
	mov.w	%d1,%sr
	mov.l	&-1,%d0
	rts


	global	free_lastcb
free_lastcb:
	mov.l	4(%sp),%a0
	mov.l	8(%a0),%d0
	beq.b	L%41
	mov.l	%d0,%a1
	cmp.l	%a1,4(%a0)
	bne.b	L%43
	clr.l	4(%a0)
	clr.l	8(%a0)
L%40:	mov.l	cfreelist,(%a1)
	mov.l	%a1,cfreelist
L%41:	rts

L%43:	mov.l	%a2,-(%sp)
	mov.l	4(%a0),%d0
L%%1:	mov.l	%d0,%a2
	mov.l	(%a2),%d0
	cmp.l	%d0,%a1
	bne.b	L%%1
	mov.l	%a2,8(%a0)
	mov.l	(%sp)+,%a2
	br.b	L%40


	global	unputc
unputc:
	movm.l	&0x3000,-(%sp)		# save d2/d3
	mov.l	12(%sp),%a0
	mov.w	%sr,%d3
	mov.w	&0x2600,%sr

	sub.l	&1,(%a0)
	blt.b	L%49
	mov.l	8(%a0),%a1
	mov.l	&0,%d2
	sub.b	&1,5(%a1)
	mov.b	5(%a1),%d2
	mov.b	(%a1,%d2.l,6.b),%d2
	mov.b	4(%a1),%d0
	cmp.b	%d0,5(%a1)
	bne.b	L%51
L%50:	mov.l	%a0,-(%sp)
	jsr	free_lastcb
	add.w	&4,%sp
L%51:	mov.w	%d3,%sr
	mov.l	%d2,%d0
	movm.l	(%sp)+,&0x000c		# restore d2/d3
	rts
L%49:
	add.l	&1,(%a0)
	mov.l	&-1,%d2
	br	L%50


	global	getcf
getcf:
	mov.w	%sr,%d1
	mov.w	&0x2600,%d1

	mov.l	cfreelist,%a0
	mov.l	%a0,%d0
	beq.b	L%53
	mov.l	(%a0),cfreelist
	clr.l	(%a0)
	clr.b	4(%a0)
	mov.b	cfreelist+7,5(%a0)
L%53:	mov.w	%d1,%sr
	rts


	global	putcf
putcf:
	mov.l	4(%sp),%a0
	mov.w	%sr,%d1
	mov.w	&0x2600,%sr

	mov.l	cfreelist,(%a0)
	mov.l	%a0,cfreelist
	tst.l	cfreelist+8
	bne.b	L%56
	mov.w	%d1,%sr
	rts
L%56:	clr.l	cfreelist+8
	mov.l	%d1,-(%sp)
	mov.l	&cfreelist,-(%sp)
	jsr	wakeup
	add.w	&4,%sp
	mov.l	(%sp)+,%d1
	mov.w	%d1,%sr
	rts


	global	getcb
getcb:
	mov.l	4(%sp),%a1
	mov.w	%sr,%d1
	mov.w	&0x2600,%sr

	mov.l	4(%a1),%a0
	mov.l	%a0,%d0
	beq.b	L%58
	mov.l	&0,%d0
	mov.b	5(%a0),%d0
	sub.b	4(%a0),%d0
	sub.l	%d0,(%a1)
	blt.b	L%59
	mov.l	(%a0),4(%a1)
	bne.b	L%62
	clr.l	8(%a1)
L%62:	mov.l	%a0,%d0
L%58:	mov.w	%d1,%sr
	rts
L%59:	mov.l	&clpanicmsg,-(%sp)
	jsr	panic
	add.w	&4,%sp
	br.b	L%59


	global	putcb
putcb:
	mov.l	%a2,-(%sp)
	mov.l	 8(%sp),%a0
	mov.l	12(%sp),%a1
	mov.w	%sr,%d1
	mov.w	&0x2600,%sr

	mov.l	8(%a1),%a2
	mov.l	%a2,%d0
	bne.b	L%65
	mov.l	%a0,4(%a1)
	bra.b	L%66
L%65:	mov.l	%a0,(%a2)
L%66:	mov.l	%a0,8(%a1)
	clr.l	(%a0)
	mov.l	&0,%d0
	mov.b	5(%a0),%d0
	sub.b	4(%a0),%d0
	add.l	%d0,(%a1)
	mov.w	%d1,%sr
	mov.l	&0,%d0
	mov.l	(%sp)+,%a2
	rts


	data	2
clpanicmsg:
	byte	'b,'a,'d,0x20,'c,'l,'i,'s
	byte	't,0x20,'c,'o,'u,'n,'t,'\n
	byte	0x00


	text

 #########################################################################################
 #
 # reboot the system
 # 	- turn off the mmu (we assume that this code is mapped 1-1 in pstart)
 #	- get the ROM stack/start pc info
 #	- cause a bus reset (which remaps the ROMs)
 #	- jump to the bootstrap start
 #
	global	doboot
doboot:
	mov.w	&HIGH%,%sr		# splhi()

	mov.l	&CACHEON,%d0	 	# make sure the cache is turned on
	mov.l	%d0,%cacr

	mov.l	&0,(%sp)		# turn off the MMU
	short	0xf017			# pmove (%sp),tc
	short	0x4000

	mov.l	0x40000000,%sp		# set up the ROM entry point
	mov.l	0x40000004,%a0

	mov.l	&rst%,%a1
	mov.l	&reboot_temp+4,%d1
	and.l	&0xfffffffc,%d1		# copy the final two instructions to
	mov.l	%d1,%a2			# a longword aligned location
	mov.l	(%a1),(%a2)

	# reset and jump to the ROMs (get the last 2 instructions in 1
	#	32-bit chunk so that when the reset is done, and the ROM
	#	mapped over the RAM, the next instruction does not need to be
	#	fetched as it is already in the cache)

	jmp	(%a2)
rst%:	reset
	jmp	(%a0)

	data
reboot_temp:	long	0,0
