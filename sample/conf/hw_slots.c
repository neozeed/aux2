#if !defined(_NO_IDENTS) && defined(_MAIN_IDENTS)
# pragma ident "@(#)ddk:2.0/new/sample/conf/hw_slots.c"
#endif

/*
 * Copyright 1990 Apple Computer, Inc.
 * All Rights Reserved.
 */

#include <stdio.h>

/**
 ** hw_slots
 **
 ** This program looks for slots with boards, then it writes this info out in
 ** a form that is usable by autoconfig via the -L option.  
 **/

main(argc, argv)
register int argc;
register char *argv[];
{
	register int slot, id, ret, used;
	char	 version[100];

	if(argc < 3) {
	    fprintf(stderr,
		"hw_slots: hummm.... did you mess with my Makefile?\n");
	    exit(1);
	}
	    
	used = 0;
	for(slot=9; slot < 15; ++slot) {
	    if((id=slot_board_id(slot)) != 0xffff && id > 0) {

		/* write out this board's info */
	        printf("%-5d %-5d ", slot, id);

		/* hummm... why doesn't this work as I expected? */
		ret = slot_serial_num(slot, version, sizeof(version));
		if(ret > 0)
		    printf("%s\n", version);
		else
		    printf("0\n");
	    } else {

		/* this slot is ours! */
		printf("%-5d %-5d %s\n", slot, atoi(argv[1]), argv[2]);
		++used;
	    }
	}


	if(!used) {
	    fprintf(stderr, "Sorry, but I couldn't find any free slots!\n");
	    exit(1);
	}

	fprintf(stderr, "\t# found %d slots for the sample driver.\n", used);
	exit(0);
}
