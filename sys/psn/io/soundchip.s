 #
 #  %Z%Copyright Apple Computer 1987	Version %I% of %M% on %E% %U%
 #
 #  Chord routine
 #
 #   Called with rts6  a3 points to ASC Chip
 #                     a4 points to parameter block
 #
 # Parameter block is as follows:
 #	 0:	volume (0-7)    00xxxyyy in binary, where  x=y
 #	 2:	spin loop speed (try 40)
 #	 6:	next note speed (try 1200)
 #	10:	count down till done (try 60000)
 #	14:	number notes to do (1-4)
 #	16:	pitch 1 (in ASC increment format) (2-4 as needed)
 #	20: 	pitch 2
 #	24: 	pitch 3
 #	28: 	pitch 4
 #
 # uses no memory and preserves d6, a3, a6 & a7 only
 #

	text
	global	chord
chord:
	movm.l	&0xFFFE,-(%sp)	 #d0-d7, a0-a6
	mov.l	64(%sp),%a4	 #get pointer to parameter block
	mov.l	&0x50F14000,%a3	 #get address of sound chip
 #	mov.w	&0x2700,%sr

	mov.b	&0x00,0x801(%a3) #quiet
	mov.b	&0x00,0x807(%a3) #22k Mac
	
	mov.w	(%a4)+,%d0	 #get volume out of parameter block
	lsl.b	&2,%d0		 #shift volume in analog position
	tst.b	0x800(%a3)	 #test if analog part (id > 0)
	bne.b	A11		 #ok for analog if id not zero
	lsl.b	&3,%d0		 #else, shift volume up to pwm position
A11:	mov.b	%d0,0x806(%a3)	 #now set chipUs volume

	mov.b	&0x00,0x802(%a3) #mono, pwm
	
	lea	0x810(%a3),%a0	 #address of wavetable data area
	clr.l	(%a0)+		 #clear phase 1
	clr.l	(%a0)+		 #clear inc 1
	clr.l	(%a0)+		 #clear phase 2
	clr.l	(%a0)+		 #clear inc 2
	clr.l	(%a0)+		 #clear phase 3
	clr.l	(%a0)+		 #clear inc 3
	clr.l	(%a0)+		 #clear phase 4
	clr.l	(%a0)+		 #clear inc 4
	mov.l	&0x7E7E7E7E,(%a0)+ #clear volumes
	mov.l	&0x7E7E7E7E,(%a0)+ #clear volumes immediatly too
	mov.l	&0x40,0x000(%a3) #data values are zero too
	mov.l	&0x40,0x200(%a3) #data values are zero too
	mov.l	&0x40,0x400(%a3) #data values are zero too
	mov.l	&0x40,0x600(%a3) #data values are zero too
	
	mov.b	&0x02,0x801(%a3) #wavetable
	mov.b	&0x00,0x803(%a3) #no one shot

	mov.l	%a3,%a0		 #fill wave memory with stuff
	mov.l	&1,%d0		 # repeat 256 byte sequence 2 times
A94:	mov.l	&0x30013F10,%d1	 # 4 byte table to fill buffer with
A93:	mov.l	&63,%d2		 # repeat 64 times
A92:	mov.b	%d1,1536(%a0)
	mov.b	%d1,1024(%a0)
	mov.b	%d1,512(%a0)
	mov.b	%d1,(%a0)+	 # move in a byte
	dbra	%d2,A92		 # done 64 times yet? if not, loop
	lsr.l	&8,%d1		 # get next byte value
	bne.b	A93		 # fill next 64 values
	dbra	%d0,A94		 # repeat the whole sequence 2 times

 #	IF 0 THEN
 #	move.l	#0x08080808,d1
 #	move.w	#31,d0	
 #@95	move.l	d1,1536(a0)
 #	move.l	d1,1024(a0)
 #	move.l	d1,512(a0)
 #	move.l	d1,(a0)+
 #	dbra	d0,@95
 #	
 #	move.l	#0x38383838,d1
 #	move.w	#31,d0	
 #@96	move.l	d1,1536(a0)
 #	move.l	d1,1024(a0)
 #	move.l	d1,512(a0)
 #	move.l	d1,(a0)+
 #	dbra	d0,@96
 #	ENDIF
	
 #d0 -- spin loop speed
 #d1 -- next note speed
 #d2 -- count down till done
 #d3 -- number notes to do
 #d4 -- spin loop counter
 #d5 -- count down till next note
 #d7 -- temp

 #a0 -- address of data for next average
 #a1 -- end of data buffer
 #a2 -- hold for average address
 #a3 -- ASC Base
 #a4 -- next note pitch
 #a5 -- location for next pitch
 #a6 -- return address

	movm.l	(%a4)+,&0x0007	 #set up init values and point at # of pitches
				 #%d0-%d2	
	mov.w	(%a4)+,%d3	 #set up number of pitches and point at pitches
	clr.l	%d5		 #next note start immediatly

	lea	0x814(%a3),%a5	 #where pitchs go
	mov.l	%a3,%a0		 #start of data
	lea	0x200(%a3),%a1	 #end of data + 1
	bra.b	A9
	
A8:
	mov.b	(%a0),%d7	 #get current sample
	mov.l	%a0,%a2		 #save address for later store back
	add.w	&1,%a0		 #bump to next address
	cmp.l	%a1,%a0		 #have we hit the end?
	bhi.b	A1		 #branch around if not
	mov.l	%a3,%a0		 #reset to start of data
A1:	add.b	(%a0),%d7	 #add next sample
	lsr.b	&1,%d7		 #average them together
	
	mov.b	%d7,1536(%a0)	 #store result in all four wave tables
	mov.b	%d7,1024(%a0)
	mov.b	%d7,512(%a0)
	mov.b	%d7,(%a0)
	
	mov.w	%d0,%d4		 #now spin for awhile
A4:	sub.w	&1,%d4
	bpl.b	A4

A9:	tst.w	%d3		 #any more notes to add?
	beq.b	A2		 #branch if none
	dbra	%d5,A2		 #time for next note?, branch if not
	mov.w	%d1,%d5		 #restart note counter
	mov.l	(%a4)+,(%a5)+	 #move the pitch
	add.l	&4,%a5		 #bump past phase
	sub.w	&1,%d3		 #now done one more note

A2:	dbra	%d2,A8		 #one more average done, do more if needed

				 #all done, silence the lot
	lea	0x814(%a3),%a0	 #address of first pitch
	clr.l	(%a0)+		 #clear inc 1
	add.l	&4,%a0		 #skip phase 2
	clr.l	(%a0)+		 #clear inc 2
	add.l	&4,%a0		 #skip phase 3
	clr.l	(%a0)+		 #clear inc 3
	add.l	&4,%a0		 #skip phase 4
	clr.l	(%a0)+		 #clear inc 4
	clr.b	0x801(%a3)	 #and put into quiet mode

 #	mov.w	&0x2000,%sr
	movm.l	(%sp)+,&0x7FFF	 #d0-d7, a0-a6
	rts

