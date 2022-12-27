 # @(#)gcr_write.s  {Apple version 1.1 89/08/15 11:44:51}
 #
 # Copyright (c) 1987, 1988, 1989 by Apple Computer, Inc.
 # All Rights Reserved.
 #
 # THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF APPLE COMPUTER, INC.
 # The copyright notice above does not evidence any actual or
 # intended publication of such source code.


 #	_sccsid[]="@(#)gcr_write.s  {Apple version 1.1 89/08/15 11:44:51}";

 #
 # Assembler version of write routine.
 #
	file	"gcr_write"

#include <sys/uconfig.h>
#include "fd.h"
#include "fdhw.h"

#define	sect	%a6			/* sector number */
#define	vp	%a5			/* VIA1 addr for slowdowns */
#define write	%a4			/* iwm write register */
#define	shake	%a3			/* iwm write-handshake register */
#define	nibl	%a2			/* nibl table address */
#define	buf	%a1			/* input data buffer */
/* a0 is temp */

#define	timer	%d7
#define	sumA	%d6			/* checksum A */
#define	sumB	%d5			/* checksum B */
#define	sumC	%d4			/* checksum C */
#define	tops	%d3			/* top bits */
#define	byteacc	%d2			/* data byte A/B/C accumulator */
#define	loop	%d1			/* loop counter */
/* d0 is temp use */

	text
	even

 #############
 # int dataout(loop,buf,nibl)	# returns err code or 0 in %d0
 # Notes: no registers saved!

dataout:

	mov	&0,tops
	mov	&0,byteacc

 # Byte A:

	mov.b	(buf)+,%d0
	addx.b	%d0,sumA		# sumA = sumA + byteA + carry
	eor.b	sumC,%d0		# byteA = byteA ^ sumC

	mov.b	%d0,byteacc		# save A for later (byteacc = 000A)
	ror.l	&8,byteacc		# byteacc = A000

	mov.b	%d0,tops		# tops = 00000000 AAaaaaaa
	rol.w	&2,tops			# tops = 000000AA aaaaaa00

 # Byte B:

	mov.b	(buf)+,%d0
	addx.b	%d0,sumB		# sumB = sumB + byteB + carry
	eor.b	sumA,%d0		# byteB = byteB ^ sumA

	mov.b	%d0,byteacc		# save B for later (byteacc = A00B)
	ror.l	&8,byteacc		# byteacc = BA00

	mov.b	%d0,tops		# tops = 000000AA BBbbbbbb
	rol.w	&2,tops			# tops = 0000AABB bbbbbb00

 # Byte C:

	mov.b	(buf)+,%d0
	addx.b	%d0,sumC		# sumC = sumC + byteC + carry
	eor.b	sumB,%d0		# byteC = byteC ^ sumB

	mov.b	%d0,byteacc		# save C (byteacc = BA0C)

	mov.b	%d0,tops		# tops = 0000AABB CCcccccc

 # NB: using LONG rotate ensures zeroes come in from high word!

	ror.l	&6,tops			# tops = 00000000 00AABBCC

 # Write tops. We don't check for lockup on this one because we
 # had a lot to do above.

wtops:
	tst.b	(vp)			# slowdown by a VIA access
	tst.b	(shake)
	bpl.b	wtops

	mov.b	(nibl,tops),(write)	# write the tops byte

 # checksum rotation here for timing.
	mov.b	sumC,%d0
	lsl.b	&1,%d0			# bit7 --> x
	rol.b	&1,sumC

	swap	byteacc			# byteacc was BA0C, now 0CBA
	and.l	&0x003f3f3f,byteacc	# dump hi bits

	mov	&0,%d0
	mov.w	gcr_writetime,timer
writeA:
	tst.b	(vp)			# slowdown by a VIA access
	tst.b	(shake)
	bmi.b	writeAOK
	dbra	timer,writeA		# -> keep checking
	bra.b	doutlock		# locked up!

writeAOK:
	mov.b	byteacc,%d0
	mov.b	(nibl,%d0),(write)	# nibblize A and write it

	mov.w	gcr_writetime,timer
	ror.l	&8,byteacc		# AxCB
writeB:
	tst.b	(vp)			# slowdown by a VIA access
	tst.b	(shake)
	bmi.b	writeBOK
	dbra	timer,writeB		# -> keep checking
	bra.b	doutlock		# locked up!

writeBOK:
	mov.b	byteacc,%d0
	mov.b	(nibl,%d0),(write)	# nibblize B and write it

	mov.w	gcr_writetime,timer
	ror.l	&8,byteacc		# BAxC
writeC:
	tst.b	(vp)			# slowdown by a VIA access
	tst.b	(shake)
	bmi.b	writeCOK
	dbra	timer,writeC		# -> keep checking
	bra.b	doutlock		# locked up!

writeCOK:
	mov.b	byteacc,%d0
	mov.b	(nibl,%d0),(write)	# nibblize C and write it

	dbra	loop,dataout
	mov	&0,%d0			# good return code
	bra.b	doutret

doutlock:
	mov	&EWTIMEOUT,%d0

doutret:
	tst.b	(shake)			# get away from write reg
	mov	%d0,%d0			# set cc's
	rts

 #############
 #int gcr_write(buf,sect)
 # char *buf;
 # int sect;				# sector number
 # NOTE: Must be called at high spl to prevent interrupts

	global	gcr_write
gcr_write:
 #	trapf				# force crash on < MC68020 cpus
	mov.l	4(%sp),%a0		# buffer address
	mov.l	8(%sp),%d0		# sector number
	movm.l	&S_ALL,-(%sp)		# save d2-d7, a2-a6
	mov.l	%a0,buf
	and.l	&0x3f,%d0		# drop any junk bits in sector number
	mov.l	%d0,sect

	mov.l	iwm_addr,%a0
	mov.l	&VIA1_ADDR,vp

 #### The first byte is written "blind" to get the chip started off.

	lea	Q6H(%a0),write		# point to write reg
	tst.b	(write)
	mov.b	&0xff,Q7H(%a0)		# first byte goes out immediately

	lea	Q6L(%a0),shake		# point to write-handshake reg

 #### Write data marks:

	mov.l	&gcr_writedm,%a0
	mov.w	gcr_writetime,timer

wrtdm:
	tst.b	(vp)			# slowdown by a VIA access
	tst.b	(shake)
	bmi.b	wrtdmOK
	dbra	timer,wrtdm		# -> keep checking
	bra	lockup			# locked up!

wrtdmOK:
	mov.b	(%a0)+,(write)		# write the mark
	tst.b	(%a0)			# more marks to do?
	bne.b	wrtdm			# -> yup

	mov.l	&gcr_nibl,nibl		# nibble table base

 #### Write the sector number:

	mov.w	gcr_writetime,timer
writesect:
	tst.b	(vp)			# slowdown by a VIA access
	tst.b	(shake)
	bmi.b	writesectOK
	dbra	timer,writesect		# -> keep checking
	bra	lockup			# locked up!

writesectOK:
	mov.b	(nibl,sect),(write)	# nibblize sector and write it

 #### Write a dummy tag field:

wrttag:
	mov	&0,sumA			# clear checksums
	mov	&0,sumB
	mov	&0,sumC
	lsl.b	&1,sumA			# clears X (used as carry)

	mov	&((12 / 3) - 1),loop	# 4 groups of 3, gets 12
	mov.l	buf,savebuf		# keep buf untouched
	mov.l	&fd_nulltag,buf		# dummy tag field
	bsr.w	dataout			# write the tag field
	bne	return			# -> error
	mov.l	savebuf,buf		# pop buffer address

 #### Write data.  We write 510 bytes first (groups of 3).  Later we will
 ####  do the last two bytes (since not an even group).

	mov	&((510 / 3) - 1),loop	# 170 groups of 3, gets 510
	bsr.w	dataout
	bne	return			# -> error

 #### Write the last two bytes (the odd group):

	mov	&0,tops
	mov.b	(buf)+,%d0

	addx.b	%d0,sumA		# sumA = sumA + byteA + carry
	eor.b	sumC,%d0		# byteA = byteA ^ sumC

	mov.l	%d0,byteacc		# save A for later (byteacc = 000A)
	swap	byteacc			# byteacc = 0A00

	mov.b	%d0,tops		# tops = 00000000 AAaaaaaa
	rol.w	&2,tops			# tops = 000000AA aaaaaa00

	mov.b	(buf)+,%d0

	addx.b	%d0,sumB		# sumB = sumB + byteB + carry
	eor.b	sumA,%d0		# byteB = byteB ^ sumA

	mov.b	%d0,byteacc		# byteacc = 0A0B

	mov.b	%d0,tops		# tops = 000000AA BBbbbbbb

 # NB: using LONG rotate ensures zeroes come in from high word!

	ror.l	&6,tops			# tops = 00000000 0000AABB
	rol.w	&2,tops			# tops = 00000000 00AABB00


	mov.w	gcr_writetime,timer
writetops:
	tst.b	(vp)			# slowdown by a VIA access
	tst.b	(shake)
	bmi.b	writetopsOK
	dbra	timer,writetops		# -> keep checking
	bra	lockup			# locked up!

writetopsOK:
	mov.b	(nibl,tops),(write)	# write tops
	
	swap	byteacc			# byteacc = 0B0A
	and.l	&0x003f003f,byteacc

	mov.w	gcr_writetime,timer
	mov	&0,%d0
	mov.b	byteacc,%d0		# recover A
writelastA:
	tst.b	(vp)			# slowdown by a VIA access
	tst.b	(shake)
	bmi.b	writelastAOK
	dbra	timer,writelastA	# -> keep checking
	bra	lockup			# locked up!

writelastAOK:
	mov.b	(nibl,%d0),(write)	# write A

	swap	byteacc

	mov.w	gcr_writetime,timer
	mov.b	byteacc,%d0		# recover B
writelastB:
	tst.b	(vp)			# slowdown by a VIA access
	tst.b	(shake)
	bmi.b	writelastBOK
	dbra	timer,writelastB	# -> keep checking
	bra	lockup			# locked up!

writelastBOK:
	mov.b	(nibl,%d0),(write)	# write B

 #### Write the checksum bytes:

L%sum:
	mov	&0,tops
	mov.b	sumA,tops		# tops = 00000000 AAaaaaaa
	rol.w	&2,tops			# tops = 000000AA aaaaaa00
	mov.b	sumB,tops		# tops = 000000AA BBbbbbbb
	rol.w	&2,tops			# tops = 0000AABB bbbbbb00
	mov.b	sumC,tops		# tops = 0000AABB CCcccccc

 # NB: using LONG rotate ensures zeroes come in from high word!

	ror.l	&6,tops			# tops = 00000000 00AABBCC

	mov.w	gcr_writetime,timer
writesumtops:
	tst.b	(vp)			# slowdown by a VIA access
	tst.b	(shake)
	bmi.b	writesumtopsOK
	dbra	timer,writesumtops	# -> keep checking
	bra	lockup			# locked up!

writesumtopsOK:
	mov.b	(nibl,tops),(write)	# write tops

	mov.w	gcr_writetime,timer
	and.b	&0x3f,sumA
writesumA:
	tst.b	(vp)			# slowdown by a VIA access
	tst.b	(shake)
	bmi.b	writesumAOK
	dbra	timer,writesumA		# -> keep checking
	bra	lockup			# locked up!

writesumAOK:
	mov.b	(nibl,sumA),(write)

	mov.w	gcr_writetime,timer
	and.b	&0x3f,sumB
writesumB:
	tst.b	(vp)			# slowdown by a VIA access
	tst.b	(shake)
	bmi.b	writesumBOK
	dbra	timer,writesumB		# -> keep checking
	bra.s	lockup			# locked up!

writesumBOK:
	mov.b	(nibl,sumB),(write)

	mov.w	gcr_writetime,timer
	and.b	&0x3f,sumC
writesumC:
	tst.b	(vp)			# slowdown by a VIA access
	tst.b	(shake)
	bmi.b	writesumCOK
	dbra	timer,writesumC		# -> keep checking
	bra.b	lockup			# locked up!

writesumCOK:
	mov.b	(nibl,sumC),(write)

 #### Write bit slip marks:

	mov.w	gcr_writetime,timer
wslip1:
	tst.b	(vp)			# slowdown by a VIA access
	tst.b	(shake)
	bmi.b	wslip1OK
	dbra	timer,wslip1		# -> keep checking
	bra.b	lockup			# locked up!
wslip1OK:
	mov.b	&0xde,(write)
    
	mov.w	gcr_writetime,timer
wslip2:
	tst.b	(vp)			# slowdown by a VIA access
	tst.b	(shake)
	bmi.b	wslip2OK
	dbra	timer,wslip2		# -> keep checking
	bra.b	lockup			# locked up!
wslip2OK:
	mov.b	&0xaa,(write)
    
	mov.w	gcr_writetime,timer
wslip3:
	tst.b	(vp)			# slowdown by a VIA access
	tst.b	(shake)
	bmi.b	wslip3OK
	dbra	timer,wslip3		# -> keep checking
	bra.b	lockup			# locked up!
wslip3OK:
	mov.b	&0xff,(write)
    
	mov.w	gcr_writetime,timer
wslip4:
	tst.b	(vp)			# slowdown by a VIA access
	mov.b	(shake),%d1
	bmi.b	wslip4OK
	dbra	timer,wslip4		# -> keep checking
	bra.b	lockup			# locked up!
wslip4OK:
	mov.b	&0xff,(write)

	mov	&0,%d0			# assume good
	and.b	&0x40,%d1		# underrun?
	bne.b	return			# -> ok
	mov	&EWUNDERRUN,%d0
	bra.b	return
    
lockup:
	mov	&EWTIMEOUT,%d0		# lockup...timeout

return:
	mov.l	iwm_addr,%a0
	tst.b	Q7L(%a0)		# out of write mode
	movm.l	(%sp)+,&R_ALL		# restore d2-d7, a2-a6
	rts

	data

savebuf:
	long	0

#undef byte

	global	fd_nulltag
fd_nulltag:
	byte	0,0,0,0,0,0,0,0,0,0,0,0

	global	gcr_writetime
gcr_writetime:
	short	2700			# about 1 ms (@ 16MHz) is plenty

	global	gcr_writedm
gcr_writedm:
	byte	0x3f,0xcf,0xf3,0xfc,0xff
	byte	0xd5,0xaa,0xad,0

	global	gcr_nibl
gcr_nibl:
	byte	0x96,0x97,0x9a,0x9b,0x9d,0x9e,0x9f,0xa6		# 00 - 07
	byte	0xa7,0xab,0xac,0xad,0xae,0xaf,0xb2,0xb3		# 08 - 0f
	byte	0xb4,0xb5,0xb6,0xb7,0xb9,0xba,0xbb,0xbc		# 10 - 17
	byte	0xbd,0xbe,0xbf,0xcb,0xcd,0xce,0xcf,0xd3		# 18 - 1f
	byte	0xd6,0xd7,0xd9,0xda,0xdb,0xdc,0xdd,0xde		# 20 - 27
	byte	0xdf,0xe5,0xe6,0xe7,0xe9,0xea,0xeb,0xec		# 28 - 2f
	byte	0xed,0xee,0xef,0xf2,0xf3,0xf4,0xf5,0xf6		# 30 - 37
	byte	0xf7,0xf9,0xfa,0xfb,0xfc,0xfd,0xfe,0xff		# 38 - 3f
