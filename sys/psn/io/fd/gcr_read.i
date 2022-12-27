# 1 ""
 # @(#)gcr_read.s  {Apple version 1.1 89/08/15 11:44:45}
 #
 # Copyright (c) 1987, 1988, 1989 by Apple Computer, Inc.
 # All Rights Reserved.
 #
 # THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF APPLE COMPUTER, INC.
 # The copyright notice above does not evidence any actual or
 # intended publication of such source code.


 #	_sccsid[]="@(#)gcr_read.s  {Apple version 1.1 89/08/15 11:44:45}";

 #
 # Assembler version of read routine.
 #
	file	"gcr_read"

 # IWM may not be hit faster than 8*FCLK (512ns) in writemode.
 # ISM may not be hit faster than 4*FCLK (256ns)
 # Nibbles come every 16us (assuming +2.5% speed var)


# 1 "../../../../usr/include/sys/uconfig.h"

































































# 68 "../../../../usr/include/sys/uconfig.h"






































# 23 ""

# 1 "./fd.h"

















# 28 "./fd.h"



































































# 282 "./fd.h"



































# 24 ""

# 1 "./fdhw.h"














# 19 "./fdhw.h"































































# 171 "./fdhw.h"













# 25 ""




















	text
	even

 #############
 # void datain(%d1,%a1,%a2)
 # Notes: no registers saved!

datain:
L%%d3:
	mov.b	(%a4),%d0		# slowdown by a VIA access
	mov.b	(%a3),%d3		# get %d3 data %d2
	bpl.b	L%%d3			# -> keep polling %a3
	mov.b	(%a2,%d3),%d3	# denibblize

 # checksum rotation. We copy hi bit to X, because we can do ADDX:

	mov.b	%d6,%d0
	lsl.b	&1,%d0			# bit7 --> X
	rol.b	&1,%d6

L%datA:
	mov.b	(%a4),%d0		# slowdown by a VIA access
	mov.b	(%a3),%d2		# %d2 A
	bpl.b	L%datA			# -> keep polling %a3
	mov.b	(%a2,%d2),%d2	# denibblize
	rol.b	&4,%d3			# %d3 = BBCC00AA
	bfins	%d3,%d2{&24:&2}	# combine top 2 bits

	eor.b	%d6,%d2		# byteA = byteA ^ %d6
	mov.b	%d2,(%a1)+		# data into buffer
	addx.b	%d2,%d4		# %d4 = %d4 + byteA + carry(x)

L%datB:
	mov.b	(%a4),%d0		# slowdown by a VIA access
	mov.b	(%a3),%d2		# %d2 B
	bpl.b	L%datB			# -> keep polling %a3
	mov.b	(%a2,%d2),%d2	# denibblize

	rol.b	&2,%d3
	bfins	%d3,%d2{&24:&2}	# combine top 2 bits

	eor.b	%d4,%d2		# byteB = byteB ^ %d4
	mov.b	%d2,(%a1)+		# data into buffer
	addx.b	%d2,%d5		# %d5 = %d5 + byteB + carry(x)

L%datC:
	mov.b	(%a4),%d0		# slowdown by a VIA access
	mov.b	(%a3),%d2		# %d2 C
	bpl.b	L%datC			# -> keep polling %a3
	mov.b	(%a2,%d2),%d2	# denibblize

	rol.b	&2,%d3
	bfins	%d3,%d2{&24:&2}	# combine top 2 bits

	eor.b	%d5,%d2		# byteC = byteC ^ %d5
	mov.b	%d2,(%a1)+		# data into buffer
	addx.b	%d2,%d6		# %d6 = %d6 + byteC + carry(x)

	dbra	%d1,L%%d3
	rts

 #############
 #int gcr_read(%a1)
 # char *%a1;
 # NOTE: Must be called at high spl to prevent interrupts

	global	gcr_read
gcr_read:
 #	trapf				# force crash on < MC68020 cpus
	mov.l	4(%sp),%a0		# buffer address
	movm.l	&(0x3f00 + 0x0020+0x0010+0x0008+0x0004),-(%sp) # save d2-d7,a2-a5

	mov.l	%a0,%a1

	mov.l	&0x50000000,%a4
	mov.l	iwm_addr,%a3
	tst.b	0x1c00(%a3)
	lea	0x1800(%a3),%a3
	tst.b	(%a3)			# set read mode

 #### Get data marks. Note that we don't worry about a "blank" diskette
 #### since we don't get here unless we've found address marks. So even
 #### if there isn't a data field, we WILL find nibbles eventually.

	mov.l	gcr_dmbytes,%d1	# try nibbles for data marks

getdm:	
	mov.l	&gcr_dm,%a0		# for quicker compares

L%dm:
	mov.b	(%a4),%d0		# slowdown by a VIA access
	mov.b	(%a3),%d0		# get expected dm %d2
	bpl.b	L%dm			# -> keep polling %a3

	cmp.b	%d0,(%a0)+		# is it proper dm?
	beq.b	gotdmark		# -> yes

	dbra	%d1,getdm
	mov	&1,%d0		# sorry
	bra	return

gotdmark:
	tst.b	(%a0)			# end of marks?
	bne.b	L%dm			# -> no, continue getting marks

 #### Get sector number and ignore it.

getsect:
	mov.b	(%a4),%d0		# slowdown by a VIA access
	mov.b	(%a3),%d0		# get sector number
	bpl.b	getsect			# -> keep polling %a3

 #### Get tag field, discard it, but include it in checksum:

gettag:
	mov.l	&(gcr_dnibt - 0x80),%a2 # %a2 base
	mov	&0,%d3
	mov	&0,%d2
	mov	&0,%d4			# clear checksums
	mov	&0,%d5
	mov	&0,%d6

	mov.l	%a1,%a5		# save buffer address
	mov	&((12 / 3) - 1),%d1	# 4 groups of 3, gets 12
	bsr.w	datain			# grab the tag field
	mov.l	%a5,%a1		# restore buffer address

 #### Get data.  We get 510 bytes first (groups of 3).  Later we will
 ####  pick up the last two bytes (since not an even group).

	mov	&((510 / 3) - 1),%d1	# 170 groups of 3, gets 510
	bsr.w	datain

 #### Get the last two bytes (the odd group):

L%last:
	mov.b	(%a4),%d0		# slowdown by a VIA access
	mov.b	(%a3),%d3		# get %d3 data %d2
	bpl.b	L%last
	mov.b	(%a2,%d3),%d3	# denibblize

 # Checksum rotation. We copy hi bit to X, because we can do ADDX:

	mov.b	%d6,%d0
	lsl.b	&1,%d0			# bit7 --> X
	rol.b	&1,%d6

L%lstA:
	mov.b	(%a4),%d0		# slowdown by a VIA access
	mov.b	(%a3),%d2		# %d2 A
	bpl.b	L%lstA
	mov.b	(%a2,%d2),%d2	# denibblize A

	rol.b	&4,%d3			# %d3 = BBxx00AA
	bfins	%d3,%d2{&24:&2}	# combine top 2 bits

	eor.b	%d6,%d2		# byteA=byteA ^ %d6
	mov.b	%d2,(%a1)+		# data into buffer
	addx.b	%d2,%d4		# %d4 = %d4 + byteA + carry(x)

L%lstB:
	mov.b	(%a4),%d0		# slowdown by a VIA access
	mov.b	(%a3),%d2		# %d2 B
	bpl.b	L%lstB
	mov.b	(%a2,%d2),%d2	# denibblize B

	rol.b	&2,%d3
	bfins	%d3,%d2{&24:&2}	# combine top 2 bits

	eor.b	%d4,%d2		# byteB = byteB ^ %d4
	mov.b	%d2,(%a1)+		# data into buffer
	addx.b	%d2,%d5		# %d5 = %d5 + byteB + carry(x)

 #### Get the checksum bytes:

L%sum:
	mov.b	(%a4),%d0		# slowdown by a VIA access
	mov.b	(%a3),%d3		# get %d3 data %d2
	bpl.b	L%sum
	mov.b	(%a2,%d3),%d3	# denibblize

	mov	&0,%d0

L%%d4:
	mov.b	(%a4),%d0		# slowdown by a VIA access
	mov.b	(%a3),%d0		# %d2 A
	bpl.b	L%%d4
	mov.b	(%a2,%d0),%d0		# denibblize A

	rol.b	&4,%d3			# %d3 = BBCC00AA
	bfins	%d3,%d0{&24:&2}	# combine top 2 bits
	mov.l	%d0,%d1

L%%d5:
	mov.b	(%a4),%d0		# slowdown by a VIA access
	mov.b	(%a3),%d0		# %d2 B
	bpl.b	L%%d5
	mov.b	(%a2,%d0),%d0		# denibblize B

	rol.b	&2,%d3
	bfins	%d3,%d0{&24:&2}	# combine top 2 bits
	mov.l	%d0,%d2

L%%d6:
	mov.b	(%a4),%d0		# slowdown by a VIA access
	mov.b	(%a3),%d0		# %d2 C
	bpl.b	L%%d6
	mov.b	(%a2,%d0),%d0		# denibblize C

	rol.b	&2,%d3
	bfins	%d3,%d0{&24:&2}	# combine top 2 bits
	mov.l	%d0,%d3

 #### Get bit slip marks:

getslip:
L%sl1:
	mov.b	(%a4),%d0		# slowdown by a VIA access
	mov.b	(%a3),%d0		# get expected dm %d2
	bpl.b	L%sl1

	cmp.b	%d0,&0xde		# is it proper mark?
	bne.b	badslip			# -> nope
L%sl2:
	mov.b	(%a4),%d0		# slowdown by a VIA access
	mov.b	(%a3),%d0		# get expected dm %d2
	bpl.b	L%sl2

	cmp.b	%d0,&0xaa		# is it proper mark?
	beq.b	checkit			# -> yes

badslip:
	mov	&3,%d0		# bad data bitslip marks
	bra.b	return

checkit:
	mov	&0,%d0			# assume good return

	cmp.b	%d1,%d4		# a match?
	bne.b	badsum			# -> nope
	cmp.b	%d2,%d5		# a match?
	bne.b	badsum			# -> nope
	cmp.b	%d3,%d6		# a match?
	beq.b	return			# -> OK

badsum:
	mov	&2,%d0

return:
	movm.l	(%sp)+,&(0x00fc + 0x0400+0x0800+0x1000+0x2000) # d2-d7, a2-a5
	rts

	data



	global	gcr_dm
gcr_dm:
	byte	0xd5,0xaa,0xad,0x00

	global	gcr_dmbytes
gcr_dmbytes:
	long	100

	global	gcr_dnibt
 # Bytes of 0xFF are invalid nibbles.
gcr_dnibt:	
	byte	0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF	# 80-87
	byte	0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF	# 88-8f
	byte	0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x00,0x01	# 90-97
	byte	0xFF,0xFF,0x02,0x03,0xFF,0x04,0x05,0x06	# 98-9f
	byte	0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0x07,0x08	# a0-a7
	byte	0xFF,0xFF,0xFF,0x09,0x0A,0x0B,0x0C,0x0D	# a8-af
	byte	0xFF,0xFF,0x0E,0x0F,0x10,0x11,0x12,0x13	# b0-b7
	byte	0xFF,0x14,0x15,0x16,0x17,0x18,0x19,0x1A	# b8-bf
	byte	0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF	# c0-c7
	byte	0xFF,0xFF,0xFF,0x1B,0xFF,0x1C,0x1D,0x1E	# c8-cf
	byte	0xFF,0xFF,0xFF,0x1F,0xFF,0xFF,0x20,0x21	# d0-d7
	byte	0xFF,0x22,0x23,0x24,0x25,0x26,0x27,0x28	# d8-df
	byte	0xFF,0xFF,0xFF,0xFF,0xFF,0x29,0x2A,0x2B	# e0-e7
	byte	0xFF,0x2C,0x2D,0x2E,0x2F,0x30,0x31,0x32	# e8-ef
	byte	0xFF,0xFF,0x33,0x34,0x35,0x36,0x37,0x38	# f0-f7
	byte	0xFF,0x39,0x3A,0x3B,0x3C,0x3D,0x3E,0x3F	# f8-ff
