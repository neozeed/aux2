#ifndef lint	/* .../sys/COMMON/io/sxt.c */
#define _AC_NAME sxt_c
#define _AC_NO_MAIN "@(#) Copyright (c) 1987, 1988, 1989 Apple Computer, Inc., 1985-87 UniSoft Corporation, All Rights Reserved.  {Apple version 2.1 89/10/13 18:29:50}"
#include <apple_notice.h>

#ifdef _AC_HISTORY
  static char *sccsid = "@(#)Copyright Apple Computer 1987, 1988, 1989\tVersion 2.1 of sxt.c on 89/10/13 18:29:50";
#endif		/* _AC_HISTORY */
#endif		/* lint */

#define _AC_MODS
/*	@(#)sxt.c	UniPlus 2.1.5	*/


/*
 * SXT --  Driver for shell layers
 */


#define	TIOCEXCL	(('t'<<8)|13)		/* Should be in 'ttyold.h' */
#define	TIOCNXCL	(('t'<<8)|14)		/* Should be in 'ttyold.h' */

#ifdef lint
#include "sys/sysinclude.h"
#else lint
#include "sys/param.h"
#include "sys/types.h"
#include "sys/sysmacros.h"
#include "sys/mmu.h"
#include "sys/page.h"
#include "sys/systm.h"
#include "sys/file.h"
#include "sys/conf.h"
#include "sys/region.h"
#include "sys/time.h"
#include "sys/proc.h"
#include "sys/ioctl.h"
#include "sys/tty.h"
#include "sys/signal.h"
#include "sys/user.h"
#include "sys/errno.h"
#include "sys/termio.h"
#include "sys/ttold.h"
#include "sys/sxt.h"
#include "sys/uio.h"
#include "sys/tuneable.h"
#endif lint

 
/*  A real terminal is associated with a number of virtual tty's.
 *  The real terminal remains the communicator to the low level
 *  driver,  while the virtual terminals and their queues are
 *  used by the upper level (ie., ttread, ttwrite).
 *
 *  real tty (tp)   
 *		linesw == SXT
 *		proc   == real driver proc routine
 *  virtual tty (vtp)
 *		linesw == original value of real tty
 *		proc   == SXTVTPROC
 */

/* Maximum size of a link structure */
#define LINKSIZE (sizeof(struct Link) + sizeof(struct Channel)*(MAXPCHAN-1))
#define LINKHEAD (sizeof(struct Link))

Link_p		linkTable[MAXLINKS];

#	if	SXTRACE == 1
char	tracing;		/* print osm msgs for tracing */
#	endif


char sxtbusy[MAXLINKS], *sxtbuf;
extern int sxt_cnt;
Link_p sxtalloc();
int sxtnullproc();
int sxtvtproc();



sxtopen(dev, flag)
{
	register struct user *up;
	register Link_p lp;
	register chan;
	register struct tty *vtp;
	register bit;
	int error = 0;

	up = &u;
	chan = CHAN(dev);

#if	SXTRACE == 1
	if (tracing)
		printf("!sxtopen: link %d, chan %d\n", LINK(dev), chan);
#endif

	/*  linkTable:	0 - unused, 1 - master slot reserved, 
	*			   >1 - master operational */
	if((lp = linkTable[LINK(dev)]) == (Link_p)1)
		return(EBUSY);

	if(lp == NULL) {
		if(chan != 0)
			/* Master on chan 0 must be opened first */
			error = EINVAL;
		else
			linkTable[LINK(dev)] = (Link_p)1;
		return(error);		/* nothing else to do here */
	}

	if(chan == 0)
		/* master already set up on this link	*/
		return(EBUSY);

	if(chan >= lp->nchans)
		/* channel number out of range	*/
		return(ENXIO);

	bit = (1 << chan);
	if(lp->open & bit) {
		if((flag & FEXCL) || (lp->xopen & bit))
			return(ENXIO);
	}
	else {
		/* open new channel */
		lp->open |= bit;

		if (lp->chans[chan].tty.t_proc == sxtnullproc ||
				lp->chans[chan].tty.t_proc == sxtvtproc)
			ttyflush(&lp->chans[chan].tty, (FREAD | FWRITE));
		bzero(	(char *) &lp->chans[chan], sizeof (struct Channel));
		sxtlinit(&lp->chans[chan], chan, LINK(dev), lp->old,
			lp->line);
		if(flag & FEXCL)
			lp->xopen |= bit;
	}

	vtp = &lp->chans[chan].tty;

	/* do the pertinent part of ttopen() here */ 
	spltty();
	if ((up->u_procp->p_pid == up->u_procp->p_pgrp)
	   && (up->u_procp->p_ttyp == NULL)
	   && (vtp->t_pgrp == 0)){
		up->u_procp->p_ttyp = &vtp->t_pgrp;
		vtp->t_pgrp = up->u_procp->p_pgrp;
		up->u_procp->p_flag |= SPGRPL;
	}
	vtp->t_state |= ISOPEN;
	SPL0();
	return(error);
}

/* ARGSUSED */
sxtclose(dev, flag)
{
	register Link_p lp;
	register chan;
	register struct tty *vtp;
	int sps;
	int i;

#if SXTRACE == 1
	if (tracing)
		printf("!sxtclose: link %d, chan %d\n", LINK(dev), CHAN(dev));
#endif

	if((lp = linkTable[LINK(dev)]) == (Link_p)1) {
		/* no work to be done - master slot was only reserved,
		 * not used.					     
		 */
		linkTable[LINK(dev)] = NULL;
		return;
	}

	chan = CHAN(dev);
	vtp = &lp->chans[chan].tty;

	vtp->t_state &= ~(BUSY|TIMEOUT);

	(*linesw[vtp->t_line].l_close)(vtp);
	if(chan == lp->controllingtty && chan != 0)
		lp->line->t_pgrp = lp->chans[0].tty.t_pgrp;

	vtp->t_pgrp = 1;		/* To keep out /dev/tty */
	vtp->t_state &= ~CARR_ON;

	chan = 1 << chan;
	lp->xopen &= ~chan;
	lp->open &= ~chan;

	sps = spltty();
	if (chan == 1)			/* e.g. channel 0 */
	{
		lp->controllingtty = 0;
		lp->line->t_line = lp->old;
		lp->line = vtp;		/* release real tty */
		vtp->t_proc = sxtnullproc;

		for (i = 1; i < lp->nchans; ++i)
		{
			vtp = &lp->chans[i].tty;

			vtp->t_pgrp = 1;
			if (vtp->t_proc == sxtnullproc ||
					vtp->t_proc == sxtvtproc)
				ttyflush(vtp, (FREAD | FWRITE));
		}
	}

	if(lp->open == 0) 
	{
		/* no other opens on this set of virt ttys */

		linkTable[LINK(dev)] = NULL;
		sxtfree(lp);
	}
	splx(sps);
}


sxtioctl(dev, cmd, data, mode)
register caddr_t data;
{
	register struct user *up;
	register Link_p lp;
	register struct tty *tp, *vtp;
	register sxtld;
	struct termio *cbp;
	struct sgttyb *tbp;
	struct sxtblock *sbp;
	int flag;
	int sps;
	int i;
	char c_line;
	int error = 0;
	extern sxtin();

	up = &u;
#if	SXTRACE == 1
	if (tracing)
		printf("!sxtioctl: cmd %x, link %d, chan %d\n", cmd,
				LINK(dev), CHAN(dev));
#endif

	if((lp = linkTable[LINK(dev)]) == (Link_p)1 && cmd != SXTIOCLINK)
		/* only a link command would be valid here */
		return(ENXIO);

	switch(cmd) {

	case SXTIOCLINK:
		if(*(int *)data > MAXPCHAN || *(int *)data < 1) {
			error = EINVAL;
			break;
		}
		if(  (tp = cdevsw[major(up->u_ttyd)].d_ttys) == NULL) {
			error = ENOTTY;
			break;
		}

		tp += minor(up->u_ttyd);	/* Real 'tty' line */

		/* find	 sxt line discipline entry number in linesw */
		for(sxtld = 0; sxtld < linecnt; sxtld++)
			if(linesw[sxtld].l_input == sxtin)
				break;
		if(sxtld == linecnt) {
			error = ENXIO;	/* SXT not in linesw */
			break;
		}
		if (lp == (Link_p)0) {
			error = EBADF;	/* file not opened */
			break;
		}
		if(lp != (Link_p)1) {
			error = EBUSY;	/* Pre-empted! */
			break;
		}

		if((lp = sxtalloc(*(int *)data)) == NULL) {
			error = ENOMEM;	/* No memory, try again */
			break;
		}

		ttyflush(tp, FREAD|FWRITE);
		lp->dev = up->u_ttyd;	/* save major/minor dev #s	*/
		lp->controllingtty = 0;	/* channel 0			*/
		lp->lwchan = 0;	/* last channel to write	*/
		lp->wpending = 0;	/* write pending bits/chan	*/
		lp->wrcnt = 0;		/* number of writes on last channel written */
		lp->line = tp;
		lp->old = tp->t_line;	/* Remember old line discipline */
		lp->nchans = *(int *)data;

		lp->chanmask = 0xFF;
		for (i = lp->nchans; i < MAXPCHAN; ++i)
			lp->chanmask >>= 1;

		lp->open = lp->xopen = 1;	/* Open channel 0	*/
		sxtlinit(&lp->chans[0], 0, LINK(dev), lp->old, tp);

		sps = spltty();
		linkTable[LINK(dev)] = lp;	/* Now visible		*/
		tp->t_line = sxtld;	/* Stack new one		*/
		tp->t_link = LINK(dev);	/* Back pointer to link structure */
		vtp = &lp->chans[0].tty;

		/* do the pertinent part of ttopen() here		*/ 
		if ((up->u_procp->p_pid == up->u_procp->p_pgrp)
		   && (up->u_procp->p_ttyp == NULL)
		   && (vtp->t_pgrp == 0)){
			up->u_procp->p_ttyp = &vtp->t_pgrp;
			vtp->t_pgrp = up->u_procp->p_pgrp;
			up->u_procp->p_flag |= SPGRPL;
		} else {
			vtp->t_pgrp = tp->t_pgrp;
		}
		vtp->t_state |= ISOPEN;
		splx(sps);
		break;

	case SXTIOCSWTCH:
		/* ***	make new vtty top dog -
		 *	download new vtty characteristics and wake up
		 */
		if (lp == (Link_p) 1 || lp == (Link_p) 0) {
			error = EINVAL;
			break;
		}
		if (!(1<<*(int *)data & lp->open) ) {
			error = EINVAL;
			break;
		}
		sps = spltty();
	/* UniSoft allows a controlling device to switch control to dev 0 */
		if ( !(
		    (CHAN(dev) == 0) ||
		    ((*(int *)data == 0) && (CHAN(dev) == lp->controllingtty))
		    ) ) {
			error = EPERM;
			splx(sps);
			break;
		}

#if	SXTRACE == 1
		if (tracing)
			printf("!sxtioctl: switch data=%d, control=%d\n",
				*(int *)data,lp->controllingtty);
#endif
		if ( lp->controllingtty != *(int *)data){
				
			lp->controllingtty = *(int *)data;
			if (*(int *)data != 0){
				/*  download valid portions of tty struct*/
				tp = lp->line;
				vtp = &lp->chans[*(int *)data].tty;
				/*  change flags */
				tp->t_iflag = vtp->t_iflag;
				tp->t_oflag = vtp->t_oflag;
				tp->t_cflag = vtp->t_cflag;
				tp->t_lflag = vtp->t_lflag;
				tp->t_pgrp = vtp->t_pgrp;
				bcopy((caddr_t)vtp->t_cc, (caddr_t)tp->t_cc,
					NCC);
				bcopy((caddr_t)&vtp->t_chars, 
				(caddr_t)&tp->t_chars, sizeof(struct ttychars));
	
				/*  do download to have new values take effect */
				(*tp->t_proc)(tp, T_PARM);
			}
		}
		splx(sps);
		wakeup( (caddr_t) &lp->chans[*(int *)data]);
		break;

	case SXTIOCWF:
		/* wait til chan *(int *)data is in foreground and
		 * then return
		 */
		if (*(int *)data == -1)
			*(int *)data = CHAN(dev);
		if (lp == (Link_p) 1 || lp == (Link_p) 0) {
			error = EINVAL;
			break;
		}
		if (!(1<<*(int *)data & lp->open) ) {
			error = EINVAL;
			break;
		}
		if ( lp->controllingtty == *(int *)data)
			/* nothing to be done */
			break;

		sps = spltty();
		while (lp->controllingtty != *(int *)data)
			(void) sleep( (caddr_t) &lp->chans[*(int *)data], TTOPRI);
		splx(sps);

		break;

	case SXTIOCBLK:
		/*
		 *  set LOBLK in indicated window
		 */

		if (*(int *)data == -1)
			*(int *)data = CHAN(dev);
		if (lp == (Link_p) 1 || lp == (Link_p) 0) {
			error = EINVAL;
			break;
		}
		if (!(1<<*(int *)data & lp->open) ) {
			error = EINVAL;
			break;
		}
		vtp = &lp->chans[*(int *)data].tty;
		vtp->t_cflag |= LOBLK;
		break;

	case SXTIOCUBLK:
		/*
		 *  unset LOBLK in indicated window
		 */
		
		if (*(int *)data == -1)
			*(int *)data = CHAN(dev);
		if (lp == (Link_p) 1 || lp == (Link_p) 0) {
			error = EINVAL;
			break;
		}
		if (!(1<<*(int *)data & lp->open) || (*(int *)data == 0)) {
			error = EINVAL;
			break;
		}
		vtp = &lp->chans[*(int *)data].tty;
		vtp->t_cflag &= ~LOBLK;
		wakeup( (caddr_t) &lp->chans[*(int *)data]);
		break;

	case SXTIOCSTAT:
		/*
		 *  return bit map of blocked channels to user
		 */

		if (lp == (Link_p) 1 || lp == (Link_p) 0) {
			error = EINVAL;
			break;
		}
		
		sbp = (struct sxtblock *) data;
		sbp->input = lp->iblocked;
		sbp->output = lp->oblocked;
		break;

#if SXTRACE == 1
	case SXTIOCTRACE:
		tracing = 1;
		break;

	case SXTIOCNOTRACE:
		tracing = 0;
		break;
#endif

	case TIOCEXCL:
		lp->xopen |= (1<<CHAN(dev));
		break;

	case TIOCNXCL:
		lp->xopen &= ~(1<<CHAN(dev));
		break;

	case TCGETA:
	case TIOCGETP:
		(void) ttiocom(&lp->chans[CHAN(dev)].tty, cmd, data, mode);
		break;

	case TIOCSETP:
		(void) ttiocom(&lp->chans[CHAN(dev)].tty, cmd, data, mode);
		if (CHAN(dev) == lp->controllingtty){
			/* TIOCSETP real tty without flush or ttywait */
			tp = lp->line;
			/* next section lifted from tty.c */

			tbp = (struct sgttyb *) data;
			tp->t_iflag = 0;
			tp->t_oflag = 0;
			tp->t_lflag = 0;
			tp->t_cflag = (tbp->sg_ispeed&CBAUD)|CREAD;
			if ((tbp->sg_ispeed&CBAUD)==B110)
				tp->t_cflag |= CSTOPB;
			tp->t_cc[VERASE] = tbp->sg_erase;
			tp->t_cc[VKILL] = tbp->sg_kill;
			flag = tbp->sg_flags;
			if (flag&O_HUPCL)
				tp->t_cflag |= HUPCL;
			if (flag&O_XTABS)
				tp->t_oflag |= TAB3;
			else if (flag&O_TBDELAY)
				tp->t_oflag |= TAB1;
			if (flag&O_LCASE) {
				tp->t_iflag |= IUCLC;
				tp->t_oflag |= OLCUC;
				tp->t_lflag |= XCASE;
			}
			if (flag&O_ECHO)
				tp->t_lflag |= ECHO;
			if (!(flag&O_NOAL))
				tp->t_lflag |= ECHOK;
			if (flag&O_CRMOD) {
				tp->t_iflag |= ICRNL;
				tp->t_oflag |= ONLCR;
				if (flag&O_CR1)
					tp->t_oflag |= CR1;
				if (flag&O_CR2)
					tp->t_oflag |= ONOCR|CR2;
			} else {
				tp->t_oflag |= ONLRET;
				if (flag&O_NL1)
					tp->t_oflag |= CR1;
				if (flag&O_NL2)
					tp->t_oflag |= CR2;
			}
			if (flag&O_RAW) {
				tp->t_cc[VTIME] = 1;
				tp->t_cc[VMIN] = 6;
				tp->t_iflag &= ~(ICRNL|IUCLC);
				tp->t_cflag |= CS8;
			} else {
				tp->t_cc[VEOF] = CEOF;
				tp->t_cc[VEOL] = 0;
				tp->t_iflag |= BRKINT|IGNPAR|ISTRIP|IXON|IXANY;
				tp->t_oflag |= OPOST;
				tp->t_cflag |= CS7|PARENB;
				tp->t_lflag |= ICANON|ISIG;
			}
			tp->t_iflag |= INPCK;
			if (flag&O_ODDP)
				if (flag&O_EVENP)
					tp->t_iflag &= ~INPCK;
				else
					tp->t_cflag |= PARODD;
			if (flag&O_VTDELAY)
				tp->t_oflag |= FFDLY;
			if (flag&O_BSDELAY)
				tp->t_oflag |= BSDLY;
	
	
			/* download tty change */
			(*tp->t_proc)(tp, T_PARM);
		}
		break;

	case TCSETA:
	case TCSETAW:
	case TCSETAF:
	case TIOCSLTC:
		(void) ttiocom(&lp->chans[CHAN(dev)].tty, cmd, data, mode);
		if (CHAN(dev) == lp->controllingtty){
			/* must perform action on real tty */
			/* insure that line disps remain correct */
			cbp = (struct termio *) data;
			c_line = cbp->c_line;
			cbp->c_line = lp->line->t_line;
			error = (*cdevsw[major(lp->dev)].d_ioctl)
				(minor(lp->dev), 
				cmd != TIOCSLTC ? TCSETA : TIOCSLTC, 
				data, mode);
			/*  now restore user buffer */
			cbp->c_line = c_line;
		}
		break;

	case TCSBRK:
		(void) ttiocom(&lp->chans[CHAN(dev)].tty, cmd, data, mode);
		if (CHAN(dev) == lp->controllingtty && *(int *)data == 0)
			(*lp->line->t_proc)(lp->line, T_BREAK);
		break;

	case TCXONC:
	case TCFLSH:
		(void) ttiocom(&lp->chans[CHAN(dev)].tty, cmd, data, mode);
		break;

	case FIONREAD:
		(void) ttiocom(&lp->chans[CHAN(dev)].tty, cmd, data, mode);
		break;

	default:
		(void) ttiocom(&lp->chans[CHAN(dev)].tty, cmd, data, mode);
		if (CHAN(dev) == lp->controllingtty)
			/* must perform action on real tty */
			error = (*cdevsw[major(lp->dev)].d_ioctl)
				(minor(lp->dev), cmd, data, mode);
		break;
	}
	return(error);
}


/* ARGSUSED */
sxtlinit(chp, channo, link, ld, tp)
register Ch_p chp;
register struct tty * tp;	/* real tty structure */
{

#if	SXTRACE == 1
		if (tracing)
			printf("!sxtlinit: channo %d\n", channo);
#else
#ifdef	lint
	channo = channo;
#endif
#endif

	ttinit(&chp->tty);
	chp->tty.t_line = ld;	/* old line discipline */
	chp->tty.t_proc = sxtvtproc;
	chp->tty.t_link = link;
	chp->tty.t_state |=  (tp->t_state
				& ~(OASLP|IASLP|TTIOW|BUSY|ISOPEN));

}


sxtread(dev, uio)
dev_t dev;
register struct uio *uio;
{
	register Link_p lp;
	register struct tty *vtp;
	register int channo;
	int sps;
	int error = 0;


#if	SXTRACE == 1
	if (tracing)
		printf("!sxtread: link %d, chan %d\n", LINK(dev),
				CHAN(dev));
#endif

	channo = CHAN(dev);
	if((lp = linkTable[LINK(dev)]) == (Link_p)1)
		error = ENXIO;
	else if (lp == (Link_p)0)
		error = EBADF;	/* link not opened */
	else if (!(lp->open & (1<<channo)))
		error = EBADF;
	else {
		vtp = &lp->chans[channo].tty;

		sps = spltty();
		while (lp->controllingtty != channo){
			lp->iblocked |= (1 << channo);
			(void)sleep( (caddr_t) &lp->chans[channo], TTOPRI);
		}
		lp->iblocked &= ~(1 << channo);
		splx(sps);
		error = (*linesw[vtp->t_line].l_read)(vtp, uio);/* virt. tty */
	}
	return(error);
}


sxtwrite(dev, uio)
dev_t dev;
register struct uio *uio;
{
	register Link_p	lp;
	register struct tty *vtp;
	register int channo;
	int sps;
	int error = 0;
	
#if	SXTRACE == 1
	if (tracing)
		printf("!sxtwrite: link %d, chan %d\n", LINK(dev),
				CHAN(dev));
#endif

	channo = CHAN(dev);
	if((lp = linkTable[LINK(dev)]) == (Link_p)1)
		error = ENXIO;
	else if (lp == (Link_p)0)
		error = EBADF;	/* link not opened */
	else if (!(lp->open & (1<<channo)))
		error = EBADF;
	else {
		channo = CHAN(dev);
		vtp = &lp->chans[channo].tty;

		sps = spltty();
		while ((vtp->t_cflag & LOBLK) &&
		    (lp->controllingtty != channo)){
			lp->oblocked |= (1 << channo);
			(void)sleep( (caddr_t) &lp->chans[channo], TTOPRI);
		}
		lp->oblocked &= ~(1 << channo);
		splx(sps);

		error = (*linesw[vtp->t_line].l_write)(vtp, uio);/* virt. tty */
	}
	return(error);
}


sxtrwrite(rtp, uio)
register struct tty *rtp;
struct uio *uio;
{
	register struct tty *vtp;
	register index;
	register Link_p	lp;

	/* write issued to a real tty device.  Look for the real tty's group
	* of virtual ttys and do the write to the controllingtty of that 
	* group.  Hash this???
	*/
	for (index = 0; index < MAXLINKS; index++){
		if ((lp=linkTable[index]) != (Link_p)0 && lp != (Link_p)1){
			if (lp->line == rtp)
				break;
		}
	}
	if (index == MAXLINKS)	/* no match */
		return(0);		/* drop output on floor */
	/* write to controlling tty	    */
	vtp = &lp->chans[lp->controllingtty].tty;
	return((*linesw[vtp->t_line].l_write)(vtp, uio));
}




sxtvtproc(vtp, cmd)
register struct tty *vtp;
{
	register Link_p	lp;
	register cnt;

	/* 
	 *     called with a virtual tty.
	 */

#if	SXTRACE == 1
	if (tracing)
		printf("!sxtvtproc: cmd %d \n", cmd);
#endif

	switch(cmd) {

	default:
		return;

	case T_WFLUSH:
		lp = linkTable[vtp->t_link];
		if (lp->controllingtty == (vtp - &lp->chans[0].tty))
			(*lp->line->t_proc)(lp->line, T_WFLUSH);
		break;

	case T_RESUME:
		lp = linkTable[vtp->t_link];
		(*lp->line->t_proc)(lp->line, T_RESUME);
		break;

	case T_OUTPUT:
		lp = linkTable[vtp->t_link];
		cnt = vtp - &lp->chans[0].tty;
		lp->wpending |= 1<< cnt;
		(*lp->line->t_proc)(lp->line, T_OUTPUT);  /* real proc */
		break;

	case T_SUSPEND:
		lp = linkTable[vtp->t_link];
		(*lp->line->t_proc)(lp->line, T_SUSPEND);
		break;

	case T_RFLUSH:
		lp = linkTable[vtp->t_link];
		if (lp->controllingtty == (vtp - &lp->chans[0].tty))
			(*lp->line->t_proc)(lp->line, T_RFLUSH);
		break;

	case T_SWTCH:
		lp = linkTable[vtp->t_link];
		/* change control to channel 0 */
		lp->controllingtty = 0;
		lp->line->t_pgrp = lp->chans[0].tty.t_pgrp;
		wakeup ((caddr_t) &lp->chans[0]);
		break;
	}
}


sxtnullproc(vtp, cmd)
register struct tty *vtp;
{
	register Link_p lp;
	unsigned char	tmp;
	int cnt = 0;
	/* 
	 *     called with a virtual tty.
	 */

#if	SXTRACE == 1
	if (tracing)
		printf("!sxtnullproc: cmd %d \n", cmd);
#endif

	if (cmd == T_OUTPUT)
	{
		lp = linkTable[vtp->t_link];

		tmp = lp->wpending;
		while (tmp >>= 1)
			cnt++;

		ttyflush(&lp->chans[cnt].tty, FWRITE);
		lp->wpending = 0;
	}
}


/*
 * real tty output routine - give cblock to device
 * multiplexing done here!
 */
sxtout(tp)
struct tty *tp;
{
	register Link_p lp;
	register cnt;
	register struct tty *vtp;
	unsigned char tmp;
	int sps;
	int retn;
	
	lp = linkTable[tp->t_link];

#if	SXTRACE == 1
	if (tracing)
		printf("!sxtout:  link %d, chan %d\n", tp->t_link,
				lp->lwchan);
#endif

	sps = spltty();
	if (lp->lwchan)
	{
		cnt = 0;
		tmp = lp->lwchan;
		while (tmp >>= 1)
			cnt++;

		vtp = &lp->chans[cnt].tty;
		vtp->t_tbuf = tp->t_tbuf;

		if (lp->wrcnt >= SXTHOG && lp->wpending != lp->lwchan)
		{
			if (vtp->t_tbuf.c_ptr != NULL)
			{
				putcf(CMATCH((struct cblock *)vtp->t_tbuf.c_ptr));
				vtp->t_tbuf.c_ptr = NULL;
				vtp->t_tbuf.c_count = 0;
			}
			tp->t_tbuf = vtp->t_tbuf;
		}
		else 
		{

			if (retn = (*linesw[vtp->t_line].l_output)(vtp))
			{	
				/* got another tbuf from that virt. tty */
				tp->t_tbuf = vtp->t_tbuf;
				vtp->t_tbuf.c_ptr = NULL;
				vtp->t_tbuf.c_count = 0;

				lp->wrcnt = (lp->wrcnt >= SXTHOG) ? 1: (lp->wrcnt + 1);

				/* return 1 so real proc will transmit */
				splx(sps);
				return(retn);
			}
			else 
			{
				/* no more data on this virt terminal */
				if (vtp->t_tbuf.c_ptr != NULL)
				{
					putcf(CMATCH((struct cblock *)vtp->t_tbuf.c_ptr));
					vtp->t_tbuf.c_ptr = NULL;
					vtp->t_tbuf.c_count = 0;
				}
				tp->t_tbuf = vtp->t_tbuf;

				lp->wpending &= ~(lp->lwchan);
			}
		}
	}
	if (lp->wpending == 0) 
	{
#if SXTRACE == 1
		if (tracing)
			printf("!sxtout: no more work to do\n");
#endif
		lp->lwchan = 0;

		/* no other data to write! */
		splx(sps);
		return(0);
	}

	for (cnt = 0; cnt < lp->nchans; ++cnt)
	{
		lp->lwchan = (lp->lwchan << 1) & lp->chanmask;
		if (lp->lwchan == 0)
			lp->lwchan = 1;
		if (lp->wpending & lp->lwchan)
			break;
	}

	if (cnt < lp->nchans)
	{
		cnt = 0;
		tmp = lp->lwchan;
		while (tmp >>= 1)
			cnt++;

		/*  channel not out of bounds */
		vtp = &lp->chans[cnt].tty;

		if (retn = (*linesw[vtp->t_line].l_output)(vtp))
		{
#if	SXTRACE == 1
			if (tracing)
				printf("!sxtout: got tbuf from new vtty %d\n",
						cnt);
#endif
			/* got a tbuf from a different virt. tty */
			lp->wrcnt = 1;
			tp->t_tbuf = vtp->t_tbuf;
			vtp->t_tbuf.c_ptr = NULL;
			vtp->t_tbuf.c_count = 0;
			/* so real proc will transmit */
			splx(sps);
			return(retn);
		}
		else 
		{
#if	SXTRACE == 1
			if (tracing)
				printf("!sxtout: %d lied\n", cnt);
#endif
			/* no more data on this virt terminal */
			/* someone lied to us!!		*/
			if (vtp->t_tbuf.c_ptr != NULL){
				putcf(CMATCH((struct cblock *) vtp->t_tbuf.c_ptr));
				vtp->t_tbuf.c_ptr = NULL;
				vtp->t_tbuf.c_count = 0;
			}
			tp->t_tbuf = vtp->t_tbuf;

			lp->wpending &= ~(lp->lwchan);
			lp->lwchan = 0;
		}
	}
	else
		lp->lwchan = 0;
	splx(sps);
	return(0);	/* we'll get called again....*/
}




/*
 * real tty input routine
 * returns data to controlling tty
 */
sxtin(tp, code)
register struct tty *tp;
{
	register struct tty *vtty;
	register Link_p	lp;
	register n;

#if	SXTRACE == 1
	if (tracing)
		printf("!sxtin:	 link %d, code %d\n", tp->t_link, code);
#endif

	lp = linkTable[tp->t_link];
	n = lp->controllingtty;
	vtty = &lp->chans[n].tty;

	switch(code) {

	case L_SWITCH:

		/* change control to channel 0 */
		/* first flush input queue     */
		if (!vtty->t_lflag & NOFLSH)
			ttyflush(vtty, FREAD);
		if (n != 0) {
			lp->controllingtty = 0;
			tp->t_pgrp = lp->chans[0].tty.t_pgrp;
		}
		wakeup ((caddr_t) &lp->chans[0]);
		break;

	case L_INTR:
	case L_QUIT:
	case L_BREAK:

		(*linesw[vtty->t_line].l_input)(vtty, code);
		break;
		
	case L_BUF:

		/* copy data to controlling tty */
		vtty->t_rbuf = tp->t_rbuf;

		(*linesw[vtty->t_line].l_input)(vtty, L_BUF);


		/* ttin() will have moved the rbuf to the inputq on the
		 * virtual tty and replaced rbuf.c_ptr with a new cblock
		 * just transfer this to the real tty rbuf field and we
		 * should be OK.
		 */
		tp->t_rbuf = vtty->t_rbuf;
		vtty->t_rbuf.c_ptr = NULL;
		vtty->t_rbuf.c_count = 0;

	default:
		return;

	}
}


sxtfree(lp)
Link_p lp;
{
	int i;

#if	SXTRACE == 1
	if (tracing)
		printf("!sxtfree\n");
#endif


	i = ((char *)lp - sxtbuf)/LINKSIZE;
	sxtbusy[i] = 0;
}

/* ARGSUSED */
Link_p
sxtalloc(arg)
{
	register i;
	Link_p lp;

#if	SXTRACE == 1
	if (tracing)
		printf("!sxtalloc\n");
#endif

	if(sxtbuf != NULL) {
		for(i=0; i < sxt_cnt; i++)
			if(!sxtbusy[i]) {
#ifndef lint	/* causes "pointer alignment problem" complaint */
				lp = (Link_p) (sxtbuf + (i*LINKSIZE));
#else lint
				lp = linkTable[0];
#endif lint
				bzero((char *)lp, LINKHEAD);
				sxtbusy[i] = 1;
				return( lp );
			}
	}
	return( NULL );
}

sxtselect(dev, rw)
dev_t dev;
int rw;
{
	register Link_p lp;
	register struct tty *vtp;
	register int channo;
	int s;


#if	SXTRACE == 1
	if (tracing)
		printf("!sxtselect: link %d, chan %d\n", LINK(dev),
			CHAN(dev));
#endif

	channo = CHAN(dev);
	if((lp = linkTable[LINK(dev)]) == (Link_p)1)
		return(1);
	if (lp == (Link_p)0)
		return(1);
	if (!(lp->open & (1<<channo)))
		return(1);
	vtp = &lp->chans[channo].tty;
	s = spl5();

	switch (rw) {

	case FREAD:
		if (lp->controllingtty == channo)
			if (ttnread(vtp) > 0)
				goto win;
		if (vtp->t_rsel &&
		    vtp->t_rsel->p_wchan == (caddr_t)&selwait)
			vtp->t_state |= TS_RCOLL;
		else
			vtp->t_rsel = u.u_procp;
		break;

	case FWRITE:
		if (lp->controllingtty == channo)
			if (vtp->t_outq.c_cc <= ttlowat[vtp->t_cflag&CBAUD])
				goto win;
		if (vtp->t_wsel &&
		    vtp->t_wsel->p_wchan == (caddr_t)&selwait)
			vtp->t_state |= TS_WCOLL;
		else
			vtp->t_wsel = u.u_procp;
		break;
	}
	splx(s);
	return(0);
win:
	splx(s);
	return(1);
}

sxtinit()
{	register int psize;

	psize = btop(sxt_cnt*LINKSIZE);
	availsmem -= psize;
	availrmem -= psize;

	if (availsmem < tune.t_minasmem || availrmem < tune.t_minarmem) {
		availsmem += psize;
		availrmem += psize;
		printf("sxt cannot allocate link buffers\n");
		return;
	}

	if ((sxtbuf = (char *)kvalloc(psize, PG_ALL, -1)) == NULL)
		printf("sxt cannot allocate link buffers\n");
}
