#
# @(#)makefile  {Apple version 1.9 90/01/15 14:47:55}
# 
# Copyright (c) 1987, 1988, 1989 by Apple Computer, Inc.
# All Rights Reserved.
#
# THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF APPLE COMPUTER, INC.
# The copyright notice above does not evidence any actual or
# intended publication of such source code.
#
# AUX Sound Manager makefile

# where to get "unshared.ld"
USRLIB =	/usr/lib

CPP	=/lib/cpp

SRC = sm.aux.c  sm.priv.c \
	samp.c  sm.asc.c  \
	wave.c  wave.prim.s \
	note.c	timbre.s

OFILES = sm.aux.o  sm.priv.o \
	samp.o  sm.asc.o  \
	wave.o  wave.prim.o \
	note.o	timbre.o

SINC=../../../../usr/include
INC=-I$(SINC) -I../../../../toolbox/inc

INSDIR = $(DEST)/etc/install.d

HDRS = samp.desc.h sm.asc.h sm.privdesc.h ../../../../toolbox/inc/sm.h \
	$(SINC)/sys/sm_aux.h


install :	snd installsys

installsys :	bootfile

bootfile::
		cp snd 		$(INSDIR)/boot.d
		chmod 755	$(INSDIR)/boot.d/snd
		chgrp sys	$(INSDIR)/boot.d/snd
		chown sys	$(INSDIR)/boot.d/snd

snd:	$(OFILES) makefile
		ld -o snd -x -r $(OFILES) $(USRLIB)/unshared.ld

all:		snd

.c.o:
		cc -c $(INC) $(CFLAGS) $*.c

.s.o:
		$(CPP) -P $(DEFS) $(INC) $*.s > $*.i
		$(AS) -o $*.o $*.i
		rm -f $*.i

$(OFILES): $(HDRS)

clean:
		rm -f *.o *.i snd
