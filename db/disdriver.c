/*
 * Copyright 1987, 1988, 1989, 1990 Apple Computer, Inc.
 * All Rights Reserved.
 *
 * THIS IS UNPUBLISHED PROPRIETARY SOURCE CODE OF APPLE COMPUTER, INC.
 * The copyright notice above does not evidence any actual or
 * intended publication of such source code.
 */


#ifdef lint
#include "sys/sysinclude.h"
#else lint
#include "compat.h"
#include "sys/param.h"
#include "sys/uconfig.h"
#include "sys/types.h"
#include "sys/mmu.h"
#include "sys/sysmacros.h"
#include "sys/page.h"
#include "sys/systm.h"
#include "sys/map.h"
#include "sys/dir.h"
#include "sys/signal.h"
#include "sys/time.h"
#include "sys/user.h"
#include "sys/errno.h"
#include "sys/region.h"
#include "sys/proc.h"
#include "sys/buf.h"
#include "sys/reg.h"
#include "sys/file.h"
#include "sys/acct.h"
#include "sys/sysinfo.h"
#include "sys/var.h"
#include "sys/ipc.h"
#include "sys/shm.h"
#include "sys/termio.h"
#include "sys/utsname.h"
#include "sys/debug.h"
#include "sys/ttychars.h"
#endif lint
#include "sys/via6522.h"
#include "sys/module.h"
#include "sys/gdisk.h"
#include "sysdebug.h"
#include "ctype.h"
#include "varargs.h"
#include "values.h"
#include "filehdr.h"
#include "syms.h"
#include "nan.h"
/* disdriver */
/*
 * Routines that drive the disassembler.
 */


#define BYTE	1
#define WORD	2
#define LONG	4

#define ISP	1
#define DSP	2

#define HEXADDR		1
#define	SYMADDR		2
#define XSYMADDR	3

#define PRBUFSIZE 120  /* max length of print line */
#define MAXCASE 512  /* max case labels */
#define MAXSYMSIZE 128  /* length of of longest symbol allowed */
#define NESYM 30  /* number of local expression symbols */
#define ESYMNAMESIZ 20  /* length of an expression symbol name */

/* expsym types */
#define ESYMNULL 0  /* empty entry */
#define ESYMREG 1  /* register */
#define ESYMLAST 2  /* last value */
#define ESYMVAL 3  /* value */

long loc;
int caseflg;
char printbuf[PRBUFSIZE];
char mneu[PRBUFSIZE];
int textbase, database;
struct syment *symtab;
char *strtab;
long symsize;
FILHDR *fhdr;
extern int *dbgregp;
extern int symtabsize;
int dotinc, space, dot;
struct esym {
	int type;
	char name[ESYMNAMESIZ];
	int val;
} esym[NESYM];

struct esym *lookupesym();
struct esym *enteresym();

/*
 * Initializes access to the unix symbol table that was loaded
 * at boot time.
 */
initsym()
{

#define STRTABADDR 0x12f00000

	if (symtabsize == 0)
		return(0);	/* symbols not loaded */

	if (fhdr) {
		return(symsize);  /* already initialized */
	}

	/* initialize access to symbol table */

	fhdr = (FILHDR *)((int)STRTABADDR);
	if (fhdr->f_magic != MC68MAGIC) {
		symtabsize = 0;
		fhdr = 0;
		symsize = 0;
		return(symsize);
	}
	symtab = (struct syment *)((int)fhdr + sizeof(FILHDR));
	strtab = (char *)symtab + fhdr->f_nsyms * SYMESZ;
	symsize = sizeof(FILHDR) + fhdr->f_nsyms * SYMESZ + 
		*(long *)strtab;
	return(symsize);
}

/*
 * Return pointer to a string which represents the symbolic address
 * for the address 'adx'. If there is no symbol table then the returned
 * address is printed in hex.
 */
char *
formatsym(adx)
int adx;
{

	if (!initsym()) {
		sprintf(printbuf, "0x%lx", adx);
	} else {
		printsym(printbuf, adx, 0);
	}
	return(printbuf);
}

/*
 * Returns the value of the symbol given in 'str' thru 'val'. If no value can
 * be found then the procedure returns -1 else it returns 0.
 */
int
symvalue(str, val)
char *str;
int *val;
{
	char buf[128];
	int value;

	if (!initsym()) {
		if (isxdigit(str[0])) {
			*val = strtol(str,(char **)0,0);
			return(0);
		}
		return(-1);
	}

	if (isdigit(str[0])) {
		*val = strtol(str,(char **)0,0);
		return(0);
	}

	if ((value = getloc(str)) == -1) {
		dbgcpy(buf, "_");
		dbgcat(buf, str);
		if ((value = getloc(buf)) == -1) {
			if (isxdigit(str[0])) {
				*val = strtol(str,(char **)0,0);
				return(0);
			} else {
				return(-1);
			}
		}
	}
	*val = value;
	return(0);
}

getsym(value)
long value;
{
	register int i;
	register long diff, oldiff;
	int sym;

	if (!initsym() || fhdr->f_nsyms == 0)
		return(-1);
	sym = -1;
	oldiff = 1000000;
	for (i=0; i<fhdr->f_nsyms; i++) {
		if (symtab[i].n_sclass == C_EXT ||
		     symtab[i].n_sclass == C_STAT) {
			diff = value - symtab[i].n_value;
			if (diff >= 0 &&
			    diff < oldiff &&
			    symtab[i].n_name[0] != '.') {
				oldiff = diff;
				sym = i;
			}
		}
		i += symtab[i].n_numaux;
	}
	return(sym);
}
 
printbyte(bufptr,byte,flag)
char *bufptr;
int byte;
int flag;
{
	if (flag && ((byte >= ' ') && (byte <= '~')))
		return(sprintf(bufptr,"'%c'",byte));
	else
		return(sprintf(bufptr,"0x%x",byte));
}

printsym(strbuf,value,nosym)
char *strbuf;
long value;
int nosym;
{
	int symindex;
	long offset;
	long symval;
	int prsize;

	if ((value < 10) && (value > -10) && (nosym < 2))
		return(sprintf(strbuf,"%ld",value));
	else if ((((value < 0x30) && (value > -0x30)) && (nosym < 2)) 
			|| (nosym == 1))
		if ((value < 0) && (value > -0x10000))
			return(sprintf(strbuf,"-0x%lx",-value));
		else
			return(sprintf(strbuf,"0x%lx",value));
	else {
		symindex = getsym(value);
		if (symindex == -1)
			return(sprintf(strbuf,"0x%lx",value));
		symval = symtab[symindex].n_value;
		offset = value - symtab[symindex].n_value;
		if (offset != 0) {
			if (nosym == 3)
				return(0);
			if (offset < 10) {
				strbuf += (prsize = prsym(strbuf,symindex));
				return(prsize + sprintf(strbuf,"+%ld",offset));
			} else if (offset < 0x1000) {
				strbuf += (prsize = prsym(strbuf,symindex));
				return(prsize + sprintf(strbuf,"+0x%lx",offset));
			} else
				return(sprintf(strbuf,"0x%lx",value));
		}
		else {
			return(prsym(strbuf,symindex));
		}
	}
}

prsym(strbuf,i)
char *strbuf;
int i;
{
	if (symtab[i].n_zeroes == 0)
		return(sprintf(strbuf,"%s",strtab+symtab[i].n_offset));
	else
		return(sprintf(strbuf,"%.8s",symtab[i].n_name));
}

long
getloc(strptr)
char *strptr;
{
	register int i;
	long base, offset;
	char sym[MAXSYMSIZE];
	register int len;

	if (!initsym())
		return(-1);

	for (len=0; len<MAXSYMSIZE; len++) {
		if (isalnum(*strptr) || (*strptr == '_'))
			sym[len] = *strptr++;
		else
			break;
	}

	sym[len] = 0;

	for (i=0; i<fhdr->f_nsyms; i++) {
		if (symtab[i].n_scnum <= N_UNDEF)
			continue;
		if (symtab[i].n_zeroes == 0) {
			if (!dbgcmp(strtab+symtab[i].n_offset,sym))
				break;
		} else if (len <= 8 && !dbgncmp(symtab[i].n_name,sym,8))
			break;
		i += symtab[i].n_numaux;
	}

	if (i == fhdr->f_nsyms)
		return(-1);

	base = symtab[i].n_value;

	return(base);
}

#define DIGIT(x) (isdigit(x)? ((x)-'0'): (10+tolower(x)-'a'))
#define MBASE 36

long
strtol(str, ptr, base)
char *str, **ptr;
int base;
{
	long val;
	int xx, sign;

	val = 0L;
	sign = 1;
	if(base < 0 || base > MBASE) {
		goto OUT;
	}
	while(isspace(*str)) {
		++str;
	}
	if(*str == '-') {
		++str;
		sign = -1;
	} else if(*str == '+') {
		++str;
	}
	if(base == 0) {
		if(*str == '0') {
			++str;
			if(*str == 'x' || *str == 'X') {
				++str;
				base = 16;
			} else if(*str == 'd' || *str == 'D') {
				++str;
				base = 10;
			} else {
				base = 8;
			}
		} else {
			base = 16;
		}
	} else if(base == 16) {
		if(str[0] == '0' && (str[1] == 'x' || str[1] == 'X')) {
			str += 2;
		}
	}

	/*
	 * for any base > 10, the digits incrementally following
	 *	9 are assumed to be "abc...z" or "ABC...Z"
	 */
	while(isalnum(*str) && (xx=DIGIT(*str)) < base) {
		/* accumulate neg avoids surprises near maxint */
		val = base*val - xx;
		++str;
	}
OUT:
	if(ptr != (char**)0) {
		*ptr = str;
	}
	return(sign*(-val));
}

static char *exprerr[] = {
	"no error",
	"unknown symbol",
	"missing operator",
	"expression dosn't evaluate to a value",
	"illegal character",
	"monadic operator to the right of a value",
	"dyadic operator with no value on the left",
	"indirection through illegal address",
};

/*
 * Evaluates the string pointed to by 'bufp'. Returns the value thru 'valp'.
 * Returns 0 if sucessful else an error code. Will print out an error
 * report if 'prterr' is non zero.
 */
expression(bufp, valp, prterr)
char **bufp;
int *valp;
int prterr;
{
	char haveleft;  /* true if have scanned of a value for the left side */
	int value;  /* value of expression */
	int right;  /* right side of dyadic op */
	char *p;  /* internal buffer pointer */
	int error;  /* error code */
	int op;  /* operator character */
	char symbuf[ESYMNAMESIZ];  /* tmp buffer for esym name */
	int i;
	struct esym *symp;

	p = *bufp;
	haveleft = 0;
	error = 0;
	value = 0;

	while (1) {
		if (isalnum(*p) || *p == '_') {
			if (haveleft) {
				error = 2;
				goto out;
			}
			if (symvalue(p, &value)) {
				error = 1;
				goto out;
			}
			while (isalnum(*p) || *p == '_') {
				p++;
			}
			haveleft = 1;
		} else {
			switch (*p++) {

			default:
				error = 4;
				goto out;
				break;

			case ' ':
			case '\0':
				p--;  /* don't eat up terminator */
			case ')':
				if (!haveleft) {
					error = 3;
				}
				goto out;
				break;

			case '(':
				if (haveleft) {
					error = 2;
					goto out;
				}
				if (error = expression(&p, &value, prterr)) {
					goto out;
				}
				haveleft = 1;
				break;

			case '$':
				if (haveleft) {
					error = 2;
					goto out;
				}
				for (i = 0; isalnum(*p) && i < ESYMNAMESIZ; i++, p++) {
					symbuf[i] = *p;
				}
				symbuf[i] = '\0';
				if ((symp = lookupesym(symbuf)) == 0) {
					error = 1;
					goto out;
				}
				value = esymvalue(symp);
				haveleft = 1;
				break;

			case '+':
			case '-':
			case '*':
			case '/':
			case '%':
			case '~':
			case '|':
			case '^':
			case '&':
			case '<':
			case '>':
				op = p[-1];
				if (haveleft) {
					/* check for monadic only ops */
					switch (op) {
					case '~':
						error = 5;
						goto out;
						break;
					default:
						break;
					}

					if (error = expression(&p, &right, prterr)) {
						goto out;
					} else {
						switch (op) {
						case '+':
							value += right;
							break;
						case '-':
							value -= right;
							break;
						case '*':
							value *= right;
							break;
						case '/':
							value /= right;
							break;
						case '%':
							value %= right;
							break;
						case '|':
							value |= right;
							break;
						case '&':
							value &= right;
							break;
						case '<':
							value <<= right;
							break;
						case '>':
							value >>= right;
							break;
						case '^':
							value ^= right;
							break;
						}
					}
				} else {
					if (error = expression(&p, &value, prterr)) {
						goto out;
					}
					switch (op) {
					case '~':
						value = ~value;
						break;
					case '+':
						break;
					case '-':
						value = -value;
						break;
					case '*':
						if (badaddr(value)) {
							error = 7;
						} else {
							value = *((int *)value);
						}
						break;
					default:
						error = 6;
						break;
					}
				}
				goto out;
			}
		}
	}
	
	out:
	if (error) {
		if (prterr) {
			dbgprintf("**** %s\n", exprerr[error]);
		}
	} else {
		*bufp = p;  /* update buffer pointer */
		*valp = value;  /* return value */
	}
	return(error);
}

/*
 * Look up 'buf' to see if its in the esym table. If so return its ptr.
 * else return 0.
 */
struct esym *
lookupesym(buf)
char *buf;
{
	struct esym *p;

	for (p = &esym[0]; p < &esym[NESYM]; p++) {
		if (dbgcmp(p->name, buf) == 0) {
			return(p);
		}
	}
	return(0);
}

/*
 * return the current value of the esym at 'p'.
 */
esymvalue(p)
struct esym *p;
{

	switch (p->type) {

#if 0
	case ESYMREG:
		return(dbgregp[dbgursgs[p->val]]);
		break;
#endif

	case ESYMVAL:
		return(p->val);
		break;

#if 0
	case ESYMLAST:
		return(dbglastval);
		break;
#endif

	default:
		return(0);
	}
}

/*
 * Enter a symbol with 'name', 'type', and 'val'. Overlays an old definition
 * if there is one. If symbol entered return symbol location else return 0.
 */
struct esym *
enteresym(type, name, val)
char *name;
int type;
int val;
{
	struct esym *p;
	struct esym *ent;

	ent = 0;
	for (p = &esym[0]; p < &esym[NESYM]; p++) {
		if (dbgcmp(p->name, name) == 0) {
			ent = p;
			break;
		}
		if (ent == 0 && p->type == ESYMNULL) {
			ent = p;
		}
	}
	if (ent) {
		dbgcpy(ent->name, name);
		ent->type = type;
		ent->val = val;
	}
	return(ent);
}

psymoff(v, symtype, s) /* Prints symbol, offset, and suffix */
	int  v;
	int  symtype;
	char *s;                       /* Suffix for print line */
{
	char buf[80];
	printsym(buf, v, 0);
	dbgprintf("%s%s",buf,s);
}

int inkdot(incr)
{
    int newdot;

    newdot = dot + incr;
    return(newdot);
}

unsigned int
chkget(pc, idsp, sz)
unsigned char *pc;
{

	switch (sz) {

		case 1:
		return(*pc);
		break;

		case 2:
		return(*(unsigned short *)(pc));
		break;

		case 4:
		return(*(unsigned int *)(pc));
		break;
	}
}

disassm(pc,count)
int count;
int pc;
{
	int loc;
	int i;
	char *bufptr;
	int prsize;
	int opcode;
	static oldcount = 24;

	if (count == 0)
		count = oldcount;
	else
		oldcount = count;
	dot = pc;
	while (count-- > 0){
		if (checkabort()) {
			dbgprintf("\n");
			return;
		}
		psymoff(dot,0,"\t");
		opcode = chkget(dot,ISP,WORD);
		printins(ISP,opcode,0);
		dbgprintf("\n");
		dot = inkdot(dotinc);
	}
}
