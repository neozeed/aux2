#ifndef u_char
#include <sys/types.h>
#endif
#ifndef B_DONE
#include <sys/buf.h>
#endif
#ifndef SOP_RDY
#include <sys/scsiccs.h>
#endif
#ifndef vio
#include <sys/vio.h>
#endif
#ifndef SST_BSY
#include <sys/scsireq.h>
#endif
#ifndef EIO
#include <sys/errno.h>
#endif

/*
 * @(#)tc.h  {Apple version 1.3 90/02/12 13:23:43}
 *
 * Copyright (c) 1987, 1988, 1989 by Apple Computer, Inc.
 * All Rights Reserved.
 *
 * THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF APPLE COMPUTER, INC.
 * The copyright notice above does not evidence any actual or
 * intended publication of such source code.
 */

/*
 *	Apple Tape Backup 40SC "tc" info: 
 *
 *	Copyright Apple Computer Inc. 1987
 *
 */

#define	NTC		8		/* max drives allowed */

/* Minor device bits: */
#define	TC_NOEOF	0x10		/* doesn't report EOF on tapemark */
#define	TC_NOREW	0x08		/* non-rewinding minor device */
#define	TC_MINOR	0x07		/* minor significant bits */

#define	WAIT		1
#define	NOWAIT		0
#define	DISC		1		/* allow disconnect */
#define	NODISC		0		/* don't disconnect */

/* Timeout stuff.  We expect tc_timer to run at 100ms resolution.  */

#define	SECONDS		10		/* tc_timer ticks in a second */
#define	MINUTES		(60 * SECONDS)	/* tc_timer ticks in a minute */
#define	T_100MS		1		/* 100ms is a single tick */

#define	T_WAKEUP	1		/* have tc_timer wake me up */
#define	T_RESTART	2		/* have tc_timer do tc_start() */

/* Various tape opcodes: */
#define	TCOP_REZERO	0x01
#define	TCOP_UNLOAD	0x1b

/* Higher-resolution error codes, simplified to fit errno.h limitations. */
#define	EBADDEV		ENODEV

/* Model info. */

struct modelinfo {
    int		(*readcap)();		/* get max allowable blk number */
    int		(*iosetup)();		/* setup read/write SCSI command */
    int		(*spacerec)();		/* space record(s) */
    int		(*spacefile)();		/* space file(s) */
    int		(*isfilemark)();	/* check for file mark */
    int		(*weof)();		/* write end-of-file mark(s) */
    int		(*iseom)();		/* check for end-of-media */
    int		(*iocget)();		/* MTIOCGET ioctl function */
    char	*mfg_name;		/* mfg name */
    char	*prod_name;		/* product name */
    short	rewtime;		/* rewind-online time, seconds */
    short	mtiotype;		/* type for mtio.h status return */
    short	blksize;		/* blksize for each I/O */
    short	physsize;		/* physical media blocksize */
    u_short	flags;			/* flags */
    u_char	senselen;		/* sense length */
    u_char	retenbits;		/* bits for retension command */
};

#define	M_DOFORMAT	0x0001		/* does format command */
#define	M_WAITLOAD	0x0002		/* must wait for autoload */
#define	M_FMERROR	0x0004		/* filemark generates error */
#define	M_EOMERROR	0x0008		/* end-of-media generates error */
#define	M_SINGLEEOF	0x0010		/* single FM at close */
#define	M_FORCESIZE	0x0020		/* must force blksize via modesel */

struct softc {
    struct buf		buf;		/* for iowait/iodone only */
    struct sense	*sensebuf;	/* sense buffer */
    caddr_t		databuf;	/* internal data buffer */
    caddr_t		b_addr;		/* local databuf pointer */
    struct mode		*modebuf;	/* mode select buffer */
    struct modelinfo	*modelinfo;	/* ptr to model-specific info */
    short		id;		/* scsi id */
    short		blk;		/* current block position on tape */
    int			maxblk;		/* max block on this cartridge */
    short		file;		/* current file  position on tape */
    short		errcnt;		/* error count */
    long		timeout;	/* timeout count for tc_timer() */
    short		timertype;	/* timeout function type */
    struct scsireq	req;		/* scsi request packet */
    struct scsig0cmd	cmd;		/* command block for general use */
    struct scsig0cmd	modecmd;	/* command block for mode select */
    unsigned		open:1;		/* 1 if opened */
    unsigned		opening:1;	/* 1 if open in progress */
    unsigned		initted:1;	/* 1 if drive initted */
    unsigned		hard:1;		/* 1 if hard error, no more I/O */
    unsigned		write:1;	/* 1 if last op was a write */
    unsigned		rewinding:1;	/* 1 if rewinding (w/o wait) on close */
    unsigned		wprot:1;	/* 1 if write protected */
    unsigned		disc:1;		/* 1 if scsi op should disconnect */
    unsigned		newread:1;	/* 1 if start of user read operation */
    unsigned		backovereof:1;	/* 1 if need to back over EOF on read */
};

/*	Unfortunately, we can't use bitfields for these structs
 *	since the compiler would pad to something other than char.
 */

/*
 * INFO FROM THE "INQUIRY" COMMAND:
 *
 * We don't really define the structure since it isn't used
 *   except for the Manufacturer and Product ID fields.
 *
 *	vendor_id = "3M"
 *	product_id = "MCD-40/SCSI"
 *
 */

#define	INQ_LEN		36		/* bytes transferred */
#define	INQ_MFG_OFF	8		/* offset to TC_MFG_ID */
#define	INQ_PROD_OFF	16		/* offset to TC_PROD_ID */

/*
 *
 *	EXTENDED SENSE DATA:
 *
 */

struct sense {
    u_char	byte0;
#define	ADVALID		0x80		/* blocknum pertains to error code */
    u_char	reserved_1;
    u_char	byte2;
#define	M_SENSEKEY	0x0f		/* mask for sense key */
    u_char	reserved_2[2];
    char	blknum[2];		/* (short) block number with error */
    char	addl_length;		/* additional sense length */
    u_char	reserved_4[4];
    char	sensecode;		/* sense code */
    u_char	reserved_5[5];
    short	cur_blknum;		/* current block number */
    short	cur_phys;		/* current phys blk number */
    u_char	cur_track;		/* current track */
    char	cur_frame[2];		/* (short) current frame number */
    char	vu_sense[2];		/* (short) VU (diagnostic) sense data */
    char	frame_err;		/* frame in error 1=a, 2=b, 3=P */
};

#define	SENSEKEY(s)	((((s)->sensebuf->byte2) & M_SENSEKEY) & 0x0f)
#define	SENSECODE(s)	(((s)->sensebuf->sensecode) & 0xff)

/* SENSE KEYS: */

#define	S_NONE		0		/* no sense key available */
#define	S_RECOVERED	1		/* recovered error (only mode PER) */
#define	S_NOTREADY	2		/* not ready */
#define	S_MEDERROR	3		/* medium error */
#define	S_HWERROR	4		/* hardware error */
#define	S_ILLEGAL	5		/* illegal request */
#define	S_ATTENTION	6		/* unit attention */
#define	S_PROTECTED	7		/* protected */
#define	S_UNFORMATTED	8		/* not formatted */
#define	S_VENDOR	9		/* vendor-unique */
#define	S_ABORTED	11		/* drive aborted the command */

/* ADDITIONAL SENSE CODES: */

#define	NOSENSE		0x00		/* no additional sense info */
#define	NOTREADY	0x04		/* drive not ready */
#define	COMMFAIL	0x08		/* logical unit communication failure */
#define	IDCRC		0x10		/* hw error or medium error on ID */
#define	READERROR	0x11		/* unrecoverable read error */
#define	SEEKERROR	0x15		/* seek positioning error */
#define	TRAN_READ	0x17		/* recovered transient error (if PER) */
#define	REC_READ	0x18		/* recovered read (ecc), only if PER */
#define	DEFECTLIST	0x19		/* defect list error */
#define	INV_OP		0x20		/* invalid command operation code */
#define	ILL_BLK		0x21		/* illegal block address */
#define	ILL_CDB		0x24		/* illegal field in CDB */
#define	INV_PARAM	0x26		/* invalid field in param list */
#define	WPROT		0x27		/* write protected */
#define	CHANGED		0x28		/* medium changed */
#define	RESET		0x29		/* power-on or reset or bus-reset */
#define	NEWMODE		0x2a		/* mode-select params changed */
#define	CORRUPTED	0x31		/* format is corrupted */
#define	CANTSPARE	0x32		/* no spare location available */
#define	DIAGFAIL	0x42		/* power-on diagnostic failure */
#define	MSGPARITY	0x47		/* message parity error */
#define	INITERROR	0x48		/* initiator detected error */
#define	BKDNOISE	0xa0		/* background noise failure */
#define	LOADFAIL	0xa7		/* logical load failure */
#define	LOADING		0xa8		/* autoload in progress */
#define	NOTAPE		0xb0		/* no cartridge in drive */
#define	REALBAD		0xb1		/* too many bad frames on verify */

/* Mode-sense and mode-select structure: */

struct mode {				/* modesense header */
    char	len;			/* sense data length */
    char	med_type;		/* medium type */
    u_char	byte_2;
#define	M_WRTPROT	0x80		/* write-protected */
    char	desc_len;		/* block descriptor length */
    
    /* block descriptor */
    char	density;		/* density code */
    u_char	reserved_1;
    short	nblks;			/* #blks involved */
    u_char	reserved_2;
    short	blklen;
};
#define	TC_MODE_LEN	12		/* bytes transferred */
