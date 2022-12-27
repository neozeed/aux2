/*
 * @(#)sm.priv.c  {Apple version 1.13 90/05/04 01:45:41}
 *
 * Copyright (c) 1987, 1988, 1989 by Apple Computer, Inc.
 * All Rights Reserved.
 *
 * THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF APPLE COMPUTER, INC.
 * The copyright notice above does not evidence any actual or
 * intended publication of such source code.
 */

/*	sm.priv.c
 *
 *	This is a file of the high level internal routines of the 
 *	Sound Manager.  It includes the queue manipulation, the
 *	command handling, the interrupt processing, the user calls
 *	and the SoundManager's command modifier.
 *
 *	Ported to AUX by Rob Smith, Feb'89
 *	This is the kernel driver version of sm.priv.c
 *	The toolbox version sends SndCmds to here via an ioctl interface.
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
#include "sys/time.h"
#include "sys/user.h"
#include "types.h"
#include "sm.h"
#include "sys/sm_aux.h"
#include "sm.privdesc.h"
#include "samp.desc.h"
#include "sm.asc.h"

extern noteMain();
extern waveMain();
extern sampMain();
extern StartTimer();
extern StopTimer();
extern snd_proc;
extern int sndintr_cnt;
extern int catch_asc_timeout();


/*
 *	Return a ptr to the aux extension struct for the
 *	given SndChannel.
 */
auxSndChPtr
getAuxChan(ch)
struct SndChannel *ch;
{
	struct SndChannel *cmpCh;
	struct auxSndCh *auxCh;
	register int i;

	for (i=0,cmpCh = &channelList[0]; cmpCh <= &channelList[TOTSYNTHS]; i++,cmpCh++)
	{
		if (cmpCh == ch)
			return((auxSndChPtr) &auxChanList[i]);
	}

	return(0);	/* not found */
}


/********************************************
 *						*
 *	Q U E U I N G   R O U T I N E S		*
 *						*
 ********************************************/


/* initQueue -
 *
 *		Initialize a SndChannel queue to be empty
 */
initQueue(ch)
SndChannelPtr	ch;
{
	auxSndChPtr	auxCh;

	ch->qHead = -1;
	ch->qTail = 0;
	auxCh = (auxSndChPtr)getAuxChan(ch);
	if (auxCh->aux_state & WAITING)
	{
	    auxCh->aux_state &= ~WAITING;
	    wakeup(&auxCh->aux_state);
	}
}



/* emptyQueue -
 *
 *		answer if a SndChannel queue is empty
 */
#define emptyQueue(ch)	(ch->qHead == -1)



/* fullQueue -
 *
 *		answer if a SndChannel queue is full
 */
#define fullQueue(ch)	(ch->qHead == ch->qTail)




/* insertInQueue -
 *		Adds a command to a SndChannel queue if it isn't full.
 *		The command is added to the end and the result is
 *		true if the queue WAS full and the command wasn't added.
 */
static Boolean
insertInQueue(ch, newCmd)
	SndChannelPtr		ch;
	struct SndCommand	*newCmd;
{
	int	saveStatus;
	
	saveStatus = spl6();		/* stop level 1 interrupts */

	if (fullQueue(ch))
	{
	splx(saveStatus);		/* restore level 1 interrupts */
	return(true);
	}
	
	if (ch->qHead == -1)
	{
		ch->qHead = ch->qTail;
		ch->flags &= ~ChNewlyEmpty;
	}
	ch->queue[ch->qTail++] = *newCmd;
	if (ch->qTail == ch->qLength) ch->qTail = 0;

	splx(saveStatus);		/* restore level 1 interrupts */
	return(false);
}



/* nextFromQueue -
 *		Remove the next command from a SndChannel queue.  The
 *		command is coppied into the callers command.  If the
 *		queue WAS empty then False is returned.
 */
static Boolean
nextFromQueue(ch, nextComm)
	SndChannelPtr		ch;
	struct SndCommand	*nextComm;
{
	int	saveStatus;
	auxSndChPtr	auxCh;
	
	saveStatus = spl6();
	
	if (emptyQueue(ch))
	{
		splx(saveStatus);
		 return(true);
	}

	*nextComm = ch->queue[ch->qHead++];
	if (ch->qHead == ch->qLength) ch->qHead = 0;
	if (ch->qHead == ch->qTail)
	{
		ch->qHead = -1;
		ch->flags |= ChNewlyEmpty;		/* signal to send an empty command */
		auxCh = (auxSndChPtr)getAuxChan(ch);
		if (auxCh->aux_state & WAITING)
		{
		    auxCh->aux_state &= ~WAITING;
		    wakeup(&auxCh->aux_state);
		}
	}
	splx(saveStatus);
	return(false);
}



/********************************************
 *						*
 *  	C H A N N E L   M O D I F I E R		*
 *						*
 ********************************************/


/* ChannelModifier -
 *
 *		This is the modifier that interprets the commands that the sound
 *		manager knows about.  They are interpreted at the end of the chain
 *		so that modifieirs may alter their behaviour entirly.  This is
 *		not a real modifier in the sense that a) there is no ModifierStub,
 *		b) it cannot request to be recalled, & c) there is nothing for it
 *		to pass on a command to.
 */
static void
ChannelModifier(ch, cmd)
	SndChannelPtr		ch;
	struct SndCommand	*cmd;
{
	ModifierStubPtr		mod;
	SndChannelPtr		chan;
	auxSndChPtr		auxCh;
	
	switch (cmd->commandNum)
	{
		case QuietCmd:
			Debug (DB_ALWAYS, ("ChanMod() - QuietCmd\n"));
			ch->flags |= ChQuiet;
			ch->wait = 0;
			break;
		
		case FlushCmd:
			initQueue(ch);
			ch->flags |= ChNewlyEmpty;
			break;
			
		case WaitCmd:
			ch->wait += cmd->wordArg;
			WriteMinTimer(ch->wait);
			break;
		
		case PauseCmd:
			ch->flags |= ChPaused;
			break;
			
		case ResumeCmd:
			ch->flags &= ~ChHeld;
			if (!emptyQueue(ch))
			{
				ch->wait = -1;
				WriteMinTimer(1);	/* start ASAP */
			}
			break;
		
		case CallBackCmd:
			auxCh = (auxSndChPtr)getAuxChan(ch);
			insertInCBQue(auxCh, cmd);
			ch->flags |= ChCallBack;	/* for select polling */
			psignal(snd_proc, SIGIO);
			break;
		
		case SyncCmd:
			ch->cmdInProgress = *cmd;
			ch->flags |= ChSyncd;
			
			for (chan = channelList; chan < &channelList[TOTSYNTHS]; chan++)
				if ((chan->flags & ChSyncd)
					&& (chan->cmdInProgress.longArg == cmd->longArg)
					&& (--chan->cmdInProgress.wordArg == 0) )
				{
					chan->flags &= ~ChSyncd;
					if (!emptyQueue(ch))
					{
						ch->wait = -1;
						WriteMinTimer(1); /* start ASAP */
					}
				}
				
			break;
				
		case HowOftenCmd:
			mod = (ModifierStubPtr)(cmd->longArg);
			mod->count = mod->every = cmd->wordArg;
			WriteMinTimer(mod->count);
			break;
		
		case WakeUpCmd:
			mod = (ModifierStubPtr)(cmd->longArg);
			mod->count = cmd->wordArg;
			WriteMinTimer(mod->count);
			break;
	}
}





/********************************************
 *						*
 *	C O M M A N D   R O U T I N E S		*
 *						*
 ********************************************/


/* processCommandFrom -
 *
 *		Process a command by routing it through the modifiers and
 *		the final synthesizer.  The third parameter is the ModifierStub
 *		that the command processing should start with.  (This
 *		allows for other parts of the Sound Manager to use this
 *		code as well as a recursive definition of this routine!)
 *
 *		This does all the crazy stuff with modifiers and command
 *		streams.  In particular, a modifier returns a boolean that
 *		indicates that it wants to be called again this time 'round.
 *		This call will be made after all other commands have been
 *		passed down the modifier chain.  The call will be made with
 *		the command's commandNum set to RequestNextCmd
 *		indicating that the modifier is being called again in response
 *		it previously returning True.  The command's wordArg is
 *		set to the number of times in a row this has happened.
 *		After each modifier call, the command is
 *		re-examined to see if the commandNum is still vaild.
 *		If it is anything other than NullCmd, it is passed on.
 *		Thus a modifier can 'eat' a command by setting it's number
 *		to NullCmd.
 *
 *		Note that the channelModifier, while not explictly in the chain
 *		is always called after the last ModifierStub.
 *
 */
static void
processCommandFrom(ch, cmd, mod)
	struct SndChannel	*ch;
	struct SndCommand	*cmd;
	struct ModifierStub	*mod;
{
	
	Debug (DB_ALWAYS, 
	    ("processCommandFrom():  ch = 0x%x cmd = %d  mod = 0x%x\n",
		ch, cmd->commandNum, mod));

	if (cmd->commandNum != NullCmd)
	{
		if (mod != nil)
		{
		    Debug (DB_ALWAYS,
			("Calling Modifier code: cmdNum = %d cmd = 0x%x\n",
			    cmd->commandNum, cmd));
		    (*mod->modifierCode)(ch, cmd, mod);
		    ChannelModifier(ch, cmd);
		}
	}
}



/* processCommand -
 *
 *		Process a command from the start of the chain.
 *
 */
static void
processCommand(ch, cmd)
	SndChannelPtr		ch;
	struct SndCommand	*cmd;
{
	if (cmd->commandNum == QuietCmd)
		ch->flags &= ~ChQuiet;
	processCommandFrom(ch, cmd, ch->modifierChain);
}



/* processNextCommand -
 *
 *		Get the next command and process it.
 */
static Boolean
processNextCommand(ch)
	SndChannelPtr	ch;
{
	struct SndCommand	cmd;
	auxSndChPtr		auxCh;
	
	if (nextFromQueue(ch, &cmd))
	{
	    /* waiting to download next sound ? */
	    auxCh = (auxSndChPtr)getAuxChan(ch);
	    if (auxCh->aux_state & WAITING)
	    {
		auxCh->aux_state &= ~WAITING;
		wakeup(&auxCh->aux_state);
	    }
	    /* if queue empty and fd already closed */
	    if (ch->flags & ChClsOnQEmpty)
	    {
		STOP_ASC_INTS(ch);

		auxCh->aux_state = 0;
		auxCh->aux_beg = 0;
		auxCh->aux_end = 0;
		ch->flags = 0;

		disposChannel(ch);
		Debug (DB_ALWAYS, ("wakeup on 0x%x\n",&ch->flags));
		wakeup(&ch->flags);	/* wakeup any sleep in sndopen() */
	    }
	    return(true);
	}
	Debug (DB_ALWAYS, ("prcNxtCmd:%d\n",cmd.commandNum));
	processCommand(ch, &cmd);
	return(false);
}



/* sendEmptyCommand -
 *
 *		Tell a resume'd or woken up channel that it is empty.
 */
static void
sendEmptyCommand(ch)
	SndChannelPtr	ch;
{
	struct SndCommand	cmd;
	
	if (ch->flags & ChNewlyEmpty)	/* no Empty Command has been sent since queue went empty */
	{
		cmd.commandNum = EmptyCmd;
		processCommand(ch, &cmd);
		ch->flags &= ~ChNewlyEmpty;	/* don't send it again */
	}
}



/* tickleModifier -
 *
 *		tickle a modifier for a wake up call.
 */
static void
tickleModifier(ch, mod)
	SndChannelPtr		ch;
	ModifierStubPtr		mod;
{
	struct SndCommand	cmd;
	
	cmd.commandNum = TickleCmd;
	processCommandFrom(ch, &cmd, mod);
}






/********************************************
 *						*
 *    C H A N N E L   R O U T I N E S		*
 *						*
 ********************************************/


/* findModifier -
 *
 *		Find any modifier stub that refers to the same modifer as the argument.
 *		Answer a pointer to it, or nil if none is found.
 */
static
ModifierStubPtr findModifier(targetMod)
	ProcPtr		targetMod;
{
	ModifierStubPtr		mod, nextMod;
	ProcPtr			compareMod;
	SndChannelPtr		chan;
	
	compareMod = targetMod;
	
	for (chan = channelList; chan < &channelList[TOTSYNTHS]; chan++)
		for (mod = chan->modifierChain; mod != nil; mod = mod->next)
			if (mod->modifierCode == compareMod)
				return(mod);
	
	return(nil);
}


/* disposChannel -
 *
 *		Free up all the ModifierStubs and the SndChannel if the ChFreeOnQuit
 *		bit is set.  In addition, remove the channel from the global list if
 *		it's on it (may not be if channel error is detected during creation)
 *		and stop the time if the list is empty.
 *
 *		Note that this code auto-shuts down the Sound Manager if this is the
 *		last channel disposed of. (Conceptually, since now there is nothing to do)
 */
disposChannel(ch)
SndChannelPtr	ch;
{
	ModifierStubPtr		mod;
	struct SndCommand	cmd;
	
	mod = ch->modifierChain;
	while (mod)
	{
		cmd.commandNum = FreeCmd;
		processCommandFrom(ch, &cmd, mod);
		mod = mod->next;
	}
}


/* checkChannel -
 *
 *		Ensure that a channel pointer passed by the user is a valid channel ptr.
 *		Return true if it's bad, false if it's good.  (Makes error test easy.)
 *		For now, simply checks for nil, in future might test other things.
 */
static Boolean
checkChannel(ch)
	SndChannelPtr	ch;
{
	if (ch == nil) return true;
	if (ch->qLength == 0) return(true);
	return(false);
}


/********************************************
 *						*
 *	  I N T E R R U P T   R O U T I N E S	*
 *						*
 ********************************************/

/* onTimeInterrupt -
 *
 *		Do the things that need to be done on timer interrupt.
 */
 
extern void
OnTimeInterrupt(timeSinceLast)
long timeSinceLast;
{
	register long			nextSpan;
	register SndChannelPtr		ch;
	register ModifierStubPtr	mod;
	register struct SampDesc	*desc;
	register struct PlayBlock	*currPB;
	int				i;
	auxSndChPtr			auxCh;
	
	nextSpan = InfiniteTime;
	
	for (i=0,ch = channelList; ch < &channelList[TOTSYNTHS]; i++,ch++)
	{
	    auxCh = &auxChanList[i];
	    if (auxCh->aux_state & INUSE)
	    {
		desc = &SampDescTbl[auxCh->aux_min_num];
		currPB = desc->nowPlaying;

		/* ch->wait:	0 means timer inactive on this chan
		 *		> 0 means timer active and counting
		 *		< 0 means we just timed out, so start next queued cmd
		 */
		if (ch->wait != 0)
		{
			ch->wait -= timeSinceLast; /* now fall into one of two cases below */
		}
		
		if (ch->wait > 0)	/* still counting */
			nextSpan = MIN(nextSpan, ch->wait);

		if (ch->wait <= 0)	/* just timed out - do next cmd */
		{
			if (currPB->playState != Quiet) /* shut off active note */
			{
			    desc = &SampDescTbl[auxCh->aux_min_num];
    			    currPB = desc->nowPlaying;
			    currPB->playState = Quiet;
			    ch->flags |= ChQuiet;	/* stop sending data */
			    STOP_ASC_INTS(ch);
			}

			ch->flags |= ChDoNextCmd;
			ch->wait = 0;
		}
	    }
	}
	
	if (nextSpan == InfiniteTime)
	{
		WriteMinTimer(-1);	/* stop timer */
	}
	else
	{
		WriteMinTimer(nextSpan);
	}

	/*
	 *	Take care of next events after timer is reset.
	 *	This all happens from IRQ level, so we cannot
	 *	do any sleeps or extended processing
	 */
	for (ch = channelList; ch < &channelList[TOTSYNTHS]; ch++)
	{
		/* check if waiting download next sound */
		auxCh = (auxSndChPtr)getAuxChan(ch);
		if (auxCh->aux_state & WAITING)
		{
		    auxCh->aux_state &= ~WAITING;
		    wakeup(&auxCh->aux_state);
		}

		if (ch->flags & ChDoNextCmd)
		{
		    ch->flags &= ~ChDoNextCmd;
		    while ((ch->wait == 0) && !emptyQueue(ch) && !(ch->flags & ChHeld))
		    {
		    	processNextCommand(ch);
		    }
		    if ((ch->wait == 0) && emptyQueue(ch) && (ch->flags & ChClsOnQEmpty))
		    {
			ch->flags = 0;
			auxCh->aux_state = 0;
			disposChannel(ch);
		    }
		}
	}
}




/********************************************
 *							*
 *  T O O L B O X  I N T E R F A C E  R O U T I N E S	*
 *							*
 ********************************************/

/* SndDoCommand -
 *
 *		Add a command to a queue.  If noWait is false, then wait for the
 *		queue to have space if it is full, else if noWait is true, simply
 *		return an error.
 *
 *	AUX - This is the SM entry point of all SND_CMD_Q commands sent from the 
 *		SM toolbox in user space. Commands are recieved through the ioctl()
 *		routine and sent here for processing.
 *		We do not use the noWait argument in the original SM code.
 */
OSErr
SndDoCommand(chan, cmd)
	SndChannelPtr		chan;
	struct SndCommand	*cmd;
{
	Boolean		full;
	int		saveStatus;
	
	Debug (DB_ALWAYS, ("DoCmd() ch 0x%x  cmdNum 0x%x\n",chan,cmd->commandNum));

	if (checkChannel(chan)) return(badChannel);
	
	saveStatus = spl6();
	full = insertInQueue(chan, cmd);
	
	/* if not waiting or playing and queue not empty */
	if ((chan->wait == 0) && !(emptyQueue(chan)) && (chan->flags & ChQuiet) &&
		!(chan->flags & ChHeld)) 
	{
		chan->wait = -1;
		WriteMinTimer(1);	/* start ASAP */
	}
	splx(saveStatus);

	return(full ? queueFull : noErr);	
}



/* SndDoImmediate -
 *
 *		Process a command immediatly.
 *		Note that the command needn't be copied even thought it
 *		will be modified by the processing since Pascal semantics
 *		ensure that it will be copied during the calling sequence.
 *
 *	AUX - This is the SM entry point of all SND_CMD_I commands sent from the 
 *		SM toolbox in user space.
 */
OSErr
SndDoImmediate(chan, cmd)
	SndChannelPtr		chan;
	struct SndCommand	*cmd;
{
	if (checkChannel(chan)) return(badChannel);
	
	processCommand(chan, cmd);

	return(noErr);	
}


ModifierStubPtr
newModPtr(ch)
SndChannelPtr	ch;
{
	register int 	i;
	register SndChannelPtr	chptr;

	i = 0;
	for (chptr = channelList; chptr < &channelList[TOTSYNTHS]; chptr++,i++)
	{
		if (ch == chptr)
			return(&modList[i]);
	}
	return(false);
}

/* SndAddModifier -
 *
 *		Add a user modifier to the chain of modifiers for a
 *		SndChannel.  A new ModifierStub gets placed at the head (called
 *		first) of the ModifierStub chain for the SndChannel.  The
 *		modifier is either pointed to by the modifierCode parameter
 *		or, if that is nil, then it is the 'snth' resource with the number
 *		given by the id parameter.  The init parameter is simply passed
 *		onto the modifier in the init command and is modifier defined.
 *		Note that this routine allocates non-relocateable memory on the heap.
 *
 *	AUX - Since we don't know about 'synth' resources in the kernel, we use
 *		the synth device minor number instead. This is passed in the 'id'
 *		parameter.
 */
static	struct SndCommand	cmdBuf;
OSErr
SndAddModifier(chan, modifierCode, id, init)
	SndChannelPtr	chan;
	ProcPtr		modifierCode;
	short		id;
	long		init;
{
	ModifierStubPtr		mod, otherMod;
	OSErr		err;
	int		machinetype;
	short		newid;
	
	if (checkChannel(chan)) return badChannel;
	
	if (modifierCode == nil)
	{
		switch(id)
		{
			case MIN_NOTE:
				modifierCode = noteMain;
				break;
			case MIN_WAVE1:
			case MIN_WAVE2:
			case MIN_WAVE3:
			case MIN_WAVE4:
				modifierCode = waveMain;
				break;
			case MIN_SAMP:
				modifierCode = sampMain;
				break;
			default:
				return(false);
		}
	}

	mod = newModPtr(chan);

	mod->count = 0;
	mod->every = 0;
	mod->userInfo = nil;
	mod->flags = 0;

	mod->modifierCode = modifierCode;
	
	mod->next = chan->modifierChain;
	chan->modifierChain = mod;
	
	cmdBuf.commandNum  = InitCmd;
	cmdBuf.longArg = init;
	processCommandFrom(chan, &cmdBuf, mod);
	
	return(noErr);
}



/* SndNewChannel -
 *
 *		Open a sound SndChannel.  A SndChannel structure is created if none
 *		exists yet.  The synth is gotten from the resource manager and
 *		inserted as the first modifier.  The init parameter is used in the
 *		init command and its meaning is dependant on the synthesizer.
 *		The userRoutine is stored for callback commands.
 *
 *		Note that this code also auto-inits the Sound Manger if it has
 *		never been called before!
 *
 *		Warning!  If you supply chanVar, you must have set 
 *		chan->qLength = StdQLength;  or the length you allocated  (-tk)
 */
OSErr
SndNewChannel(chan, synth, init, userRoutine)
	struct SndChannel	*chan;
	short		synth;
	long		init;
	ProcPtr		userRoutine;
{
	ProcPtr		*synthCodeHandle;
	OSErr		err;

	chan->flags = 0;
	chan->flags &= ~(ChPaused | ChSyncd | ChNewlyEmpty | ChDoNextCmd | ChCallBack | ChClsOnQEmpty);
	chan->flags 		|= ChQuiet;
	chan->modifierChain	= nil;
	chan->callBack		= userRoutine;	/* will always be NULL for AUX */
	chan->userInfo		= nil;
	chan->wait		= 0;
	chan->qHead		= -1;
	chan->qTail		= 0;
	
	if ((err = SndAddModifier(chan, nil, synth, init)) != noErr)
	{
		disposChannel(chan);
		return(err);
	}
	
	return(noErr);
}
