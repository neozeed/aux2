 # @(#)as_subs.s  {Apple version 1.3 89/08/15 12:22:28}
 #
 # Copyright (c) 1987, 1988, 1989 by Apple Computer, Inc.
 # All Rights Reserved.
 #
 # THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF APPLE COMPUTER, INC.
 # The copyright notice above does not evidence any actual or
 # intended publication of such source code.

 #	_sccsid[]="@(#)as_subs.s  {Apple version 1.3 89/08/15 12:22:28}";

	file	"as_subs"

	text
	even

 ################
 # void delay_ms(n)
 # Delay a specified number of millisecond intervals.
 ################

	global	delay_ms

delay_ms:
	mov.l	timedbra,%d0		# counts per 1 ms
	bra.b	delayloop

 ################
 # void delay_100us(n)
 # Delay a specified number of 100-microsecond intervals.
 ################

	global	delay_100us

delay_100us:
	mov.l	timedbra_us,%d0		# counts per 100 us

delayloop:
	muls.l	4(%sp),%d0		# how many you want
	mov.l	%d0,%d1
	swap	%d1
delayus1:
	dbra	%d0,delayus1		# spineroonie
	tst.w	%d1			# check for 0000 hi order
	beq.b	delayusexit
	dbra	%d1,delayus1		# do another 64k
delayusexit:
	rts


 ######################
 ##### these should live in the kernel & be setup from auto_data !!!
 ######################
	data
	global	timedbra
	global	timedbra_us
timedbra:
	long	2620			# 16MHz nominal value (1ms)
timedbra_us:
	long	0
