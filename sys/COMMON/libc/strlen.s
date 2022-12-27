	#	@(#)Copyright Apple Computer 1987	Version 1.3 of strlen.s on 87/11/11 21:16:21
	#	@(#)strlen.s	UniPlus 2.1.1
	#
	#	M68000 String(3C) Routine
	#
	#	(C) Copyright 1983 by Motorola Inc.
	#
	#	Written by: Steve Sheahan
	#
	# strlen - returns the number of characters in s, not including the
	#	   terminating null character

	file	"strlen.s"
	#
	# Input:	s - the string in question
	#
	# Output:	the length of string "s" 
	#
	# Registers:	%a0 - points into s
	#		%a1 - points at s + 1 for subtraction
	#		%d0 - scratch

					# int
					# strlen(s)
					# register char *s;
					# {
	text
	global	strlen
strlen:
	mov.l	4(%sp),%a0		# addr(s)
	lea.l	1(%a0),%a1		# s0 = s + 1
					# while (*s++ != '\0')
L%1:
	tst.b	(%a0)+
	bne.b	L%1
					# return (s - s0);
	mov.l	%a0,%d0
	sub.l	%a1,%d0
	rts
