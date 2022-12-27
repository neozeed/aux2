# 1 ""
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


# 1 "../../../../usr/include/sys/uconfig.h"

































































# 68 "../../../../usr/include/sys/uconfig.h"






































# 19 ""

# 1 "./fd.h"

















# 28 "./fd.h"



































































# 282 "./fd.h"



































# 20 ""

# 1 "./fdhw.h"














# 19 "./fdhw.h"































































# 171 "./fdhw.h"













# 21 ""


















	text
	even

 #############
 # int dataout(%d1,%a1,%a2)	# returns err code or 0 in %d0
 # Notes: no registers saved!

dataout:

	mov	&0,%d3
	mov	&0,%d2

 # Byte A:

	mov.b	(%a1)+,%d0
	addx.b	%d0,%d6		# %d6 = %d6 + byteA + carry
	eor.b	%d4,%d0		# byteA = byteA ^ %d4

	mov.b	%d0,%d2		# save A for later (%d2 = 000A)
	ror.l	&8,%d2		# %d2 = A000

	mov.b	%d0,%d3		# %d3 = 00000000 AAaaaaaa
	rol.w	&2,%d3			# %d3 = 000000AA aaaaaa00

 # Byte B:

	mov.b	(%a1)+,%d0
	addx.b	%d0,%d5		# %d5 = %d5 + byteB + carry
	eor.b	%d6,%d0		# byteB = byteB ^ %d6

	mov.b	%d0,%d2		# save B for later (%d2 = A00B)
	ror.l	&8,%d2		# %d2 = BA00

	mov.b	%d0,%d3		# %d3 = 000000AA BBbbbbbb
	rol.w	&2,%d3			# %d3 = 0000AABB bbbbbb00

 # Byte C:

	mov.b	(%a1)+,%d0
	addx.b	%d0,%d4		# %d4 = %d4 + byteC + carry
	eor.b	%d5,%d0		# byteC = byteC ^ %d5

	mov.b	%d0,%d2		# save C (%d2 = BA0C)

	mov.b	%d0,%d3		# %d3 = 0000AABB CCcccccc

 # NB: using LONG rotate ensures zeroes come in from high word!

	ror.l	&6,%d3			# %d3 = 00000000 00AABBCC

 # Write %d3. We don't check for lockup on this one because we
 # had a lot to do above.

wtops:
	tst.b	(%a5)			# slowdown by a VIA access
	tst.b	(%a3)
	bpl.b	wtops

	mov.b	(%a2,%d3),(%a4)	# %a4 the %d3 byte

 # checksum rotation here for timing.
	mov.b	%d4,%d0
	lsl.b	&1,%d0			# bit7 --> x
	rol.b	&1,%d4

	swap	%d2			# %d2 was BA0C, now 0CBA
	and.l	&0x003f3f3f,%d2	# dump hi bits

	mov	&0,%d0
	mov.w	gcr_writetime,%d7
writeA:
	tst.b	(%a5)			# slowdown by a VIA access
	tst.b	(%a3)
	bmi.b	writeAOK
	dbra	%d7,writeA		# -> keep checking
	bra.b	doutlock		# locked up!

writeAOK:
	mov.b	%d2,%d0
	mov.b	(%a2,%d0),(%a4)	# nibblize A and %a4 it

	mov.w	gcr_writetime,%d7
	ror.l	&8,%d2		# AxCB
writeB:
	tst.b	(%a5)			# slowdown by a VIA access
	tst.b	(%a3)
	bmi.b	writeBOK
	dbra	%d7,writeB		# -> keep checking
	bra.b	doutlock		# locked up!

writeBOK:
	mov.b	%d2,%d0
	mov.b	(%a2,%d0),(%a4)	# nibblize B and %a4 it

	mov.w	gcr_writetime,%d7
	ror.l	&8,%d2		# BAxC
writeC:
	tst.b	(%a5)			# slowdown by a VIA access
	tst.b	(%a3)
	bmi.b	writeCOK
	dbra	%d7,writeC		# -> keep checking
	bra.b	doutlock		# locked up!

writeCOK:
	mov.b	%d2,%d0
	mov.b	(%a2,%d0),(%a4)	# nibblize C and %a4 it

	dbra	%d1,dataout
	mov	&0,%d0			# good return code
	bra.b	doutret

doutlock:
	mov	&6,%d0

doutret:
	tst.b	(%a3)			# get away from %a4 reg
	mov	%d0,%d0			# set cc's
	rts

 #############
 #int gcr_write(%a1,%a6)
 # char *%a1;
 # int %a6;				# sector number
 # NOTE: Must be called at high spl to prevent interrupts

	global	gcr_write
gcr_write:
 #	trapf				# force crash on < MC68020 cpus
	mov.l	4(%sp),%a0		# buffer address
	mov.l	8(%sp),%d0		# sector number
	movm.l	&(0x3f00 + 0x003e),-(%sp)		# save d2-d7, a2-a6
	mov.l	%a0,%a1
	and.l	&0x3f,%d0		# drop any junk bits in sector number
	mov.l	%d0,%a6

	mov.l	iwm_addr,%a0
	mov.l	&0x50000000,%a5

 #### The first byte is written "blind" to get the chip started off.

	lea	0x1a00(%a0),%a4		# point to %a4 reg
	tst.b	(%a4)
	mov.b	&0xff,0x1e00(%a0)		# first byte goes out immediately

	lea	0x1800(%a0),%a3		# point to %a4-handshake reg

 #### Write data marks:

	mov.l	&gcr_writedm,%a0
	mov.w	gcr_writetime,%d7

wrtdm:
	tst.b	(%a5)			# slowdown by a VIA access
	tst.b	(%a3)
	bmi.b	wrtdmOK
	dbra	%d7,wrtdm		# -> keep checking
	bra	lockup			# locked up!

wrtdmOK:
	mov.b	(%a0)+,(%a4)		# %a4 the mark
	tst.b	(%a0)			# more marks to do?
	bne.b	wrtdm			# -> yup

	mov.l	&gcr_nibl,%a2		# nibble table base

 #### Write the sector number:

	mov.w	gcr_writetime,%d7
writesect:
	tst.b	(%a5)			# slowdown by a VIA access
	tst.b	(%a3)
	bmi.b	writesectOK
	dbra	%d7,writesect		# -> keep checking
	bra	lockup			# locked up!

writesectOK:
	mov.b	(%a2,%a6),(%a4)	# nibblize sector and %a4 it

 #### Write a dummy tag field:

wrttag:
	mov	&0,%d6			# clear checksums
	mov	&0,%d5
	mov	&0,%d4
	lsl.b	&1,%d6			# clears X (used as carry)

	mov	&((12 / 3) - 1),%d1	# 4 groups of 3, gets 12
	mov.l	%a1,savebuf		# keep %a1 untouched
	mov.l	&fd_nulltag,%a1		# dummy tag field
	bsr.w	dataout			# %a4 the tag field
	bne	return			# -> error
	mov.l	savebuf,%a1		# pop buffer address

 #### Write data.  We %a4 510 bytes first (groups of 3).  Later we will
 ####  do the last two bytes (since not an even group).

	mov	&((510 / 3) - 1),%d1	# 170 groups of 3, gets 510
	bsr.w	dataout
	bne	return			# -> error

 #### Write the last two bytes (the odd group):

	mov	&0,%d3
	mov.b	(%a1)+,%d0

	addx.b	%d0,%d6		# %d6 = %d6 + byteA + carry
	eor.b	%d4,%d0		# byteA = byteA ^ %d4

	mov.l	%d0,%d2		# save A for later (%d2 = 000A)
	swap	%d2			# %d2 = 0A00

	mov.b	%d0,%d3		# %d3 = 00000000 AAaaaaaa
	rol.w	&2,%d3			# %d3 = 000000AA aaaaaa00

	mov.b	(%a1)+,%d0

	addx.b	%d0,%d5		# %d5 = %d5 + byteB + carry
	eor.b	%d6,%d0		# byteB = byteB ^ %d6

	mov.b	%d0,%d2		# %d2 = 0A0B

	mov.b	%d0,%d3		# %d3 = 000000AA BBbbbbbb

 # NB: using LONG rotate ensures zeroes come in from high word!

	ror.l	&6,%d3			# %d3 = 00000000 0000AABB
	rol.w	&2,%d3			# %d3 = 00000000 00AABB00


	mov.w	gcr_writetime,%d7
writetops:
	tst.b	(%a5)			# slowdown by a VIA access
	tst.b	(%a3)
	bmi.b	writetopsOK
	dbra	%d7,writetops		# -> keep checking
	bra	lockup			# locked up!

writetopsOK:
	mov.b	(%a2,%d3),(%a4)	# %a4 %d3
	
	swap	%d2			# %d2 = 0B0A
	and.l	&0x003f003f,%d2

	mov.w	gcr_writetime,%d7
	mov	&0,%d0
	mov.b	%d2,%d0		# recover A
writelastA:
	tst.b	(%a5)			# slowdown by a VIA access
	tst.b	(%a3)
	bmi.b	writelastAOK
	dbra	%d7,writelastA	# -> keep checking
	bra	lockup			# locked up!

writelastAOK:
	mov.b	(%a2,%d0),(%a4)	# %a4 A

	swap	%d2

	mov.w	gcr_writetime,%d7
	mov.b	%d2,%d0		# recover B
writelastB:
	tst.b	(%a5)			# slowdown by a VIA access
	tst.b	(%a3)
	bmi.b	writelastBOK
	dbra	%d7,writelastB	# -> keep checking
	bra	lockup			# locked up!

writelastBOK:
	mov.b	(%a2,%d0),(%a4)	# %a4 B

 #### Write the checksum bytes:

L%sum:
	mov	&0,%d3
	mov.b	%d6,%d3		# %d3 = 00000000 AAaaaaaa
	rol.w	&2,%d3			# %d3 = 000000AA aaaaaa00
	mov.b	%d5,%d3		# %d3 = 000000AA BBbbbbbb
	rol.w	&2,%d3			# %d3 = 0000AABB bbbbbb00
	mov.b	%d4,%d3		# %d3 = 0000AABB CCcccccc

 # NB: using LONG rotate ensures zeroes come in from high word!

	ror.l	&6,%d3			# %d3 = 00000000 00AABBCC

	mov.w	gcr_writetime,%d7
writesumtops:
	tst.b	(%a5)			# slowdown by a VIA access
	tst.b	(%a3)
	bmi.b	writesumtopsOK
	dbra	%d7,writesumtops	# -> keep checking
	bra	lockup			# locked up!

writesumtopsOK:
	mov.b	(%a2,%d3),(%a4)	# %a4 %d3

	mov.w	gcr_writetime,%d7
	and.b	&0x3f,%d6
writesumA:
	tst.b	(%a5)			# slowdown by a VIA access
	tst.b	(%a3)
	bmi.b	writesumAOK
	dbra	%d7,writesumA		# -> keep checking
	bra	lockup			# locked up!

writesumAOK:
	mov.b	(%a2,%d6),(%a4)

	mov.w	gcr_writetime,%d7
	and.b	&0x3f,%d5
writesumB:
	tst.b	(%a5)			# slowdown by a VIA access
	tst.b	(%a3)
	bmi.b	writesumBOK
	dbra	%d7,writesumB		# -> keep checking
	bra.s	lockup			# locked up!

writesumBOK:
	mov.b	(%a2,%d5),(%a4)

	mov.w	gcr_writetime,%d7
	and.b	&0x3f,%d4
writesumC:
	tst.b	(%a5)			# slowdown by a VIA access
	tst.b	(%a3)
	bmi.b	writesumCOK
	dbra	%d7,writesumC		# -> keep checking
	bra.b	lockup			# locked up!

writesumCOK:
	mov.b	(%a2,%d4),(%a4)

 #### Write bit slip marks:

	mov.w	gcr_writetime,%d7
wslip1:
	tst.b	(%a5)			# slowdown by a VIA access
	tst.b	(%a3)
	bmi.b	wslip1OK
	dbra	%d7,wslip1		# -> keep checking
	bra.b	lockup			# locked up!
wslip1OK:
	mov.b	&0xde,(%a4)
    
	mov.w	gcr_writetime,%d7
wslip2:
	tst.b	(%a5)			# slowdown by a VIA access
	tst.b	(%a3)
	bmi.b	wslip2OK
	dbra	%d7,wslip2		# -> keep checking
	bra.b	lockup			# locked up!
wslip2OK:
	mov.b	&0xaa,(%a4)
    
	mov.w	gcr_writetime,%d7
wslip3:
	tst.b	(%a5)			# slowdown by a VIA access
	tst.b	(%a3)
	bmi.b	wslip3OK
	dbra	%d7,wslip3		# -> keep checking
	bra.b	lockup			# locked up!
wslip3OK:
	mov.b	&0xff,(%a4)
    
	mov.w	gcr_writetime,%d7
wslip4:
	tst.b	(%a5)			# slowdown by a VIA access
	mov.b	(%a3),%d1
	bmi.b	wslip4OK
	dbra	%d7,wslip4		# -> keep checking
	bra.b	lockup			# locked up!
wslip4OK:
	mov.b	&0xff,(%a4)

	mov	&0,%d0			# assume good
	and.b	&0x40,%d1		# underrun?
	bne.b	return			# -> ok
	mov	&10,%d0
	bra.b	return
    
lockup:
	mov	&6,%d0		# lockup...timeout

return:
	mov.l	iwm_addr,%a0
	tst.b	0x1c00(%a0)		# out of %a4 mode
	movm.l	(%sp)+,&(0x00fc + 0x7c00)		# restore d2-d7, a2-a6
	rts

	data

savebuf:
	long	0



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
