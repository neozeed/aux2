#ifndef lint	/* .../sys/RPC/sys/svc.c */
#define _AC_NAME svc_c
#define _AC_NO_MAIN "@(#) Copyright (c) 1983-87 Sun Microsystems Inc., All Rights Reserved.  {Apple version 1.3 88/05/22 19:08:23}"
#include <apple_notice.h>

#ifdef _AC_HISTORY
  static char *sccsid = "@(#)Copyright Apple Computer 1987\tVersion 1.3 of svc.c on 88/05/22 19:08:23 1.1 86/02/03 Copyr 1984 Sun Micro";
#endif		/* _AC_HISTORY */
#endif		/* lint */

/* NFSSRC @(#)svc.c	2.1 86/04/14 */
#ifndef lint
#define _AC_MODS
#endif

/*
 * svc.c, Server-side remote procedure call interface.
 *
 * There are two sets of procedures here.  The xprt routines are
 * for handling transport handles.  The svc routines handle the
 * list of service routines.
 *
 * Copyright (C) 1984, Sun Microsystems, Inc.
 */

#ifdef KERNEL
#include "sys/param.h"
#include "sys/signal.h"
#include "sys/types.h"
#include "sys/socket.h"
#include "sys/socketvar.h"
#include "sys/time.h"
#include "netinet/in.h"
#include "rpc/types.h"
#include "rpc/xdr.h"
#include "rpc/auth.h"
#include "rpc/clnt.h"
#include "rpc/rpc_msg.h"
#include "rpc/svc.h"
#include "rpc/svc_auth.h"
char *kmem_alloc();
caddr_t rqcred_head;  /* head of cashed, free authentication parameters */
#else
#include <sys/errno.h>
#include <sys/time.h>
#include <rpc/rpc.h>
#include <rpc/pmap_clnt.h>

#define NOFILE 32

static SVCXPRT *xports[NOFILE];
int svc_fds;
extern errno;
char *malloc();
#endif

#define NULL_SVC ((struct svc_callout *)0)
#define	RQCRED_SIZE	400		/* this size is excessive */

/*
 * The services list
 * Each entry represents a set of procedures (an rpc program).
 * The dispatch routine takes request structs and runs the
 * apropriate procedure.
 */
static struct svc_callout {
	struct svc_callout *sc_next;
	u_long		    sc_prog;
	u_long		    sc_vers;
	void		    (*sc_dispatch)();
} *svc_head;

static struct svc_callout *svc_find();

/* ***************  SVCXPRT related stuff **************** */

/*
 * Activate a transport handle.
 */
#ifdef KERNEL
/*ARGSUSED*/
#endif
void
xprt_register(xprt)
	SVCXPRT *xprt;
{
#ifndef KERNEL
	register int sock = xprt->xp_sock;

	if (sock < NOFILE) {
		xports[sock] = xprt;
		svc_fds |= (1 << sock);
	}
#endif
}

/*
 * De-activate a transport handle. 
 */
#ifndef KERNEL
void
xprt_unregister(xprt) 
	SVCXPRT *xprt;
{ 
	register int sock = xprt->xp_sock;

	if ((sock < NOFILE) && (xports[sock] == xprt)) {
		xports[sock] = (SVCXPRT *)0;
		svc_fds &= ~(1 << sock);
	}
} 
#endif


/* ********************** CALLOUT list related stuff ************* */

/*
 * Add a service program to the callout list.
 * The dispatch routine will be called when a rpc request for this
 * program number comes in.
 */
#ifdef KERNEL 
/*ARGSUSED*/ 
#endif
bool_t
svc_register(xprt, prog, vers, dispatch, protocol)
	SVCXPRT *xprt;
	u_long prog;
	u_long vers;
	void (*dispatch)();
	int protocol;
{
	struct svc_callout *prev;
	register struct svc_callout *s;

	if ((s = svc_find(prog, vers, &prev)) != NULL_SVC) {
		if (s->sc_dispatch == dispatch)
			goto pmap_it;  /* he is registering another xptr */
		return (FALSE);
	}
	s = (struct svc_callout *)mem_alloc(sizeof(struct svc_callout));
#ifndef KERNEL
	if (s == (struct svc_callout *)0) {
		return (FALSE);
	}
#endif
	s->sc_prog = prog;
	s->sc_vers = vers;
	s->sc_dispatch = dispatch;
	s->sc_next = svc_head;
	svc_head = s;
pmap_it:
#ifndef KERNEL
	/* now register the information with the local binder service */
	if (protocol) {
		return (pmap_set(prog, vers, protocol, xprt->xp_port));
	}
#endif
	return (TRUE);
}

/*
 * Remove a service program from the callout list.
 */
void
svc_unregister(prog, vers)
	u_long prog;
	u_long vers;
{
	struct svc_callout *prev;
	register struct svc_callout *s;

	if ((s = svc_find(prog, vers, &prev)) == NULL_SVC)
		return;
	if (prev == NULL_SVC) {
		svc_head = s->sc_next;
	} else {
		prev->sc_next = s->sc_next;
	}
	s->sc_next = NULL_SVC;
	mem_free((char *) s, (u_int) sizeof(struct svc_callout));
#ifndef KERNEL
	/* now unregister the information with the local binder service */
	(void)pmap_unset(prog, vers);
#endif
}

/*
 * Search the callout list for a program number, return the callout
 * struct.
 */
static struct svc_callout *
svc_find(prog, vers, prev)
	u_long prog;
	u_long vers;
	struct svc_callout **prev;
{
	register struct svc_callout *s, *p;

	p = NULL_SVC;
	for (s = svc_head; s != NULL_SVC; s = s->sc_next) {
		if ((s->sc_prog == prog) && (s->sc_vers == vers))
			goto done;
		p = s;
	}
done:
	*prev = p;
	return (s);
}

/* ******************* REPLY GENERATION ROUTINES  ************ */

/*
 * Send a reply to an rpc request
 */
bool_t
svc_sendreply(xprt, xdr_results, xdr_location)
	register SVCXPRT *xprt;
	xdrproc_t xdr_results;
	caddr_t xdr_location;
{
	struct rpc_msg rply; 

	rply.rm_direction = REPLY;  
	rply.rm_reply.rp_stat = MSG_ACCEPTED; 
	rply.acpted_rply.ar_verf = xprt->xp_verf; 
	rply.acpted_rply.ar_stat = SUCCESS;
	rply.acpted_rply.ar_results.where = xdr_location;
	rply.acpted_rply.ar_results.proc = xdr_results;
	return (SVC_REPLY(xprt, &rply)); 
}

/*
 * No procedure error reply
 */
void
svcerr_noproc(xprt)
	register SVCXPRT *xprt;
{
	struct rpc_msg rply;

	rply.rm_direction = REPLY;
	rply.rm_reply.rp_stat = MSG_ACCEPTED;
	rply.acpted_rply.ar_verf = xprt->xp_verf;
	rply.acpted_rply.ar_stat = PROC_UNAVAIL;
	SVC_REPLY(xprt, &rply);
}

/*
 * Can't decode args error reply
 */
void
svcerr_decode(xprt)
	register SVCXPRT *xprt;
{
	struct rpc_msg rply; 

	rply.rm_direction = REPLY; 
	rply.rm_reply.rp_stat = MSG_ACCEPTED; 
	rply.acpted_rply.ar_verf = xprt->xp_verf;
	rply.acpted_rply.ar_stat = GARBAGE_ARGS;
	SVC_REPLY(xprt, &rply); 
}

#ifndef KERNEL
/*
 * Some system error
 */
void
svcerr_systemerr(xprt)
	register SVCXPRT *xprt;
{
	struct rpc_msg rply; 

	rply.rm_direction = REPLY; 
	rply.rm_reply.rp_stat = MSG_ACCEPTED; 
	rply.acpted_rply.ar_verf = xprt->xp_verf;
	rply.acpted_rply.ar_stat = SYSTEM_ERR;
	SVC_REPLY(xprt, &rply); 
}
#endif !KERNEL

/*
 * Authentication error reply
 */
void
svcerr_auth(xprt, why)
	SVCXPRT *xprt;
	enum auth_stat why;
{
	struct rpc_msg rply;

	rply.rm_direction = REPLY;
	rply.rm_reply.rp_stat = MSG_DENIED;
	rply.rjcted_rply.rj_stat = AUTH_ERROR;
	rply.rjcted_rply.rj_why = why;
	SVC_REPLY(xprt, &rply);
}

/*
 * Auth too weak error reply
 */
void
svcerr_weakauth(xprt)
	SVCXPRT *xprt;
{

	svcerr_auth(xprt, AUTH_TOOWEAK);
}

/*
 * Program unavailable error reply
 */
void 
svcerr_noprog(xprt)
	register SVCXPRT *xprt;
{
	struct rpc_msg rply;  

	rply.rm_direction = REPLY;   
	rply.rm_reply.rp_stat = MSG_ACCEPTED;  
	rply.acpted_rply.ar_verf = xprt->xp_verf;  
	rply.acpted_rply.ar_stat = PROG_UNAVAIL;
	SVC_REPLY(xprt, &rply);
}

/*
 * Program version mismatch error reply
 */
void  
svcerr_progvers(xprt, low_vers, high_vers)
	register SVCXPRT *xprt; 
	u_long low_vers;
	u_long high_vers;
{
	struct rpc_msg rply;

	rply.rm_direction = REPLY;
	rply.rm_reply.rp_stat = MSG_ACCEPTED;
	rply.acpted_rply.ar_verf = xprt->xp_verf;
	rply.acpted_rply.ar_stat = PROG_MISMATCH;
	rply.acpted_rply.ar_vers.low = low_vers;
	rply.acpted_rply.ar_vers.high = high_vers;
	SVC_REPLY(xprt, &rply);
}

/* ******************* SERVER INPUT STUFF ******************* */

/*
 * Get server side input from some transport.
 *
 * Statement of authentication parameters management:
 * This function owns and manages all authentication parameters, specifically
 * the "raw" parameters (msg.rm_call.cb_cred and msg.rm_call.cb_verf) and
 * the "cooked" credentials (rqst->rq_clntcred).
 * However, this function does not know the structure of the cooked 
 * credentials, so it make the following assumptions: 
 *   a) the structure is contiguous (no pointers), and
 *   b) the cred structure size does not exceed RQCRED_SIZE bytes. 
 * In all events, all three parameters are freed upon exit from this routine.
 * The storage is trivially management on the call stack in user land, but
 * is mallocated in kernel land.
 */
void
#ifdef KERNEL
svc_getreq(xprt)
	register SVCXPRT *xprt;
#else
svc_getreq(rdfds)
	int rdfds;
#endif
{
	register enum xprt_stat stat;
	struct rpc_msg msg;
	int prog_found;
	u_long low_vers;
	u_long high_vers;
	struct svc_req r;
#ifndef KERNEL
	register int sock;
	register int readfds = rdfds & svc_fds;
	register SVCXPRT *xprt;
	char cred_area[2*MAX_AUTH_BYTES + RQCRED_SIZE];
#else
	char *cred_area;  /* too big to allocate on call stack */
	/*
	 * Firstly, allocate the authentication parameters' storage
	 */
	if (rqcred_head) {
		cred_area = rqcred_head;
		rqcred_head = *(caddr_t *)rqcred_head;
	} else {
		cred_area = mem_alloc(2*MAX_AUTH_BYTES + RQCRED_SIZE);
	}
#endif
	msg.rm_call.cb_cred.oa_base = cred_area;
	msg.rm_call.cb_verf.oa_base = &(cred_area[MAX_AUTH_BYTES]);
	r.rq_clntcred = &(cred_area[2*MAX_AUTH_BYTES]);

#ifndef KERNEL
	for (sock = 0; readfds != 0; sock++, readfds >>= 1) {
	    if ((readfds & 1) != 0) {
		/* sock has input waiting */
		xprt = xports[sock];
#endif
		/* now receive msgs from xprtprt (support batch calls) */
		do {
			if (SVC_RECV(xprt, &msg)) {

				/* now find the exported program and call it */
				register struct svc_callout *s;
				enum auth_stat why;

				r.rq_xprt = xprt;
				r.rq_prog = msg.rm_call.cb_prog;
				r.rq_vers = msg.rm_call.cb_vers;
				r.rq_proc = msg.rm_call.cb_proc;
				r.rq_cred = msg.rm_call.cb_cred;
				/* first authenticate the message */
				if ((why= _authenticate(&r, &msg)) != AUTH_OK) {
					svcerr_auth(xprt, why);
					goto call_done;
				}
				/* now match message with a registered service*/
				prog_found = FALSE;
				low_vers = 0 - 1;
				high_vers = 0;
				for (s = svc_head; s != NULL_SVC; s = s->sc_next) {
					if (s->sc_prog == r.rq_prog) {
						if (s->sc_vers == r.rq_vers) {
							(*s->sc_dispatch)(&r, xprt);
							goto call_done;
						}  /* found correct version */
						prog_found = TRUE;
						if (s->sc_vers < low_vers)
							low_vers = s->sc_vers;
						if (s->sc_vers > high_vers)
							high_vers = s->sc_vers;
					}   /* found correct program */
				}
				/*
				 * if we got here, the program or version
				 * is not served ...
				 */
				if (prog_found)
					svcerr_progvers(xprt,
					low_vers, high_vers);
				else
					 svcerr_noprog(xprt);
				/* Fall through to ... */
			}
		call_done:
			if ((stat = SVC_STAT(xprt)) == XPRT_DIED){
				SVC_DESTROY(xprt);
				break;
			}
		} while (stat == XPRT_MOREREQS);
#ifndef KERNEL
	    }
	}
#else
	/*
	 * free authentication parameters' storage
	 */
	*(caddr_t *)cred_area = rqcred_head;
	rqcred_head = cred_area;
#endif
}


/*
 * This is the rpc server side idle loop
 * Wait for input, call server program.
 */
#ifdef KERNEL
int Rpccnt;
void
svc_run(xprt)
	SVCXPRT *xprt;
{
	int	s;

	while (TRUE) {
		s = splnet();
		while (xprt->xp_sock->so_rcv.sb_cc == 0)
			sbwait(&xprt->xp_sock->so_rcv);
		(void) splx(s);
		svc_getreq(xprt);
		Rpccnt++;
	}
}
#else
void
svc_run()
{
	int readfds;

	while (TRUE) {
		readfds = svc_fds;
		switch (select(32, &readfds, (int *)0, (int *)0,
		    (struct timeval *)0)) {

		case -1:
			if (errno == EINTR)
				continue;
			else {
				perror("svc.c: - Select failed");
				return;
			}
		case 0:
			continue;
		default:
			svc_getreq(readfds);
		}
	}
}
#endif
