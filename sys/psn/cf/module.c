/*
 *	Copyright 1989 Apple Computer, Inc.
 *	All Rights Reserved
 *
 *	THIS SOFTWARE PRODUCT CONTAINS THE UNPUBLISHED SOURCE CODE OF
 *			APPLE COMPUTER, INC.
 *
 *	The copyright notice above does not evidence any actual 
 *	or intended publication of such source code.
 */

/* This SCCS information must not generate space in the .data section! */
#if !defined(_NO_IDENTS) && defined(_MISC_IDENTS)
# /*pragma*/ ident "@(#)kern:module.c	1.2 89/09/21 "
#endif

#include <sys/types.h>
#include <sys/module.h>

/*
 * "Dummy" modules which are built into the base kernel.
 */
static struct mods _DefMods[] = 
{
{ "scc",	0,	0, 0, 0, 0,	MO_CHAR | MO_TTY,	"sc"	} ,
{ "scsi",	24,	0, 0, 0, 0,	MO_BLOCK,		"hd"	} ,
{ "tty",	0,	0, 0, 0, 0,	MO_SOFT,		"tt"	} ,
{ "streams",	0,	0, 0, 0, 0,	MO_SOFT,		"str"	} ,
};
