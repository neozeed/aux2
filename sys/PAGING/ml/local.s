	#	@(#)Copyright Apple Computer 1987	Version 1.5 of local.s on 89/04/07 17:26:55
	file	"local.s"
 #	@(#)local.s	UniPlus VVV.2.1.3

#define LOCORE
#define __sys_signal_h
#include "sys/param.h"
	global  runrun, curpri, mtimer, lticks, copypte
	data	

	even
curpri:		long	0		#
mtimer:		long	0		#
lticks:		long	0		#
copypte:	long	0		#
runrun:		long	0		# for process switching

#ifndef	MMB
	global	sup_rp
sup_rp:		long 	0           	# long descriptor to load into
		long	0		#	srp (supv root ptr) 
#endif MMB
	global	cpu_rp
cpu_rp:		long 	0           	# long descriptor to load into
		long	0		#	crp (cpu root ptr) 
	# <@(#)local.s	6.1>
