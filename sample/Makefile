###############################################################################
# pragma ident "@(#)ddk:2.0/sample/Makefile"
#
# Copyright 1990 Apple Computer, Inc.
# All Rights Reserved.
###############################################################################
#
# Description file for the sample driver.
#
# You can use this description file to make any of these goals:
#
#   all         - Compile the sample.c file.
#   install     - Install the configuration files into /.
#   conf        - Install and configures the system.
#   unconf      - Uninstalls and configures the system.
#   clean       - Remove all object files.
#   clobber     - Remove all files that were created during the build.
#   depend      - Build the 'Makefile.Deps' dependencies file.
#

#
# This Makefile has been tested under the Bourne shell only.
#

SHELL=		/bin/sh

#
# This tells make to look in the conf directory for files.
#

VPATH=		conf

#
# All module names must be lowercase because the newunix(1M) command only
# looks for lowercase names.
#

MODULE_NAME=	sample

#
# The sample driver doesn't have an associated card and therefore uses a
# load file that the autoconfig command will read to match against information
# in the master file (usually, autoconfig reads the slot cards directly to find
# this information).  Both the board ID and board version must match what's
# listed in the master file before the driver will be loaded into the kernel,
# so use this file only to store and assign the board ID and version number. In
# reality, the board ID is assigned by Apple Computer's Developer Tech Support
# group (DTS), but for the purposes of this example, use a number that is
# unlikely to be used by any other cards in the system.
# The number 9999 seems reasonable, but if it clashes with a board ID that you
# have in your system, change the 9999 to something else!  The version number
# is not a problem and can be anything you want.
#

BOARD_ID=	9999
BOARD_VERNUM=	1234

#
# The load file is created by a program called hw_conf when the 'conf' target
# is made. hw_conf reads the slots, finds all the board IDs, and creates a
# load file that is later used by autoconfig. Any expansion slots that are
# empty are assigned to the sample driver.  If you have a real card to test,
# and the card has the ability to return a 'board id' when requested, you
# should null-out this variable so the load file won't be used.
#

LOADFILE=	conf/loadfile

#
# These are the standard values that are used in making the A/UX kernel, so
# they are reproduced here for your convenience.
#

INC=		/usr/include
INCDIRS=	-I. -I$(INC)
VERBOSE=
ZFLAGS=		-Zn -F
LDFLAGS=
LINTFLAGS=	-bn
DEFINES=	-DKERNEL -Uvax -Usun -Dm68k -Dmc68020 -Dmc68881 -DINET \
		-DETHERLINK -DSTREAMS -UQUOTA -DPAGING -DNFS -DBSD=43 \
		-DAUTOCONFIG -DSCREEN -DSLOTS -DPASS_MAJOR -DPOSIX -DSIG43
CFLAGS=		-A 2 $(DEFINES) $(INCDIRS) $(VERBOSE) $(ZFLAGS) -O
CFLAGS_NO_O=	-A 2 $(DEFINES) $(INCDIRS) $(VERBOSE) $(ZFLAGS)
AR=		/bin/ar
AS=		/bin/as
AUTOCONFIG=	/etc/autoconfig
CC=		/bin/cc
CPP=		/lib/cpp
KCONFIG=	/etc/kconfig
LD=		/bin/ld
LINT=		/usr/bin/lint
MKDIR=		/bin/mkdir
MV=		/bin/mv
CPP=		/lib/cpp
NEWUNIX=	/etc/newunix
NM=		/bin/nm

#
# The 'all' goal
#

all:		.FAKE sample.o

#
# The 'install' goal
# Install the scripts in their proper places. Notice that all the scripts have
# the same name, but reside in different directories.  This is the standard
# naming scheme.
#

install:	.FAKE all \
		/etc/install.d/boot.d/$(MODULE_NAME)     \
		/etc/install.d/init.d/$(MODULE_NAME)     \
		/etc/install.d/startup.d/$(MODULE_NAME)  \
		/etc/install.d/master.d/$(MODULE_NAME)   \
		/etc/install.d/$(MODULE_NAME)            \
		/etc/uninstall.d/$(MODULE_NAME)

#
# The 'conf' goal
# Install and configure the new driver into the kernel.
#

conf:		.FAKE install install_scripts autoconfig

install_scripts: .FAKE
		$(NEWUNIX) $(MODULE_NAME)

#
# The 'unconf' goal
# Uninstall and configure the sample driver out of the kernel.
#

unconf:		.FAKE uninstall_scripts autoconfig

uninstall_scripts: .FAKE
		if [ -f /etc/uninstall.d/$(MODULE_NAME) ]; then \
		  $(NEWUNIX) no$(MODULE_NAME); \
		fi
		rm -f /etc/install.d/boot.d/$(MODULE_NAME)
		rm -f /etc/install.d/init.d/$(MODULE_NAME)
		rm -f /etc/install.d/startup.d/$(MODULE_NAME)
		rm -f /etc/install.d/master.d/$(MODULE_NAME)
		rm -f /etc/install.d/$(MODULE_NAME)
		rm -f /etc/uninstall.d/$(MODULE_NAME)

#
# Do the actual autoconfig.
#

autoconfig:	.FAKE $(LOADFILE)
		# Save any kconfig changes
		@if [ -f "/unix" ]; then \
		  echo "\t# saving current kconfig values..."; \
		  $(KCONFIG) -a -n /unix \
		    > /tmp/ddk_sample_aa; \
		else \
		  rm -f /tmp/ddk_sample_aa; \
		fi

		#
		# Generate a new kernel. The -v, -i, -o, -m, -b, -s, and -d,
		# options aren't necessary and are here so you can see how they
		# are used.
		#

		$(AUTOCONFIG) -I -v \
		  -i /etc/config.d/newunix \
		  -o /unix \
		  -m /etc/master.d \
		  -b /etc/boot.d \
		  -S /etc/startup \
		  -s /etc/startup.d \
		  -d /etc/init.d \
		  -M /etc/master \
		  $(LOADFILE:V)-L $(LOADFILE)

		#
		# Restore the saved kconfig values.
		#

		@if [ -f "/tmp/ddk_sample_aa" ]; then \
		  echo "\t# restoring kconfig values..."; \
		  $(KCONFIG) -n /unix \
		  < /tmp/ddk_sample_aa; \
		fi
		@rm -f /tmp/ddk_sample_aa

		#
		# Tell the user to reboot
		#

		sync; sync; sync
		@echo
		@echo "You are now ready to shutdown and boot the new kernel.\c"
		@echo " You should stop the\nautomatic reboot, enter the \c"
		@echo "Startup application shell and launch the new\nkernel \c"
		@echo "using the following command line:\n"
		@echo "     launch -nv  \c"
		@echo

#
# The 'clean' goal
#

clean:		.FAKE
		rm -f sample.o

#
# The 'clobber' goal
#

clobber:	.FAKE clean
		rm -f conf/hw_slots $(LOADFILE)

#
# Handle 'lint' and 'tags'
#

lint tags:	.FAKE

#
# The 'depend' goal, which updates the 'Makefile.Deps' file.
#

depend:		.FAKE .SILENT sample.c conf/hw_slots.c
		echo "\t# creating the Makefile.Deps file ...";\
		echo "# Automatically Generated on `date`" > Makefile.Deps;\
		set -- "$(>:=*.[csfply])";\
		for _x in $${@};do\
		  $(CPP) -Y -M$$'(INC)' $(INCDIRS) $(DEFINES) \
		    $${_x} >> Makefile.Deps;\
		done

#
# Compile the sample driver.  In general, drivers are not compiled with the -O
# flag and the -F flag is specified to tell the 'C' compiler not to save and
# restore the floating-point registers during function calls.  Your driver
# should not using floating-point instructions.
#

sample.o:	sample.c
		$(CC) -c $(CFLAGS_NO_O) sample.c

conf/hw_slots:	conf/hw_slots.c
		$(CC) -o $(@) $(?) -lslots

#
# The loadfile must be made everytime to be safe.
#

$(LOADFILE):	.OLDTIME conf/hw_slots
		conf/hw_slots ${BOARD_ID} ${BOARD_VERNUM} > $(@)

/etc/install.d/boot.d/$(MODULE_NAME): sample.o
		cp $(?) $(@)
		chmod 0640 $(@)
		chown bin $(@)
		chgrp bin $(@)

/etc/install.d/init.d/$(MODULE_NAME): conf/init
		cp $(?) $(@)
		chmod 0750 $(@)
		chown bin $(@)
		chgrp bin $(@)

/etc/install.d/startup.d/$(MODULE_NAME): conf/startup
		cp $(?) $(@)
		chmod 0750 $(@)
		chown bin $(@)
		chgrp bin $(@)

/etc/install.d/master.d/$(MODULE_NAME): conf/master
		sed -e "s/%BOARD_ID%/$(BOARD_ID)/" \
		    -e "s/%BOARD_VERNUM%/$(BOARD_VERNUM)/" $(?) \
		    > /tmp/ddk_sample_aa
		cp /tmp/ddk_sample_aa $(@)
		chmod 0640 $(@)
		chown bin $(@)
		chgrp bin $(@)
		rm -f /tmp/ddk_sample_aa

/etc/install.d/$(MODULE_NAME): conf/install
		cp $(?) $(@)
		chmod 0750 $(@)
		chown bin $(@)
		chgrp bin $(@)

/etc/uninstall.d/$(MODULE_NAME): conf/uninstall
		cp $(?) $(@)
		chmod 0750 $(@)
		chown bin $(@)
		chgrp bin $(@)

#
# The following is a "soft" include directive, which that if the included file
# does not exist, make will not halt with a fatal error.
#

Include Makefile.Deps

