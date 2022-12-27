#
# Copyright 1987, 1988, 1989, 1990 Apple Computer, Inc.
# All Rights Reserved.
#
# THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF APPLE COMPUTER, INC.
# The copyright notice above does not evidence any actual or
# intended publication of such source code.
#

#* @(#)makefile  {Apple version 1.7 89/12/04 15:32:36}

# tc tape driver (Apple Backup 40SC)

#### PREFIXES, so that things happen to a specified "target" path:

# where to get "unshared.ld"
USRLIB =	/usr/lib

# where to install "notc" script
UNINSDIR =	$(DEST)/etc/uninstall.d

# where to install "tc" script
INSDIR =	$(DEST)/etc/install.d

#### end of prefixes

CFILES =	tcdata.c tcfunc.c tcinit.c tcioctl.c tcopen.c \
		tcpos.c	tcstrat.c tcsubs.c tctimer.c
OFILES =	tcdata.o tcfunc.o tcinit.o tcioctl.o tcopen.o \
		tcpos.o tcstrat.o tcsubs.o tctimer.o

HFILES =	tc.h

ALLFILES = 	$(HFILES) $(CFILES)

# Make rules
SHELL = /bin/sh

install :	installsys

installsys :	$(INSDIR)/boot.d/tc 

$(INSDIR)/boot.d/tc: tc
		cp tc 		$(INSDIR)/boot.d
		chmod 750	$(INSDIR)/boot.d/tc
		chgrp bin	$(INSDIR)/boot.d/tc
		chown bin	$(INSDIR)/boot.d/tc

tc	:	$(OFILES) makefile
		ld -o tc -x -r $(OFILES) $(USRLIB)/unshared.ld

.c.o:
		cc -c -I$(SINC) $(CFLAGS) $*.c

tcdata.o:	tc.h
tcfunc.o:	tc.h
tcinit.o:	tc.h
tcioctl.o:	tc.h
tcopen.o:	tc.h
tcpos.o:	tc.h
tcstrat.o:	tc.h
tcsubs.o:	tc.h
tctimer.o:	tc.h
