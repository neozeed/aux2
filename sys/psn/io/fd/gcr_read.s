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

#include <sys/uconfig.h>
#include "fd.h"
#include "fdhw.h"

#define	savebuf	%a5			/* saved buffer */
#define	vp	%a4			/* VIA1 addr for slowdowns */
#define	iwm	%a3			/* iwm data port */
#define	dnib	%a2			/* dnibl table address */
#define	buf	%a1			/* input data buffer */
/* a0 is temp */

#define	intr	%d7			/* 0xff to poll irq, 0 not */
#define	sumC	%d6			/* checksum A */
#define	sumB	%d5			/* checksum B */
#define	sumA	%d4			/* checksum C */
#define	tops	%d3			/* top bits */
#define	byte	%d2			/* data byte */
#define	loop	%d1			/* loop counter */
#define	dsumC	tops
#define	dsumB	byte
#define	dsumA	loop			/* sums from disk sector */
/* d0 is temp use */

	text
	even

 #############
 # void datain(loop,buf,dnib)
 # Notes: no registers saved!

datain:
L%tops:
	mov.b	(vp),%d0		# slowdown by a VIA access
	mov.b	(iwm),tops		# get tops data byte
	bpl.b	L%tops			# -> keep polling iwm
	mov.b	(dnib,tops),tops	# denibblize

 # checksum rotation. We copy hi bit to X, because we can do ADDX:

	mov.b	sumC,%d0
	lsl.b	&1,%d0			# bit7 --> X
	rol.b	&1,sumC

L%datA:
	mov.b	(vp),%d0		# slowdown by a VIA access
	mov.b	(iwm),byte		# byte A
	bpl.b	L%datA			# -> keep polling iwm
	mov.b	(dnib,byte),byte	# denibblize
	rol.b	&4,tops			# tops = BBCC00AA
	bfins	tops,byte{&24:&2}	# combine top 2 bits

	eor.b	sumC,byte		# byteA = byteA ^ sumC
	mov.b	byte,(buf)+		# data into buffer
	addx.b	byte,sumA		# sumA = sumA + byteA + carry(x)

L%datB:
	mov.b	(vp),%d0		# slowdown by a VIA access
	mov.b	(iwm),byte		# byte B
	bpl.b	L%datB			# -> keep polling iwm
	mov.b	(dnib,byte),byte	# denibblize

	rol.b	&2,tops
	bfins	tops,byte{&24:&2}	# combine top 2 bits

	eor.b	sumA,byte		# byteB = byteB ^ sumA
	mov.b	byte,(buf)+		# data into buffer
	addx.b	byte,sumB		# sumB = sumB + byteB + carry(x)

L%datC:
	mov.b	(vp),%d0		# slowdown by a VIA access
	mov.b	(iwm),byte		# byte C
	bpl.b	L%datC			# -> keep polling iwm
	mov.b	(dnib,byte),byte	# denibblize

	rol.b	&2,tops
	bfins	tops,byte{&24:&2}	# combine top 2 bits

	eor.b	sumB,byte		# byteC = byteC ^ sumB
	mov.b	byte,(buf)+		# data into buffer
	addx.b	byte,sumC		# sumC = sumC + byteC + carry(x)

	dbra	loop,L%tops
	rts

 #############
 #int gcr_read(buf)
 # char *buf;
 # NOTE: Must be called at high spl to prevent interrupts

	global	gcr_read
gcr_read:
 #	trapf				# force crash on < MC68020 cpus
	mov.l	4(%sp),%a0		# buffer address
	movm.l	&(S_ALLD + S_A2+S_A3+S_A4+S_A5),-(%sp) # save d2-d7,a2-a5

	mov.l	%a0,buf

	mov.l	&VIA1_ADDR,vp
	mov.l	iwm_addr,iwm
	tst.b	Q7L(iwm)
	lea	Q6L(iwm),iwm
	tst.b	(iwm)			# set read mode

 #### Get data marks. Note that we don't worry about a "blank" diskette
 #### since we don't get here unless we've found address marks. So even
 #### if there isn't a data field, we WILL find nibbles eventually.

	mov.l	gcr_dmbytes,loop	# try nibbles for data marks

getdm:	
	mov.l	&gcr_dm,%a0		# for quicker compares

L%dm:
	mov.b	(vp),%d0		# slowdown by a VIA access
	mov.b	(iwm),%d0		# get expected dm byte
	bpl.b	L%dm			# -> keep polling iwm

	cmp.b	%d0,(%a0)+		# is it proper dm?
	beq.b	gotdmark		# -> yes

	dbra	loop,getdm
	mov	&EDMARKS,%d0		# sorry
	bra	return

gotdmark:
	tst.b	(%a0)			# end of marks?
	bne.b	L%dm			# -> no, continue getting marks

 #### Get sector number and ignore it.

getsect:
	mov.b	(vp),%d0		# slowdown by a VIA access
	mov.b	(iwm),%d0		# get sector number
	bpl.b	getsect			# -> keep polling iwm

 #### Get tag field, discard it, but include it in checksum:

gettag:
	mov.l	&(gcr_dnibt - 0x80),dnib # dnib base
	mov	&0,tops
	mov	&0,byte
	mov	&0,sumA			# clear checksums
	mov	&0,sumB
	mov	&0,sumC

	mov.l	buf,savebuf		# save buffer address
	mov	&((12 / 3) - 1),loop	# 4 groups of 3, gets 12
	bsr.w	datain			# grab the tag field
	mov.l	savebuf,buf		# restore buffer address

 #### Get data.  We get 510 bytes first (groups of 3).  Later we will
 ####  pick up the last two bytes (since not an even group).

	mov	&((510 / 3) - 1),loop	# 170 groups of 3, gets 510
	bsr.w	datain

 #### Get the last two bytes (the odd group):

L%last:
	mov.b	(vp),%d0		# slowdown by a VIA access
	mov.b	(iwm),tops		# get tops data byte
	bpl.b	L%last
	mov.b	(dnib,tops),tops	# denibblize

 # Checksum rotation. We copy hi bit to X, because we can do ADDX:

	mov.b	sumC,%d0
	lsl.b	&1,%d0			# bit7 --> X
	rol.b	&1,sumC

L%lstA:
	mov.b	(vp),%d0		# slowdown by a VIA access
	mov.b	(iwm),byte		# byte A
	bpl.b	L%lstA
	mov.b	(dnib,byte),byte	# denibblize A

	rol.b	&4,tops			# tops = BBxx00AA
	bfins	tops,byte{&24:&2}	# combine top 2 bits

	eor.b	sumC,byte		# byteA=byteA ^ sumC
	mov.b	byte,(buf)+		# data into buffer
	addx.b	byte,sumA		# sumA = sumA + byteA + carry(x)

L%lstB:
	mov.b	(vp),%d0		# slowdown by a VIA access
	mov.b	(iwm),byte		# byte B
	bpl.b	L%lstB
	mov.b	(dnib,byte),byte	# denibblize B

	rol.b	&2,tops
	bfins	tops,byte{&24:&2}	# combine top 2 bits

	eor.b	sumA,byte		# byteB = byteB ^ sumA
	mov.b	byte,(buf)+		# data into buffer
	addx.b	byte,sumB		# sumB = sumB + byteB + carry(x)

 #### Get the checksum bytes:

L%sum:
	mov.b	(vp),%d0		# slowdown by a VIA access
	mov.b	(iwm),tops		# get tops data byte
	bpl.b	L%sum
	mov.b	(dnib,tops),tops	# denibblize

	mov	&0,%d0

L%sumA:
	mov.b	(vp),%d0		# slowdown by a VIA access
	mov.b	(iwm),%d0		# byte A
	bpl.b	L%sumA
	mov.b	(dnib,%d0),%d0		# denibblize A

	rol.b	&4,tops			# tops = BBCC00AA
	bfins	tops,%d0{&24:&2}	# combine top 2 bits
	mov.l	%d0,dsumA

L%sumB:
	mov.b	(vp),%d0		# slowdown by a VIA access
	mov.b	(iwm),%d0		# byte B
	bpl.b	L%sumB
	mov.b	(dnib,%d0),%d0		# denibblize B

	rol.b	&2,tops
	bfins	tops,%d0{&24:&2}	# combine top 2 bits
	mov.l	%d0,dsumB

L%sumC:
	mov.b	(vp),%d0		# slowdown by a VIA access
	mov.b	(iwm),%d0		# byte C
	bpl.b	L%sumC
	mov.b	(dnib,%d0),%d0		# denibblize C

	rol.b	&2,tops
	bfins	tops,%d0{&24:&2}	# combine top 2 bits
	mov.l	%d0,dsumC

 #### Get bit slip marks:

getslip:
L%sl1:
	mov.b	(vp),%d0		# slowdown by a VIA access
	mov.b	(iwm),%d0		# get expected dm byte
	bpl.b	L%sl1

	cmp.b	%d0,&0xde		# is it proper mark?
	bne.b	badslip			# -> nope
L%sl2:
	mov.b	(vp),%d0		# slowdown by a VIA access
	mov.b	(iwm),%d0		# get expected dm byte
	bpl.b	L%sl2

	cmp.b	%d0,&0xaa		# is it proper mark?
	beq.b	checkit			# -> yes

badslip:
	mov	&EDSLIP,%d0		# bad data bitslip marks
	bra.b	return

checkit:
	mov	&0,%d0			# assume good return

	cmp.b	dsumA,sumA		# a match?
	bne.b	badsum			# -> nope
	cmp.b	dsumB,sumB		# a match?
	bne.b	badsum			# -> nope
	cmp.b	dsumC,sumC		# a match?
	beq.b	return			# -> OK

badsum:
	mov	&EDSUM,%d0

return:
	movm.l	(%sp)+,&(R_ALLD + R_A2+R_A3+R_A4+R_A5) # d2-d7, a2-a5
	rts

	data

#undef byte

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
