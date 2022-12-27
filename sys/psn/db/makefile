CFLAGS=  -DKERNEL -Uvax -Usun -Dm68k -Dmc68020 -Dmc68881 -DINET -DETHERLINK \
	 -DSTREAMS -UQUOTA -DAUTOCONFIG -DSCREEN -DSLOTS -DPASS_MAJOR \
	 -DPAGING -DNFS -I. -I$(SINC) -Zn -F

OFILES= db.o sysdebug.o opcode.o disdriver.o doprnt.o mmuprt.o var.o

all: $(OFILES) 
	ld -r -o debugger.o $(OFILES)

sysdebug.o:	sysdebug.h

doprnt.o:	sysdebug.h

disdriver.o:	sysdebug.h

opcode.o:	sysdebug.h

mmuprt.o:       sysdebug.h

var.o:          sysdebug.h
