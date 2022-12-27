#ifndef lint	/* .../sys/COMMON/os/geninit.c */
#define _AC_NAME geninit_c
#define _AC_MAIN "@(#) Copyright (c) 1987 Apple Computer, Inc., All Rights Reserved.  {Apple version 1.4 89/09/14 11:01:32}"
#include <apple_notice.h>

#ifdef _AC_HISTORY
  static char *sccsid = "@(#)Copyright Apple Computer 1989\tVersion 1.4 of geninit.c on 89/09/14 11:01:32";
#endif		/* _AC_HISTORY */
#endif		/* lint */

#define _AC_MODS
/*	@(#)geninit.c	*/
#include	<sys/param.h>

main()
{
	printf("/* Due to a BUG in the generic linker\n");
	printf("   the size + length must not add up to \n");
	printf("   something with bit 31 set */\n");
	printf("MEMORY {\n");
	printf("	VALID: org = 0x%X, len = 0x%X\n", KMEMORG,
					0x7fffffff-KMEMORG);
	printf("}\n");
	printf("SECTIONS {\n");
	printf("	pstart 0x%X: {\n", KMEMORG);
	printf("			%s (.text, .data)\n",PFILE);
	printf("			endpstart = .;\n");
	printf("		}\n");
	printf("	.text 0x%X: {}\n", TEXTSTART);
	printf("	.data 0x%X: {}\n", DATASTART);
	printf("	.bss  0x%X: {}\n", BSSSTART);
	printf("	MODULES 0x0(COPY): {\n");
	printf("			%s (.data)\n", MFILE);
	printf("		}\n");
	printf("}\n");
	printf("u = 0x%X;\n", UDOT);

	exit(0);
}
