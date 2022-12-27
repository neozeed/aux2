/*
 * @(#)sm.aux.c  {Apple version 1.21 90/05/04 11:05:02}
 *
 * Copyright (c) 1987, 1988, 1989 by Apple Computer, Inc.
 * All Rights Reserved.
 *
 * THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF APPLE COMPUTER, INC.
 * The copyright notice above does not evidence any actual or
 * intended publication of such source code.
 */

#include "sys/types.h"
#include "sys/uio.h"
#include "sys/ioctl.h"
#include "sys/errno.h"
#include "sys/buf.h"
#include "sys/param.h"
#include "sys/sysmacros.h"
#include "sys/file.h"
#include "sys/via6522.h"
#include "sys/oss.h"
#include "sys/uconfig.h"
#include "sys/mmu.h"
#include "sys/page.h"
#include "sys/map.h"
#include "sys/systm.h"
#include "sys/tuneable.h"
#include "sys/time.h"
#include "sys/user.h"
#include "sm.h"
#include "sys/sm_aux.h"
#include "sm.privdesc.h"
#include "samp.desc.h"
#include "sm.asc.h"


int		sampleType;	/* sample buffering technique */
int		synthType;	/* type of sampled synth being used - MIN_SAMP or MIN_RAW */
int		resetChan;	/* used to reset sound driver */
unsigned char	*loopRepeat;
unsigned char	*sampBuf;	/* ptr to waveform buffers for samp and raw synths */
unsigned char	*waveBuf;	/* ptr to waveform buffers for wave synths */
struct rawSndCtl rawSndInfo;
struct proc *snd_proc;
extern int sndintr_cnt;
extern int catch_asc_timeout();
extern int (*lvl2funcs[])();


/*
 *	CallBack queuing routines
 */

/* initCBQueue -
 *
 *		Initialize a CallBack queue to be empty
 */
initCBQueue(auxCh)
auxSndChPtr	auxCh;
{
	auxCh->qHead = -1;
	auxCh->qTail = 0;
	auxCh->qLength = AuxQLength;
}

/* emptyCBQueue -
 *
 *		answer if a CallBack queue is empty
 */
#define emptyCBQueue(auxCh)	(auxCh->qHead == -1)

/* fullCBQueue -
 *
 *		answer if a CallBack queue is full
 */
#define fullCBQueue(ch)	(auxCh->qHead == auxCh->qTail)

/* insertInCBQue -
 *		Adds a command to a CallBack queue if it isn't full.
 *		The command is added to the end and the result is
 *		true if the queue WAS full and the command wasn't added.
 */
Boolean
insertInCBQue(auxCh, newCmd)
auxSndChPtr	auxCh;
struct SndCommand	*newCmd;
{
	int	saveStatus;
	
	saveStatus = spl6();

	if (fullCBQueue(auxCh))
	{
	splx(saveStatus);
	return(true);
	}
	
	if (auxCh->qHead == -1)
		auxCh->qHead = auxCh->qTail;

	auxCh->queue[auxCh->qTail++] = *newCmd;
	if (auxCh->qTail == auxCh->qLength)
		auxCh->qTail = 0;

	splx(saveStatus);
	return(false);
}

/* nextFromCBQue -
 *		Remove the next command from a CallBack queue.  The
 *		command is copied into the callers command.  If the
 *		queue WAS empty then False is returned.
 */
Boolean
nextFromCBQue(auxCh, nextComm)
auxSndChPtr	auxCh;
struct SndCommand	*nextComm;
{
	int	saveStatus;
	
	saveStatus = spl6();
	
	if (emptyCBQueue(auxCh))
	{
		splx(saveStatus);
		 return(true);
	}

	*nextComm = auxCh->queue[auxCh->qHead++];
	if (auxCh->qHead == auxCh->qLength)
		auxCh->qHead = 0;

	if (auxCh->qHead == auxCh->qTail)
		auxCh->qHead = -1;

	splx(saveStatus);
	return(false);
}


/*
 *	Standard Unix driver routines
 */

sndinit()
{
    register struct SndChannel	*ch;
    register struct auxSndCh	*auxCh;
    register int physpages;

    InitTimer();
    SNDIER(0);
    CLRSNDIRQ();

    ((ASCSpace *) ASC)->ctrl.mode = ASCQuietMode;

    for (ch = channelList; ch < &channelList[TOTSYNTHS]; ch++)
    {
	ch->next = 0;
	ch->wait = 0;
	ch->modifierChain = 0;
	ch->callBack = 0;
	ch->flags = 0;
    }

    for (auxCh = auxChanList; auxCh < &auxChanList[TOTSYNTHS]; auxCh++)
    {
	auxCh->aux_snd_fd = -1;
	auxCh->aux_state = 0;
    }

    sampBuf = (unsigned char *) 0;	/* init samp buffer as unallocated */
    waveBuf = (unsigned char *) 0;	/* init wave buffer as unallocated */

    synthType = 0;
    resetChan = 0;

    rawSndInfo.sampleRate = 0x10000;	/* defaults until changed by raw ioctl */
    rawSndInfo.flags = 0;
    sndintr_cnt = -1;

    return(0);
}

/*
 * Allocate snd waveform buffers - these stay around forever
 * once they have been allocated.
 */
unsigned char *
getSndBuf(physpages)
int physpages;
{
    unsigned char *buf;

    availrmem -= physpages;
    availsmem -= physpages;
	
    if (availrmem < tune.t_minarmem || availsmem < tune.t_minasmem)
    {
	availrmem += physpages;
	availsmem += physpages;
	return((unsigned char *) 0);
    }
	
    while ((buf = (unsigned char *)kvalloc((physpages), PG_ALL, -1)) == NULL)
    {
	mapwant(sptmap)++;
	sleep(sptmap, PMEM);
    }
    Debug (DB_ALWAYS, ("alloc sndBuf @ 0x%x with %d physpages.\n",buf,physpages));
    return((unsigned char *) buf);
}

/*
 * This creates a SoundChannel and allocates the attackBuf
 * and sustainBuf if the synth is a sampler.
 */
sndopen(dev, flag)
dev_t dev;
int   flag;
{
    register SndChannelPtr ch;
    register auxSndChPtr auxCh;
    register struct SampDesc	*myDesc;
    register struct PlayBlock	*currPB;
    int physpages;

    if (minor(dev) == MIN_RST)
    {
	resetChan++;
	return(0);
    }

    if ((ch = &channelList[minor(dev)]) >= &channelList[TOTSYNTHS])
    {
	Debug (DB_ALWAYS, ("Invalid sndCh min number:0x%x\n",minor(dev)));
	return(ENODEV);
    }

    auxCh = &auxChanList[minor(dev)];

    if (auxCh->aux_state & INUSE)	/* this minor already open */
    {
	Debug (DB_ALWAYS, ("sndCh min number 0x%x busy.\n",minor(dev)));
	return(EBUSY);
    }

    /*
     *	If the channel was closed while busy and it is still
     *	busy, block until done.
     */
    Debug (DB_ALWAYS, ("sndopen min %d: (0x%x) = 0x%x\n",minor(dev),&ch->flags,ch->flags));
    while (ch->flags & ChClsOnQEmpty)
    {
	Debug (DB_ALWAYS, ("sleep in sndopen\n"));
	(void)sleep((caddr_t)(&ch->flags), PZERO);
	if (resetChan)
		return(EIO);
	Debug (DB_ALWAYS, ("wokeup from sndopen\n"));
    }

    switch (minor(dev))
    {
    case MIN_NOTE:
	    if ((auxChanList[MIN_WAVE1].aux_state & INUSE) ||
			(auxChanList[MIN_SAMP].aux_state & INUSE) ||
			(auxChanList[MIN_RAW].aux_state & INUSE))
	    {
			Debug (DB_ALWAYS, ("Cannot open - Other than NOTE INUSE\n"));
			return(EMFILE);
	    }

	    auxCh->aux_beg = 0;
	    auxCh->aux_end = 0;
	    synthType = MIN_NOTE;
	    break;

    case MIN_WAVE1:		/* fill ASC wave buffer */
    case MIN_WAVE2:
    case MIN_WAVE3:
    case MIN_WAVE4:
	    /*
	     *	On 1st access, allocate 1 page for wave buffers.
	     */
	    if (waveBuf == 0)
	    {
	       	if ((waveBuf = (unsigned char *)getSndBuf(1)) == 0)
		{
		    Debug (DB_ALWAYS, ("Cannot open - no waveBuf.\n"));
		    return(E2BIG);
		}
	    }

	    if ((auxChanList[MIN_NOTE].aux_state & INUSE) ||
		(auxChanList[MIN_SAMP].aux_state & INUSE) ||
		(auxChanList[MIN_RAW].aux_state & INUSE))
	    {
			Debug (DB_ALWAYS, ("Cannot open - Other than WAVE INUSE\n"));
			return(EMFILE);
	    }

	    auxCh->aux_beg = waveBuf;
	    auxCh->aux_end = auxCh->aux_beg + PAGESIZE - 1;
	    synthType = MIN_WAVE1;
	    break;

    case MIN_SAMP:
	    /*
	     *	On 1st access, allocate sampled sound buffers (2 @ 64K)
	     */
	    if (sampBuf == 0)
	    {
	       	if ((sampBuf = (unsigned char *)getSndBuf( ((ATTACKBUFSIZ + SUSTBUFSIZ) / PAGESIZE) )) == 0)
		{
		    Debug (DB_ALWAYS, ("Cannot open - no sampBuf.\n"));
		    return(E2BIG);
		}
	    }

	    if ((auxChanList[MIN_NOTE].aux_state & INUSE) ||
		(auxChanList[MIN_WAVE1].aux_state & INUSE) ||
		(auxChanList[MIN_RAW].aux_state & INUSE))
	    {
			Debug (DB_ALWAYS, ("Cannot open - Other than SAMP INUSE\n"));
			return(EMFILE);
	    }

	    synthType = MIN_SAMP;
	    auxCh->aux_beg = sampBuf;
	    auxCh->aux_end = auxCh->aux_beg + ATTACKBUFSIZ + SUSTBUFSIZ - 1;
	    break;

    case MIN_RAW:
	    /*
	     *	On 1st access, allocate sampled sound buffers (2 @ 64K)
	     */
	    if (sampBuf == 0)
	    {
	       	if ((sampBuf = (unsigned char *)getSndBuf( ((ATTACKBUFSIZ + SUSTBUFSIZ) / PAGESIZE) )) == 0)
		{
		    Debug (DB_ALWAYS, ("Cannot open - no sampBuf.\n"));
		    return(E2BIG);
		}
	    }

	    if ((auxChanList[MIN_NOTE].aux_state & INUSE) ||
		(auxChanList[MIN_WAVE1].aux_state & INUSE) ||
		(auxChanList[MIN_SAMP].aux_state & INUSE))
	    {
			Debug (DB_ALWAYS, ("Cannot open - Other than RAW INUSE\n"));
			return(EMFILE);
	    }

	    synthType = MIN_RAW;
	    auxCh->aux_beg = sampBuf;
	    auxCh->aux_end = auxCh->aux_beg + ATTACKBUFSIZ + SUSTBUFSIZ - 1;
	    auxCh->aux_min_num = minor(dev);
	    auxCh->aux_state = INUSE;

	    if (ASCAlloc() == false)
	    {
	        u.u_rval1 = -1;
		return(EBUSY); /* can't get the sound chip */
	    }

	    myDesc = &SampDescTbl[auxCh->aux_min_num];

	    myDesc->nowPlaying = &(myDesc->blocks[0]);
	    myDesc->reallyQuiet = true;
	    myDesc->block = false;
	    myDesc->whyTickled = NoReason;

	    myDesc->nowPlaying->playState = Quiet;
	    myDesc->nowPlaying->wantsToQuit = false;

	    myDesc->nowPlaying->getSamp = auxCh->aux_beg;
	    myDesc->nowPlaying->putSamp = auxCh->aux_beg;
	    myDesc->nowPlaying->currEnd = auxCh->aux_end;
	    myDesc->nowPlaying->sndStart = auxCh->aux_beg;
	    myDesc->nowPlaying->sndEnd = auxCh->aux_end;
	    myDesc->nowPlaying->loopStart = 0;
	    myDesc->nowPlaying->loopEnd = 0;
	    myDesc->nowPlaying->rate = rawSndInfo.sampleRate;
	    myDesc->nowPlaying->totalLength = ATTACKBUFSIZ + SUSTBUFSIZ;
	    myDesc->nowPlaying->fraction = 0;
	    currPB = myDesc->nowPlaying;

	    ch->flags = ChQuiet;
	    auxCh->aux_state &= ~(LOOPFILL | LASTFILL | FINISHED);
	    auxCh->aux_state |= FIRSTFILL;
	    auxCh->aux_cnt = 0;
	    sampleType = X_RAW;

	    ASCInit();
	    return(0);;

    default:
	    break;
    }

    auxCh->aux_min_num = minor(dev);
    snd_proc = u.u_procp;

    auxCh->aux_state = INUSE;
    auxCh->aux_cnt = 0;
    ch->qLength = StdQLength;
    initCBQueue(auxCh);		/* initialize callBack queue */

    SndNewChannel(ch, minor(dev), 0, 0);

    return(0);
}


sndclose(dev, flag)
dev_t dev;
int   flag;
{
    register SndChannelPtr ch;
    register auxSndChPtr auxCh;
    register struct SampDesc	*desc;
    register struct PlayBlock	*currPB;
    register int s;

    ch = &channelList[minor(dev)];
    auxCh = &auxChanList[minor(dev)];
    desc = &SampDescTbl[auxCh->aux_min_num];
    currPB = desc->nowPlaying;

    /*
     * We go ahead and return to the user even if the
     * snd queue is not empty and the ASC is still busy.
     * We will continue to process the queue until we
     * are done and then shut down the channel for real.
     * If we get another sndopen() before we are done,
     * we block there until finished.
     */

    switch(minor(dev))
    {
	case MIN_NOTE:
	case MIN_WAVE1:
	case MIN_WAVE2:
	case MIN_WAVE3:
	case MIN_WAVE4:
	    if ( !(ch->flags & ChQuiet) || (ch->qHead != -1))
	    {
		Debug (DB_ALWAYS, ("setting ChClsOnQE. flags=0x%x qHead=%x\n",ch->flags,ch->qHead));
		ch->flags |= ChClsOnQEmpty;
	    }
	    else
	    {
		Debug (DB_ALWAYS, ("Normal close\n"));
		ch->flags = 0;
		auxCh->aux_state = 0;
		disposChannel(ch);
	    }
	    break;

	case MIN_SAMP:
	    if ( (!(ch->flags & ChQuiet) || (ch->qHead != -1)) && !(auxCh->aux_state & STOPPED))
	    {
		Debug (DB_ALWAYS, ("setting ChClsOnQE. flags=0x%x qHead=%x\n",ch->flags,ch->qHead));
		ch->flags |= ChClsOnQEmpty;
	    }
	    else
	    {
		Debug (DB_ALWAYS, ("Normal close\n"));
		auxCh->aux_beg = 0;
		auxCh->aux_end = 0;
		ch->flags = 0;
		auxCh->aux_state = 0;
		disposChannel(ch);
	    }
	    break;

    	case MIN_RAW:
	    s = spl6();
	    if ( !(ch->flags & ChQuiet) && !(auxCh->aux_state & STOPPED))
	    {
		Debug (DB_ALWAYS, ("setting ChClsOnQE. flags=0x%x aux_state=%x\n",ch->flags,auxCh->aux_state));
		ch->flags |= ChClsOnQEmpty;
	    }
	    else
	    {
		Debug (DB_ALWAYS, ("Normal close\n"));
		ch->flags &= ~ChClsOnQEmpty;
		ch->flags |= ChQuiet;	/* stop sending data */
		currPB->playState = Quiet;
		currPB->putSamp = auxCh->aux_beg;
		auxCh->aux_state = 0;
		ASCFree(ch);
	    }
	    splx(s);
	    break;

	case MIN_RST:
	    resetChan = 0;
	    break;

    } /* end switch */

    return(0);
}



sndwrite(dev, uio)
dev_t dev;
register struct uio *uio;
{
    register SndChannelPtr ch;
    register auxSndChPtr auxCh;
    register struct iovec *iov;
    register int count;
    register struct SampDesc	*desc;
    register struct PlayBlock	*currPB;
    register int s;

    ch = &channelList[minor(dev)];
    auxCh = &auxChanList[minor(dev)];
    desc = &SampDescTbl[auxCh->aux_min_num];
    currPB = desc->nowPlaying;

    if (minor(dev) == MIN_RST)	/* can reset sound driver by writing to this dev */
    {
	InitTimer();
	ASCFree(ch);

	((ASCSpace *) ASC)->ctrl.mode = ASCQuietMode;

	for (ch = channelList; ch < &channelList[TOTSYNTHS]; ch++)
	{
	    STOP_ASC_INTS(ch);
	    wakeup(&ch->flags);  /* cancel any sndopen() sleeps */
	    ch->next = 0;
	    ch->wait = 0;
	    ch->modifierChain = 0;
	    ch->callBack = 0;
	    ch->flags = 0;
	}

	for (auxCh = auxChanList; auxCh < &auxChanList[TOTSYNTHS]; auxCh++)
	{
	    if (auxCh->aux_state & ASLEEP)
	    {
		auxCh->aux_state = 0;
		STOP_ASC_INTS(ch);
		wakeup(&auxCh->aux_cnt);  /* cancel any sndwrite() sleeps */
		ASCFree(ch);
	    }
	    auxCh->aux_snd_fd = -1;
	    auxCh->aux_state = 0;
	}

	synthType = 0;
    }

    Debug (DB_ALWAYS, ("synth.beg = 0x%x  synth.end = 0x%x\n",auxCh->aux_beg,auxCh->aux_end));
    if ((synthType < MIN_WAVE1) || (synthType > MIN_RAW)) /* make sure a sample/raw/wave synth has been opened */
    {
	Debug (DB_ALWAYS, ("sndwrite: synthType = Note\n"));
	return(-1);
    }

    /* check if there has been a SND_HDR ioctl lately */
    if (auxCh->aux_state & NEWSAMPLE)
    {
	auxCh->aux_state &= ~NEWSAMPLE;
	currPB->putSamp = auxCh->aux_beg;
	auxCh->aux_cnt = 0;
    }

    while ((uio->uio_resid) && (auxCh->aux_state & INUSE))
    {
	Debug (DB_ALWAYS, ("resid = %d\n",uio->uio_resid));
	iov = uio->uio_iov;
	if (iov->iov_len == 0)
	{
	    uio->uio_iov++;
	    uio->uio_iovcnt--;
	    continue;
	}


	count = SUSTBUFSIZ - auxCh->aux_cnt;
	if ((currPB->putSamp + count) >= auxCh->aux_end)
		count = auxCh->aux_end - currPB->putSamp;

	count = min(iov->iov_len, count);

	if (s = uiomove(currPB->putSamp, count, UIO_WRITE, uio))
	{
	    Debug (DB_ALWAYS, ("sndwr: uiomove err rtn\n"));
	    return(s);		/* uiomove() returns errno */
	}

	s = spl6();

	auxCh->aux_cnt += count; /* inc number of unplayed bytes in buffer - dec'd in sndintr() */

	Debug (DB_ALWAYS, (" wcnt:0x%x ",auxCh->aux_cnt));
	/* If we had to stop ASC due to lack of data, restart */
	if (auxCh->aux_state & STOPPED)
	{
		Debug (DB_ALWAYS, ("sndwr: ASCAttack after STOPPED\n"));
		auxCh->aux_state &= ~STOPPED;
		ASCAttack(ch);
	}


	Debug (DB_ALWAYS, ("putSamp:0x%x ",(currPB->putSamp + count)));
	/* on first fill, sleep when at end until getSamp halfway into sustainBuf */
	if ((currPB->putSamp += count) == auxCh->aux_end)
	{
	    if (auxCh->aux_state & FIRSTFILL)
	    {
		Debug (DB_ALWAYS, ("1st SLEEP\n"));
		auxCh->aux_state |= ASLEEP;
		Debug (DB_ALWAYS, ("1st WU\n"));
		sleep(&auxCh->aux_cnt, PZERO); /* wakeup from sndintr() */
		if (resetChan)
		{
			splx(s);
			return(EIO);
		}
	    }
	    /* wrap around to start of sustainBuf */
	    currPB->putSamp = auxCh->aux_beg + ATTACKBUFSIZ;
	}

	/* start the raw device as soon as we have some data */
	if ((minor(dev) == MIN_RAW) && (auxCh->aux_state & FIRSTFILL))
	{
		Debug (DB_ALWAYS, ("sndwr: ASCAttack after raw FIRSTFILL\n"));
		auxCh->aux_state &= ~FIRSTFILL;
		auxCh->aux_state |= LOOPFILL;
		ch->flags &= ~ChQuiet;
		ASCAttack(ch);
	}

	if (auxCh->aux_cnt == SUSTBUFSIZ)	/* if full, but more to do ... */
	{
	    if ((ch->flags & ChQuiet) && 
		(currPB->putSamp == auxCh->aux_beg + ATTACKBUFSIZ))
	    {
		splx(s);
		return(0);	/* AttackBuf full - has not played yet */
	    }

	    if (uio->uio_resid)
	    {
		if (auxCh->aux_state & FINISHED)
		{
		    auxCh->aux_state &= ~FINISHED;
		    splx(s);
		    return(0);
		}
		auxCh->aux_state |= ASLEEP;
		Debug (DB_ALWAYS, ("2nd SLEEP\n"));
		sleep(&auxCh->aux_cnt, PZERO);
		if (resetChan)
		{
			splx(s);
			return(EIO);
		}
		Debug (DB_ALWAYS, ("2nd WU\n"));
	    }
	}
	splx(s);
    }
    return(0);
}


sndioctl(dev, cmd, addr, mode)
dev_t   dev;
register caddr_t addr;
{
    register unsigned char	vol;
    register unsigned int	compmask, bitmask;
    register SndChannelPtr	ch;
    register auxSndChPtr	auxCh;
    register struct SampDesc	*desc;
    register struct PlayBlock	*currPB;
    register int		s;
    register struct SndCommand	callBackCmd;

    ch = &channelList[minor(dev)];
    auxCh = &auxChanList[minor(dev)];

    Debug (DB_ALWAYS, ("sndioctl() - ch:%d cmd:0x%x\n",minor(dev),cmd));

    switch(cmd)
    {
	case SND_VOL:
	    /* volume reg = XXX00000 */
	    vol = ((* (char *) addr) << 5);
	    ((ASCSpace *) ASC)->ctrl.volControl = vol;
	    break;

	case SND_HDR:
	    Debug (DB_ALWAYS, ("got SND_HDR\n"));
	    s = spl6();
	    while ( !(ch->flags & ChQuiet) || (ch->qHead != -1)) /* wait till done with last note in queue */
	    {
	    	Debug (DB_ALWAYS, ("ch->flags:0x%x  qHead:0x%x\n",ch->flags,ch->qHead));
		auxCh->aux_state |= WAITING;
		sleep(&auxCh->aux_state, PZERO);
	        Debug (DB_ALWAYS, ("after SND_HDR sleep\n"));
		if (resetChan) {
			splx(s);
			return(EIO);
		}
	    }

	    splx(s);

	    /* stash this sample descriptor for as long as the channel is open */
	    curSndHdr.samplePtr = ((struct SoundHeader *) addr)->samplePtr;
	    curSndHdr.length = ((struct SoundHeader *) addr)->length;
	    curSndHdr.sampleRate = ((struct SoundHeader *) addr)->sampleRate;
	    curSndHdr.loopStart = ((struct SoundHeader *) addr)->loopStart;
	    curSndHdr.loopEnd = ((struct SoundHeader *) addr)->loopEnd;

	    /* Determine buffering technique.
	     * Rather than waste time looping on < 10 bytes, we just assume no looping. This
	     * is really just a time killer until we get a QuietCmd anyway.
	     */
	    if ((curSndHdr.loopEnd == 0) || (curSndHdr.loopEnd - curSndHdr.loopStart <= 10))
	    {
		curSndHdr.loopEnd = 0;
		curSndHdr.loopStart = 0;
	    	if (curSndHdr.length < ATTACKBUFSIZ)
	    		sampleType = A_NLOOP;
	    	else if (curSndHdr.length < (ATTACKBUFSIZ + SUSTBUFSIZ))
	    		sampleType = AS_NLOOP;
	    	else
	    		sampleType = X_NLOOP;
	    }
	    else
	    {
	    	if (curSndHdr.length < ATTACKBUFSIZ)
	    		sampleType = A_LOOP;
	    	else if (curSndHdr.length < (ATTACKBUFSIZ + SUSTBUFSIZ))
	    		sampleType = AS_LOOP;
	    	else if ((curSndHdr.loopEnd - curSndHdr.loopStart) <= SUSTBUFSIZ)
	    		sampleType = X_LOOPSUST;
	    	else
	    		sampleType = X_LOOPX;
	    }

	    /* reset the buffer ptr to start of AttackBuf at next write */
	    auxCh->aux_state |= NEWSAMPLE;

	    break;

	case SND_CMD_I:
	    cmdBuf.commandNum = ((struct SndCommand *) addr)->commandNum;
	    Debug (DB_ALWAYS, ("got SND_CMD_I %d\n",cmdBuf.commandNum));
	    cmdBuf.wordArg = ((struct SndCommand *) addr)->wordArg;
	    cmdBuf.longArg = ((struct SndCommand *) addr)->longArg;
	    SndDoImmediate(ch, &cmdBuf);
	    break;

	case SND_CMD_Q:
	    cmdBuf.commandNum = ((struct SndCommand *) addr)->commandNum;
	    Debug (DB_ALWAYS, ("got SND_CMD_Q %d\n",cmdBuf.commandNum));
	    cmdBuf.wordArg = ((struct SndCommand *) addr)->wordArg;
	    cmdBuf.longArg = ((struct SndCommand *) addr)->longArg;
	    SndDoCommand(ch, &cmdBuf);
	    break;

	case SND_CALLBACK:
	    /*
	     *	This command lets the toolbox poll the kernel
	     *	sound driver to see which, if any, sound channel sent
	     *	up a SIGIO to request a callBack to a user routine.
	     *	A select call to any opened sound file descriptor will
	     *	return the info about all sound channels/file descriptors.
	     */
	    s = spl6();
	    compmask = 0;
	    bitmask = 1;
	    for (auxCh = auxChanList; auxCh < &auxChanList[TOTSYNTHS]; auxCh++)
	    {
		/* check if callback pending on this dev */
		if (auxCh->qHead != -1)
		    compmask |= bitmask;
		else
		    ch->flags &= ~ChCallBack;

		bitmask = bitmask<<1;
	    }
	    splx(s);
	    Debug (DB_ALWAYS, ("sndDriver callBack compmask = 0x%x\n",compmask));
	    u.u_rval1 = compmask;
	    break;

	case SND_GET_CB:
	    nextFromCBQue(auxCh, &callBackCmd);
	    ((struct SndCommand *) addr)->commandNum = callBackCmd.commandNum;
	    ((struct SndCommand *) addr)->wordArg = callBackCmd.wordArg;
	    ((struct SndCommand *) addr)->longArg = callBackCmd.longArg;
	    break;

	case SND_RAW_CTL:
	    rawSndInfo.sampleRate = ((struct rawSndCtl *) addr)->sampleRate;
	    rawSndInfo.flags = ((struct rawSndCtl *) addr)->flags;
	    break;

	case SND_QFULL:
	    if (ch->qHead == ch->qTail)
		u.u_rval1 = true;
	    else
		u.u_rval1 = false;
	    break;

	default:
	    Debug (DB_ALWAYS, ("got illegal SND_CMD\n"));
	    return(EINVAL);
	}
	return(0);
}

sndintr()
{
    register SndChannelPtr	ch;
    register auxSndChPtr	auxCh;
    register char		flags;
    register struct SampDesc	*desc;
    register struct PlayBlock	*currPB;
    register unsigned char	*samp, *currEnd;
    register Fixed		frac;
    register Fixed		inc, cnt;
    register unsigned long	totalLength, delta;
    unsigned char		s;
    unsigned int 		state;
    register int		i, pl;

    CLRSNDIRQ();

    ch = &channelList[synthType];
    auxCh = &auxChanList[synthType];
    desc = &SampDescTbl[auxCh->aux_min_num];
    currPB = desc->nowPlaying;
    sndintr_cnt++;

    pl = spl6();
    if ((currPB->playState == Quiet) || (resetChan))	/* stop playing */
    {
	goto sndDone;
    }

    frac = currPB->fraction;
    inc = currPB->rate;
    samp = currPB->getSamp;
    currEnd = currPB->currEnd;
    totalLength = currPB->totalLength;
    cnt = 0;

    Debug (DB_INTR, ("currPB @ 0x%x frac:0x%x inc:0x%x samp:0x%x end:0x%x l:0x%x\n",
	currPB,frac,inc,samp,currEnd,totalLength));

    /* feed the ASC until full - since fake stereo, only deal with A-side */
    while ( flags = ((ASCSpace *) ASC)->ctrl.fifoInterrupt,
			!(flags & ASCFullA) || (flags & ASCHalfA) )
    {
	if (samp >= currEnd)
	{
	    switch (sampleType)
	    {
		case A_NLOOP:
		case AS_NLOOP:
		    Debug(DB_INTR, ("A(S)_NLOOP\n"));
		    goto sndDone;

		case A_LOOP:
		case AS_LOOP:
		    Debug (DB_INTR, ("A(S)_LOOP\n"));
		    if (currPB->playState == Quiet)
			goto sndDone;
		    samp = currPB->sndStart + currPB->loopStart;
		    break;

		case X_NLOOP:
		    Debug (DB_INTR, ("X_NLOOP-"));
    		    if ((state = auxCh->aux_state) & FIRSTFILL)
		    {
			Debug (DB_INTR, ("1st\n"));
		    	currEnd = currPB->currEnd = currPB->sndEnd;
			auxCh->aux_state &= ~FIRSTFILL;
			auxCh->aux_state |= LOOPFILL;
			auxCh->aux_cnt = auxCh->aux_end - currPB->getSamp;
			if (auxCh->aux_state & ASLEEP)
			{
				auxCh->aux_state &= ~ASLEEP;
				wakeup(&auxCh->aux_cnt);
			}
		    }
		    else if (state & LOOPFILL)
		    {
			Debug (DB_INTR, ("loop\n"));
		        samp = currPB->sndStart + ATTACKBUFSIZ;
			if (totalLength <= 0)
			    goto sndDone;

			if (totalLength < SUSTBUFSIZ)
		    	    currEnd = currPB->currEnd = currPB->sndStart + ATTACKBUFSIZ + totalLength;
		    }
		    break;

		case X_RAW:
		    Debug (DB_INTR, ("X_RAW\n"));
		    samp = currPB->sndStart + ATTACKBUFSIZ;

		    /* keep pos length, go until sndclose() & cnt == 0 */
		    totalLength = ATTACKBUFSIZ + SUSTBUFSIZ;
		    break;

		case X_LOOPX:
		    Debug (DB_INTR, ("X_LOOPX-"));
    		    if ((state = auxCh->aux_state) & FIRSTFILL)
		    {
			Debug (DB_INTR, ("1st\n"));
		    	currEnd = currPB->currEnd = currPB->sndEnd;
			auxCh->aux_state &= ~FIRSTFILL;
			auxCh->aux_state |= LOOPFILL;
			auxCh->aux_cnt = auxCh->aux_end - currPB->getSamp;
			cnt = 0; 	/* count bytes from this pt on */
			if (auxCh->aux_state & ASLEEP)
			{
				auxCh->aux_state &= ~ASLEEP;
				wakeup(&auxCh->aux_cnt);
			}
		    }
		    else if (state & LOOPFILL)
		    {
			Debug (DB_INTR, ("loop\n"));
			/* loop until QuietCmd or OnTimeInterrupt() timeout */
			if (currPB->playState == Quiet)
				goto sndDone;
		        samp = currPB->sndStart + ATTACKBUFSIZ;
		    }
		    break;

		case X_LOOPSUST:
		    Debug (DB_INTR, ("X_LOOPX-"));
		    /* loop until QuietCmd or OnTimeInterrupt() timeout */
		    if (currPB->playState == Quiet)
			goto sndDone;
    		    if ((state = auxCh->aux_state) & FIRSTFILL)
		    {
			Debug (DB_INTR, ("1st\n"));
		    	currEnd = currPB->currEnd = currPB->sndEnd;
			auxCh->aux_state &= ~FIRSTFILL;
			auxCh->aux_state |= LOOPFILL;
			auxCh->aux_cnt = auxCh->aux_end - currPB->getSamp;
			cnt = 0; 	/* count bytes from this pt on */
			loopRepeat = 0;
			if (auxCh->aux_state & ASLEEP)
			{
				auxCh->aux_state &= ~ASLEEP;
				wakeup(&auxCh->aux_cnt);
			}
		    }
		    else if (state & LOOPFILL)
		    {
			Debug (DB_INTR, ("loop\n"));
		        samp = currPB->sndStart + ATTACKBUFSIZ;
			if (totalLength < SUSTBUFSIZ)
			{
		    	    currEnd = currPB->currEnd = currPB->sndStart + ATTACKBUFSIZ + totalLength;
			    auxCh->aux_state &= ~LOOPFILL;
			    auxCh->aux_state |= LASTFILL;
			}
		    }
		    else if (state & LASTFILL)
		    {
			Debug (DB_INTR, ("last\n"));
			if (loopRepeat == 0)
			{
			    loopRepeat = samp - (currPB->loopEnd - currPB->loopStart);
			    if (loopRepeat < (currPB->sndStart + ATTACKBUFSIZ))
			    {
				delta = (currPB->sndStart + ATTACKBUFSIZ) - loopRepeat;
				if (delta < frac>>16)
					delta = frac>>16;
				loopRepeat = currPB->sndEnd - delta;
			    }
			}
			samp = loopRepeat;
		    }
		    break;

		default:
		    samp = currPB->sndStart;	/* illegal */
	    }
	}

	/* OutputFakeStereo */
	s = Interp(frac, samp);
	((ASCSpace *) ASC)->data.fifo[1][0] = s;
	((ASCSpace *) ASC)->data.fifo[0][0] = s;

	frac += inc;
	samp += frac >> 16;
	totalLength -= (frac >> 16);
	cnt += inc;			/* keep track of number of bytes played this intr */
					/*  in Fixed pt binary */
	frac &= 0xffff;

    }

    currPB->fraction = frac;
    currPB->getSamp = samp;
    currPB->totalLength = totalLength;

    Debug (DB_INTR, ("getSamp:0x%x ",samp));

    if (auxCh->aux_state & (LOOPFILL | LASTFILL))
    {
	cnt = (cnt >> 16);	/* bytes played - convert Fixed to int */
	/* 
	 * If user process is having problems
	 * pumping down data fast enough, wait.
	 */
	if ((auxCh->aux_cnt -= cnt) <= 0) {
		if (ch->flags & ChClsOnQEmpty)
			goto sndDone;
		auxCh->aux_state |= STOPPED;
		currPB->getSamp == currPB->putSamp; /* In case cnt went negative */
		auxCh->aux_cnt = 0;
		Debug (DB_INTR, ("wakeup from STOPPED\n"));
		STOP_ASC_INTS(ch);
		wakeup(&auxCh->aux_cnt);
		splx(pl);
		return;
	}
    }


    /* check if more data needed from user process */
    Debug (DB_INTR, ("icnt:0x%x ",auxCh->aux_cnt));
    if ((auxCh->aux_cnt < LOWATER) && (auxCh->aux_state & ASLEEP))
    {
	Debug (DB_INTR, ("LOWATER WU\n"));
	auxCh->aux_state &= ~ASLEEP;
	wakeup(&auxCh->aux_cnt);
    }

    START_ASC_INTS();
    splx(pl);
    return;


sndDone:
    STOP_ASC_INTS(ch);
    currPB->playState = Quiet;
    ch->flags |= ChQuiet;	/* stop sending data */
    for (i=0, flags=1; ((i<100000) && (flags != 0));i++)
    {
    	flags = ((ASCSpace *) ASC)->ctrl.fifoInterrupt;
    }
    if (ch->flags & ChClsOnQEmpty) {
	Debug (DB_ALWAYS, ("iDONE-ChClsOnQEmpty\n"));
	ch->flags &= ~ChClsOnQEmpty;
	ch->flags |= ChQuiet;	/* stop sending data */
	currPB->playState = Quiet;
	currPB->putSamp = auxCh->aux_beg;
	auxCh->aux_state = 0;
	ASCFree(ch);
	splx(pl);
	return;
    }
    Debug (DB_ALWAYS, ("iDONE\n"));
    currPB->putSamp = auxCh->aux_beg;
    if (auxCh->aux_state & (LOOPFILL | LASTFILL))
    {
	cnt = (cnt >> 16);	/* bytes played - convert Fixed to int */
	auxCh->aux_cnt -= cnt;
    }
    /* if not waiting and queue not empty */
    if ((ch->wait == 0) && (ch->qHead != -1)) 
    {
	WriteMinTimer(1);	/* start next queued snd cmd ASAP */
	splx(pl);
	return;
    }
    auxCh->aux_state |= FINISHED;
    if (auxCh->aux_state & WAITING)	/* waiting on next SND_HDR ? */
    {
	auxCh->aux_state &= ~WAITING;
	wakeup(&auxCh->aux_state);
	Debug (DB_ALWAYS, ("sndintr SND_HDR wu\n"));
    }
    if (auxCh->aux_state & ASLEEP)	/* Just in case */
    {
	auxCh->aux_state &= ~ASLEEP;
	wakeup(&auxCh->aux_cnt);
    }
    splx(pl);
    return;
}

