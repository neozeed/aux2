#ifndef lint	/* .../sys/COMMON/os/stream.c */
#define _AC_NAME stream_c
#define _AC_NO_MAIN "@(#) Copyright (c) 1984-85 AT&T-IS, 1985-87 UniSoft Corporation, All Rights Reserved.  {Apple version 2.1 89/10/13 19:24:16}"
#include <apple_notice.h>

#ifdef _AC_HISTORY
  static char *sccsid = "@(#)Copyright Apple Computer 1987\tVersion 2.1 of stream.c on 89/10/13 19:24:16";
#endif		/* _AC_HISTORY */
#endif		/* lint */

#define _AC_MODS
/*	@(#)stream.c	UniPlus 2.1.7	*/
/*   Copyright (c) 1984 AT&T	and UniSoft Systems */
/*     All Rights Reserved  	*/

/*   THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF AT&T and UniSoft Systems */
/*   The copyright notice above does not evidence any   	*/
/*   actual or intended publication of such source code.	*/

#include "sys/types.h"
#include "sys/param.h"
#include "sys/dir.h"
#include "sys/signal.h"
#include "sys/mmu.h"
#include "sys/page.h"
#include "sys/time.h"
#include "sys/user.h"
#include "sys/sysmacros.h"
#include "sys/stropts.h"
#include "sys/stream.h"
#include "sys/conf.h"
#include "sys/var.h"
#include "sys/debug.h"

#ifndef ASSERT
#define ASSERT(x)
#endif ASSERT

extern char qrunflag;
extern char queueflag;


mblk_t	*mbfreelist;			/* allocate freelist */
dblk_t	*dbfreelist[NCLASS];		/* allocate freelist heads */

unsigned short	rbsize[] = { 4, 16, 64, 128, 256, 512, 1024, 2048, 4096 };	/* real block sizes */

unsigned short	bsize[] = { 4, 16, 64, 100, 200, 300, 600, 1200, 2400 };	/* size for q limits */

char	dblkflag;   /* on if can not allocate data blocks */

struct	queue *qhead;			/* head of queues to run */
struct	queue *qtail;			/* last queue */


/*
 * Allocate a block
 * Get a block of at least size bytes big.
 * If can not get it in class that fits then
 * try to get it in one class up
 * Return pointer to dblock desc or NULL if error
 * or no blocks available.
 */

mblk_t *
allocb(size, pri)
register size;
{
	register dblk_t *databp;
	register mblk_t *bp;
	register s;
	register class;

	s = splstr();


	/*
	 * If allocating blocks in init produced
	 * an error then return NULL
	 */
	if (dblkflag) 
	{
		splx(s);
		return(NULL);
	}


	/*
	 * get class of buffer needed.  If no class fits return NULL.
	 */
	class = getclass(size);
	if (class >= NCLASS) {
		splx(s);
		return(NULL);
	}

	/* 
	 * try to get a data block.  If none is available try the next 
	 * large class.  Return NULL if unable to allocate.
	 */
	if (!(databp = dbfreelist[class]))
		if (class == (NCLASS - 1) || !(databp = dbfreelist[++class])) {
			splx(s);
			return(NULL);
		}
	dbfreelist[class] = databp->db_freep;
	databp->db_freep = NULL;

	/*
	 * Have got a data block, try for message block.  Message blocks are
	 * also allocated in dupb() below.
	 */
	if (!(bp = mbfreelist)) {
		/*
		 * couldn't get message block desc
		 * Free data block
		 */
		dbfreelist[class] = databp;
		splx(s);
		return(NULL);
	}
	mbfreelist = bp->b_next;
	splx(s);

	/*
	 * initialize message block and
	 * data block descriptors
	 */
	bp->b_next = NULL;
	bp->b_cont = NULL;
	bp->b_datap = databp;
	bp->b_rptr = databp->db_base;
	bp->b_wptr = databp->db_base;
	bp->b_datap->db_lim = databp->db_base+rbsize[class];
	databp->db_class = class;
	databp->db_type = M_DATA;
	/*
	 * set reference count to 1 (first use)
	 */
	databp->db_ref = 1;
#ifdef HOWFAR 
#ifdef STRDEBUG
	bp->b_debug = 0;
#endif
#endif
	return(bp);
}


/*
 * Return message block to its free list.
 * If its data block reference count = 1, 
 * also return the data block to the free
 * list for its class.
 */

freeb(bp)
register mblk_t *bp;
{
	register s;
	register class;

	ASSERT(bp);

#ifdef HOWFAR 
#ifdef STRDEBUG
	ASSERT((bp->b_debug&MM_FREE) == 0);
	ASSERT((bp->b_debug&MM_Q) == 0);
	bp->b_debug |= MM_FREE;
#endif
#endif

	if (!bp) return;

	class = bp->b_datap->db_class;
	
	s = splstr();

	bp->b_next = mbfreelist;
	mbfreelist = bp;
	if (bp->b_datap->db_ref <= 1) {
		bp->b_datap->db_ref = 0;
		bp->b_datap->db_freep = dbfreelist[class];
		dbfreelist[class] = bp->b_datap;
		wakeup(&rbsize[class]);
	} else
		bp->b_datap->db_ref--;
	bp->b_datap = NULL;
	splx(s);
	return;
}



/*
 * Free all message blocks in message (uses freeb).  
 * The message may be NULL.
 */

freemsg(bp)
register mblk_t *bp;
{
	mblk_t *next;

	while (bp) {
	    next = bp->b_cont;
	    freeb(bp);
	    bp = next;
	}
}


/*
 * Duplicate a message block
 *
 * Allocate a message block and assign proper
 * values to it (read and write pointers)
 * and link it to existing data block.
 * Increment reference count of data block.
 */

mblk_t *
dupb(bp)
register mblk_t *bp;
{
	register s;
	register mblk_t *bp1;

	ASSERT(bp);

#ifdef HOWFAR 
#ifdef STRDEBUG
	ASSERT((bp->b_debug&MM_FREE) == 0);
	ASSERT((bp->b_debug&MM_Q) == 0);
#endif
#endif
	s = splstr();
	if ( !(bp1 = mbfreelist) ) {
		splx(s);
		return(NULL);
	}
	mbfreelist = bp1->b_next;
	splx(s);
	bp1->b_next = NULL;
	bp1->b_cont = NULL;
	bp1->b_rptr = bp->b_rptr;
	bp1->b_wptr = bp->b_wptr;
	bp1->b_datap = bp->b_datap;
	bp1->b_datap->db_ref++;
#ifdef HOWFAR 
#ifdef STRDEBUG
	bp1->b_debug = 0;
#endif
#endif
	return(bp1);
}


/*
 * Duplicate a message (uses dupb)
 *
 * If dupb returns a NULL then free message
 * return NULL
 */

mblk_t *
dupmsg(bp)
register mblk_t *bp;
{
	register mblk_t *head = NULL;
	register mblk_t *bp1;

	if (!bp || !(bp1 = head = dupb(bp))) return(NULL);

	while (bp->b_cont) {
	     if (!(bp1->b_cont = dupb(bp->b_cont))) {
		    freemsg(head);
		    return(NULL);
	     }
	     bp1 = bp1->b_cont;
	     bp = bp->b_cont;
	}
	return(head);
}



/*
 * Get a message off head of queue.
 *
 * If the queue is empty then set the QWANTR flag to indicate
 * that it is wanted by a reader (the queue's service procedure).
 * If the queue is not empty remove the first message,
 * turn off the QWANTR flag, substract the weighted size
 * of the message from the queue count, and turn off the QFULL
 * flag if the count is less than the high water mark.
 *
 * In all cases, if the QWANTW flag is set (the upstream
 * module wants to write to the queue) AND the count is
 * below the low water mark, enable the upstream queue and
 * turn off the QWANTW flag.
 *
 * A pointer to the first message on the queue (if any) is
 * returned, or NULL (if not).
 */

mblk_t *
getq(q)
register queue_t *q;
{
	register mblk_t *bp;
	register mblk_t *tmp;
	register s;

	ASSERT(q);

	s = splstr();
	if (!(bp = q->q_first)) q->q_flag |= QWANTR;
	else {
		if (!(q->q_first = bp->b_next))	q->q_last = NULL;
		else q->q_first->b_prev = NULL;
		bp->b_next = NULL;
		for (tmp = bp; tmp; tmp = tmp->b_cont)
			q->q_count -= bsize[tmp->b_datap->db_class];
		if (q->q_count < q->q_hiwat)
			q->q_flag &= ~QFULL;
		q->q_flag &= ~QWANTR;
	}

	if (q->q_count<=q->q_lowat && q->q_flag&QWANTW) {
		q->q_flag &= ~QWANTW;
		/* find nearest back queue with service proc */
		for (q = backq(q); q && !q->q_qinfo->qi_srvp; q = backq(q));
		if (q) qenable(q);
	}
	splx(s);
#ifdef HOWFAR 
#ifdef STRDEBUG
	if (bp) {
		register mblk_t *m;

		m = bp;
		while (m) {
			ASSERT((m->b_debug&MM_FREE) == 0);
			m->b_debug &= ~MM_Q;
			m = m->b_cont;
		}
	}
#endif
#endif
	return(bp);
}

/*
 * Return 1 if the queue is not full.  If the queue is full, return
 * 0 (may not put message).  Set QWANTW flag (caller wants to write
 * to the queue).
 */
canput(q)
queue_t *q;
{
	ASSERT(q);
	while (q->q_next && !q->q_qinfo->qi_srvp) q = q->q_next;
	if (q->q_flag & QFULL) {
		q->q_flag |= QWANTW;
		return(0);
	}
	return(1);
}


/*
 * Put a message on a queue.  
 *
 * Messages are enqueued on a priority basis.  
 *
 * Add appropriate weighted data block sizes to queue count.
 * If queue hits high water mark then set QFULL flag.
 *
 * If QNOENAB is not set (putq is allowed to enable the queue),
 * enable the queue only if the message is PRIORITY,
 * or the QWANTR flag is set (indicating that the service procedure 
 * is ready to read the queue.
 */

putq(q, bp)
register queue_t *q;
register mblk_t *bp;
{
	register s;
	register mblk_t *tmp;
	register mcls = queclass(bp);

	ASSERT(q && bp);

#ifdef HOWFAR 
#ifdef STRDEBUG
	tmp = bp;
	while (tmp) {
		ASSERT((tmp->b_debug&MM_FREE) == 0);
		ASSERT((tmp->b_debug&MM_Q) == 0);
		tmp->b_debug |= MM_Q;
		tmp = tmp->b_cont;
	}
#endif
#endif
	s = splstr();

	/* 
	 * If queue is empty or queue class of message is less than
	 * that of the last one on the queue, tack on to the end.
	 */
	if ( !q->q_first || (mcls <= queclass(q->q_last)) ){
		if (q->q_first) {
			q->q_last->b_next = bp;
			bp->b_prev = q->q_last;
		} else {
			q->q_first = bp;
			bp->b_prev = NULL;
		}
		bp->b_next = NULL;
		q->q_last = bp;

	} else {
		register mblk_t *nbp = q->q_first;

		while (queclass(nbp) >= mcls) nbp = nbp->b_next;
		bp->b_next = nbp;
		bp->b_prev = nbp->b_prev;
		if (nbp->b_prev) nbp->b_prev->b_next = bp;
		else q->q_first = bp;
		nbp->b_prev = bp;
	}

#ifdef NOTDEF
	q->q_flag &= ~QWANTW;
#endif NOTDEF

	for (tmp = bp; tmp; tmp = tmp->b_cont)
		q->q_count += bsize[tmp->b_datap->db_class];
	if (q->q_count >= q->q_hiwat) q->q_flag |= QFULL;

	if (  !(q->q_flag & QNOENB) && 
	      ( (mcls > QNORM) || q->q_flag & QWANTR) )
		qenable(q);

	splx(s);
}


/*
 * Put stuff back at beginning of Q according to priority order.
 * See comment on putq above for details.
 */

putbq(q, bp)
register queue_t *q;
register mblk_t *bp;
{
	register s;
	register mblk_t *tmp;
	register mcls = queclass(bp);

	ASSERT(q && bp);
	ASSERT(bp->b_next == NULL);

#ifdef HOWFAR 
#ifdef STRDEBUG
	tmp = bp;
	while (tmp) {
		ASSERT((tmp->b_debug&MM_FREE) == 0);
		ASSERT((tmp->b_debug&MM_Q) == 0);
		tmp->b_debug |= MM_Q;
		tmp = tmp->b_cont;
	}
#endif
#endif
	s = splstr();

	/* 
	 * If queue is empty of queue class of message >= that of the
	 * first message, place on the front of the queue.
	 */
	if ( !q->q_first || (mcls >= queclass(q->q_first))) {
		bp->b_next = q->q_first;
		bp->b_prev = NULL;
		if (q->q_first) q->q_first->b_prev = bp;
		else q->q_last = bp;
		q->q_first = bp;
	}
	else {
		register mblk_t *nbp = q->q_first;

		while ((nbp->b_next) && (queclass(nbp->b_next) > mcls))
				nbp = nbp->b_next;

		if (bp->b_next = nbp->b_next) 
			nbp->b_next->b_prev = bp;
		else
			q->q_last = bp;
		nbp->b_next = bp;
		bp->b_prev = nbp;
	}

#ifdef NOTDEF
	q->q_flag &= ~QWANTW;
#endif

	for (tmp = bp; tmp; tmp = tmp->b_cont)
		q->q_count += bsize[tmp->b_datap->db_class];
	if (q->q_count >= q->q_hiwat) q->q_flag |= QFULL;

	if (  !(q->q_flag & QNOENB) && 
	      ( (mcls > QNORM) || q->q_flag & QWANTR) )
		qenable(q);

	splx(s);
}



/*
 * Flush a queue.  Flag == 0 indicates only data messages are to be 
 * flushed; flag == 1 indicates that all messages are to be flushed.
 */

flushq(q, flag)
register queue_t *q;
{
	register mblk_t *bp, *nbp;
	register s;

	ASSERT(q);

	s = splstr();
	bp = q->q_first;
	q->q_first = NULL;
	if (q->q_last)
		q->q_last->b_next = NULL;
	q->q_last = NULL;
	q->q_count = 0;
	q->q_flag &= ~QFULL;

	while (bp) {
		nbp = bp->b_next;
#ifdef HOWFAR 
#ifdef STRDEBUG
		{
			register mblk_t *m;

			m = bp;
			while (m) {
				m->b_debug &= ~MM_Q;
				m = m->b_cont;
			}
		}
#endif
#endif
		if (bp->b_datap->db_type != M_DATA
		    && !flag)
			putq(q, bp);
		else
			freemsg(bp);
		bp = nbp;
	}

	/*
	 * if queue has a module that wants to write to it then
	 * enable the module to write to it.
	 */
	if ((q->q_count <= q->q_lowat) && (q->q_flag & QWANTW)) {
		/* find nearest back queue with service proc */
		for (q = backq(q); q && !q->q_qinfo->qi_srvp; q = backq(q));
		if (q) qenable(q);
	}
	splx(s);
}



/*
 * Init routine run from main at boot time.  Memory allocation is
 * processor dependent.
 */

qinit(base)
register char *base;
{
	register dblk_t *databp;
	register mblk_t *msgbp;
	register i, j;
	register int size;


	INITLOCK(&qstrlock, 1);
	INITLOCK(&qblksema, 1);
	INITLOCK(&qqsema, 1);
	INITLOCK(&qrunsema, 1);


	/*
	 * Initialize buffers space and set up message block
	 * freelist and datablock freelists
	 */
	i = 0;
	for (j=0; j<v.v_nblk4096; i++, j++) {
		databp = &dblock[i];
		databp->db_class = 8;
		databp->db_base = base;
		base += 4096;
		databp->db_freep = dbfreelist[8];
		dbfreelist[8] = databp;
	}
	for (j=0; j<v.v_nblk2048; i++, j++) {
		databp = &dblock[i];
		databp->db_class = 7;
		databp->db_base = base;
		base += 2048;
		databp->db_freep = dbfreelist[7];
		dbfreelist[7] = databp;
	}
	for (j=0; j<v.v_nblk1024; i++, j++) {
		databp = &dblock[i];
		databp->db_class = 6;
		databp->db_base = base;
		base += 1024;
		databp->db_freep = dbfreelist[6];
		dbfreelist[6] = databp;
	}
	for (j=0; j<v.v_nblk512; i++, j++) {
		databp = &dblock[i];
		databp->db_class = 5;
		databp->db_base = base;
		base += 512;
		databp->db_freep = dbfreelist[5];
		dbfreelist[5] = databp;
	}
	for (j=0; j<v.v_nblk256; i++, j++) {
		databp = &dblock[i];
		databp->db_class = 4;
		databp->db_base = base;
		base += 256;
		databp->db_freep = dbfreelist[4];
		dbfreelist[4] = databp;
	}
	for (j=0; j<v.v_nblk128; i++, j++) {
		databp = &dblock[i];
		databp->db_class = 3;
		databp->db_base = base;
		base += 128;
		databp->db_freep = dbfreelist[3];
		dbfreelist[3] = databp;
	}
	for (j=0; j<v.v_nblk64; i++, j++) {
		databp = &dblock[i];
		databp->db_class = 2;
		databp->db_base = base;
		base += 64;
		databp->db_freep = dbfreelist[2];
		dbfreelist[2] = databp;
	}
	for (j=0; j<v.v_nblk16; i++, j++) {
		databp = &dblock[i];
		databp->db_class = 1;
		databp->db_base = base;
		base += 16;
		databp->db_freep = dbfreelist[1];
		dbfreelist[1] = databp;
	}
	for (j=0; j<v.v_nblk4; i++, j++) {
		databp = &dblock[i];
		databp->db_class = 0;
		databp->db_base = base;
		base += 4;
		databp->db_freep = dbfreelist[0];
		dbfreelist[0] = databp;
	}


	/*
	 * set up of message block freelist happens here
	 */
	for (j=0; j<nmblock; j++) {
		msgbp = &mblock[j];
		msgbp->b_next = mbfreelist;
#ifdef HOWFAR
#ifdef STRDEBUG
		msgbp->b_debug = MM_FREE;
#endif
#endif
		mbfreelist = msgbp;
	}
}



/*
 * allocate a pair of queues
 */

queue_t *
allocq()
{
	register queue_t *qp;
	register s;
	static queue_t zeroR =
	  { NULL,NULL,NULL,NULL,NULL,NULL,0,QUSE|QREADR,0,0,0,0,NULL};
	static queue_t zeroW =
	  { NULL,NULL,NULL,NULL,NULL,NULL,0,QUSE,0,0,0,0,NULL};

	s = splstr();
	for (qp = queue; qp < &queue[v.v_nqueue]; qp += 2) {
		if ((qp->q_flag & QUSE) == 0) {
			*qp = zeroR;
			*WR(qp) = zeroW;
			splx(s);
			return(qp);
		}
	}
	splx(s);
	return(NULL);
}




/*
 * Put a zero-length control message on the queue.
 */

putctl(q, type)
queue_t *q;
{
	register mblk_t *bp;

	if ((type == M_DATA) || !(bp = allocb(0,BPRI_HI)))
		return(0);
	bp->b_datap->db_type = type;
	(*q->q_qinfo->qi_putp)(q, bp);
	return(1);
}



/*
 * Put a control message with a single byte parameter on the queue.
 */

putctl1(q, type, param)
queue_t *q;
{
	register mblk_t *bp;

	if ((type == M_DATA) || !(bp = allocb(1,BPRI_HI)))
		return(0);
	bp->b_datap->db_type = type;
	*bp->b_wptr++ = param;
	(*q->q_qinfo->qi_putp)(q, bp);
	return(1);
}



/*
 * Put a control control message (0 length) on the queue via putq
 * (does not use the queue's put procedure as in putctl).
 * This is intended for internal use within a module and should
 * never be used to pass messages between modules!
 */

qpctl(q, type)
register queue_t *q;
{
	register mblk_t *bp;

	if ((type == M_DATA) || !(bp = allocb(0,BPRI_HI)))
		return(0);
	bp->b_datap->db_type = type;
	putq(q, bp);
	return(1);
}



/*
 * Put a control control message with a single byte parameter
 * on the queue via putq (does not use the queue's put procedure 
 * as in putctl).  This is intended for internal use within a module 
 * and should never be used to pass messages between modules!
 */

qpctl1(q, type, param)
register queue_t *q;
{
	register mblk_t *bp;

	if ((type == M_DATA) || !(bp = allocb(1,BPRI_HI)))
		return(0);
	bp->b_datap->db_type = type;
	*bp->b_wptr++ = param;
	putq(q, bp);
	return(1);
}



/*
 * return the queue upstream from this one
 */

queue_t *
backq(q)
register queue_t *q;
{
	ASSERT(q);

	q = OTHERQ(q);
	if (q->q_next) {
		q = q->q_next;
		return(OTHERQ(q));
	}
	return(NULL);
}



/*
 * Send a block back up the queue in reverse from this
 * one (e.g. to respond to ioctls)
 */

qreply(q, bp)
register queue_t *q;
mblk_t *bp;
{
	ASSERT(q && bp);

#ifdef HOWFAR 
#ifdef STRDEBUG
	ASSERT((bp->b_debug&MM_FREE) == 0);
	ASSERT((bp->b_debug&MM_Q) == 0);
#endif
#endif
	q = OTHERQ(q);
	ASSERT(q->q_next);
	(*q->q_next->q_qinfo->qi_putp)(q->q_next, bp);
}




/*
 * Streams Queue Scheduling
 * 
 * Queues are enabled through qenable() when they have messages to 
 * process.  They are serviced by queuerun(), which runs each enabled
 * queue's service procedure.  The call to queuerun() is processor
 * dependent - the general principle is that it be run whenever a queue
 * is enabled but before returning to user level.  For system calls,
 * the function runqueues() is called if their action causes a queue
 * to be enabled.  For device interrupts, queuerun() should be
 * called before returning from the last level of interrupt.  Beyond
 * this, no timing assumptions should be made about queue scheduling.
 */


/*
 * Enable a queue: put it on list of those whose service procedures are
 * ready to run and set up the scheduling mechanism.
 */

qenable(q)
register queue_t *q;
{
	register s;

	ASSERT(q);

	if (!q->q_qinfo->qi_srvp) return;

	s = splstr();
	/*
	 * Do not place on run queue if already enabled.
	 */
	if (q->q_flag & QENAB) {
		splx(s);
		return;
	}

	/*
	 * mark queue enabled and place on run list
	 */
	q->q_flag |= QENAB;

	if (!qhead) qhead = q;
	else qtail->q_link = q;
	qtail = q;
	q->q_link = NULL;

	/*
	 * set up scheduling mechanism
	 */
	setqsched();
	splx(s);
}



/*
 * Run the service procedures of each enabled queue
 *	-- must not be reentered
 *
 * Called by service mechanism (processor dependent) if there
 * are queues to run.  The mechanism is reset.
 */

queuerun()
{
	register queue_t *q;
	register s;

	s = splstr();
	while (q = qhead) {
		if (!(qhead = q->q_link)) qtail = NULL;
		q->q_flag &= ~QENAB;
		ASSERT(q->q_qinfo->qi_srvp);
		SPL0();				/* <-Unisoft Change(see clock)*/

		(*q->q_qinfo->qi_srvp)(q);

		splstr();
	}
	qrunflag = 0;
	splx(s);
}

/*
 * Function to kick off queue scheduling for those system calls
 * that cause queues to be enabled (read, recv, write, send, ioctl).
 */
runqueues()
{
	register s;

	s = splhi();
	if (qrunflag & !queueflag) {
		queueflag = 1;
		splx(s);
		queuerun();
		queueflag = 0;
		return;
	}
	splx(s);
}


/*
 * Copies data from message block to newly allocated message block and
 * data block.  (dupb(), in contrast, shares the data block.)
 * The new message block pointer is returned if successful, NULL
 * if not.
 */

mblk_t *
copyb(bp)
register mblk_t *bp;
{
	register mblk_t *bp1;
	register dblk_t *dp, *ndp;

	ASSERT(bp);

#ifdef HOWFAR 
#ifdef STRDEBUG
	ASSERT((bp->b_debug&MM_FREE) == 0);
	ASSERT((bp->b_debug&MM_Q) == 0);
#endif
#endif
	dp = bp->b_datap;
	if (!(bp1 = allocb(dp->db_lim - dp->db_base,BPRI_MED)))
		return(NULL);
	ndp = bp1->b_datap;
	ndp->db_type = dp->db_type;
	bp1->b_rptr = ndp->db_base + (bp->b_rptr - dp->db_base);
	bp1->b_wptr = ndp->db_base + (bp->b_wptr - dp->db_base);
	bcopy(dp->db_base, ndp->db_base, dp->db_lim - dp->db_base);
	return(bp1);
}


/*
 * Copies data from a message to a newly allocated message by 
 * copying each block in the message (uses copyb, not dupb).
 * A pointer to the new message is returned if successful,
 * NULL if not.
 */

mblk_t *
copymsg(bp)
register mblk_t *bp;
{
	register mblk_t *head = NULL;
	register mblk_t 	 *bp1;

	if (!bp || !(bp1 = head = copyb(bp))) return(NULL);

	while (bp->b_cont) {
	     if (!(bp1->b_cont = copyb(bp->b_cont))) {
		    freemsg(head);
		    return(NULL);
	     }
	     bp1 = bp1->b_cont;
	     bp = bp->b_cont;
	}
	return(head);

}




/* 
 * link a message block to tail of message.
 */

linkb(mp, bp)
register mblk_t *mp;
register mblk_t *bp;
{

	ASSERT(mp && bp);
#ifdef HOWFAR 
#ifdef STRDEBUG
	ASSERT((mp->b_debug&MM_FREE) == 0);
	ASSERT((mp->b_debug&MM_Q) == 0);
	ASSERT((bp->b_debug&MM_FREE) == 0);
	ASSERT((bp->b_debug&MM_Q) == 0);
#endif
#endif

	for (;mp->b_cont; mp = mp->b_cont);
	mp->b_cont = bp;
}


/*
 * Unlink a message block from the head of a message and
 * return a pointer to the remainder of the message, which 
 * may be NULL.
 */

mblk_t *
unlinkb(bp)
register mblk_t *bp;
{
	register mblk_t *bp1;

	ASSERT(bp);

#ifdef HOWFAR 
#ifdef STRDEBUG
	ASSERT((bp->b_debug&MM_FREE) == 0);
	ASSERT((bp->b_debug&MM_Q) == 0);
#endif
#endif
	bp1 = bp->b_cont;
	bp->b_cont = NULL;
	return(bp1);
}


/* 
 * remove a message block "bp" from message "mp"
 *
 * If the block is in the message, it is removed and a pointer
 * to the resulting message (which may be NULL) is returned.
 * If the block is not found in the message, -1 is returned.
 */

mblk_t *
rmvb(mp,bp)
register mblk_t *mp;
register mblk_t *bp;
{
	register mblk_t *mp1 = mp;
	register mblk_t *bp1 = NULL;

#ifdef HOWFAR 
#ifdef STRDEBUG
	ASSERT((mp->b_debug&MM_FREE) == 0);
	ASSERT((mp->b_debug&MM_Q) == 0);
	ASSERT((bp->b_debug&MM_FREE) == 0);
	ASSERT((bp->b_debug&MM_Q) == 0);
#endif
#endif

	for (; mp1; mp1 = mp1->b_cont) {
		if (mp1 == bp) {
			if (bp1) {
				bp1->b_cont = mp1->b_cont;
				mp1->b_cont = NULL;
				return(mp);
			} else {
				bp1 = mp1->b_cont;
				mp1->b_cont = NULL;
				return(bp1);
			}
		}
		bp1 = mp1;
	}
	return((mblk_t *)-1);
}

/*
 * get number of data bytes in message
 */

msgdsize(bp)
register mblk_t *bp;
{
	register int count = 0;

#ifdef HOWFAR 
#ifdef STRDEBUG
	ASSERT((bp->b_debug&MM_FREE) == 0);
#endif
#endif
	for (; bp; bp = bp->b_cont)
		if (bp->b_datap->db_type == M_DATA) {
			ASSERT(bp->b_wptr >= bp->b_rptr);
			count += bp->b_wptr - bp->b_rptr;
		}
	return(count);
}


/* 
 * find module
 * 
 * return index into fmodsw
 * or -1 if not found
 */

findmod(name)
register char *name;
{
	register int i, j;

	for (i = 0; i < fmodcnt; i++)
		for (j = 0; j < FMNAMESZ + 1; j++) {
			if (fmodsw[i].f_name[j] != name[j])
				break;
			if (name[j] == '\0')
				return(i);
		}
	return(-1);
}


/*
 * get class of buffer.  Returns the smallest class whose size is
 * greater than the requested size.  If the requested size exceeds
 * any class size, NCLASS is returned (an invalid class).
 */

getclass(size)
register size;
{
	register class;

	for (class = 0; class < NCLASS; class++)
		if (size <= rbsize[class])
			break;
	return(class);
}


/* 
 * return number of messages on queue
 */

qsize(qp)
register queue_t *qp;
{
	register count = 0;
	register mblk_t *mp;

	ASSERT(qp);

	for ( mp = qp->q_first; mp; mp = mp->b_next)
		count++;

	return(count);
}
