#ifndef lint	/* .../sys/psn/io/macenv.c */
#define _AC_NAME macenv_c
#define _AC_NO_MAIN "@(#) Copyright (c) 1989 Apple Computer, Inc., All Rights Reserved.  {Apple version 2.18 90/04/06 13:32:33}"
#include <apple_notice.h>

#ifdef _AC_HISTORY
#endif		/* _AC_HISTORY */
#endif		/* lint */

/*
    This file contains code to provide emulation for a subset of the Macintosh
environment in the A/UX kernel.  It is currently used to provide access to
the Macintosh Slot Manager and Macintosh video drivers.

    This file contains C code to provide enough of a Macintosh environment
to allow Macintosh video drivers to work inside the A/UX kernel.  Some low
memory variables and osme A-line traps are supported.  Note that we never
close these drivers so traps used only during cleanup are not needed.

    The following low-memory variables are supported:

    MemErr
    JVBLTask
    OSTable
    ToolTable

    The following A-line traps are supported:

    _ResrvMem
    _NewPtr
    _NewHandle
    _DisposPtr
    _DisposHandle
    _HLock
    _StackSpace
    _SIntInstall
    _SIntRemove

    There are two aline trap handler routines.  The kernel uses the one in the
ROM for the mac OS.  User programs use one defined in bnetivec.s.  We switch the
kernel one in whenever we call a driver.  The rest of the time, the user one
is installed.
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
#include "sys/via6522.h"	/* for SLOT_LO and SLOT_HI */

/*#define VERBOSE		/* print verbose debugging stuff */
#define REAL_PRAM		/* use ROM Read/WriteXPram routines */

#ifdef VERBOSE
#define VPRINTF(stuff)          printf stuff
#else !VERBOSE
#define VPRINTF(stuff)
#endif VERBOSE

				/* flags in the trap word */
#define asyncBit	0x0400	/* execute asynchronously */
#define noQueueBit	0x0200	/* execute immediately */

				/* low memory variables */
#define ToolTable	((ProcPtr *) 0xe00)
#define MemTop		(* (int *) 0x108)
#define OSTable		((ProcPtr *) 0x400)
#define Line1010	(* (ProcPtr *) 0x28)
#define MemErr		(* (short *) 0x220)
#define HWCfgFlags	(* (short *) 0xb22)
#define MMU32Bit	(* (char *) 0xcb2)
#define JVBLTask	(* (ProcPtr *) 0xd28)
#define ROMBase		(* (char **) 0x2ae)
#define MinusOne	(* (long *) 0xa06)
#define sInfoPtr	(* (char **) 0xcbc)
#define sRsrcTblPtr	(* (char **) 0xd24)
#define SysZone		(* (char **) 0x2a6)
#define ApplZone	(* (char **) 0x2aa)
#define JIODone		(* (ProcPtr *) 0x8fc)
#define UTableBase	(* (DCtlHandle **) 0x11c)
#define UnitNtryCnt	(* (short *) 0x1d2)
#define	JSwapMMU	(* (ProcPtr *) 0xdbc)
				/* handy constants */
#define ROM_BASE	((char *) 0x40800000)
#define NTOOL_TRAP	1024	/* length of ToolTable */
#define NOS_TRAP	256	/* length of OSTable */
#define FIRST_SLOT_UNIT	0x48	/* first unit table entry for slot devices */
#define NSLOTS		(SLOT_HI - SLOT_LO + 1)
#define NUNITS		(FIRST_SLOT_UNIT + NSLOTS)
					/* these constants define positions */
					/* in the ROM "foreign" OS address */
					/* table */

#define FOREIGN_OS_0	0	/* First entry into Foreign OS table */
#define FOREIGN_OS_1	1
#define FOREIGN_OS_2	2
#define FOREIGN_OS_3	3
#define FOREIGN_OS_4	4

#define INIT_DISPATCH	0	/* address of trap table init routine */
#define A_LINE		1	/* address of linea trap handler */
#define BAD_TRAP	2	/* no such trap routine */
#define KERNEL_A_LINE	3	/* kernel linea trap handler */
#define INIT_SM		4	/* routine to init slot manager */
#define ROM_COUNT	5	/* number of addresses in table */

				/* handy error codes */
#define noErr		0	/* no problem */
#define memFullErr   -108	/* Not enough room in heap zone */
#define unimpErr	-4	/* unimplemented error */

				/* csCode values for video driver calls */
#define Init		0
#define SetMode		2
#define SetEntries	3
#define GrayScreen	5
#define SetGray		6
				/* csCode values for video status calls */
#define GetMode 	2
#define GetEntries 	3
#define GetPages 	4
#define GetBaseAddr 	5
#define GetGray		6
#define GetVideoDefault 9

struct driver
{
    short	drvrFlags;	/* flag word */
    short	drvrDelay;	/* ticks between periodic actions */
    short	drvrEMask;	/* DA event mask */
    short	drvrMenu;	/* menu ID of associated menu */
    short	drvrOpen;	/* offset to Open routine */
    short	drvrPrime;	/* Offset to Prime routine */
    short	drvrControl;	/* Offset to Control routine */
    short	drvrStatus;	/* Offset to Status routine */
    short	drvrClose;	/* Offset to Close routine */
    char	drvrName[1];	/* driver name */
};

struct SQElem
{
    struct SQElem *SQLink;
    short	SQType;
    short	SQPrio;
    long	SQAddr;
    long	SQParam;
};

extern long noSupport();	/* wrapper for unimplemented trap routine */
extern short callDriver();
extern int slotpatch();
static ProcPtr userAline;	/* aline handler for user programs */
static struct SQElem *intHandler[16];
static char *ROMArray[ROM_COUNT];
static DCtlHandle unitTable[NUNITS];

ProcPtr kernelAline;		/* aline handler for kernel */
ProcPtr secondaryInit;		/* address of secondary init in ROM */

unsigned short RomVersion;	/* 0x0178, 0x067c, ... */

/*
    This routine initializes the mac driver support.
*/

initMacEnvironment()
{
    extern long vblTask();
    extern long lineAVector();
    extern long ioDone();
    extern ProcPtr OSPatch[];	/* patch table defined below */
    register int i;
    register ProcPtr *dst,p;
    register ProcPtr *src;
    struct SpBlock pb;
    extern int rbv_exists;

    RomVersion = * (unsigned short *) (ROM_BASE + 8);

    initROMArray();

    ROMBase = ROM_BASE;
    MinusOne = -1;
    SysZone = (char *) -1;
    ApplZone = (char *) -1;
    userAline = lineAVector;
    HWCfgFlags |= 0x200;		/* set UNIX bit */
    MMU32Bit =1;			/* set 32 mode */

    JVBLTask = vblTask;
    JIODone = ioDone;
    sInfoPtr = (char *) -1;
    sRsrcTblPtr = (char *) -1;
    UTableBase = unitTable;
    UnitNtryCnt = NUNITS;

    if (rbv_exists)
	MemTop = 0x200000;
    else
	* (char *)0xdd2 &= ~(1<<5); /* make sure no RBV present */

    /* call ROM routine to init the disp tables */
    (*(ProcPtr) ROMArray[INIT_DISPATCH])();
    kernelAline = Line1010;

    dst = ToolTable;
    for (i = NTOOL_TRAP; --i >= 0; )
	*dst++ = noSupport;
    dst = OSTable;
    src = &OSPatch[0];
    for (i = NOS_TRAP; --i >= 0; ) {
	p = *src++;
	if (p == 0)
	    dst++;
	else
	    *dst++ = p;
    }
    doPatches();
    if (secondaryInit) {
	Line1010 = kernelAline;
	VPRINTF(("calling StartSDeclMgr\n"));
	i = callStartSDeclMgr(ROMArray[INIT_SM],0);
	VPRINTF(("StartSDeclMgr returned %d\n",i));
	bzero(&pb, sizeof(struct SpBlock));
	callSecondaryInit(secondaryInit,&pb);
    } else {
	slotpatch();
    }
    Line1010 = userAline;
}

/*
    This routine initializes the ROMArray array.  This array contains a bunch
of pointers to interesting things in the ROM.  The array is copied from ROM
and then ROM_BASE is added to each element to relocate it to the ROM's
virtual address.  Then, if we are using old ROMs, we hard code things that
have been added to the array.
*/

initROMArray()
{
    register int i;
    register long *ptr;
    extern long romaline;

    ptr = ((long *) (ROM_BASE + (* (long *) (ROM_BASE + 0x16))));

    ROMArray[INIT_DISPATCH]	= ROM_BASE + ptr[FOREIGN_OS_0];
    ROMArray[A_LINE]		= ROM_BASE + ptr[FOREIGN_OS_1];
    ROMArray[BAD_TRAP]		= ROM_BASE + ptr[FOREIGN_OS_2];

    switch (RomVersion) {
	case 0x178:
	    ROMArray[KERNEL_A_LINE] = ROM_BASE + 0x64ba;
	    ROMArray[INIT_SM] = ROM_BASE + 0x4152;
	    break;
	
	case 0x067c:		/* Aurora */
	    ROMArray[KERNEL_A_LINE] = (char *) romaline;	/* startup.c */
	    ROMArray[INIT_SM] = ROM_BASE + ptr[FOREIGN_OS_3];
	    break;

	default:
	    ROMArray[KERNEL_A_LINE]	= ROM_BASE + ptr[FOREIGN_OS_3];
	    ROMArray[INIT_SM]		= ROM_BASE + ptr[FOREIGN_OS_4];
	    break;
    }
}

/*
    The doPatches routine installs any patches to the trap dispatch tables.
*/

ProcPtr realSlotManager;	/* used by slot manager patch */

doPatches()
{
    extern long aSlotManagerPatch(),aGetResource(),aTickCount();
    extern long aGetDeviceList();
    extern long noSupport1();

    realSlotManager = OSTable[0x6e];
    OSTable[0x6e] = aSlotManagerPatch;
    ToolTable[416] = aGetResource;
    ToolTable[373] = aTickCount;
    JSwapMMU = OSTable[0x5d];
    /*
     * These two patches fix many SuperMac cards.  A couple of these cards
     * need the GetDeviceList trap, and we don't support Device Lists yet.
     * The A-Line trap 0xab03 is a quickdraw trap which if available lets
     * users know that 32-Bit QuickDraw is available on this machine. 
     * SuperMac cards need to know if this is only to determine if large
     * format video devices are supported.  However, we don't let them use the
     * trap until the Mac environment is up and running. [Mr.C]
     */
    ToolTable[553] = aGetDeviceList;
    ToolTable[771] = noSupport1;		/* Aline 0xAB03 */

    switch (RomVersion) {
	case 0x67c:
	    secondaryInit = (ProcPtr)(ROM_BASE + 0x62c0);
	    break;
	default:
	    secondaryInit = 0;
	    break;
    }
}

/*
    This routine calls a driver's open routine.  It also initializes various
fields in the dce.
*/

int callOpen(dceHandle)
AuxDCE **dceHandle;
{
    int rval;
    struct IOParam pb;
    struct driver *driver;
    register int i;
    struct AuxDCE *dce = *dceHandle;

    for (i = FIRST_SLOT_UNIT; unitTable[i] != 0; i++)
	if (i == NUNITS)
	    panic("Can't find unit table entry for video card");
    unitTable[i] = (DCtlHandle) dceHandle;
    driver = * (struct driver **) dce->dCtlDriver;
    dce->dCtlFlags = driver->drvrFlags | 0x20;	/* set open bit */
    dce->dCtlRefNum = ~i;
    pb.ioTrap = noQueueBit;
    pb.ioCmdAddr = (char *) -1;			/* hack for RasterOps */
    Line1010 = kernelAline;
    VPRINTF(("Calling open routine for slot %d\n",dce->dCtlSlot));
    rval = callDriver((char *) driver + driver->drvrOpen,&pb,dce);
    VPRINTF(("open routine returned %d\n",rval));
    Line1010 = userAline;
    return rval;
}

/*
    This routine calls a driver's control routine.  csParam is expected to
point into the current process's user space.  This means that we have to copy
various things between user and kernel address spaces.
*/

int callControl(vp,csCode,csParam)
struct video *vp;
int csCode;
char *csParam;
{
    extern char *cNewPtr();
    int rval;
    char *csTable;
    int inCount = 0, outCount = 0;

    /* This buffer must be at least as large as the */
    /* largest thing pointed to by csParam */
    char buffer[sizeof(struct VDPgInfo)];

#define vdPgInfo	(* (struct VDPgInfo *) buffer)
#define vdEntryRecord	(* (struct VDEntryRecord *) buffer)

    switch (csCode) {
	case SetMode:
	    inCount = outCount = sizeof(struct VDPgInfo);
	    break;
	case GrayScreen:
	case SetGray:
	    inCount = sizeof(struct VDPgInfo);
	    break;
	case SetEntries:
	    inCount = sizeof(struct VDEntryRecord);
	    break;
	default:
	    printf("unsupported control csCode: %d\n",csCode);
	    return -17;		/* controlErr */
    }
    if (inCount > 0 && copyin(csParam,buffer,inCount)) {
	printf("callControl: copyin failed\n");
	return -17;
    }
    if (csCode == SetEntries) {	/* Need to copy csTable for SetEntries */
	inCount = (vdEntryRecord.csCount + 1) * 8;
	csTable = cNewPtr(inCount);
	if (csTable == 0) {
	    printf("callControl: can't allocate %d bytes for csTable\n",
		    inCount);
	    return -17;
	}
	if (copyin(vdEntryRecord.csTable,csTable,inCount)) {
	    cDisposPtr(csTable);
	    printf("callControl: copyin of csTable failed\n");
	    return -17;
	}
	vdEntryRecord.csTable = csTable;
    }
    rval = kallControl(vp,csCode,buffer);
    switch (csCode) {
	case SetEntries:
	    cDisposPtr(csTable);
	    break;
    }
    if (outCount > 0 && copyout(buffer,csParam,outCount)) {
	printf("callControl: copyout failed\n");
	return -17;
    }
    return rval;
}

/*
    This routine calls a driver's control routine on behalf of the kernel.
*/

int kallControl(vp,csCode,csParam)
struct video *vp;
int csCode;
char *csParam;
{
    static struct CntrlParam pb;
    struct AuxDCE *dce;
    struct driver *driver;
    struct VDPgInfo *vdp;
    int rval;

    pb.ioTrap = noQueueBit;
    pb.ioCmdAddr = (char *) -1;		/* hack for RasterOps */
    pb.csCode = csCode;
    * (char **) pb.csParam = csParam;
    dce = &vp->dce;
    driver =  * (struct driver **) dce->dCtlDriver;

    if (csCode == SetMode) {
        vdp = (struct VDPgInfo *)csParam;
        vdp->csBaseAddr = 0;
    }
    Line1010 = kernelAline;
    rval = callDriver((char *) driver + driver->drvrControl,&pb,dce);
    if (rval != 0)
	VPRINTF(("Control routine %d returned %d\n",csCode,rval));
    else if (csCode == SetMode)
	fixVideoAddress(vp,((struct VDPgInfo *) csParam)->csMode, csParam);
    Line1010 = userAline;
    return rval;
}

/*
    Given a video structure and a new mode, this routine updates the
video_addr field of the structure to point to the beginning of the
current video page.  The driver should hand this information back
to us, but CQD ignores that, so we can't really trust it either.
    This code is pretty much lifted from the GetDevPixMap routine
in CQD.
*/

static fixVideoAddress(vp,mode, vdp)
struct video *vp;
int mode;
struct VDPgInfo *vdp;
{
    register struct video_data *viddatp;

    if ((vdp->csBaseAddr != 0) && !((vp->video_base == (char *)0xFBB08000) &&
				    (vp == video_index[0])))
    {
	viddatp = &vp->video_data;
	vp->video_addr = ((unsigned long)vdp->csBaseAddr & 0xff000000) 
	    + viddatp->v_baseoffset;
	vp->video_base = (unsigned long) vdp->csBaseAddr & 0xff000000;
	vp->dce.dCtlDevBase = (long) vp->video_base;
    } else {
#define mVidParams	1
#define MinorBaseOS	10
	struct SpBlock pb;
	long address;

	if (vp->dce.dCtlSlot == 0)
	    return;

	pb.spSlot = vp->dce.dCtlSlot;
	pb.spID = vp->dce.dCtlSlotId;
	pb.spExtDev = vp->dce.dCtlExtDev;
	if (slotmanager(_sRsrcInfo,&pb) == 0)		/* get resource list */
	{
	    pb.spID = MinorBaseOS;
	    if (slotmanager(_sReadLong,&pb) == 0)		/* get base of RAM */
	    {
		address = (long) pb.spsPointer & 0xff000000;
		address += pb.spResult;
		pb.spID = mode;
		if (slotmanager(_sFindStruct,&pb) == 0)	/* find list for mode */
		{
		    pb.spID = mVidParams;
		    /* get video parameter block */
		    if (slotmanager(_sGetBlock,&pb) == 0)
		    {
			address += ((struct video_data *)pb.spResult)->v_baseoffset;
			cDisposPtr((char *) pb.spResult);
			vp->video_addr = (caddr_t) address;
		    }
		}
	    }
	}
    }
}

/*
    This routine makes calls to a Macintosh video driver to set the frame buffer
to 1 bit/pixel mode, if the driver supports it.  Otherwise, it does nothing.
*/

int setDefaultMode(vp)
struct video *vp;
{
    struct VDPgInfo vdp;
    int rval;
    static long csTable[] =
    {
	0x0000ffff, 0xffffffff,
	0x00000000, 0x00000000
    };
    static struct VDEntryRecord vde =
    {
	(char *) csTable,
	0,		/* start = 0 */
	1		/* count = 1 (2 entries) */
    };

    if (vp->video_data.v_pixelsize != 1)
	rval = 0;
    else
	{
	vdp.csMode = vp->video_def_mode;
	vdp.csData = 0;
	vdp.csPage = 0;
	rval = kallControl(vp,SetMode,(char *) &vdp);
	vdp.csMode = 1;
	kallControl(vp,SetGray,(char *) &vdp);
	kallControl(vp,SetEntries,(char *) &vde);
	}
    return rval;
}

/*
    This routine calls a driver's status routine.  csParam is expected to
point into the current process's user space.  This means that we have to copy
various things between user and kernel address spaces.
*/

int callStatus(vp,csCode,csParam)
struct video *vp;
int csCode;
char *csParam;
{
    extern char *cNewPtr();
    int rval;
    char *csTable, *user_csTable;
    int inCount = 0, outCount = 0;

    /* This buffer must be at least as large as the */
    /* largest thing pointed to by csParam */
    char buffer[sizeof(struct VDPgInfo)];

    switch (csCode) {
    case GetMode:
	outCount = sizeof(struct VDPgInfo);
	break;
    case GetEntries:
	inCount = outCount = sizeof(struct VDEntryRecord);
	break;
    case GetPages:
    case GetBaseAddr:
	inCount = outCount = sizeof(struct VDPgInfo);
	break;
    case GetGray:
	outCount = sizeof(struct VDPgInfo);
	break;
    case GetVideoDefault:
	outCount = sizeof(struct video_default);
	break;
    default:
	printf("unsupported status csCode: %d\n",csCode);
	return -18;		/* statusErr */
    }

    if (inCount > 0 && copyin(csParam,buffer,inCount)) {
	printf("callControl: copyin failed\n");
	return -18;
    }
    if (csCode == GetEntries) {	/* Need to copy csTable for GetEntries */
	outCount = (vdEntryRecord.csCount + 1) * 8;
	csTable = cNewPtr(outCount);
	if (csTable == 0) {
	    printf("callStatus: can't allocate %d bytes for csTable\n",
		    inCount);
	    return -18;
	}
	user_csTable = vdEntryRecord.csTable;
	vdEntryRecord.csTable = csTable;
    }
    rval = kallStatus(vp,csCode,buffer);
    if (csCode == GetEntries) {
	if (copyout(csTable,user_csTable,outCount)) {
	    cDisposPtr(csTable);
	    printf("callStatus: copyout of csTable failed\n");
	    return -18;
	}
	vdEntryRecord.csTable = user_csTable;
	cDisposPtr(csTable);
    }
    if (outCount > 0 && copyout(buffer,csParam,outCount)) {
	printf("callControl: copyout failed\n");
	return -18;
    }
    return rval;
}

/*
    This routine calls a driver's control routine on behalf of the kernel.
*/

int kallStatus(vp,csCode,csParam)
struct video *vp;
int csCode;
char *csParam;
{
    static struct CntrlParam pb;
    struct AuxDCE *dce;
    struct driver *driver;
    int rval;

    pb.ioTrap = noQueueBit;
    pb.ioCmdAddr = (char *) -1;		/* hack for RasterOps */
    pb.csCode = csCode;
    * (char **) pb.csParam = csParam;
    dce = &vp->dce;
    driver =  * (struct driver **) dce->dCtlDriver;
    Line1010 = kernelAline;
    rval = callDriver((char *) driver + driver->drvrStatus,&pb,dce);
    if (rval != 0)
	VPRINTF(("Control routine %d returned %d\n",csCode,rval));
    Line1010 = userAline;
    return rval;
}

/*
    This routine gets called if we detect that the mac device driver overran
its stack.  There is nothing we can do but panic.
*/

trashedStack(dce)
struct AuxDCE *dce;
{
    pre_panic();
    printf("The video driver in slot %d suffered a stack overflow\n",
	    dce->dCtlSlot);
    panic("video stack overflow");
}

/*
    This routine gets called when there is a slot interrupt on the given slot.
It calls the appropriate slot interrupt routine.  We should allow for a queue
of interrupt routines like the mac does, but we don't, so there.
*/

callSlotInt(slot)
int slot;
{
    register struct SQElem *p;
    ProcPtr saveLine1010;

    p = intHandler[slot];
    if (p == 0)
	panic("slot interrupt with no handler");
    saveLine1010 = Line1010;
    Line1010 = kernelAline;
    if (callSlotTask(p->SQAddr,p->SQParam) == 0)
	panic("Polling routine didn't handle interrupt");
    Line1010 = saveLine1010;
}

/*
    This routine allocates, and clears some memory.  The number of bytes
requested is long-word aligned.
*/

static char *getmesomememory(logicalSize)
register int logicalSize;
{
    extern caddr_t video_alloc();
    register char *rval,*ptr;

    ptr = rval = video_alloc(logicalSize + (4 - logicalSize & 3));
    if (ptr != 0)
	{
	while (--logicalSize >= 0)
	    *ptr++ = 0;
	}
    return rval;
}

static long ioDone()
{
    pre_panic();
    printf("A video driver attempted to use the JIODone vector\n");
    panic("IODone");
}

/*
    This routine gets called when an attempt is made to use an A-line trap that
is not supported in the kernel.
*/

noKernelSupport(trap,d1)
unsigned short trap,d1;
{
    pre_panic();
    printf("Attempt to use unsupported A-line trap: ");
    if ((trap & 0xf800) == 0xa800)
	{
	printf("%x",trap);
	if ((d1 & 0xf800) == 0xa000)
	    printf("or %x",d1);
	}
    else
	if ((d1 & 0xf800) == 0xa000)
	    printf("%x",d1);
	else
	    printf("Can't find trap word!");
    printf("\n");
    panic("Unsupported kernel A-line trap");
}

int cSetTrapAddress(address, trap)
    ProcPtr address;
    int trap;
{
    if (trap & 0x800)		/* tool trap */
	return(unimpErr);
    else 
	if ((trap & 0xff) == 0x6e) { /* slot manager trap! */
	    realSlotManager = address;
	    return(noErr);
	}
    return(unimpErr);
}

/*
    Reserve memory in the heap.  Actually does nothing.  Return a result code.
*/

int cResrvMem(cbNeeded)
long cbNeeded;
{
    VPRINTF(("ResrvMem(%d)\n",(int) cbNeeded));
    MemErr = noErr;
    return noErr;
}

/*
    Allocate a non-relocatable block.  Return a pointer to it.
*/

char *cNewPtr(logicalSize)
long logicalSize;
{
    char *rval;

    VPRINTF(("NewPtr(%d)",(int) logicalSize));
    rval = getmesomememory(logicalSize);
    if (rval == 0)
	MemErr = memFullErr;
    else
	MemErr = noErr;
    VPRINTF((" returns 0x%x\n",rval));
    return rval;
}

/*
    Allocate a relocatable block.  Return a handle to it.
*/

char **cNewHandle(logicalSize)
long logicalSize;
{
    register char **rval;

    VPRINTF(("NewHandle(%d)",(int) logicalSize));
    rval = (char **) getmesomememory(logicalSize + 4);
    if (rval == 0)
	MemErr = memFullErr;
    else
	{
	MemErr = noErr;
	*rval = (char *) rval + 4;
	}
    VPRINTF((" returns 0x%x\n",rval));
    return rval;
}

/*
    Dispose of a non-relocatable block.  Return a result code.
*/

int cDisposPtr(p)
char *p;
{
    VPRINTF(("DisposPtr(0x%x)\n",(int) p));
    if (p != 0)
	video_free((caddr_t) p);
    MemErr = noErr;
    return noErr;
}

/*
    Dispose of a relocatable block.  Return a result code.
*/

int cDisposHandle(h)
char **h;
{
    VPRINTF(("DisposHandle(0x%x)\n",(int) h));
    if (h != 0)
	video_free((caddr_t) h);
    MemErr = noErr;
    return noErr;
}

/*
    Lock a relocatable block.  Return a result code.
*/

int cHLock(h)
char **h;
{
    VPRINTF(("HLock(0x%x)\n",(int) h));
    MemErr = noErr;
    return noErr;
}

/*
    Return the amount of free space on the stack.
*/

long cStackSpace()
{
    pre_panic();
    printf("StackSpace()\n");
    panic("unsupported call");
}

/*
    Install a slot interrupt servicer.  Return a result code.
*/

int cSIntInstall(sIntQElemPtr,theSlot)
struct SQElem *sIntQElemPtr;
unsigned char theSlot;
{
    VPRINTF(("SIntInstall(0x%x,%d)\n",sIntQElemPtr,theSlot));
    if (intHandler[theSlot] != 0)
	panic("Multiple interrupt handlers for one slot");
    intHandler[theSlot] = sIntQElemPtr;
    return noErr;
}

/*
    Remove a slot interrupt servicer.  Return a result code.
*/

int cSIntRemove(sIntQElemPtr,theSlot)
struct SQElem *sIntQElemPtr;
unsigned char theSlot;
{
    VPRINTF(("SIntRemove(0x%x,%d)\n",sIntQElemPtr,theSlot));
    intHandler[theSlot] = 0;
    return noErr;
}

/*
    Return a handle to a resource from some resource file or the ROM resource
map.  Actually, always return nil.  This called is used by some E-Machines
cards to get a gamme table.
*/

char **cGetResource(type,id)
long type;
short id;
{
    VPRINTF(("GetResource('%c%c%c%c',%d)\n",
	(type >> 24) & 0xff,
	(type >> 16) & 0xff,
	(type >>  8) & 0xff,
	(type      ) & 0xff,
	id));
    return 0;
}

/*
    OSPatch is an array which contains a function pointer for each OS trap.
If the entry is useROM, we use the ROMs routine for the trap.  Otherwise,
we use the given function pointer.
*/

#define useROM	((ProcPtr) 0)
extern long aResrvMem(), aNewPtr(), aNewHandle(), aDisposPtr();
extern long aDisposHandle(), aHLock(), aStackSpace();
extern long aSIntInstall(), aSIntRemove();
extern long aStripAddress();
extern long aSwapMMUMode();
extern long aInsTime(), aPrimeTime(), aRmvTime();
extern long aSetVideoDefault();
extern long aSetTrapAddress();

static ProcPtr OSPatch[NOS_TRAP] =
{
    /* trap    0 = a000 */ noSupport,
    /* trap    1 = a001 */ noSupport,
    /* trap    2 = a002 */ noSupport,
    /* trap    3 = a003 */ noSupport,
    /* trap    4 = a004 */ noSupport,
    /* trap    5 = a005 */ noSupport,
    /* trap    6 = a006 */ noSupport,
    /* trap    7 = a007 */ noSupport,
    /* trap    8 = a008 */ noSupport,
    /* trap    9 = a009 */ noSupport,
    /* trap   10 = a00a */ noSupport,
    /* trap   11 = a00b */ noSupport,
    /* trap   12 = a00c */ noSupport,
    /* trap   13 = a00d */ noSupport,
    /* trap   14 = a00e */ noSupport,
    /* trap   15 = a00f */ noSupport,
    /* trap   16 = a010 */ noSupport,
    /* trap   17 = a011 */ noSupport,
    /* trap   18 = a012 */ noSupport,
    /* trap   19 = a013 */ noSupport,
    /* trap   20 = a014 */ noSupport,
    /* trap   21 = a015 */ noSupport,
    /* trap   22 = a016 */ noSupport,
    /* trap   23 = a017 */ noSupport,
    /* trap   24 = a018 */ noSupport,
    /* trap   25 = a019 */ noSupport,
    /* trap   26 = a01a */ useROM,	/* GetZone */
    /* trap   27 = a01b */ useROM,	/* SetZone */
    /* trap   28 = a01c */ noSupport,
    /* trap   29 = a01d */ noSupport,
    /* trap   30 = a01e */ aNewPtr,
    /* trap   31 = a01f */ aDisposPtr,
    /* trap   32 = a020 */ noSupport,
    /* trap   33 = a021 */ noSupport,
    /* trap   34 = a022 */ aNewHandle,
    /* trap   35 = a023 */ aDisposHandle,
    /* trap   36 = a024 */ noSupport,
    /* trap   37 = a025 */ noSupport,
    /* trap   38 = a026 */ noSupport,
    /* trap   39 = a027 */ noSupport,
    /* trap   40 = a028 */ noSupport,
    /* trap   41 = a029 */ aHLock,
    /* trap   42 = a02a */ noSupport,
    /* trap   43 = a02b */ noSupport,
    /* trap   44 = a02c */ noSupport,
    /* trap   45 = a02d */ noSupport,
    /* trap   46 = a02e */ useROM,	/* BlockMove */
    /* trap   47 = a02f */ noSupport,
    /* trap   48 = a030 */ noSupport,
    /* trap   49 = a031 */ noSupport,
    /* trap   50 = a032 */ noSupport,
    /* trap   51 = a033 */ noSupport,
    /* trap   52 = a034 */ noSupport,
    /* trap   53 = a035 */ noSupport,
    /* trap   54 = a036 */ noSupport,
    /* trap   55 = a037 */ noSupport,
    /* trap   56 = a038 */ noSupport,
    /* trap   57 = a039 */ useROM,	/* ReadDateTime */
    /* trap   58 = a03a */ useROM,	/* SetDateTime */
    /* trap   59 = a03b */ noSupport,
    /* trap   60 = a03c */ noSupport,
    /* trap   61 = a03d */ noSupport,
    /* trap   62 = a03e */ noSupport,
    /* trap   63 = a03f */ noSupport,
    /* trap   64 = a040 */ aResrvMem,
    /* trap   65 = a041 */ noSupport,
    /* trap   66 = a042 */ noSupport,
    /* trap   67 = a043 */ noSupport,
    /* trap   68 = a044 */ noSupport,
    /* trap   69 = a045 */ noSupport,
    /* trap   70 = a046 */ useROM,
    /* trap   71 = a047 */ aSetTrapAddress,
    /* trap   72 = a048 */ noSupport,
    /* trap   73 = a049 */ noSupport,
    /* trap   74 = a04a */ noSupport,
    /* trap   75 = a04b */ noSupport,
    /* trap   76 = a04c */ noSupport,
    /* trap   77 = a04d */ noSupport,
    /* trap   78 = a04e */ noSupport,
    /* trap   79 = a04f */ noSupport,
    /* trap   80 = a050 */ noSupport,
    /* trap   81 = a051 */ useROM,	/* ReadXPRam */
    /* trap   82 = a052 */ useROM,	/* WriteXPRam */
    /* trap   83 = a053 */ useROM,	/* jClkNoMem */
    /* trap   84 = a054 */ noSupport,
    /* trap   85 = a055 */ aStripAddress,
    /* trap   86 = a056 */ noSupport,
    /* trap   87 = a057 */ noSupport,
    /* trap   88 = a058 */ aInsTime,
    /* trap   89 = a059 */ aRmvTime,
    /* trap   90 = a05a */ aPrimeTime,
    /* trap   91 = a05b */ noSupport,
    /* trap   92 = a05c */ noSupport,
    /* trap   93 = a05d */ aSwapMMUMode,
    /* trap   94 = a05e */ noSupport,
    /* trap   95 = a05f */ noSupport,
    /* trap   96 = a260 */ noSupport,
    /* trap   97 = a061 */ noSupport,
    /* trap   98 = a062 */ noSupport,
    /* trap   99 = a063 */ noSupport,
    /* trap  100 = a064 */ noSupport,
    /* trap  101 = a065 */ aStackSpace,
    /* trap  102 = a066 */ noSupport,
    /* trap  103 = a067 */ noSupport,
    /* trap  104 = a068 */ noSupport,
    /* trap  105 = a069 */ noSupport,
    /* trap  106 = a06a */ noSupport,
    /* trap  107 = a06b */ noSupport,
    /* trap  108 = a06c */ noSupport,
    /* trap  109 = a06d */ noSupport,
    /* trap  110 = a06e */ useROM,	/* Slot Manager */
    /* trap  111 = a06f */ noSupport,
    /* trap  112 = a070 */ noSupport,
    /* trap  113 = a071 */ noSupport,
    /* trap  114 = a072 */ noSupport,
    /* trap  115 = a073 */ noSupport,
    /* trap  116 = a074 */ noSupport,
    /* trap  117 = a075 */ aSIntInstall,
    /* trap  118 = a076 */ aSIntRemove,
    /* trap  119 = a077 */ noSupport,
    /* trap  120 = a078 */ noSupport,
    /* trap  121 = a079 */ noSupport,
    /* trap  122 = a07a */ noSupport,
    /* trap  123 = a07b */ noSupport,
    /* trap  124 = a07c */ noSupport,
    /* trap  125 = a07d */ noSupport,
    /* trap  126 = a07e */ noSupport,
    /* trap  127 = a07f */ noSupport,
    /* trap  128 = a080 */ useROM,	/* GetDefaultVideo */
    /* trap  129 = a081 */ aSetVideoDefault,
    /* trap  130 = a082 */ noSupport,
    /* trap  131 = a083 */ noSupport,
    /* trap  132 = a084 */ noSupport,
    /* trap  133 = a085 */ noSupport,
    /* trap  134 = a086 */ noSupport,
    /* trap  135 = a087 */ noSupport,
    /* trap  136 = a088 */ noSupport,
    /* trap  137 = a089 */ noSupport,
    /* trap  138 = a08a */ noSupport,
    /* trap  139 = a08b */ noSupport,
    /* trap  140 = a08c */ noSupport,
    /* trap  141 = a08d */ noSupport,
    /* trap  142 = a08e */ noSupport,
    /* trap  143 = a08f */ noSupport,
    /* trap  144 = a090 */ noSupport,
    /* trap  145 = a091 */ noSupport,
    /* trap  146 = a092 */ noSupport,
    /* trap  147 = a093 */ noSupport,
    /* trap  148 = a094 */ noSupport,
    /* trap  149 = a095 */ noSupport,
    /* trap  150 = a096 */ noSupport,
    /* trap  151 = a097 */ noSupport,
    /* trap  152 = a098 */ noSupport,
    /* trap  153 = a099 */ noSupport,
    /* trap  154 = a09a */ noSupport,
    /* trap  155 = a09b */ noSupport,
    /* trap  156 = a09c */ noSupport,
    /* trap  157 = a09d */ noSupport,
    /* trap  158 = a09e */ noSupport,
    /* trap  159 = a09f */ noSupport,
    /* trap  160 = a0a0 */ noSupport,
    /* trap  161 = a0a1 */ noSupport,
    /* trap  162 = a0a2 */ noSupport,
    /* trap  163 = a0a3 */ noSupport,
    /* trap  164 = a0a4 */ noSupport,
    /* trap  165 = a0a5 */ noSupport,
    /* trap  166 = a0a6 */ noSupport,
    /* trap  167 = a0a7 */ noSupport,
    /* trap  168 = a0a8 */ noSupport,
    /* trap  169 = a0a9 */ noSupport,
    /* trap  170 = a0aa */ noSupport,
    /* trap  171 = a0ab */ noSupport,
    /* trap  172 = a0ac */ noSupport,
    /* trap  173 = a0ad */ noSupport,
    /* trap  174 = a0ae */ noSupport,
    /* trap  175 = a0af */ noSupport,
    /* trap  176 = a0b0 */ noSupport,
    /* trap  177 = a0b1 */ noSupport,
    /* trap  178 = a0b2 */ noSupport,
    /* trap  179 = a0b3 */ noSupport,
    /* trap  180 = a0b4 */ noSupport,
    /* trap  181 = a0b5 */ noSupport,
    /* trap  182 = a0b6 */ noSupport,
    /* trap  183 = a0b7 */ noSupport,
    /* trap  184 = a0b8 */ noSupport,
    /* trap  185 = a0b9 */ noSupport,
    /* trap  186 = a0ba */ noSupport,
    /* trap  187 = a0bb */ noSupport,
    /* trap  188 = a0bc */ noSupport,
    /* trap  189 = a0bd */ useROM,	/* CacheFlush */
    /* trap  190 = a0be */ noSupport,
    /* trap  191 = a0bf */ noSupport,
    /* trap  192 = a0c0 */ noSupport,
    /* trap  193 = a0c1 */ noSupport,
    /* trap  194 = a0c2 */ noSupport,
    /* trap  195 = a0c3 */ noSupport,
    /* trap  196 = a0c4 */ noSupport,
    /* trap  197 = a0c5 */ noSupport,
    /* trap  198 = a0c6 */ noSupport,
    /* trap  199 = a0c7 */ noSupport,
    /* trap  200 = a0c8 */ noSupport,
    /* trap  201 = a0c9 */ noSupport,
    /* trap  202 = a0ca */ noSupport,
    /* trap  203 = a0cb */ noSupport,
    /* trap  204 = a0cc */ noSupport,
    /* trap  205 = a0cd */ noSupport,
    /* trap  206 = a0ce */ noSupport,
    /* trap  207 = a0cf */ noSupport,
    /* trap  208 = a0d0 */ noSupport,
    /* trap  209 = a0d1 */ noSupport,
    /* trap  210 = a0d2 */ noSupport,
    /* trap  211 = a0d3 */ noSupport,
    /* trap  212 = a0d4 */ noSupport,
    /* trap  213 = a0d5 */ noSupport,
    /* trap  214 = a0d6 */ noSupport,
    /* trap  215 = a0d7 */ noSupport,
    /* trap  216 = a0d8 */ noSupport,
    /* trap  217 = a0d9 */ noSupport,
    /* trap  218 = a0da */ noSupport,
    /* trap  219 = a0db */ noSupport,
    /* trap  220 = a0dc */ noSupport,
    /* trap  221 = a0dd */ noSupport,
    /* trap  222 = a0de */ noSupport,
    /* trap  223 = a0df */ noSupport,
    /* trap  224 = a0e0 */ noSupport,
    /* trap  225 = a0e1 */ noSupport,
    /* trap  226 = a0e2 */ noSupport,
    /* trap  227 = a0e3 */ noSupport,
    /* trap  228 = a0e4 */ noSupport,
    /* trap  229 = a0e5 */ noSupport,
    /* trap  230 = a0e6 */ noSupport,
    /* trap  231 = a0e7 */ noSupport,
    /* trap  232 = a0e8 */ noSupport,
    /* trap  233 = a0e9 */ noSupport,
    /* trap  234 = a0ea */ noSupport,
    /* trap  235 = a0eb */ noSupport,
    /* trap  236 = a0ec */ noSupport,
    /* trap  237 = a0ed */ noSupport,
    /* trap  238 = a0ee */ noSupport,
    /* trap  239 = a0ef */ noSupport,
    /* trap  240 = a0f0 */ noSupport,
    /* trap  241 = a0f1 */ noSupport,
    /* trap  242 = a0f2 */ noSupport,
    /* trap  243 = a0f3 */ noSupport,
    /* trap  244 = a0f4 */ noSupport,
    /* trap  245 = a0f5 */ noSupport,
    /* trap  246 = a0f6 */ noSupport,
    /* trap  247 = a0f7 */ noSupport,
    /* trap  248 = a0f8 */ noSupport,
    /* trap  249 = a0f9 */ noSupport,
    /* trap  250 = a0fa */ noSupport,
    /* trap  251 = a0fb */ noSupport,
    /* trap  252 = a0fc */ noSupport,
    /* trap  253 = a0fd */ noSupport,
    /* trap  254 = a0fe */ noSupport,
    /* trap  255 = a0ff */ noSupport
};
