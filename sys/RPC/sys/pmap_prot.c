/* @(#)pmap_prot.c	1.2 87/09/18 3.2/4.3NFSSRC */
/* @(#)pmap_prot.c	1.2 86/10/28 NFSSRC */
#ifndef lint
static char sccsid[] = "@(#)pmap_prot.c 1.1 86/09/24 Copyr 1984 Sun Micro";
#endif

/*
 * pmap_prot.c
 * Protocol for the local binder service, or pmap.
 *
 * Copyright (C) 1984, Sun Microsystems, Inc.
 */

#ifdef KERNEL
#include "rpc/types.h"
#include "rpc/xdr.h"
#include "rpc/pmap_prot.h"
#else
#include <rpc/types.h>
#include <rpc/xdr.h>
#include <rpc/pmap_prot.h>
#endif

bool_t
xdr_pmap(xdrs, regs)
	XDR *xdrs;
	struct pmap *regs;
{

	if (xdr_u_long(xdrs, &regs->pm_prog) && 
		xdr_u_long(xdrs, &regs->pm_vers) && 
		xdr_u_long(xdrs, &regs->pm_prot))
		return (xdr_u_long(xdrs, &regs->pm_port));
	return (FALSE);
}
