#ifndef lint	/* .../sys/psn/io/sdisk.c */
#define _AC_NAME sdisk_c
#define _AC_NO_MAIN "@(#) Copyright (c) 1987 Apple Computer Inc., All Rights Reserved.  {Apple version 1.9 88/10/06 17:39:32}"
#include <apple_notice.h>

#ifdef _AC_HISTORY
  static char *sccsid = "@(#)Copyright Apple Computer 1987\tVersion 1.9 of sdisk.c on 88/10/06 17:39:32";
#endif		/* _AC_HISTORY */
#endif		/* lint */

#define _AC_MODS
#ifdef	HOWFAR
extern int	T_sdisk;
extern int	T_gdisk;
#endif	HOWFAR
/*	@(#)sdisk.c	1.5 - 4/21/87
 *
 *	sdisk -- low level SCSI disk routines.
 *
 *
 *
 *	These routines comprise the low level device specific interface for
 *	SCSI disks.  They are called from the generic disk driver, and in
 *	turn they call the SCSI manager.
 */


#include <sys/types.h>
#include <sys/param.h>
#include <sys/sysmacros.h>
#include <sys/debug.h>
#include <sys/ssioctl.h>

#ifndef	STANDALONE
#include <sys/utsname.h>
#include <sys/elog.h>
#include <sys/erec.h>
#include <sys/buf.h>
#include <sys/iobuf.h>
#endif	STANDALONE

#include <sys/gdisk.h>
#include <sys/vio.h>
#include <sys/gdkernel.h>
#include <sys/scsiccs.h>
#include <sys/scsireq.h>
#include <sys/diskformat.h>


extern	sdread(), sdwrite(), sddriveinit(), sdbadblock();
extern	sdformat(), sdrecover(), sdshutdown();
static	sdcmd(), sdret();

extern  char *scsi_strings[];

struct genprocs sdgprocs = {	/* default generic procs info */
	sdread,
	sdwrite,
	sddriveinit,
	sdbadblock,
	sdformat,
	sdrecover,
	sdshutdown,
	};


#define	NID 7	/* number of SCSI IDs */

#ifdef DEBUG
#define SDEBUG(x) x	/* debugging */
#else
#define SDEBUG(x)	/* no debug in production */
#endif

/*	State info.
 *	State, here, determines the behavior when a SCSI function completes.
 *	Values for state numbers should be less than 31.
 */

#define	SDNORMAL	0x00

#define	SDINIT1		0x01
#define	SDINIT2		0x02
#define	SDINIT3		0x03
#define	SDINIT4		0x04
#define	SDINIT5		0x05
#define	SDINIT6		0x06

#define SDFMT1		0x07
#define SDFMT2		0x08
#define	SDRECOVER	0x09
#define	SDDPT		0x0a	/* Special ugly hack for DPT caching */
#define SDINIT7		0x0b	/* mode set cure for non-std blksize */


/*	Scsi info flags.
 */

#define	SDF_COMCOM	0x01	/* Flag that drive uses common command set */
#define SDF_MODERN	0x02	/* Drive recognizes INQ	command */
#define	SDF_DPT		0x04	/* Special case for DPT caching drives */
#define SDF_VIO		0x08	/* Flag vio was passed in, not built */

#define	MAXCMDSIZE	10
#define	CAPSIZE		8

static	isinit = 0;

/* SCSI disk info structure.
 * Processing state info for each active drive.
 */
struct sdinfo {
	int	sdstate;		/* current id state */
	int	sdflags;		/* Various flags */
	struct  gentask *sdtaskp;	/* pointer to task data structure */
	struct  scsireq sdreq;		/* Request structure for this id */
	struct	diskformat *sdfmt;	/* Format info for this drive */
	int	sdgoodops;		/* Number of recent successful ops. */
	int	sdwriteops;		/* Number of recent writes */
	} sdinfo[NID];

static char	sdcmdbuf[NID][MAXCMDSIZE];
static struct sdextsense {
	u_char	errinfo;
	u_char	fill1;
	u_char	key;
	u_char	blk[4];
	u_char	len;
	u_char	fill2[4];
	u_char	code;
	u_char	fill3[5];
	u_short	cyl;
	u_char	head;
	u_char	sect;
	u_char	rdcount[3];
	u_char	seekcount[3];
	u_char	uncor_read;
	u_char	uncor_write;
	u_char	seek;
	} sdsensebuf[NID];
#define	SENSESIZE	sizeof(struct sdextsense)

/*	Error messages indexed by key code of sense message.
 */

static	char *sdkeystrings[] = {
	"No sense information available",	/* 0 */
	"Recovered error, code = 0x%x",		/* 1 */
	"Unit Not Ready",			/* 2 */
	"Medium Error, code = 0x%x",		/* 3 */
	"Hardware Error, code = 0x%x",		/* 4 */
	"Illegal request, code = 0x%x",		/* 5 */
	"Unit Attention",			/* 6 */
	"Data Protect error",			/* 7 */
	"Blank Check Error",			/* 8 */
	"Vendor Unique Error code = 0x%x",	/* 9 */
	"Copy Aborted error",			/* A */
	"Aborted Command",			/* B */
	"Equal Search data",			/* C */
	"Volume Overflow",			/* D */
	"Miscompare Error",			/* E */
	"Sense key 0xF, code = 0x%x",		/* F */
	};

struct	inq {
	u_char	devtype;	/* zero if direct access device */
	u_char	jnk1[2];
	u_char	respfmt;	/* one if common command set */
	u_char	len;		/* length of remainder of request */
	u_char	jnk2[3];
	u_char	vendor[8];	/* vendor name */
	u_char	product[16];	/* name of the product */
	u_char	revision[4];	/* manufacturer dependent */
	};
#define INQSIZE		sizeof(struct inq)

struct	errpage {
	u_char	pagenum;
	u_char	pagelen;
	u_char	params;
	u_char	jnk[5];
	};


/*	Data for parameter field of error page */
#define	EP_AWRE	0x80	/* automatic write reallocation */
#define EP_ARRE	0x40	/* Automatic read reallocation */
#define EP_TB	0x20	/* Transfer block */
#define EP_RC	0x10	/* Read continuous */
#define EP_EEC	0x08	/* Enable early correction */
#define EP_PER	0x04	/* Post Error */
#define EP_DTE	0x02	/* Disable transfer on error */
#define EP_DCR	0x01	/* Disable correction */
#define EP_MASK (EP_TB | EP_EEC | EP_PER | EP_DTE | EP_DCR)
#define EP_VALUES (EP_TB | EP_PER)

struct	errout {	/* mode info output format */
	u_char	jnk[4];
	struct	errpage outpage;
	};

struct	errin {		/* mode info input format */
	u_char	junk[12];
	struct	errpage inpage;
	};

struct	fmtmode {
	u_char jnk1[3];
	u_char	len;
	u_char	jnk2[4];
	long	blksize;
	};


/*	DPT info.
 */

#define	DPT_WRITES	100	/* Force flush after this many writes */
#define	DPT_FLUSH	0x35
#define	DPTFAST			/* Define for faster, less secure performance */

doinit()

{
	register i;
	register struct sdinfo *sp;

	for (sp = sdinfo, i = 0; i < NID; ++sp, ++i) {
		sp->sdreq.faddr = sdret;
		sp->sdreq.sensebuf = (caddr_t)&sdsensebuf[i];
		sp->sdreq.senselen = SENSESIZE;
		sp->sdreq.cmdbuf = sdcmdbuf[i];
		sp->sdstate = SDNORMAL;
	}
	isinit = 1;
}

/*	sdbadblock -- remove from current bad block list.
 */

/*ARGSUSED*/
sdbadblock(taskp)

struct gentask *taskp;
{
struct	reass {
	long	listlen;
	long	blknum;
	} *rp;

	ASSERT(gdctlnum(taskp->gtmaj) < NID);
	ASSERT(sdinfo[gdctlnum(taskp->gtmaj)].sdstate == SDNORMAL);

	TRACE(T_sdisk, ("sdbadblock block %d\n", taskp->gtblock));
	if(!isinit)
		doinit();
	taskp->gtnreq = sizeof(struct reass);
	rp = (struct reass *) taskp->gtaddr;
	rp->listlen = 4;
	rp->blknum = taskp->gtblock;
	sdcmd(taskp, SOP_REASS);
	return;
}


/*	sdcmd -- Set up SCSI command.
 */

static
sdcmd(taskp, cmd)

register struct gentask *taskp;
{
	register struct sdinfo *sp;
	register struct scsireq *rqp;
	struct vio *vp = (struct vio *)0;
	
	ASSERT(isinit);
	ASSERT(taskp->gtdnum < 8);
	sp = &sdinfo[gdctlnum(taskp->gtmaj)];
	rqp = &sp->sdreq;
	sp->sdtaskp = taskp;
	rqp->driver = (long)sp;
	rqp->databuf = taskp->gtaddr;
	rqp->datalen = taskp->gtnreq;
	rqp->niovsent = taskp->gtvdone;
	rqp->timeout = 1 + (taskp->gtnreq>>10);	/* 1 + 1sec/kbyte */
	rqp->revcode[0] = REVCODE[0];
	rqp->revcode[1] = REVCODE[1];
	rqp->revcode[2] = REVCODE[2];
	rqp->revcode[3] = 0;

	/* if vio-call reset per vio databuf */
	if ( taskp->gtaddr == (caddr_t)-1 )  {
		vp = &taskp->gtvio;	/* flag that vio is supplied */
		rqp->databuf = taskp->gtaddr = vp->vio_iov[0].vio_base;
		/*rqp->datalen = taskp->gtnreq = vp->vio_tot;please,not twice!*/
		sp->sdflags |= SDF_VIO;	/* indicate vio */
	}

	switch(cmd) {
	case SOP_READ:
	case SOP_WRITE:	
		scsig0cmd(rqp, cmd, taskp->gtdnum, 
			taskp->gtblock, taskp->gtnreq >> 9, 0);
#ifndef	DPTFAST	/* If compiled, write through to disk always */
		if((sp->sdflags & SDF_DPT) && cmd == SOP_WRITE)
			rqp->cmdbuf[5] |= 0x40;
#endif	DPTFAST
		rqp->flags = (cmd == SOP_READ) ? SRQ_READ : 0;
		break;
	case SOP_FMT:
		scsig0cmd(rqp, cmd, taskp->gtdnum, taskp->gtblock >> 8, 
				taskp->gtblock, 0);
		rqp->timeout = SRT_NOTIME;
		rqp->flags = 0;
		break;
	case SOP_INQ:
		scsig0cmd(rqp, cmd, taskp->gtdnum, 0, taskp->gtnreq, 0);
		rqp->flags = SRQ_READ;
		break;
	case SOP_RDY:
		scsig0cmd(rqp, cmd, taskp->gtdnum, 0, 0, 0);
		rqp->flags = SRQ_READ;	/* "reads" zero bytes, */
		
		break;
	case SOP_REASS:
		scsig0cmd(rqp, cmd, taskp->gtdnum, 0, 0, 0);
		rqp->flags = 0;
		break;
	case SOP_EJECT:
		bzero(rqp->cmdbuf, 10);
		rqp->cmdbuf[0] = SOP_EJECT;
		rqp->cmdbuf[1] = taskp->gtdnum << 5;
		rqp->flags = 0;
		rqp->cmdlen = 10;
		break;
	case SOP_READCAP:
		bzero(rqp->cmdbuf, 10);
		rqp->cmdbuf[0] = SOP_READCAP;
		rqp->cmdbuf[1] = taskp->gtdnum << 5;
		rqp->flags = SRQ_READ;
		rqp->cmdlen = 10;
		break;
	case SOP_GETMODE:
	case SOP_SELMODE:
		scsig0cmd(rqp, cmd, taskp->gtdnum, taskp->gtblock, taskp->gtnreq, 0);
		rqp->flags = cmd == SOP_GETMODE ? SRQ_READ : 0;
		break;
	case DPT_FLUSH:
		bzero(rqp->cmdbuf, 10);
		rqp->cmdbuf[0] = DPT_FLUSH;
		rqp->cmdbuf[1] = taskp->gtdnum << 5;
		rqp->flags = SRQ_READ;
		rqp->timeout = 20;
		rqp->cmdlen = 10;
		break;
	default:
		panic("unimplemented command in sdcmd");
		break;
	}
	TRACE(T_sdisk, ("scsi request id %d cmd 0x%x\n", 
		gdctlnum(taskp->gtmaj), cmd));

	/*viodmp("sdisk",vp); /* */
	(void) scsireq(gdctlnum(taskp->gtmaj), rqp, vp);
	return;
}


/*ARGSUSED*/
sddriveinit(taskp)

struct gentask *taskp;
{
	register struct sdinfo *sp;

	TRACE(T_sdisk, ( "sddriveinit\n"));
	if(!isinit) {
		doinit();
	}
	ASSERT(gdctlnum(taskp->gtmaj) < NID);
	ASSERT(sdinfo[gdctlnum(taskp->gtmaj)].sdstate == SDNORMAL);
	sp = &sdinfo[gdctlnum(taskp->gtmaj)];
	sp->sdstate = SDINIT1;
	sp->sdgoodops = 0;

	/*	Start init process by testing unit ready */
	
	taskp->gtnreq = 0;
	sdcmd(taskp, SOP_RDY);
	return;
}

/*ARGSUSED*/
sdformat(taskp)

struct gentask *taskp;
{
	register struct diskformat *dfp;
	register struct sdinfo *sp;
	register struct fmtmode *fmtp;

	ASSERT(gdctlnum(gdctlnum(taskp->gtmaj)) < NID);
	ASSERT(sdinfo[gdctlnum(taskp->gtmaj)].sdstate == SDNORMAL);
	TRACE(T_sdisk, ("sdformat called\n"));
	if(!isinit)
		doinit();
	sp = &sdinfo[gdctlnum(taskp->gtmaj)];
	fmtp = (struct fmtmode *)taskp->gtaddr;

	bzero(taskp->gtaddr, sizeof(struct fmtmode));
	sp->sdfmt = dfp = (struct diskformat *)taskp->gtnreq;
	if(dfp->d_secsize == 532)
		fmtp->blksize = 532;
	else
		fmtp->blksize = 512;
	fmtp->len = 8;
	sp->sdstate = SDFMT1;
	taskp->gtnreq = sizeof(struct fmtmode);
	taskp->gtblock = 0;
	sdcmd(taskp, SOP_SELMODE);
	return;
}

/*	sdrecover -- allow the drive to recover after an error.
 *	     The generic code will call this routine after the device
 *	specific code reports an error.  If we were in normal processing,
 *	we take appropriate error recovery actions.  If we were not
 *	in normal processing, we rely on this routine being called to
 *	allow us to backout any state specific device conditions.
 */

/*ARGSUSED*/
sdrecover(taskp)

struct gentask *taskp;
{
	register struct sdinfo *sp;
	int	ret;

	TRACE(T_sdisk, ("sdrecover\n"));
	ASSERT(gdctlnum(taskp->gtmaj) < NID);
	sp = &sdinfo[gdctlnum(taskp->gtmaj)];
	if(sp->sdstate == SDNORMAL && sp->sdgoodops > 0) {
		sp->sdstate = SDRECOVER;
		taskp->gtnreq = 0;
		sdcmd(taskp, SOP_RDY);
		return;
	}
	if(sp->sdgoodops == 0) {
		ret = GDR_FAILED;
	}
	else
		ret = GDR_OK;
	sp->sdstate = SDNORMAL;
	(*taskp->gtretproc)(taskp, ret);
	return;
}

/*ARGSUSED*/
sdread(taskp)

struct gentask *taskp;
{

	ASSERT(gdctlnum(taskp->gtmaj) < NID);
	ASSERT(sdinfo[gdctlnum(taskp->gtmaj)].sdstate == SDNORMAL);

	TRACE((T_sdisk | T_gdisk), ("sdread bn = %d\n", taskp->gtblock));
	sdcmd(taskp, SOP_READ);
	return;
}


#define	bit(n)	(1 << (n))

/*	sdret -- return from SCSI interrupt.
 */

static
sdret(rqp)

register struct scsireq *rqp;
{
	register struct gentask *taskp;
	register struct sdinfo *sp;
	register struct drqual *qualp;
	struct	sdextsense *sdp;
	struct	inq *iqp;
	struct	errpage *errp;
	int	ret;

	TRACE(T_sdisk, ("sdret(0x%s)ret= %d\n", rqp, rqp->ret));
	sp = (struct sdinfo *)rqp->driver;
	ASSERT(sp >= sdinfo && sp < &sdinfo[NID]);
	ASSERT(rqp == &sp->sdreq);
	taskp = sp->sdtaskp;
	ASSERT(taskp->gtcp->cttaskp == taskp);
	ASSERT(taskp->gtcp->ctactive->drqual == taskp->gtqual);
	switch(rqp->ret) {
	case 0:
		ret = GDR_OK;
		if(sp->sdstate == SDNORMAL)
			sp->sdgoodops++;
		break;
	case SST_STAT:
		sdp = (struct sdextsense *)rqp->sensebuf;
		if(bit(sp->sdstate) & (bit(SDINIT1) | bit(SDINIT2) 
		   | bit(SDINIT4) | bit(SDINIT5) | bit(SDINIT7) | bit(SDDPT)))
			ret = GDR_OK;
		else if(rqp->sensesent > 4 
		    && (sdp->errinfo & 0x70) == 0x70) {
			TRACE(T_sdisk, ("sense buf check key = 0x%x\n",
				sdp->key));
			switch(sdp->key & 0xF) {
			case 0:		/* No Additional sense */
				ret = GDR_OK;
				if(sp->sdstate == SDNORMAL)
					sp->sdgoodops++;
				break;
			case 1:		/* Corrected error */
				ret = GDR_CORR;
				break;
			case 2:		/* Not ready */
				if(sp->sdgoodops > 0)
					ret = GDR_AGAIN;
				else
					ret = GDR_FAILED;
				break;
			case 6:		/* Unit attention */
				ret = GDR_AGAIN;
				break;
			default:
				ret = GDR_FAILED;
				break;
			}
			gderr(taskp, sdkeystrings[sdp->key & 0xF], sdp->code);
		}
		else if(sp->sdgoodops > 0)
			ret = GDR_AGAIN;
		else
			ret = GDR_FAILED;
		if( (sdp->errinfo & 0x70) != 0x70)  {
			SDEBUG(printf("errinfo = 0x%x\n",sdp->errinfo));
		}
		if(ret != GDR_OK)
			sp->sdstate = SDNORMAL;
		break;
	case SST_BSY: case SST_CMD: case SST_COMP: case SST_PROT:
	case SST_MORE:
	case SST_LESS:
		if(bit(sp->sdstate) & (bit(SDINIT1) | bit(SDINIT2) 
			| bit(SDINIT3) | bit(SDINIT5) | bit(SDDPT))) {
			ret = GDR_OK;
			break;
		}
		else {
			sp->sdstate = SDNORMAL;
			/* quiet if first retry */
			if ( taskp->gtcp->ctretry )
				gderr(taskp, scsi_strings[rqp->ret], 0);
			ret = GDR_AGAIN;
		}
		break;
	case SST_SEL:
	case SST_TIMEOUT:
		gderr(taskp, scsi_strings[rqp->ret], 0);
		if(sp->sdstate ==  SDDPT) {
			ret = GDR_OK;
			break;
		}
		if(sp->sdgoodops > 1 && sp->sdstate == SDNORMAL)
			ret = GDR_AGAIN;
		else {
			sp->sdstate = SDNORMAL;
			ret = GDR_FAILED;
		}
		break;
	case SST_AGAIN:
		ret = GDR_AGAIN;
		sp->sdstate = SDNORMAL;
		break;	
	default:
		gderr(taskp, scsi_strings[rqp->ret], 0);
		ret = GDR_FAILED;
		sp->sdstate = SDNORMAL;
		sp->sdgoodops = 0;
		break;
	}
	if(ret != GDR_OK || sp->sdstate == SDNORMAL) {
		taskp->gtndone = rqp->datasent;
		taskp->gtvdone = rqp->niovsent;
		(*taskp->gtretproc)(taskp, ret);
		return;
	}

	switch(sp->sdstate) {
	case SDFMT1:
		sp->sdstate = SDFMT2;
		taskp->gtnreq = 0;
		if(sp->sdfmt->d_ileave == DISKDEFAULT)
			taskp->gtblock = 0;
		else
			taskp->gtblock = sp->sdfmt->d_ileave;
		sdcmd(taskp, SOP_FMT);
		break;
	case SDFMT2:
		sp->sdstate = SDNORMAL;
		(*taskp->gtretproc)(taskp, ret);
		return;
	/* selmode needed to reset adaptek write-inhibit feature */
	case SDINIT1:		/* set device to default blocksize */
		SDEBUG(printf("SDINIT1 "));
		sp->sdstate = SDINIT2;
		taskp->gtnreq = 0;
		taskp->gtblock = 0;
		sdcmd(taskp, SOP_SELMODE);
		break;
	case SDINIT2:
		SDEBUG(printf("SDINIT2 "));
		sp->sdstate = SDINIT3;
		taskp->gtnreq = CAPSIZE;
		sdcmd(taskp, SOP_READCAP);
		break;
	case SDINIT3:
		SDEBUG(printf("SDINIT3 "));
		qualp = taskp->gtqual;
		if((qualp->dqmaxbn = *((long *)taskp->gtaddr)) < 100) {
			SDEBUG(printf("drive c%dd%d has inadequate capacity of %d blocks\n",
				gdctlnum(taskp->gtmaj), taskp->gtdnum, qualp->dqmaxbn));
			(*taskp->gtretproc)(taskp, GDR_FAILED);
			return;
		}
		qualp->dqblksize = *((long *)(&taskp->gtaddr[4]));
		SDEBUG(printf("maxbn = %d blksize = 0x%x\n", 
			qualp->dqmaxbn, qualp->dqblksize));
		ASSERT(DEV_BSIZE == 512);
		if(qualp->dqblksize == 532) {
			(void) scsichar(gdctlnum(taskp->gtmaj), SDC_532, SDC_532);
			qualp->dqblksize = 512;
			ret = GDR_OK;
		}
		else if(qualp->dqblksize == 512) {
			(void) scsichar(gdctlnum(taskp->gtmaj), 0, SDC_532);
			ret = GDR_OK;
		}
		else if( qualp->dqblksize > 512 )  {
			struct mode_sel_blks *msp;
			/*
			 * try setting blocksize to 512
			 * to fix cdrom default 4096 blocksize
			 */
			msp = (struct mode_sel_blks *)taskp->gtaddr;
			SDEBUG(printf("Oversize device, %d byte blocks\n", 
					qualp->dqblksize));
			(void) scsichar(gdctlnum(taskp->gtmaj), 0, SDC_532);
			/* rescale maxbn to 512 byte blocks */
			qualp->dqmaxbn *= (qualp->dqblksize/512);
			qualp->dqblksize = 512;

			/* set blksize to 512 */
			bzero( (char *)msp, sizeof *msp);
			msp->ms_block = 8;	/* we have 1 block */
			msp->ms_bl_isb = 2;	/* blksize 512 bytes */

			/* do selmode */
			sp->sdstate = SDINIT7;
			taskp->gtnreq = sizeof *msp;
			taskp->gtblock = 0;
			sdcmd(taskp, SOP_SELMODE);
			break;
		}  else  {
			SDEBUG(printf("Unsupported blocksize, %d byte blocks\n", 
							qualp->dqblksize));
			ret = GDR_FAILED; 
		}
		if(ret != GDR_OK) {
			sp->sdstate = SDNORMAL;
			(*taskp->gtretproc)(taskp, ret);
			return;
		}
		else {
			sp->sdstate = SDINIT4;
			taskp->gtnreq = INQSIZE;
			sdcmd(taskp, SOP_INQ);
		}
		break;
#define ps(f,l) {char x = f[l]; f[l] = '\0'; printf("%s ",f); f[l] = x;}
	case SDINIT4:
		iqp = (struct inq *)taskp->gtaddr;
		if(rqp->datasent > 5)  {
			SDEBUG( printf("SDINIT4 ");
			        ps(iqp->vendor,8);
			        ps(iqp->product,16);
			        ps(iqp->revision,4);
			        printf("\n") );
		}
		if(strncmp(iqp->vendor, "DPT", 3) == 0 &&
		   strncmp(iqp->product, "PM3010", 6) == 0)
			sp->sdflags |= SDF_DPT;
		if(rqp->datasent >= 5 && iqp->devtype == 0 
		   && iqp->respfmt == 1) {
			sp->sdflags |= (SDF_COMCOM | SDF_MODERN);
			SDEBUG(printf("major %d is common command set\n", taskp->gtmaj));
			sp->sdstate = SDINIT5;
			taskp->gtnreq = sizeof(struct errin);
			taskp->gtblock = 1 << 8;
			sdcmd(taskp, SOP_GETMODE);
			break;
		}
		else {
			sp->sdflags &= ~(SDF_COMCOM | SDF_MODERN);
			if(rqp->datasent >= 5) {
				if(iqp->devtype == 0 || iqp->devtype == 5) {
					if ( iqp->devtype == 5 )  {
						/* cd-rom */
						taskp->gtcp->ctflags |= CTF_CDROM;
					}
					if(iqp->len > 0) {
						sp->sdflags |= SDF_MODERN;
						SDEBUG(printf
						("major %d is modern disk\n",
						taskp->gtmaj));
					}
				}
				else {
					int i;
					SDEBUG(printf("drive c%dd%d is not a disk\n",
					gdctlnum(taskp->gtmaj), taskp->gtdnum));

					for(i=10000000;--i;)
						;
				}
			}
			else {
				SDEBUG(printf("Error on inq for major %d\n",
						taskp->gtmaj));
			}
		}
		sp->sdstate = SDNORMAL;
		(*taskp->gtretproc)(taskp, GDR_OK);
		return;
	case SDINIT5:
		errp = &((struct errin *)taskp->gtaddr)->inpage;
		TRACE(T_sdisk,
		("len = %d err page num = 0x%x len = 0x%x params = 0x%x\n",
			rqp->datasent, errp->pagenum, 
			errp->pagelen, errp->params));
		if(rqp->datasent < sizeof(struct errin) 
		  || (errp->pagenum & 0xF) != 1 || errp->pagelen < 2) {
			sp->sdstate = SDNORMAL;
		}
		else {
			if((errp->params & EP_MASK) != EP_VALUES) {
				errp->params &= ~EP_MASK;
				errp->params |= EP_VALUES;
				sp->sdstate = SDINIT6;
			}
			else
				sp->sdstate = SDNORMAL;
		}
		if(sp->sdstate == SDNORMAL) {
			(*taskp->gtretproc)(taskp, GDR_OK);
		}
		else {
			errp->pagenum = 1;	/* Don't set default */
			taskp->gtnreq = sizeof(struct errout);
			taskp->gtblock = 0;
			bzero(taskp->gtaddr, sizeof(struct errout));
			((struct errout *)taskp->gtaddr)->outpage = *errp;
			sdcmd(taskp, SOP_SELMODE);
		}
		break;
	case SDINIT6:
		sp->sdstate = SDNORMAL;
		(*taskp->gtretproc)(taskp, GDR_OK);
		return;
	case SDRECOVER:
		sp->sdstate = SDNORMAL;
		(*taskp->gtretproc)(taskp, GDR_OK);
		return;
	case SDDPT:
		sp->sdstate = SDNORMAL;
		sdcmd(taskp, SOP_WRITE);
		break;
	case SDINIT7:
		SDEBUG(printf("SDINIT7 "));
		sp->sdstate = SDINIT4;
		taskp->gtnreq = INQSIZE;
		taskp->gtblock = 0;
		sdcmd(taskp, SOP_INQ);
		break;
	default:
		panic("unknown state in sdreturn\n");
		break;
	}
		
}

/*ARGSUSED*/
sdshutdown(taskp)

struct gentask *taskp;
{
	ASSERT(gdctlnum(taskp->gtmaj) < NID);
	ASSERT(sdinfo[gdctlnum(taskp->gtmaj)].sdstate == SDNORMAL);
	TRACE(T_sdisk, ("sdshutdown called arg = 0x%x\n", taskp->gtnreq));

	if ( taskp->gtnreq == GD_SHUT_EJECT )  {
		sdcmd( taskp, SOP_EJECT);
	}  else  {
		
		(*taskp->gtretproc)(taskp, GDR_OK);
	}
	return;
}

/*ARGSUSED*/
sdwrite(taskp)

struct gentask *taskp;
{
	register struct sdinfo *sp;

	TRACE((T_sdisk | T_gdisk), ("sdwrite bn = %d\n", taskp->gtblock));
	ASSERT(gdctlnum(taskp->gtmaj) < NID);
	ASSERT(sdinfo[gdctlnum(taskp->gtmaj)].sdstate == SDNORMAL);
#ifdef	DPTFAST
	sp = &sdinfo[gdctlnum(taskp->gtmaj)];
	if(++(sp->sdwriteops) > DPT_WRITES && (sp->sdflags & SDF_DPT)) {
		sp->sdwriteops = 0;
		sp->sdstate = SDDPT;
		sdcmd(taskp, DPT_FLUSH);
		return;
	}
#endif	DPTFAST
	sdcmd(taskp, SOP_WRITE);
	return;
}
