/*
 * @(#)nfsconf.c  {Apple version 1.21 90/02/02 11:18:04}
 *
 * Copyright (c) 1987, 1988, 1989 by Apple Computer, Inc.
 * All Rights Reserved.
 *
 * THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF APPLE COMPUTER, INC.
 * The copyright notice above does not evidence any actual or
 * intended publication of such source code.
 */

#if !defined(lint) && !defined(NO_SCCS_IDS)
static char _sccsid[]="@(#)nfsconf.c  {Apple version 1.21 90/02/02 11:18:04}";
#endif

/*	@(#)nfsconf.c	UniPlus VVV.2.1.24	*/
/*
 *  Configuration information
 */


#include	"sys/config.h"
#include	"sys/param.h"
#include	"sys/types.h"
#include	"sys/sysmacros.h"
#include	"sys/mmu.h"
#include	"sys/conf.h"
#include	"sys/cpuid.h"
#include	"sys/space.h"
#include	"sys/uconfig.h"
#include	"sys/iobuf.h"
#include	"sys/termio.h"
#include	"sys/stream.h"

extern nodev(), nulldev(), seltrue(), ttselect();
extern strinit(), strselect();
extern shlinit(), shlrinit();
extern lineinit();
extern struct streamtab lineinfo;
extern struct streamtab cloneinfo;
extern struct streamtab shlinfo;
extern struct streamtab shlrinfo;
extern ptcopen(), ptcclose();
extern ptcread(), ptcwrite(), ptcioctl(), ptcselect();
extern ptsopen(), ptsclose();
extern ptsread(), ptswrite(), ptsioctl();
extern syopen();
extern syread(), sywrite(), syioctl(), syselect();
extern mminit(), mmread(), mmwrite(), mmioctl();
extern erropen(), errclose();
extern errread();
extern osmopen(), osmclose();
extern osmread(), osmselect();
extern sxtopen(), sxtclose();
extern sxtread(), sxtwrite(), sxtioctl();
extern sxtselect();
extern prfread(), prfwrite(), prfioctl();
extern cacheinit();
extern parityinit();
extern iop_init();

extern nvram_init(), nvram_open(), nvram_close();
extern nvram_read(), nvram_write();
extern scinit(), scopen(), scclose();
extern scread(), scwrite(), scioctl();
extern scsiinit();
extern hdread(), hdwrite(), hdioctl(), hdioctl();
extern hdstrategy(), hdopen(), hdclose(), hdprint();
extern fd_init(), fd_open(), fd_close(), fd_strategy(), fd_print();
extern fd_read(), fd_write(), fd_ioctl();
extern iop_open(), iop_close(), iop_write();	/* /dev/iop */
extern fpioctl();
extern via1init(), via2init();
extern struct streamtab disp_tab;
extern	mouseopen(), mouseclose(), mouseread(), mousewrite(), mouseioctl();  

extern	video_init();
extern	fdb_init();
extern	key_init();
extern	mouse_init();
extern	dispinit();
extern  lcl_lckinit();

extern struct tty sc_tty[];
extern struct ttyptr sc_ttptr[];
int	sxt_cnt = 1;

struct bdevsw bdevsw[] = {
	nodev,    nulldev,   nulldev,      nulldev,   /*  0 */
	nodev,    nulldev,   nulldev,      nulldev,   /*  1 */
	nodev,    nulldev,   nulldev,      nulldev,   /*  2 */
	nodev,    nulldev,   nulldev,      nulldev,   /*  3 */
	nodev,    nulldev,   nulldev,      nulldev,   /*  4 */
	fd_open,  fd_close,  fd_strategy,  fd_print,  /*  5 */
	nodev,    nulldev,   nulldev,      nulldev,   /*  6 */
	nodev,    nulldev,   nulldev,      nulldev,   /*  7 */
	nodev,    nulldev,   nulldev,      nulldev,   /*  8 */
	nodev,    nulldev,   nulldev,      nulldev,   /*  9 */
	nodev,    nulldev,   nulldev,      nulldev,   /* 10 */
	nodev,    nulldev,   nulldev,      nulldev,   /* 11 */
	nodev,    nulldev,   nulldev,      nulldev,   /* 12 */
	nodev,    nulldev,   nulldev,      nulldev,   /* 13 */
	nodev,    nulldev,   nulldev,      nulldev,   /* 14 */
	nodev,    nulldev,   nulldev,      nulldev,   /* 15 */
	nodev,    nulldev,   nulldev,      nulldev,   /* 16 */
	nodev,    nulldev,   nulldev,      nulldev,   /* 17 */
	nodev,    nulldev,   nulldev,      nulldev,   /* 18 */
	nodev,    nulldev,   nulldev,      nulldev,   /* 19 */
	nodev,    nulldev,   nulldev,      nulldev,   /* 20 */
	nodev,    nulldev,   nulldev,      nulldev,   /* 21 */
	nodev,    nulldev,   nulldev,      nulldev,   /* 22 */
	nodev,    nulldev,   nulldev,      nulldev,   /* 23 */
	hdopen,   hdclose,   hdstrategy,   hdprint,   /* 24 */
	hdopen,   hdclose,   hdstrategy,   hdprint,   /* 25 */
	hdopen,   hdclose,   hdstrategy,   hdprint,   /* 26 */
	hdopen,   hdclose,   hdstrategy,   hdprint,   /* 27 */
	hdopen,   hdclose,   hdstrategy,   hdprint,   /* 28 */
	hdopen,   hdclose,   hdstrategy,   hdprint,   /* 29 */
	hdopen,   hdclose,   hdstrategy,   hdprint,   /* 30 */
	nodev,    nulldev,   nulldev,      nulldev,   /* 31 */
	nodev,    nulldev,   nulldev,      nulldev,   /* 32 */
	nodev,    nulldev,   nulldev,      nulldev,   /* 33 */
	nodev,    nulldev,   nulldev,      nulldev,   /* 34 */
	nodev,    nulldev,   nulldev,      nulldev,   /* 35 */
	nodev,    nulldev,   nulldev,      nulldev,   /* 36 */
	nodev,    nulldev,   nulldev,      nulldev,   /* 37 */
	nodev,    nulldev,   nulldev,      nulldev,   /* 38 */
	nodev,    nulldev,   nulldev,      nulldev,   /* 39 */
	nodev,    nulldev,   nulldev,      nulldev,   /* 40 */
	nodev,    nulldev,   nulldev,      nulldev,   /* 41 */
	nodev,    nulldev,   nulldev,      nulldev,   /* 42 */
	nodev,    nulldev,   nulldev,      nulldev,   /* 43 */
	nodev,    nulldev,   nulldev,      nulldev,   /* 44 */
	nodev,    nulldev,   nulldev,      nulldev,   /* 45 */
	nodev,    nulldev,   nulldev,      nulldev,   /* 46 */
	nodev,    nulldev,   nulldev,      nulldev,   /* 47 */
	nodev,    nulldev,   nulldev,      nulldev,   /* 48 */
	nodev,    nulldev,   nulldev,      nulldev,   /* 49 */
	nodev,    nulldev,   nulldev,      nulldev,   /* 50 */
	nodev,    nulldev,   nulldev,      nulldev,   /* 51 */
	nodev,    nulldev,   nulldev,      nulldev,   /* 52 */
	nodev,    nulldev,   nulldev,      nulldev,   /* 53 */
	nodev,    nulldev,   nulldev,      nulldev,   /* 54 */
	nodev,    nulldev,   nulldev,      nulldev,   /* 55 */
	nodev,    nulldev,   nulldev,      nulldev,   /* 56 */
	nodev,    nulldev,   nulldev,      nulldev,   /* 57 */
	nodev,    nulldev,   nulldev,      nulldev,   /* 58 */
	nodev,    nulldev,   nulldev,      nulldev,   /* 59 */
	nodev,    nulldev,   nulldev,      nulldev,   /* 60 */
	nodev,    nulldev,   nulldev,      nulldev,   /* 61 */
	nodev,    nulldev,   nulldev,      nulldev,   /* 62 */
	nodev,    nulldev,   nulldev,      nulldev,   /* 63 */
};

struct cdevsw cdevsw[] = {
	scopen,   scclose,   scread,   scwrite,   scioctl,   
	sc_tty, ttselect, 0,    				/*  0 */
	syopen,   nulldev,   syread,   sywrite,   syioctl,   
	0, syselect, 0,   					/*  1 */
	nulldev,  nulldev,   mmread,   mmwrite,   mmioctl,     
	0, seltrue, 0,    					/*  2 */
	erropen,  errclose,  errread,  nulldev,   nulldev,     
	0, seltrue, 0,    					/*  3 */
	nodev,    nulldev,   nulldev,  nulldev,   nulldev,   
	0, seltrue, 0,    					/*  4 */
	fd_open,   fd_close,   fd_read,   fd_write,   fd_ioctl,   
	0, seltrue, 0,    					/*  5 */
	nulldev,  nulldev,   nulldev,  nulldev,   fpioctl,   
	0, seltrue, 0,    					/*  6 */
	nulldev,  nulldev,   nulldev,  nulldev,   nulldev,
	0, strselect, &disp_tab,				/*  7 */
	mouseopen,mouseclose,mouseread,mousewrite,mouseioctl,   
	0, seltrue, 0, 						/*  8 */
	nodev,    nulldev,   nulldev,  nulldev,   nulldev,     
	0, seltrue, 0,    					/*  9 */
	sxtopen,  sxtclose,  sxtread,  sxtwrite,  sxtioctl,  
	0, sxtselect, 0,    					/* 10 */
	nulldev,  nulldev,   prfread,  prfwrite,  prfioctl,  
	0, seltrue, 0,    					/* 11 */
	nulldev,  nulldev,   nulldev,  nulldev,   nulldev,     
	0, strselect, &cloneinfo,    				/* 12 */
	nodev,    nulldev,   nulldev,  nulldev,   nulldev,     
	0, strselect, &shlinfo, 				/* 13 */
	nvram_open,nvram_close,nvram_read,nvram_write,nulldev,     
	0, seltrue, 0,    					/* 14 */
	nodev,    nulldev,   nulldev,  nulldev,   nulldev,     
	0, seltrue, 0,    					/* 15 */
	nodev,    nulldev,   nulldev,  nulldev,   nulldev,     
	0, seltrue, 0,    					/* 16 */
	nodev,    nulldev,   nulldev,  nulldev,   nulldev,     
	0, seltrue, 0,    					/* 17 */
	nodev,    nulldev,   nulldev,  nulldev,   nulldev,     
	0, seltrue, 0,    					/* 18 */
	iop_open, iop_close, nodev,    iop_write, nodev,     
	0, seltrue, 0,    					/* 19 */
	ptcopen,  ptcclose,  ptcread,  ptcwrite,  ptcioctl,  
	0, ptcselect, 0,  					/* 20 */
	ptsopen,  ptsclose,  ptsread,  ptswrite,  ptsioctl,  
	0, ttselect, 0, 	  				/* 21 */
	osmopen,  osmclose,  osmread,  nulldev,   nulldev,     
	0, osmselect, 0,    					/* 22 */
	nodev,    nulldev,   nulldev,  nulldev,   nulldev,     
	0, seltrue, 0,    					/* 23 */
	hdopen,   hdclose,   hdread,   hdwrite,   hdioctl,     
	0, seltrue, 0,    					/* 24 */
	hdopen,   hdclose,   hdread,   hdwrite,   hdioctl,     
	0, seltrue, 0,    					/* 25 */
	hdopen,   hdclose,   hdread,   hdwrite,   hdioctl,     
	0, seltrue, 0,    					/* 26 */
	hdopen,   hdclose,   hdread,   hdwrite,   hdioctl,     
	0, seltrue, 0,    					/* 27 */
	hdopen,   hdclose,   hdread,   hdwrite,   hdioctl,     
	0, seltrue, 0,    					/* 28 */
	hdopen,   hdclose,   hdread,   hdwrite,   hdioctl,     
	0, seltrue, 0,    					/* 29 */
	hdopen,   hdclose,   hdread,   hdwrite,   hdioctl,     
	0, seltrue, 0,    					/* 30 */
	nodev,    nulldev,   nulldev,  nulldev,   nulldev,     
	0, seltrue, 0,    					/* 31 */
	nodev,    nulldev,   nulldev,  nulldev,   nulldev,     
	0, seltrue, 0,    					/* 32 */
	nodev,    nulldev,   nulldev,  nulldev,   nulldev,     
	0, seltrue, 0,    					/* 33 */
	nodev,    nulldev,   nulldev,  nulldev,   nulldev,     
	0, seltrue, 0,    					/* 34 */
	nodev,    nulldev,   nulldev,  nulldev,   nulldev,     
	0, seltrue, 0,    					/* 35 */
	nodev,    nulldev,   nulldev,  nulldev,   nulldev,     
	0, seltrue, 0,    					/* 36 */
	nodev,    nulldev,   nulldev,  nulldev,   nulldev,     
	0, seltrue, 0,    					/* 37 */
	nodev,    nulldev,   nulldev,  nulldev,   nulldev,     
	0, seltrue, 0,    					/* 38 */
	nodev,    nulldev,   nulldev,  nulldev,   nulldev,     
	0, seltrue, 0,    					/* 39 */
	nodev,    nulldev,   nulldev,  nulldev,   nulldev,     
	0, seltrue, 0,    					/* 40 */
	nodev,    nulldev,   nulldev,  nulldev,   nulldev,     
	0, seltrue, 0,    					/* 41 */
	nodev,    nulldev,   nulldev,  nulldev,   nulldev,     
	0, seltrue, 0,    					/* 42 */
	nodev,    nulldev,   nulldev,  nulldev,   nulldev,     
	0, seltrue, 0,    					/* 43 */
	nodev,    nulldev,   nulldev,  nulldev,   nulldev,     
	0, seltrue, 0,    					/* 44 */
	nodev,    nulldev,   nulldev,  nulldev,   nulldev,     
	0, seltrue, 0,    					/* 45 */
	nodev,    nulldev,   nulldev,  nulldev,   nulldev,     
	0, seltrue, 0,    					/* 46 */
	nodev,    nulldev,   nulldev,  nulldev,   nulldev,     
	0, seltrue, 0,    					/* 47 */
	nodev,    nulldev,   nulldev,  nulldev,   nulldev,     
	0, seltrue, 0,    					/* 48 */
	nodev,    nulldev,   nulldev,  nulldev,   nulldev,     
	0, seltrue, 0,    					/* 49 */
	nodev,    nulldev,   nulldev,  nulldev,   nulldev,     
	0, seltrue, 0,    					/* 50 */
	nodev,    nulldev,   nulldev,  nulldev,   nulldev,     
	0, seltrue, 0,    					/* 51 */
	nodev,    nulldev,   nulldev,  nulldev,   nulldev,     
	0, seltrue, 0,    					/* 52 */
	nodev,    nulldev,   nulldev,  nulldev,   nulldev,     
	0, seltrue, 0,    					/* 53 */
	nodev,    nulldev,   nulldev,  nulldev,   nulldev,     
	0, seltrue, 0,    					/* 54 */
	nodev,    nulldev,   nulldev,  nulldev,   nulldev,     
	0, seltrue, 0,    					/* 55 */
	nodev,    nulldev,   nulldev,  nulldev,   nulldev,     
	0, seltrue, 0,    					/* 56 */
	nodev,    nulldev,   nulldev,  nulldev,   nulldev,     
	0, seltrue, 0,    					/* 57 */
	nodev,    nulldev,   nulldev,  nulldev,   nulldev,     
	0, seltrue, 0,    					/* 58 */
	nodev,    nulldev,   nulldev,  nulldev,   nulldev,     
	0, seltrue, 0,    					/* 59 */
	nodev,    nulldev,   nulldev,  nulldev,   nulldev,     
	0, seltrue, 0,    					/* 60 */
	nodev,    nulldev,   nulldev,  nulldev,   nulldev,     
	0, seltrue, 0,    					/* 61 */
	nodev,    nulldev,   nulldev,  nulldev,   nulldev,     
	0, seltrue, 0,    					/* 62 */
	nodev,    nulldev,   nulldev,  nulldev,   nulldev,     
	0, seltrue, 0,    					/* 63 */
};

int	bdevcnt = sizeof(bdevsw)/sizeof(bdevsw[0]);
int	cdevcnt = sizeof(cdevsw)/sizeof(cdevsw[0]);


/*
 *	Streams modules 
 */

struct fmodsw fmodsw[64] = {
	"line", 	&lineinfo,
	"shlr", 	&shlrinfo,
};

int fmodcnt = sizeof(fmodsw)/sizeof(struct fmodsw);

dev_t	rootdev = makedev(0xFF, 0xFF);
dev_t	pipedev = makedev(0xFF, 0xFF);
dev_t	swapdev = makedev(0xFF, 0xFF);
daddr_t	swaplow = 0;
int	swapcnt = 0;			/* use partition size for swapcnt */

dev_t	dumpdev = makedev(0xFF, 0xFF);
extern hddump();
int	(*dump)() = hddump;
int	dump_addr = 0;

struct ttyptr *tty_stat[] = {
	sc_ttptr,
	0
};

int	(*init_first[64])() = {
	strinit,
	via1init,
	via2init,
	iop_init,  /* MUST precede fdb_,key_, scinit, fd_init, mouse_init */
	video_init,
	fdb_init,
	key_init,
	mouse_init,
	dispinit,
	(int(*)())0
};	/* force it into .data */
int	init_firstl = sizeof(init_first)/sizeof(init_first[0]);


int	(*init_second[64])() = {(int(*)())0};	/* force it into .data */
int	init_secondl = sizeof(init_second)/sizeof(init_second[0]);

int	(*init_normal[64])() = {
	lineinit,
	shlinit,
	shlrinit,
	scinit,
	scsiinit,
	fd_init,
	parityinit,	/* must happen before cacheinit */
	cacheinit,
	mminit,
	lcl_lckinit,    /* must happen before nfsinit */
	(int (*)())0
};
int	init_normall = sizeof(init_normal)/sizeof(init_normal[0]);


int	(*init_0[64])() = {(int(*)())0};	/* force it into .data */
int	init_0l = sizeof(init_0)/sizeof(init_0[0]);

int	(*init_last[64])() = {(int(*)())0};	/* force it into .data */
int	init_lastl = sizeof(init_last)/sizeof(init_last[0]);
