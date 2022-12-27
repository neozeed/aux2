	#	@(#)Copyright Apple Computer 1987	Version 1.10 of uinters.s on 90/05/01 01:19:46
/*	@(#)uinters.s	UniPlus VVV.2.1.3	*/
/*
 * (C) 1986 UniSoft Corp. of Berkeley CA
 *
 * UniPlus Source Code. This program is proprietary
 * with Unisoft Corporation and is not to be reproduced
 * or used in any manner except as authorized in
 * writing by Unisoft.
 */
/*
 *	Warning!!! ----> this only runs on 68020s ......
 */

#define SAVEREGS	0x3f38
#define RESTREGS	0x1cfc

#define	VP		 4+36
#define UIP		 8+36
#define	OLDHEIGHT	12+36
#define	NEWHEIGHT	16+36
#define VISIBLE		20+36

#define	c_mx		0x0000
#define	c_my		0x0004
#define	c_cx		0x0008
#define	c_cy		0x000c
#define	c_smx		0x0010
#define	c_smy		0x0014
#define	c_ssx		0x0018
#define	c_ssy		0x001c
#define	c_hpx		0x0020
#define	c_hpy		0x0024
#define	c_saved		0x0028
#define	c_mask		0x0030
#define	c_cursor	0x0050
#define	c_data		0x0450


	global ui_small1
ui_small1:
	movm.l	&SAVEREGS,-(%sp)
	mov.l	VP(%sp),%a2		/* beginning of frame buffer */
	mov.l	UIP(%sp),%a1		/* user interface structure pointer */
	mov.l	c_smx(%a1),%d0		/* screen row increment */
	tst.l	VISIBLE(%a7)		/* only restore if saved */
	beq	skip11%

	/*
    	 *	First restore the old data
	 */
	mov.l	OLDHEIGHT(%sp),%d4	/* initialize loop counter */
	sub.l	&1,%d4			/* adjust for dbra */
	lea.l	c_data(%a1),%a3
	mov.l	c_cy(%a1),%d1		/* Get the screen row in %d1 */
	sub.l	c_hpy(%a1),%d1		/* And get the hot point offset */
	mulu.l	%d0,%d1			/* Now get the row pixel address */
	mov.l	c_cx(%a1),%d2		/* now get the screen col */
	sub.l	c_hpx(%a1),%d2		/* And get the hot point offset */
	add.l	%d1,%d2			/* now we have the pixel address */
loop10%:				/* if before the beginning of the */
	bge	loop11%			/* 	screen skip on past */
	add.l	%d0,%d2			/* increment the screen address */
	dbra	%d4,loop10%		/* dbra doesn't affect the condition codes */
	br	skip11%
loop11%:
	mov.w	(%a3)+,%d1		/* Now move the data */
	bfins	%d1,(%a2){%d2:&16} 
	add.l	%d0,%d2			/* increment the screen address */
	dbra	%d4,loop11%

	/*
    	 *	Then save the new data
	 *	and write the cursor
	 */
skip11%:
	mov.l	NEWHEIGHT(%sp),%d4	/* initialize loop counter */
	sub.l	&1,%d4			/* adjust for dbra */
	mov.l	&0,%d3
	mov.l	c_my(%a1),%d1		/* Get the screen row in %d1 */
	sub.l	c_hpy(%a1),%d1		/* And get the hot point offset */
	mulu.l	%d0,%d1			/* Now get the row pixel address */
	mov.l	(%a1),%d2		/* now get the screen col (c_mx == 0) */
	sub.l	c_hpx(%a1),%d2		/* And get the hot point offset */
	bgt	chkrm15%
chklm15%:
	mov.l	%d2,%d6
	cmp.l	%d6,&-16
	bge	clip15%
	mov.l	&-16,%d6
	br	clip15%
chkrm15%:
	mov.l	%d0,%d6
	sub.l	%d2,%d6
	cmp.l	%d6,&16
	bls	clip15%
	mov.l	&16,%d6
clip15%:
	mov.w	(clipmask,%d6*2),%d6
	lea.l	c_data(%a1),%a3
	lea.l	c_mask(%a1),%a0
	lea.l	c_cursor(%a1),%a1	/* get address of cursor data */
	add.l	%d1,%d2			/* now we have the pixel address */
loop15%:				/* if before the beginning of the */
	bge	loop12%			/* 	screen skip on past */
	add.w	&2,%a0
	add.w	&2,%a1
	add.l	%d0,%d2			/* increment the screen address */
	dbra	%d4,loop15%		/* dbra doesn't affect the condition codes */
	br	skip12%
loop12%:
	bfextu	(%a2){%d2:&16},%d1	/* extract data from screen memory */
	mov.w	%d1,(%a3)+		/* Now save it */
	mov.w	(%a0)+,%d3		/* get the mask */
	mov.w	(%a1)+,%d5		/* get the cursor data */
	mov.w	%d5,%d7			/* need duplicate */
	and.w	%d3,%d5			/* only want those bits specified by mask */
	and.w	%d6,%d5			/* clip it */
	not.w	%d3			/* get complement of mask */
	and.w	%d3,%d7			/* need bits not specified by mask */
	and.w	%d6,%d7			/* clip it */
	not.w	%d6			/* complement original clip mask */
	or.w	%d6,%d3
	and.w	%d3,%d1			/* preserve necessary bits in orig data */
	not.w	%d6			/* restore original clip mask */
	eor.w	%d7,%d1			/* exclusive or the cursor data with orig */
	or.w	%d5,%d1			/* or in bits specified by mask */
	bfins	%d1,(%a2){%d2:&16} 	/* pound in the cursor bits */
	add.l	%d0,%d2			/* increment the screen address */
	dbra	%d4,loop12%		/* dbra doesn't affect the condition codes */
skip12%:
	movm.l	(%sp)+,&RESTREGS
	rts





	global ui_small2
ui_small2:
	movm.l	&SAVEREGS,-(%sp)
	mov.l	VP(%sp),%a2		/* beginning of frame buffer */
	mov.l	UIP(%sp),%a1		/* user interface structure pointer */
	mov.l	c_smx(%a1),%d0		/* screen row increment */
	lsl	&1,%d0			/* make it a bit increment */
	tst.l	VISIBLE(%a7)		/* only restore if saved */
	beq	skip21%

	/*
    	 *	First restore the old data
	 */
	mov.l	OLDHEIGHT(%sp),%d4	/* initialize loop counter */
	sub.l	&1,%d4			/* adjust for dbra */
	lea.l	c_data(%a1),%a3
	mov.l	c_cy(%a1),%d1		/* Get the screen row in %d1 */
	sub.l	c_hpy(%a1),%d1		/* And get the hot point offset */
	mulu.l	%d0,%d1			/* Now get the row pixel address */
	mov.l	c_cx(%a1),%d2		/* now get the screen col */
	sub.l	c_hpx(%a1),%d2		/* And get the hot point offset */
	lsl	&1,%d2			/* make it a bit increment */
	add.l	%d1,%d2			/* now we have the pixel address */
loop20%:				/* if before the beginning of the */
	bge	loop21%			/* 	screen skip on past */
	add.l	%d0,%d2			/* increment the screen address */
	dbra	%d4,loop20%		/* dbra doesn't affect the condition codes */
	br	skip21%
loop21%:				/* if before the beginning of the */
	mov.l	(%a3)+,%d1		/* Now move the data */
	bfins	%d1,(%a2){%d2:&32} 
	add.l	%d0,%d2			/* increment the screen address */
	dbra	%d4,loop21%		/* dbra doesnt affect the condition codes */

	/*
    	 *	Next save the new data
	 */
skip21%:
	mov.l	NEWHEIGHT(%sp),%d4	/* initialize loop counter */
	sub.l	&1,%d4			/* adjust for dbra */
	lea.l	c_data(%a1),%a4
	lea.l	c_cursor(%a1),%a3
	lea.l	c_mask(%a1),%a0
	mov.l	c_my(%a1),%d1		/* Get the screen row in %d1 */
	sub.l	c_hpy(%a1),%d1		/* And get the hot point offset */
	mulu.l	%d0,%d1			/* Now get the row pixel address */
	mov.l	(%a1),%d2		/* now get the screen col (c_mx == 0) */
	sub.l	c_hpx(%a1),%d2		/* And get the hot point offset */
	bgt	chkrm25%
chklm25%:
	mov.l	%d2,%d6
	cmp.l	%d6,&-16
	bge	clip25%
	mov.l	&-16,%d6
	br	clip25%
chkrm25%:
	mov.l	%d0,%d6
	lsr.l	&1,%d6			/* turn back into pixel address */
	sub.l	%d2,%d6
	cmp.l	%d6,&16
	bls	clip25%
	mov.l	&16,%d6
clip25%:
	mov.w	(clipmask,%d6*2),%d7
	lsl	&1,%d2			/* make it a bit increment */
	add.l	%d1,%d2			/* now we have the pixel address */
loop28%:
	bge	enter23%		/* if before the beginning of the */
	add.w	&2,%a0			/* bump the mask pointer */
	add.w	&4,%a3			/* bump the cursor pointer */
	add.l	%d0,%d2			/* increment the row address */
	dbra	%d4,loop28%		/* dbra doesnt affect the condition codes */
	movm.l	(%sp)+,&RESTREGS
	rts
enter23%:
	mov.w	%d4,loopcnt
loop23%:
	mov.l	&0,%d4
	mov.w	(%a0)+,%d3		/* get the cursor mask */
	mov.l	&15,%d5
loop24%:
	bfextu	(%a2){%d2:&2},%d1	/* grab the old data */
	bfins	%d1,(%a4){%d4:&2}	/* store it away */
	btst	%d5,%d7			/* see if we're being clipped */
	beq	next25%
	bfextu	(%a3){%d4:&2},%d6	/* grab the new cursor */
	btst	%d5,%d3			/* test the mask msb */
	bne	stuff24%
	eor.b	%d1,%d6			/* XOR the data with underlying pixel */
stuff24%:
	bfins	%d6,(%a2){%d2:&2}	/* stuff it on the screen */
next25%:
	add.l	&2,%d2			/* bump screen bit offset */
	add.l	&2,%d4			/* bump data/cursor bit offset */
	dbra	%d5,loop24%
	sub.l	&32,%d2			/* restore original bit offset */
	add.l	%d0,%d2			/* increment screen address */
	add.w	&4,%a3
	add.w	&4,%a4
	sub.w	&1,loopcnt
	bpl	loop23%
	movm.l	(%sp)+,&RESTREGS
	rts




	global ui_small4
ui_small4:
	movm.l	&SAVEREGS,-(%sp)
	mov.l	VP(%sp),%a2		/* beginning of frame buffer */
	mov.l	UIP(%sp),%a1		/* user interface structure pointer */
	mov.l	c_smx(%a1),%d0		/* screen row increment */
	lsl	&2,%d0			/* make it a bit increment */
	tst.l	VISIBLE(%a7)		/* only restore if saved */
	beq	skip41%

	/*
    	 *	First restore the old data
	 */
	mov.l	OLDHEIGHT(%sp),%d4	/* initialize loop counter */
	sub.l	&1,%d4			/* adjust for dbra */
	lea.l	c_data(%a1),%a3
	mov.l	c_cy(%a1),%d1		/* Get the screen row in %d1 */
	sub.l	c_hpy(%a1),%d1		/* And get the hot point offset */
	mulu.l	%d0,%d1			/* Now get the row pixel address */
	mov.l	c_cx(%a1),%d2		/* now get the screen col */
	sub.l	c_hpx(%a1),%d2		/* And get the hot point offset */
	lsl	&2,%d2			/* make it a bit increment */
	add.l	%d1,%d2			/* now we have the pixel address */
loop40%:				/* if before the beginning of the */
	bge	loop41%			/* 	screen skip on past */
	add.l	%d0,%d2			/* increment the screen address */
	dbra	%d4,loop40%		/* dbra doesn't affect the condition codes */
	br	skip41%
loop41%:
	mov.l	(%a3)+,%d1		/* Now move the data */
	bfins	%d1,(%a2){%d2:&32} 
	mov.l	(%a3)+,%d1		/* Now move the data */
	bfins	%d1,4(%a2){%d2:&32} 
	add.l	%d0,%d2			/* increment the screen address */
	dbra	%d4,loop41%		/* dbra doesnt affect the condition codes */

	/*
    	 *	Then save new data + draw cursor
	 */
skip41%:
	mov.l	NEWHEIGHT(%sp),%d4	/* initialize loop counter */
	sub.l	&1,%d4			/* adjust for dbra */
	lea.l	c_data(%a1),%a4
	lea.l	c_cursor(%a1),%a3
	lea.l	c_mask(%a1),%a0
	mov.l	c_my(%a1),%d1		/* Get the screen row in %d1 */
	sub.l	c_hpy(%a1),%d1		/* And get the hot point offset */
	mulu.l	%d0,%d1			/* Now get the row pixel address */
	mov.l	(%a1),%d2		/* now get the screen col (c_mx == 0) */
	sub.l	c_hpx(%a1),%d2		/* And get the hot point offset */
	bgt	chkrm45%
chklm45%:
	mov.l	%d2,%d7
	cmp.l	%d7,&-16
	bge	clip45%
	mov.l	&-16,%d7
	br	clip45%
chkrm45%:
	mov.l	%d0,%d7
	lsr.l	&2,%d7			/* turn back into pixel address */
	sub.l	%d2,%d7
	cmp.l	%d7,&16
	bls	clip45%
	mov.l	&16,%d7
clip45%:
	mov.w	(clipmask,%d7*2),%d7
	lsl	&2,%d2			/* make it a bit increment */
	add.l	%d1,%d2			/* now we have the pixel address */
loop48%:
	bge	enter43%		/* if before the beginning of the */
	add.w	&2,%a0			/* bump the mask pointer */
	add.w	&8,%a3			/* bump the cursor pointer */
	add.l	%d0,%d2			/* increment the row address */
	dbra	%d4,loop48%		/* dbra doesnt affect the condition codes */
	movm.l	(%sp)+,&RESTREGS
	rts
enter43%:
	mov.w	%d4,loopcnt
loop43%:
	mov.l	&0,%d4
	mov.w	(%a0)+,%d3		/* get the cursor mask */
	mov.l	&15,%d5
loop44%:
	bfextu	(%a2){%d2:&4},%d1	/* grab the old data */
	bfins	%d1,(%a4){%d4:&4}	/* store it away */
	btst	%d5,%d7			/* see if we're being clipped */
	beq	next45%
	bfextu	(%a3){%d4:&4},%d6	/* grab the new cursor */
	btst	%d5,%d3			/* test the mask msb */
	bne	stuff44%
	eor.b	%d1,%d6			/* XOR the data with underlying pixel */
stuff44%:
	bfins	%d6,(%a2){%d2:&4}	/* stuff it on the screen */
next45%:
	add.l	&4,%d2			/* bump screen bit offset */
	add.l	&4,%d4			/* bump data/cursor bit offset */
	dbra	%d5,loop44%
	sub.l	&64,%d2			/* restore original bit offset */
	add.l	%d0,%d2			/* increment screen address */
	add.w	&8,%a3
	add.w	&8,%a4
	sub.w	&1,loopcnt
	bpl	loop43%
	movm.l	(%sp)+,&RESTREGS
	rts



	global ui_small8
ui_small8:
	movm.l	&SAVEREGS,-(%sp)	/* save a2-a3,d2-d7 */
	mov.l	VP(%sp),%a2		/* beginning of frame buffer */
	mov.l	UIP(%sp),%a1		/* user interface structure pointer */
	mov.l	c_smx(%a1),%d0		/* screen row increment */
	tst.l	VISIBLE(%a7)		/* only restore if saved */
	beq	skip81%

	/*
    	 *	First restore the old data
	 */
	mov.l	OLDHEIGHT(%sp),%d4	/* initialize loop counter */
	sub.l	&1,%d4			/* adjust for dbra */
	lea.l	c_data(%a1),%a3
	mov.l	c_cy(%a1),%d1		/* Get the screen row in %d1 */
	sub.l	c_hpy(%a1),%d1		/* And get the hot point offset */
	mulu.l	%d0,%d1			/* Now get the row pixel address */
	mov.l	c_cx(%a1),%d2		/* now get the screen col */
	sub.l	c_hpx(%a1),%d2		/* And get the hot point offset */
	add.l	%d1,%d2			/* now we have the pixel address */
loop80%:				/* if before the beginning of the */
	bge	loop81%			/* 	screen skip on past */
	add.l	%d0,%d2			/* increment the screen address */
	dbra	%d4,loop80%		/* dbra doesn't affect the condition codes */
	br	skip81%
loop81%:
	lea.l	(%a2,%d2.l),%a0
	mov.l	(%a3)+,(%a0)+
	mov.l	(%a3)+,(%a0)+
	mov.l	(%a3)+,(%a0)+
	mov.l	(%a3)+,(%a0)
	add.l	%d0,%d2			/* increment the screen address */
	dbra	%d4,loop81%

	/*
    	 *	Then save new data + draw the cursor
	 */
skip81%:
	mov.l	NEWHEIGHT(%sp),%d4	/* initialize loop counter */
	sub.l	&1,%d4			/* adjust for dbra */
	lea.l	c_data(%a1),%a4
	lea.l	c_cursor(%a1),%a3
	lea.l	c_mask(%a1),%a0
	mov.l	c_my(%a1),%d1		/* Get the screen row in %d1 */
	sub.l	c_hpy(%a1),%d1		/* And get the hot point offset */
	mulu.l	%d0,%d1			/* Now get the row pixel address */
	mov.l	(%a1),%d2		/* now get the screen col (c_mx == 0) */
	sub.l	c_hpx(%a1),%d2		/* And get the hot point offset */
	bgt	chkrm85%
chklm85%:
	mov.l	%d2,%d7
	cmp.l	%d7,&-16
	bge	clip85%
	mov.l	&-16,%d7
	br	clip85%
chkrm85%:
	mov.l	%d0,%d7
	sub.l	%d2,%d7
	cmp.l	%d7,&16
	bls	clip85%
	mov.l	&16,%d7
clip85%:
	mov.w	(clipmask,%d7*2),%d7
	add.l	%d1,%d2			/* now we have the pixel address */
loop88%:
	bge	enter83%		/* if before the beginning of the */
	add.w	&2,%a0			/* bump the mask pointer */
	add.w	&16,%a3			/* bump the data pointer */
	add.l	%d0,%d2			/* increment the row address */
	dbra	%d4,loop88%		/* dbra doesnt affect the condtion codes */
	movm.l	(%sp)+,&RESTREGS
	rts
enter83%:
	lea.l	(%a2,%d2.l),%a2		/* compute initial pixel pointer */
loop83%:
	mov.w	(%a0)+,%d3		/* get the cursor mask */
	mov.l	%a2,%a1			/* get pointer to screen */
	lea.l	(%a2,%d0.l),%a2		/* increment pointer by row size */
	mov.l	&15,%d5
loop84%:
	mov.b	(%a1),%d1
	mov.b	%d1,(%a4)+		/* save current pixel */
	btst	%d5,%d7			/* see if we're being clipped */
	beq	next85%
	mov.b	(%a3),%d2		/* fetch cursor data */
	btst	%d5,%d3			/* test the mask msb */
	bne	stuff84%
	eor.b	%d1,%d2			/* XOR the data with underlying pixel */
stuff84%:
	mov.b	%d2,(%a1)		/* store it away */
next85%:
	add.w	&1,%a3			/* bump pointer to cursor data */
	add.w	&1,%a1			/* bump pointer to screen memory */
	dbra	%d5,loop84%
	dbra	%d4,loop83%
	movm.l	(%sp)+,&RESTREGS
	rts




	global ui_small16
ui_small16:
	movm.l	&SAVEREGS,-(%sp)	/* save a2-a3,d2-d7 */
	mov.l	VP(%sp),%a2		/* beginning of frame buffer */
	mov.l	UIP(%sp),%a1		/* user interface structure pointer */
	mov.l	c_smx(%a1),%d0		/* screen row increment */
	lsl	&1,%d0			/* make it a byte increment */
	tst.l	VISIBLE(%a7)		/* only restore if saved */
	beq	skip61%

	/*
    	 *	First restore the old data
	 */
	mov.l	OLDHEIGHT(%sp),%d4	/* initialize loop counter */
	sub.l	&1,%d4			/* adjust for dbra */
	lea.l	c_data(%a1),%a3
	mov.l	c_cy(%a1),%d1		/* Get the screen row in %d1 */
	sub.l	c_hpy(%a1),%d1		/* And get the hot point offset */
	mulu.l	%d0,%d1			/* Now get the row pixel address */
	mov.l	c_cx(%a1),%d2		/* now get the screen col */
	sub.l	c_hpx(%a1),%d2		/* And get the hot point offset */
	lsl	&1,%d2			/* make it a byte increment */
	add.l	%d1,%d2			/* now we have the pixel address */
loop60%:				/* if before the beginning of the */
	bge	loop61%			/* 	screen skip on past */
	add.l	%d0,%d2			/* increment the screen address */
	dbra	%d4,loop60%		/* dbra doesn't affect the condition codes */
	br	skip61%
loop61%:
	lea.l	(%a2,%d2.l),%a0
	mov.l	(%a3)+,(%a0)+
	mov.l	(%a3)+,(%a0)+
	mov.l	(%a3)+,(%a0)+
	mov.l	(%a3)+,(%a0)+
	mov.l	(%a3)+,(%a0)+
	mov.l	(%a3)+,(%a0)+
	mov.l	(%a3)+,(%a0)+
	mov.l	(%a3)+,(%a0)
	add.l	%d0,%d2			/* increment the screen address */
	dbra	%d4,loop61%

	/*
    	 *	Then save new data + draw the cursor
	 */
skip61%:
	mov.l	NEWHEIGHT(%sp),%d4	/* initialize loop counter */
	sub.l	&1,%d4			/* adjust for dbra */
	lea.l	c_data(%a1),%a4
	lea.l	c_cursor(%a1),%a3
	lea.l	c_mask(%a1),%a0
	mov.l	c_my(%a1),%d1		/* Get the screen row in %d1 */
	sub.l	c_hpy(%a1),%d1		/* And get the hot point offset */
	mulu.l	%d0,%d1			/* Now get the row pixel address */
	mov.l	(%a1),%d2		/* now get the screen col (c_mx == 0) */
	sub.l	c_hpx(%a1),%d2		/* And get the hot point offset */
	bgt	chkrm65%
chklm65%:
	mov.l	%d2,%d7
	cmp.l	%d7,&-16
	bge	clip65%
	mov.l	&-16,%d7
	br	clip65%
chkrm65%:
	mov.l	%d0,%d7
	lsr.l	&1,%d7			/* turn back into a pixel address */
	sub.l	%d2,%d7
	cmp.l	%d7,&16
	bls	clip65%
	mov.l	&16,%d7
clip65%:
	mov.w	(clipmask,%d7*2),%d7
	lsl	&1,%d2			/* make it a byte increment */
	add.l	%d1,%d2			/* now we have the pixel address */
loop68%:
	bge	enter63%		/* if before the beginning of the */
	add.w	&2,%a0			/* bump the mask pointer */
	add.w	&32,%a3			/* bump the data pointer */
	add.l	%d0,%d2			/* increment the row address */
	dbra	%d4,loop68%		/* dbra doesnt affect the condtion codes */
	movm.l	(%sp)+,&RESTREGS
	rts
enter63%:
	lea.l	(%a2,%d2.l),%a2		/* compute initial pixel pointer */
loop63%:
	mov.w	(%a0)+,%d3		/* get the cursor mask */
	mov.l	%a2,%a1			/* get pointer to screen */
	lea.l	(%a2,%d0.l),%a2		/* increment pointer by row size */
	mov.l	&15,%d5
loop64%:
	mov.w	(%a1),%d1
	mov.w	%d1,(%a4)+		/* save current pixel */
	btst	%d5,%d7			/* see if we're being clipped */
	beq	next65%
	mov.w	(%a3),%d2		/* fetch cursor data */
	btst	%d5,%d3			/* test the mask msb */
	bne	stuff64%
	not.w	%d2
	eor.w	%d1,%d2			/* XOR the data with underlying pixel */
stuff64%:
	mov.w	%d2,(%a1)		/* store it away */
next65%:
	add.w	&2,%a3			/* bump pointer to cursor data */
	add.w	&2,%a1			/* bump pointer to screen memory */
	dbra	%d5,loop64%
	dbra	%d4,loop63%
	movm.l	(%sp)+,&RESTREGS
	rts



	global ui_small32
ui_small32:
	movm.l	&SAVEREGS,-(%sp)	/* save a2-a3,d2-d7 */
	mov.l	VP(%sp),%a2		/* beginning of frame buffer */
	mov.l	UIP(%sp),%a1		/* user interface structure pointer */
	mov.l	c_smx(%a1),%d0		/* screen row increment */
	lsl	&2,%d0			/* make it a byte increment */
	tst.l	VISIBLE(%a7)		/* only restore if saved */
	beq	skip31%

	/*
    	 *	First restore the old data
	 */
	mov.l	OLDHEIGHT(%sp),%d4	/* initialize loop counter */
	sub.l	&1,%d4			/* adjust for dbra */
	lea.l	c_data(%a1),%a3
	mov.l	c_cy(%a1),%d1		/* Get the screen row in %d1 */
	sub.l	c_hpy(%a1),%d1		/* And get the hot point offset */
	mulu.l	%d0,%d1			/* Now get the row pixel address */
	mov.l	c_cx(%a1),%d2		/* now get the screen col */
	sub.l	c_hpx(%a1),%d2		/* And get the hot point offset */
	lsl	&2,%d2			/* make it a byte increment */
	add.l	%d1,%d2			/* now we have the pixel address */
loop30%:				/* if before the beginning of the */
	bge	loop31%			/* 	screen skip on past */
	add.l	%d0,%d2			/* increment the screen address */
	dbra	%d4,loop30%		/* dbra doesn't affect the condition codes */
	br	skip31%
loop31%:
	lea.l	(%a2,%d2.l),%a0
	mov.l	(%a3)+,(%a0)+
	mov.l	(%a3)+,(%a0)+
	mov.l	(%a3)+,(%a0)+
	mov.l	(%a3)+,(%a0)+
	mov.l	(%a3)+,(%a0)+
	mov.l	(%a3)+,(%a0)+
	mov.l	(%a3)+,(%a0)+
	mov.l	(%a3)+,(%a0)+
	mov.l	(%a3)+,(%a0)+
	mov.l	(%a3)+,(%a0)+
	mov.l	(%a3)+,(%a0)+
	mov.l	(%a3)+,(%a0)+
	mov.l	(%a3)+,(%a0)+
	mov.l	(%a3)+,(%a0)+
	mov.l	(%a3)+,(%a0)+
	mov.l	(%a3)+,(%a0)
	add.l	%d0,%d2			/* increment the screen address */
	dbra	%d4,loop31%

	/*
    	 *	Then save new data + draw the cursor
	 */
skip31%:
	mov.l	NEWHEIGHT(%sp),%d4	/* initialize loop counter */
	sub.l	&1,%d4			/* adjust for dbra */
	lea.l	c_data(%a1),%a4
	lea.l	c_cursor(%a1),%a3
	lea.l	c_mask(%a1),%a0
	mov.l	c_my(%a1),%d1		/* Get the screen row in %d1 */
	sub.l	c_hpy(%a1),%d1		/* And get the hot point offset */
	mulu.l	%d0,%d1			/* Now get the row pixel address */
	mov.l	(%a1),%d2		/* now get the screen col (c_mx == 0) */
	sub.l	c_hpx(%a1),%d2		/* And get the hot point offset */
	bgt	chkrm35%
chklm35%:
	mov.l	%d2,%d7
	cmp.l	%d7,&-16
	bge	clip35%
	mov.l	&-16,%d7
	br	clip35%
chkrm35%:
	mov.l	%d0,%d7
	lsr.l	&2,%d7			/* turn back into a pixel address */
	sub.l	%d2,%d7
	cmp.l	%d7,&16
	bls	clip35%
	mov.l	&16,%d7
clip35%:
	mov.w	(clipmask,%d7*2),%d7
	lsl	&2,%d2			/* make it a byte increment */
	add.l	%d1,%d2			/* now we have the pixel address */
loop38%:
	bge	enter33%		/* if before the beginning of the */
	add.w	&2,%a0			/* bump the mask pointer */
	add.w	&64,%a3			/* bump the data pointer */
	add.l	%d0,%d2			/* increment the row address */
	dbra	%d4,loop38%		/* dbra doesnt affect the condtion codes */
	movm.l	(%sp)+,&RESTREGS
	rts
enter33%:
	lea.l	(%a2,%d2.l),%a2		/* compute initial pixel pointer */
loop33%:
	mov.w	(%a0)+,%d3		/* get the cursor mask */
	mov.l	%a2,%a1			/* get pointer to screen */
	lea.l	(%a2,%d0.l),%a2		/* increment pointer by row size */
	mov.l	&15,%d5
loop34%:
	mov.l	(%a1),%d1
	mov.l	%d1,(%a4)+		/* save current pixel */
	btst	%d5,%d7			/* see if we're being clipped */
	beq	next35%
	mov.l	(%a3),%d2		/* fetch cursor data */
	btst	%d5,%d3			/* test the mask msb */
	bne	stuff34%
	not.l	%d2
	eor.l	%d1,%d2			/* XOR the data with underlying pixel */
stuff34%:
	mov.l	%d2,(%a1)		/* store it away */
next35%:
	add.w	&4,%a3			/* bump pointer to cursor data */
	add.w	&4,%a1			/* bump pointer to screen memory */
	dbra	%d5,loop34%
	dbra	%d4,loop33%
	movm.l	(%sp)+,&RESTREGS
	rts



	data

	short	0x0000, 0x0001, 0x0003, 0x0007
	short	0x000f, 0x001f, 0x003f, 0x007f
	short	0x00ff, 0x01ff, 0x03ff, 0x07ff
	short	0x0fff, 0x1fff, 0x3fff, 0x7fff
clipmask:
	short	0xffff
	short	0x8000, 0xc000, 0xe000, 0xf000
	short	0xf800, 0xfc00, 0xfe00, 0xff00
	short	0xff80, 0xffc0, 0xffe0, 0xfff0
	short	0xfff8, 0xfffc, 0xfffe, 0xffff


loopcnt:
	short	0
