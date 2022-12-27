#ifndef lint	/* .../sys/NET/sys/bnetinit.c */
#define _AC_NAME bnetinit_c
#define _AC_NO_MAIN "@(#) Copyright (c) 1985-87 UniSoft Corporation, All Rights Reserved.  {Apple version 1.2 87/11/11 21:16:43}"
#include <apple_notice.h>

#ifdef _AC_HISTORY
  static char *sccsid = "@(#)Copyright Apple Computer 1987\tVersion 1.2 of bnetinit.c on 87/11/11 21:16:43";
#endif		/* _AC_HISTORY */
#endif		/* lint */

#define _AC_MODS
/*	@(#)bnetinit.c UniPlus 2.1.3	*/

#include "sys/types.h"
#include "sys/sysent.h"
#include "vaxuba/ubavar.h"

#ifndef	AUTOCONFIG
short	netoff = 0;
#endif

/*
 * Initialize network code.  Called from main().
 */
BNETinit()
{
	register int s;
	extern struct uba_device ubdinit[];
	register struct uba_driver *udp;
	register struct uba_device *ui = &ubdinit[0];
	extern int nulldev();
	extern int accept();
	extern int bind();
	extern int connect();
	extern int getpeername();
	extern int getsockname();
	extern int getsockopt();
	extern int listen();
	extern int recv();
	extern int recvfrom();
	extern int recvmsg();
	extern int select();
	extern int send();
	extern int sendmsg();
	extern int sendto();
	extern int setsockopt();
	extern int shutdown();
	extern int socket();
	extern int socketpair();

#ifdef	HOWFAR
	printf("BNETinit initializing B-NET system calls\n");
#endif
	sysent[70].sy_call = accept;
	sysent[71].sy_call = bind;
	sysent[72].sy_call = connect;
	sysent[75].sy_call = getpeername;
	sysent[76].sy_call = getsockname;
	sysent[77].sy_call = getsockopt;
	sysent[78].sy_call = listen;
	sysent[79].sy_call = recv;
	sysent[80].sy_call = recvfrom;
	sysent[81].sy_call = recvmsg;
	sysent[82].sy_call = select;
	sysent[83].sy_call = send;
	sysent[84].sy_call = sendmsg;
	sysent[85].sy_call = sendto;
	sysent[90].sy_call = setsockopt;
	sysent[91].sy_call = shutdown;
	sysent[92].sy_call = socket;
	sysent[93].sy_call = socketpair;

#ifdef	HOWFAR
	printf("BNETinit calling mbinit\n");
#endif
	mbinit();
	for(ui = &ubdinit[0] ; udp = ui->ui_driver ; ui++) {
		if (ui->ui_alive)
			continue;
		if (*udp->ud_probe != nulldev) {
#ifdef	HOWFAR
			printf("BNETinit calling 0x%x (ud_probe for unit %d)\n", *udp->ud_probe, ui->ui_unit);
#endif
			if ((*udp->ud_probe)(ui)) {
				ui->ui_alive = 1;
				udp->ud_dinfo[ui->ui_unit] = ui;
#ifdef	HOWFAR
				printf("BNETinit calling 0x%x (ud_attach for unit %d)\n", *udp->ud_attach, ui->ui_unit);
#endif
				(*udp->ud_attach)(ui);
			}
		}
	}
#ifdef INET
#ifdef	HOWFAR
	printf("BNETinit calling loattach\n");
#endif
	loattach();			/* XXX */
#ifdef	HOWFAR
	printf("BNETinit calling ifinit\n");
#endif
	/*
	 * Block reception of incoming packets
	 * until protocols have been initialized.
	 */
	s = splimp();
	ifinit();
#ifdef	HOWFAR
	printf("BNETinit calling domaininit\n");
#endif
	domaininit();			/* must follow interfaces */
	splx(s);
#endif
}
