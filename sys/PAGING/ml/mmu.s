 #
 # Copyright 1987, 1988, 1989 Apple Computer, Inc.
 # All Rights Reserved.
 #
 # THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF APPLE COMPUTER, INC.
 # The copyright notice above does not evidence any actual or
 # intended publication of such source code.
 #
 #	@(#)Copyright Apple Computer 1989	Version 2.3 of mmu.s on 90/04/09
 #	file	"mmu.s"

 # MMU Table functions:
 #
 #	XXX_ptr: Gets a pointer to an XXX table, given a virtual address
 #		 and a context (kernel, user)
 #
 #	XXX_ent: Gets address of an XXX entry for a given virtual address
 #		 in a given context (kernel, user)
 #
 # For pointers, XXX is one of L2, Pt, Pg.
 #
 # For entries, XXX is one of L1, L2, Pt.
 #
 # The distinction between ptr and ent is: ent = &tbl[ix(addr)].
 #
 # Note: This code is set up for 040, but assumes 020/030, since it expects
 #	 early termination descriptors (not defined for the 040).
 #	 Early termination is only assumed for L1 entities.

#define LOCORE
#define __sys_signal_h
#include	"mch.h"
#include "sys/uconfig.h"
#include "sys/param.h"

	set	HIGH%,0x2700		# High priority supervisor mode (spl 7)
	set	LOW%,0x2000		# Low priority, supervisor mode (spl 0)

	text
	global	L1_ent, L2_ptr, L2_ent, Pt_ptr, Pt_ent, Pg_ptr
	global	MMU_sync

	set	L2_ADDR%, 0xfffffe00
	set	PT_ADDR%, 0xffffff00
	set	PG_ADDR%, 0xfffff000
	set	DT_DT_Bit%, 01

 # (Dt_t *) L1_ent(Dt_t *, (unsigned int))
L1_ent: mov.l	4(%sp), %a1		# Get table address
	mov.l	8(%sp), %d0		#  and virtual address
	bfextu	%d0{&0:&7}, %d0		# Get the L1 index
	lea	(0,%a1,%d0.l*4), %a0	# Get the L1 entry
	mov.l	%a0, %d0
	rts

 # (Dt_t *) L2_ptr(Dt_t *, (unsigned int))
L2_ptr:	mov.l	4(%sp), %a1		# Get table address
	mov.l	8(%sp), %d0		#  and virtual address
	bfextu	%d0{&0:&7}, %d0		# Get the L1 index
	mov.l	(0,%a1,%d0.l*4), %d0	# Get the L1 entry
	beq	M_err
	btst	&DT_DT_Bit%, %d0	# Check for Early term.
	beq	M_ETerm
	mov.l	%d0, %d1
	and.w	&L2_ADDR%, %d0		# Knock off the dt mode bits
	mov.l	%d0, %a0
	rts

 # (Dt_t *) L2_ent(Dt_t *, (unsigned int))
L2_ent:	mov.l	4(%sp), %a1		# Get table address
	mov.l	8(%sp), %d0		#  and virtual address
	mov.l	%d0, %d1		#  and save the vaddr
	bfextu	%d0{&0:&7}, %d0		# Get the L1 index
	mov.l	(0,%a1,%d0.l*4), %d0	# Get the L1 entry
	beq	M_err
	btst	&DT_DT_Bit%, %d0	# Check for Early term.
	beq	M_ETerm
	and.w	&L2_ADDR%, %d0		# Knock off the dt mode bits
	mov.l	%d0, %a0
	bfextu	%d1{&7:&7}, %d0		# Get the L2 index
	lea	(0,%a0,%d0.l*4), %a0	# Get the L2 entry address
	mov.l	%a0, %d0
	rts

M_ETerm:clr.l	%d0
	mov.l	%d0, %a0
	rts

M_err:	mov.l	&0xffffffff, %d0
	mov.l	%d0, %a0
	rts

 # (pte_t *) Pt_ptr(Dt_t *, (unsigned int))
Pt_ptr:	mov.l	4(%sp), %a1		# Get table address
	mov.l	8(%sp), %d0		#  and virtual address
	mov.l	%d0, %d1		#  and save the vaddr
	bfextu	%d0{&0:&7}, %d0		# Get the L1 index
	mov.l	(0,%a1,%d0.l*4), %d0	# Get the L1 entry
	beq	M_err
	btst	&DT_DT_Bit%, %d0	# Check for Early term.
	beq	M_ETerm
	and.w	&L2_ADDR%, %d0		# Knock off the dt mode bits
	mov.l	%d0, %a0
	bfextu	%d1{&7:&7}, %d0		# Get the L2 index
	mov.l	(0,%a0,%d0.l*4), %d0	# Get the L2 entry
	beq	M_err
	btst	&DT_DT_Bit%, %d0	# Check for Early term.
	beq	M_err
	and.w	&PT_ADDR%, %d0		# Knock off the dt mode bits
	mov.l	%d0, %a0
	rts

 # (pte_t *) Pt_ent(Dt_t *, (unsigned int))
Pt_ent:	mov.l	4(%sp), %a1		# Get table address
	mov.l	8(%sp), %d0		#  and virtual address
	mov.l	%d0, %d1		#  and save the vaddr
	bfextu	%d0{&0:&7}, %d0		# Get the L1 index
	mov.l	(0,%a1,%d0.l*4), %d0	# Get the L1 entry
	beq	M_err
	btst	&DT_DT_Bit%, %d0	# Check for Early term.
	beq	M_ETerm
	and.w	&L2_ADDR%, %d0		# Knock off the dt mode bits
	mov.l	%d0, %a0
	bfextu	%d1{&7:&7}, %d0		# Get the L2 index
	mov.l	(0,%a0,%d0.l*4), %d0	# Get the L2 entry
	beq	M_err
	btst	&DT_DT_Bit%, %d0	# Check for Early term.
	beq	M_err
	and.w	&PT_ADDR%, %d0		# Knock of the pt mode bits
	mov.l	%d0, %a0
	bfextu	%d1{&14:&6}, %d0	# Get the Pt index
	lea	(0,%a0,%d0.l*4), %a0	# Get the Pt entry address
	mov.l	%a0, %d0
	rts

 # (caddr_t *) Pg_ptr(Dt_t *, (unsigned int))
Pg_ptr:	mov.l	4(%sp), %a1		# Get table address
	mov.l	8(%sp), %d0		#  and virtual address
	mov.l	%d0, %d1		#  and save the vaddr
	bfextu	%d0{&0:&7}, %d0		# Get the L1 index
	mov.l	(0,%a1,%d0.l*4), %d0	# Get the L1 entry
	beq	M_err
	btst	&DT_DT_Bit%, %d0	# Check for Early term.
	beq	M_ETerm
	and.w	&L2_ADDR%, %d0		# Knock off the dt mode bits
	mov.l	%d0, %a0
	bfextu	%d1{&7:&7}, %d0		# Get the L2 index
	mov.l	(0,%a0,%d0.l*4), %d0	# Get the L2 entry
	beq	M_err
	btst	&DT_DT_Bit%, %d0	# Check for Early term.
	beq	M_err
	and.w	&PT_ADDR%, %d0		# Knock off the dt mode bits
	mov.l	%d0, %a0
	bfextu	%d1{&14:&6}, %d0	# Get the Pt index
	mov.l	(0,%a0,%d0.l*4), %d0	# Get the Pt entry address
	beq	M_err
	btst	&DT_DT_Bit%, %d0	# Check for Early term.
	bne	M_err
	and.w	&PG_ADDR%, %d0		# Knock off the pt mode bits
	mov.l	%d0, %a0
	rts

 # MMU_sync - updated the CPU root ptr, flush caches and ATC - used after
 #  changing the page table root for 24/32 bit swap operation.
 # ASSUME called at spl0.

MMU_sync:
	lea.l	cpu_rp,%a0		# update pointer to root table
	mov.l	([UDOT+U_PROCP%],P_ROOT%),4(%a0)
	#	pmove	(%a0),%crp	# load the user's root pointer
	short	0xF010
	short	0x4c00

	mov.l	&CACHECLR,%d0		# clear the virtual on-chip caches
	mov.l	%d0,%cacr		#  for new address structure.
#ifdef CLR_CACHE
	mov.l	&1,-(%sp)
	jsr	clr_cache
	add.w	&4,%sp
#endif CLR_CACHE
	mov.l	UDOT+U_USER%+8,%d0	# reset the cache control register
	mov.l	%d0,%cacr		#  for new address structure.
	rts
