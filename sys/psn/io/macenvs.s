/*
    This file contains assembler routines used by macenv.c and slotmgr.c.
*/

	set	MemErr,0x220
	set	Line1010,0x28

/*
int callStartSDeclMgr(func,warmStart)
int (*func)();
int warmStart;

    This routine calls the Slot Manager's initialization routine.  The address
of this routine is passed in as a parameter.  The warmStart flag gets passed to
the routine in D0.
*/

	global	callStartSDeclMgr
callStartSDeclMgr:
	mov.l	4(%sp),%a0	# a0 gets func
	mov.l	8(%sp),%d0	# d0 gets warmStart flag
	movm.l	&0x3f3f,-(%sp)	# save everything but C scratch registers
	jsr	(%a0)		# call the routine
	movm.l	(%sp)+,&0xfcfc	# restore saved registers
	rts			# and go home

	global	callSecondaryInit
callSecondaryInit:
	mov.l	4(%sp), %a1	# a1 gets func
	mov.l	8(%sp), %a0	# a0 gets spBlock ptr
	movm.l	&0x3f3f, -(%sp)	# save all but C scratch regs
	jsr	(%a1)		# call the routine
	movm.l	(%sp)+, &0xfcfc # restore regs
	rts

/*
    This routine is a patch for the Slot Manager trap.  It calls a C routine
that looks like this:

int SlotmanagerPatch(selector,pb,callRealSM)
int selector;
SpBlock *pb;
int *callRealSM;

If that routine sets the flag pointed to by callRealSM, then this routine will
call the real Slot manager routine.  Otherwise, the value returned by
SlotManagerPatch will be returned to the original caller.

When we call SlotManagerPatch, the stack looks like this:
	36	return to original caller
	32	saved d0
	28	callRealSM flag
	16	saved d1, a0, a1
	12	pointer to callRealSM flag
	8	parameter block pointer
	4	selector
	0	return to aSlotManagerPatch
*/

	global	aSlotManagerPatch
aSlotManagerPatch:
	mov.l	%d0,-(%sp)	# save d0 seperately
	sub.l	&4,-(%sp)	# allocate space for the callRealSM flag
	movm.l	&0x40c0,-(%sp)	# save the C compiler's other scratch registers
	pea	12(%sp)		# pass the address of my flag
	mov.l	%a0,-(%sp)	# pass the parameter block pointer
	mov.l	%d0,-(%sp)	# pass the selector
	jsr	SlotManagerPatch# call the C patch routine
	add.l	&12,%sp		# strip C parameters
	movm.l	(%sp)+,&0x0302	# restore registers
	tst.l	(%sp)+		# should we call the real one?
	bne	callGeorge
	add.l	&4,%sp		# throw away saved d0
	rts			# return to original caller
callGeorge:			# call the real SM routine
	mov.l	(%sp)+,%d0	# restore d0
	mov.l	realSlotManager,-(%sp)
	rts			# jump to ROM

/*
int SlotManager(selector,pb)
int selector;
struct SpBlock *pb;

    This routine calls the Slot Manager via an a-line trap.
*/

        global  SlotManager
SlotManager:
        mov.l   4(%sp),%d0
        mov.l   8(%sp),%a0
	mov.l	%d2,-(%sp)	# save d2 since Slot Manager may trash it
        jsr	aSlotManagerPatch
	mov.l	(%sp)+,%d2	# restore d2
        rts

/*
int callRealSlotManager(selector,pb)
int selector;
SpBlock *pb;

    This routine calls the real Slot Manager trap in ROM.
*/

	global	callRealSlotManager
callRealSlotManager:
	mov.l	4(%sp),%d0	# selector gets passed in D0
	mov.l	8(%sp),%a0	# pb gets passed in a0
	mov.l	realSlotManager,%a1
	mov.l	%d2,-(%sp)	# save d2, it is not a C scratch register
	jsr	(%a1)		# call the real Slot Manager routine
	mov.l	(%sp)+,%d2	# restore d2
	rts			# and return to my caller

/*
int callDriver(func,pb,dce)
int (*func)();
something *pb,*dce;

    This routine calls a routine in a device driver.  All registers except the
C compiler's scratch registers are preserved.  Some of the routines return a
result code in d0, and this just gets passed back to the caller.
    We have to play games with the stack.  It seems that some drivers are
allocating more stuff on the stack than we have room for.  For future drivers,
we will document judicious use of the StackSpace call.  For current drivers,
use a temporary stack that is big enough.  Exactly what big enough means is
up for grabs.  Since we always point the stack at the same temporary buffer,
this routine is definitely not re-entrant.  This shouldn't be a problem, since
we never call the driver asynchronously.  Just in case, we try to catch any
overflows of this temporary stack.  stackMagic is a migic number that is stored
just before the stack.  If this value changes, we know that the stack got
mushed.  This test isn't perfect, but it may be good enough.
    Note that there is an interesting bug in some Radius video drivers.
They save and restore all registers when you call their control routine.
This includes d0, which should be used for the return value.  So, we clear d0
just before calling them so things will look ok.  My guess is that the Mac
OS happens to leave d0 clear at this point as well.
*/

	data
	lcomm	stackMagic,4
	lcomm	tempStack,4096
	lcomm	saveSP,4
	text

	global	callDriver
callDriver:
	mov.l	12(%sp),%a1	# a1 gets the dce
	mov.l	8(%sp),%a0	# a0 gets parameter block pointer
	movm.l	&0x3f3f,-(%sp)	# save everything but d0,d1,a0,a1
	mov.l	52(%sp),%a2	# a2 gets func
	mov.l	%sp,saveSP	# save current value of stack pointer
	lea	saveSP,%sp	# point stack at end of tempStack
	mov.l	&0x12345678,stackMagic
	clr.l	%d0		# make radius monitors work
	jsr	(%a2)		# call the function
	mov.l	saveSP,%sp	# and restore stack pointer to original stack
	movm.l	(%sp)+,&0xfcfc	# restore saved registers
	cmp.l	stackMagic,&0x12345678
	beq	stackOK
	mov.l	12(%sp),-(%sp)	# trashedStack(dce);
	jsr	trashedStack	# Oh no!  The stack was overrun!  panic!
stackOK:
	rts			# and get out of Dodge City (thanks Phil)
				# no, Goldman!

/*
callSlotTask(func,param)
int (*func)();
long param;

    This routine calls a slot's interrupt routine.  The routine expects a
parameter in a1.  It saves all registers but a1 and d0.  It returns a status
int d0.  Non-zero means that the routine handled the interrupt.  Zero means
that we are in trouble unless we have another routine to call.
    The documentation claims that the interrupt routine will preserve all
registers except a1 and d0.  However, there is a RasterOps driver that
expects to be able to trash a0-a3 and d0-d3.  So, we save a2-a3 and d2-d3
since a0-a1 and d0-d1 are C scratch registers anyway.
*/

	global	callSlotTask
callSlotTask:
	mov.l	4(%sp),%a0	# a0 gets the func
	mov.l	8(%sp),%a1	# a1 gets param
	movm.l	&0x3030,-(%sp)	# save a2-a3 and d2-d3
	jsr	(%a0)		# call the routine
	movm.l	(%sp)+,&0x0c0c	# restore registers
	rts			# and go home

/*
    This routine gets called by video drivers.  It is supposed to call any
VBL tasks that are pending for the slot whose number is in d0.  For now, it
just returns.  Perhaps we will change this when we do cursor's correctly.
*/
	global	vblTask
vblTask:
	rts

/*
 * ReadXPRam, WriteXPRam, ReadDateTime, SetDateTime are wrappers which allow
 * code in the kernel to use these A-Line traps.  In C, they are defined as:
 *
 * void ReadXPRam(buffer,count)	or WriteXPRam(buffer,count)
 *     char *buffer;
 *     long count;		low word is offset, high word is count
 *
 * ReadDateTime(timebuf)	or SetDateTime(seconds)
 *     long *timebuf;		       long seconds;
 * 
 * These routines are currently used by the nvram driver and by
 * a timeout routine (time_fix_timeout - which corrects time due to
 * lost 60Hz interrupts).  Since the are called both at user level
 * and mess with the hardware vias, it is necessary to spl.
 */

        global  ReadXPRam
ReadXPRam:
	mov.l	Line1010,-(%sp)	# save current A-line trap handler
	mov.l	kernelAline,Line1010
        mov.l   8(%sp),%a0	# pointer gets passed in a0
        mov.l   12(%sp),%d0	# count and index get passed in d0
	mov.w	%sr,-(%sp)	# save sr
	mov.w	&0x2700, %sr	# spl7
        short   0xa051
	mov.w	(%sp)+,%sr	# restore sr
        mov.l   (%sp)+,Line1010	# restore old A-line trap handler
        rts

        global  WriteXPRam
WriteXPRam:
        mov.l   Line1010,-(%sp)	# save current A-line trap handler
        mov.l   kernelAline,Line1010
        mov.l   8(%sp),%a0	# pointer gets passed in a0
        mov.l   12(%sp),%d0	# count and index get passed in d0
	mov.w	%sr,-(%sp)	# save sr
	mov.w	&0x2700, %sr	# spl7
        short   0xa052
	mov.w	(%sp)+,%sr	# restore sr
	mov.l	(%sp)+,Line1010	# restore old A-line trap handler
        rts

        global  ReadDateTime
ReadDateTime:
        mov.l   Line1010,-(%sp)	# save current A-line trap handler
        mov.l   kernelAline,Line1010
        mov.l   8(%sp),%a0	# pointer gets passed in a0
	mov.w	%sr,-(%sp)	# save sr
	mov.w	&0x2700, %sr	# spl7
        short   0xa039		# ReadDateTime
	mov.w	(%sp)+,%sr	# restore sr
	mov.l	(%sp)+,Line1010	# restore old A-line trap handler
        rts

        global  SetDateTime
SetDateTime:
        mov.l   Line1010,-(%sp)	# save current A-line trap handler
        mov.l   kernelAline,Line1010
        mov.l   8(%sp),%d0	# pointer gets passed in d0
	mov.w	%sr,-(%sp)	# save sr
	mov.w	&0x2700, %sr	# spl7
        short   0xa03a		# SetDateTime
	mov.w	(%sp)+,%sr	# restore sr
	mov.l	(%sp)+,Line1010	# restore old A-line trap handler
        rts

/*
    This routine is a wrapper that calls the C routine noKernelSupport when an
A-line trap gets used that is not supported by the A/UX kernel.
*/

	global  noSupport
noSupport:
	mov.l   %d1,-(%sp)
	mov.l   4(%sp),%a1
	mov.l   -4(%a1),-(%sp)
	jsr     noKernelSupport
	add.l   &8,%sp
	rts

/*
    Used for SuperMac video cards.  See doPatches in macenv.c.
*/
	global  noSupport1
noSupport1:
	jmp	noSupport


/*
    These routines are the wrappers for the A-line traps that are supported.
*/
	global aSetTrapAddress
aSetTrapAddress:
	mov.l	%d0,-(%sp)
	mov.l	%a0,-(%sp)
	jsr	cSetTrapAddress
	add.l	&8,%sp
	rts

	global	aResrvMem
aResrvMem:
	mov.l	%d0,-(%sp)
	jsr	cResrvMem
	add.l	&4,%sp
	rts

	global	aNewPtr
aNewPtr:
	mov.l	%d0,-(%sp)
	jsr	cNewPtr
	add.l	&4,%sp
	mov.w   MemErr,%d0
	ext.l   %d0
	rts

	global	aNewHandle
aNewHandle:
	mov.l	%d0,-(%sp)
	jsr	cNewHandle
	add.l	&4,%sp
	mov.w   MemErr,%d0
	ext.l   %d0
	rts

	global	aDisposPtr
aDisposPtr:
	mov.l	%a0,-(%sp)
	jsr	cDisposPtr
	add.l	&4,%sp
	rts

	global	aDisposHandle
aDisposHandle:
	mov.l	%a0,-(%sp)
	jsr	cDisposPtr
	add.l	&4,%sp
	rts

	global	aHLock
aHLock:
	mov.l	%a0,-(%sp)
	jsr	cHLock
	add.l	&4,%sp
	rts

	global	aStackSpace
aStackSpace:
	jsr	cStackSpace
	rts

	global	aSIntInstall
aSIntInstall:
	mov.l	%d0,-(%sp)
	mov.l	%a0,-(%sp)
	jsr	cSIntInstall
	add.l	&8,%sp
	rts

	global	aSIntRemove
aSIntRemove:
	mov.l	%d0,-(%sp)
	mov.l	%a0,-(%sp)
	jsr	cSIntRemove
	add.l	&8,%sp
	rts

/*
    StripAddress does nothing in a 32 bit world.
*/

	global	aStripAddress
aStripAddress:
	rts

/*
    TickCount just returns lbolt.
*/

	global	aTickCount
aTickCount:
	mov.l	lbolt,4(%sp)
	rts

/*
    SwapMMUMode does nothing, but pretends that it succeeds.  The new Slot
Manager uses this to temporarily change to 32 bit mode when it accesses
slot ROMs.  Since we are always in 32 bit mode, this is a noop.  If I felt
a little more ambitious, I'd check to make sure that this never gets called
to move us into 24 bit mode.
*/

/* The time manager traps do nothing either. */

	global	aSwapMMUMode
	global	aInsTime,aPrimeTime,aRmvTime
	global	aSetVideoDefault
aSwapMMUMode:
aInsTime:
aPrimeTime:
aRmvTime:
aSetVideoDefault:
	mov.l	&0,%d0
	rts

/*
 * This is a hack for SuperMac video cards.  A couple of them need the
 * GetDeviceList A-Line trap, and we don't support it in the kernel.
 * This insane looking structure, and the aGetDeviceList patch will allow
 * those cards to run.
 */
	data
DList:	long	DList
DList2:	long	DList3,0,0,0
	short	0x0
	long	DList2,0,0
DList3:	long	0
	text

	global	aGetDeviceList
aGetDeviceList:
	mov.l	(%sp)+,%a0
	mov.l	DList,(%sp)
	jmp	(%a0)

	global  aGetResource
aGetResource:
	mov.w   4(%sp),-(%sp)
	sub.l   &2,%sp
	mov.l   10(%sp),-(%sp)
	jsr     cGetResource
	add.l   &8,%sp
	mov.l   %a0,10(%sp)
	rtd     &6



