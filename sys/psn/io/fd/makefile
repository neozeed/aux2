 # @(#)makefile  {Apple version 1.5 89/08/25 17:04:40}
 #
 # Copyright (c) 1987, 1988, 1989 by Apple Computer, Inc.
 # All Rights Reserved.
 #
 # THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF APPLE COMPUTER, INC.
 # The copyright notice above does not evidence any actual or
 # intended publication of such source code.

#	@(#)makefile  {Apple version 1.5 89/08/25 17:04:40}

# fdhd driver

.SUFFIXES :
.SUFFIXES : .i .sl .o .s .c

SFILES =	as_subs.s \
		gcr_read.s \
		gcr_write.s

#Intermediate files produced by running cpp on .s to feed .i to as
IFILES =	as_subs.i \
		gcr_read.i \
		gcr_write.i

SLFILES =	as_subs.sl \
		gcr_read.sl \
		gcr_write.sl

CFILES =	chipsubs.c \
		data.c \
		format.c \
		func.c \
		gcr_fmttrk.c \
		gcrsubs.c \
		geometry.c \
		init.c \
		iopsubs.c \
		mbsubs1.c \
		mbsubs2.c \
		mb_rw.c \
		mfm_fmttrk.c \
		mfmsubs.c \
		open.c \
		params.c \
		start.c \
		subs.c

GCROFILES =	gcr_read.o \
		gcr_write.o

COFILES =	as_subs.o \
		chipsubs.o \
		data.o \
		format.o \
		func.o \
		gcr_fmttrk.o \
		gcrsubs.o \
		geometry.o \
		init.o \
		iopsubs.o \
		mbsubs1.o \
		mbsubs2.o \
		mb_rw.o \
		mfm_fmttrk.o \
		mfmsubs.o \
		open.o \
		params.o  \
		start.o \
		subs.o

ALLPROGS =	fd 

OFILES =	$(GCROFILES) $(MFMOFILES) $(COFILES)

HFILES =	fd.h fdhw.h

ALLFILES = 	$(HFILES) $(SFILES) $(CFILES)

# Make rules
SHELL = /bin/sh
fd	:	$(OFILES)
		ld -o fd -x -r $(OFILES) /usr/lib/unshared.ld

.c.o:
		cc -c -I$(SINC) $(CFLAGS) $*.c

.s.o:
		as $*.i -o $*.o

.s.i:
		rm -f $@
		/lib/cpp -DASM -I$(SINC) < $< > $@
		chmod a-w $@

.i.sl:		
		sl -e $*

.i.o:
		as $< -o $@

clean:
		rm -f $(ALLPROGS) *.sl *.o *.i

lnt:		$(CFILES)
			lint -u -DLINT $(CFILES) | tee lnt

printc:		$(CFILES) $(HFILES)
		enscript -p - -G -2r $? | $(PRINT)
		touch printc

printsl:	$(SLFILES)
		enscript -p - -G -r -f Courier8 $? | $(PRINT)
		touch printsl

tags:		$(ALLFILES)
		ctags $(CFILES) $(HFILES)
		astags $(SFILES)

chipsubs.o:	fd.h fdhw.h	$(SINC)/sys/iopmgr.h
data.o:		fd.h 		$(SINC)/sys/iopmgr.h	$(SINC)/sys/fdioctl.h
data.o:		$(SINC)/sys/swimiop.h
format.o:	fd.h		$(SINC)/sys/iopmgr.h
func.o:		fd.h		$(SINC)/sys/iopmgr.h
gcr_fmttrk.o:	fd.h fdhw.h	$(SINC)/sys/iopmgr.h
gcrsubs.o:	fd.h fdhw.h	$(SINC)/sys/iopmgr.h
geometry.o:	fd.h		$(SINC)/sys/iopmgr.h
init.o:		fd.h 		$(SINC)/sys/iopmgr.h	$(SINC)/sys/fdioctl.h
iopsubs.o:	fd.h		$(SINC)/sys/iopmgr.h	$(SINC)/sys/fdioctl.h
iopsubs.o:					$(SINC)/sys/swimiop.h
mbsubs1.o:	fd.h fdhw.h	$(SINC)/sys/iopmgr.h	$(SINC)/sys/fdioctl.h
mbsubs2.o:	fd.h fdhw.h	$(SINC)/sys/iopmgr.h
mb_rw.o:	fd.h fdhw.h	$(SINC)/sys/iopmgr.h	$(SINC)/sys/fdioctl.h
mfm_fmttrk.o:	fd.h fdhw.h	$(SINC)/sys/iopmgr.h
mfmsubs.o:	fd.h fdhw.h	$(SINC)/sys/iopmgr.h
open.o:		fd.h		$(SINC)/sys/iopmgr.h	$(SINC)/sys/fdioctl.h
params.o:	fd.h		$(SINC)/sys/iopmgr.h
start.o:	fd.h fdhw.h	$(SINC)/sys/iopmgr.h	$(SINC)/sys/fdioctl.h
subs.o:		fd.h		$(SINC)/sys/iopmgr.h

as_subs.i:	as_subs.s
as_subs.i:	fd.h fdhw.h	$(SINC)/sys/iopmgr.h
as_subs.o:	as_subs.i

gcr_read.i:	gcr_read.s
gcr_read.i:	fd.h fdhw.h	$(SINC)/sys/iopmgr.h
gcr_read.o:	gcr_read.i

gcr_write.i:	gcr_write.s
gcr_write.i:	fd.h fdhw.h	$(SINC)/sys/iopmgr.h
gcr_write.o:	gcr_write.i
