#ifndef lint	/* .../sys/RPC/sys/auth_none.c */
#define _AC_NAME auth_none_c
#define _AC_NO_MAIN "@(#) Copyright (c) 1983-87 Sun Microsystems Inc., All Rights Reserved.  {Apple version 1.3 88/05/22 19:07:21}"
#include <apple_notice.h>

#ifdef _AC_HISTORY
  static char *sccsid = "@(#)Copyright Apple Computer 1987\tVersion 1.3 of auth_none.c on 88/05/22 19:07:21 1.1 86/02/03 Copyr 1984 Sun Micro";
#endif		/* _AC_HISTORY */
#endif		/* lint */

/* NFSSRC @(#)auth_none.c	2.1 86/04/14 */
#ifndef lint
#define _AC_MODS
#endif

/*
 * auth_none.c
 * Creates a client authentication handle for passing "null" 
 * credentials and verifiers to remote systems. 
 * 
 * Copyright (C) 1984, Sun Microsystems, Inc. 
 */

#include <rpc/types.h>
#include <rpc/xdr.h>
#include <rpc/auth.h>
#define MAX_MARSHEL_SIZE 20

/*
 * Authenticator operations routines
 */
static void	authnone_verf();
static void	authnone_destroy();
static bool_t	authnone_marshal();
static bool_t	authnone_validate();
static bool_t	authnone_refresh();

static struct auth_ops ops = {
	authnone_verf,
	authnone_marshal,
	authnone_validate,
	authnone_refresh,
	authnone_destroy
};

static AUTH	no_client;
static char	marshalled_client[MAX_MARSHEL_SIZE];
static u_int	mcnt;

AUTH *
authnone_create()
{
	XDR xdr_stream;
	register XDR *xdrs;

	if (! mcnt) {
		no_client.ah_cred = no_client.ah_verf = _null_auth;
		no_client.ah_ops = &ops;
		xdrs = &xdr_stream;
		xdrmem_create(xdrs, marshalled_client, (u_int)MAX_MARSHEL_SIZE,
		    XDR_ENCODE);
		(void)xdr_opaque_auth(xdrs, &no_client.ah_cred);
		(void)xdr_opaque_auth(xdrs, &no_client.ah_verf);
		mcnt = XDR_GETPOS(xdrs);
		XDR_DESTROY(xdrs);
	}
	return (&no_client);
}

/*ARGSUSED*/
static bool_t
authnone_marshal(client, xdrs)
	AUTH *client;
	XDR *xdrs;
{

	return ((*xdrs->x_ops->x_putbytes)(xdrs, marshalled_client, mcnt));
}

static void 
authnone_verf()
{
}

static bool_t
authnone_validate()
{

	return (TRUE);
}

static bool_t
authnone_refresh()
{

	return (FALSE);
}

static void
authnone_destroy()
{
}
