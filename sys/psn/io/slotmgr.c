/*
    This file contains code to give the A/UX kernel access to the MAC OS
slot manager, which resides in the MAC II ROM.  The slotmanager system call
is also implemented here.  This code relies heavily on the Macintosh OS
emulation provided by macenv.c.
*/

#include "sys/types.h"
#include "sys/errno.h"
#include "sys/param.h"
#include "sys/dir.h"
#include "sys/signal.h"
#include "sys/uconfig.h"
#include "sys/sysmacros.h"
#include "sys/reg.h"
#include "sys/mmu.h"
#include "sys/page.h"
#include "sys/systm.h"
#include "sys/time.h"
#include "sys/user.h"
#include "mac/types.h"
#include "mac/osutils.h"
#include "mac/segload.h"
#include "mac/files.h"
#include "mac/quickdraw.h"
#include "mac/devices.h"
#include "sys/video.h"
#include "sys/slotmgr.h"

				/* low memory variables */
#define Line1010	(* (ProcPtr *) 0x28)

extern ProcPtr kernelAline;	/* aline handler for kernel */

/*
    This is the slotmanager system call.
*/

sysslotmanager(uap)
struct
    {
    int	selector;
    struct SpBlock *pb;
    } *uap;
{
    extern int rbv_monitor;
    static struct SpBlock pb;

    if (copyin(uap->pb,&pb,sizeof(pb)))
	u.u_rval1 = smUnExBusErr;
    else
	{
	if (rbv_monitor && pb.spSlot == MACIIci_XSLOT) {
	    pb.spSlot = 0;
	    u.u_rval1 = callSlotManager(uap->selector,&pb);
	    pb.spSlot = MACIIci_XSLOT;
	} else
	    u.u_rval1 = callSlotManager(uap->selector,&pb);
	copyout(&pb,uap->pb,sizeof(pb));
	}
}

/*
    This routine calls the slot manager.  The parameter block is assumed to
be copied from the current process's address space.  This means that the
parameter block may contain user-space addresses which must be handled
carefully.  In this case, we temporarily replace the user-space address
with the address of a kernel buffer.  Then, we use either copyin or
copyout to move the data between the adress spaces.
*/

static int callSlotManager(selector,pb)
int selector;
register struct SpBlock *pb;
{
    char b[256];
    char *buffer = b;
    register int rval;
    caddr_t saveAddress;


    switch (selector)
	{
	case _sReadByte:	/* "normal" cases, just make the darned call */
	case _sReadWord:
	case _sReadLong:
	case _sGetsRsrcInfo:
	case _sFindStruct:
	case _sVersion:
	case _sSetsRsrcState:
	case _sInsertSRTRec:
	case _sNextTypesRsrc:
	case _sGetsRsrc:
	case _sNextRsrc:
	case _sRsrcInfo:
	case _sUpdateSRT:
	case _sCkCardStatus:
	case _sFindDevBase:
	case _sPtrToSlot:
	case _sCardChanged:
	case _sOffsetData:
	case _sReadPBSize:
	case _sCalcStep:
	case _sSearchSRT:
	case _sCalcsPointer:
	case _sGetTypesRsrc:
	case _sFindsRsrcPtr:
	    rval = slotmanager(selector,pb);
	    break;
	case _sGetBlock:	/* calls that allocate memory */
	case _sGetcString:
	    saveAddress = (caddr_t) pb->spResult;
	    rval = slotmanager(selector,pb);
	    if (rval == 0)
		{
		if (saveAddress != 0 &&
			copyout(pb->spResult,saveAddress,pb->spSize))
		    rval = smUnExBusErr;
		cDisposPtr(pb->spResult);
		}
	    pb->spResult = (long) saveAddress;
	    break;
	case _sReadPRAMRec:	/* need to copyout the data */
	case _sReadDrvrName:
	case _sReadInfo:
	case _sReadFHeader:
	case _sReadStruct:
	    {
	    int size;
	    int dofree = 0;

	    switch (selector)
		{
		case _sReadPRAMRec:
		    size = 8;
		    break;
		case _sReadDrvrName:
		    size = 256;
		    break;
		case _sReadInfo:
		    size = sizeof(struct SInfoRecord);
		    break;
		case _sReadFHeader:
		    size = sizeof(struct FHeaderRec);
		    break;
		case _sReadStruct:
		    size = pb->spSize;
		    buffer = kmem_alloc (size);
		    dofree = 1;
		    break;
		}
	    saveAddress = (caddr_t) pb->spResult;
	    pb->spResult = (long) buffer;
	    rval = slotmanager(selector,pb);
	    pb->spResult = (long) saveAddress;
	    if (rval == 0 && copyout(buffer,saveAddress,size))
		rval = smUnExBusErr;
	    if (dofree)
		kmem_free (buffer, size);
	    }
	    break;
	case _sPutPRAMRec:	/* need to copyin the data */
	    saveAddress = (caddr_t) pb->spsPointer;
	    if (copyin(saveAddress,buffer,8))
		rval = smUnExBusErr;
	    else
		{
		pb->spsPointer = (char *) buffer;
		rval = slotmanager(selector,pb);
		pb->spsPointer = (char *) saveAddress;
		}
	    break;
	default:		/* calls we've never heard of */
	    printf("unsupported Slot Manager selector: %d\n",selector);
	    rval = smSelOOBErr;
	}
    return rval;
}

/*
    This routine calls the slot manager.  The parameter block, and all
pointers within the parameter block are expected to point into the
kernel's address space.
*/

int slotmanager(selector,pb)
int selector;
struct SpBlock *pb;
{
    int rval;
    ProcPtr saveAline;

    saveAline = Line1010;
    Line1010 = kernelAline;
    rval = SlotManager(selector,pb);
    Line1010 = saveAline;
    return rval;
}

/*
    This routine gets called by the assembler patch to the slot manager.  If
this routine sets the int pointed to by the "callRealSM" variable, the
real slot manager will be called after this routine returns.  Otherwise, the
value returned by this routine will be returned to the original caller of the
Slot Manager.
*/

int SlotManagerPatch(selector,pb,callRealSM)
int selector;
struct SpBlock *pb;
int *callRealSM;
{   
    extern unsigned short RomVersion;

    *callRealSM = 1;		/* assume we will want the real one called */
}
