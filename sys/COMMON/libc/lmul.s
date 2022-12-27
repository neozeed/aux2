	#	@(#)Copyright Apple Computer 1987	Version 1.3 of lmul.s on 87/11/11 21:16:15
	#	@(#)lmul.s	UniPlus 2.1.1
	# long multiply

	file	"lmul.s"
	global	lmul%%

lmul%%:
	link	%fp,&0
	mov.l	%d2,%a0
	mov.l	%d3,%a1
	mov.w	%d0,%d2
	mov.w	%d0,%d1
	ext.l	%d1
	swap.w	%d1
	swap.w	%d0
	sub.w	%d0,%d1
	mov.w	10(%fp),%d0
	mov.w	%d0,%d3
	ext.l	%d3
	swap.w	%d3
	sub.w	8(%fp),%d3
	muls.w	%d0,%d1
	muls.w	%d2,%d3
	add.w	%d1,%d3
	muls.w	%d2,%d0
	swap.w	%d0
	sub.w	%d3,%d0
	swap.w	%d0
	mov.l	%a0,%d2
	mov.l	%a1,%d3
	unlk	%fp
	rts
