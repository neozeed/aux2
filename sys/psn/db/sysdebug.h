#define DBGBUFSZ 4096

#ifdef M68881
#define mc68881		/* user.h uses this form. */
#endif M68881

#define VARB	11
#define VARD	13
#define VARE	14
#define VARM	22
#define VARS	28
#define VART	29

#define COREMAGIC 0140000

#define RD	0
#define WT	1
#define NSP	0
#define	ISP	1
#define	DSP	2
#define STAR	4
#define STARCOM 0200
#define DSYM	7
#define ISYM	2
#define ASYM	1
#define NSYM	0
#define ESYM	(-1)
#define BKPTSET	1
#define BKPTEXEC 2
#define	SYMSIZ	100
#define MAXSIG	20

#define BPT	0x4E42		/* Trap 2, breakpoint instruction */

#define FD	0200
#define	SETTRC	0
#define	RIUSER	1
#define	RDUSER	2
#define	RUSTR	3
#define WIUSER	4
#define	WDUSER	5
#define	WUSTR	6
#define	RUREGS	10
#define	WUREGS	11
#define	CONTIN	7
#define	EXIT	8
#define SINGLE	9

#if 0
#define ps	17		/* register offset definitions	*/
#define sp	15
#define pc	16
#define a6	14
#endif

#define MAXPOS	80
#define MAXLIN	128
#define MAXSTRLEN	BUFSIZ	/* I don't like it, but that's what cc does */
#define EOR	'\n'
#define TB	'\t'
#define QUOTE	0200
#define STRIP	0177
#define LOBYTE	0377
#define EVEN	-2

union
{
	int	I[2];
	long	L;
} itolws;

#if 0
#define leng(a)		((long)((unsigned)(a)))
#define shorten(a)	((int)(a))
#define itol(a,b)	(itolws.I[0]=(a), itolws.I[1]=(b), itolws.L)
#define itol68(a,b)	((a << 16) | (b & 0xFFFF))
#endif


#define TYPE	typedef			/* algol-like statement definitions */
#define STRUCT	struct
#define UNION	union
#define REG	register

#define BEGIN	{
#define END	}

#define IF	if(
#define THEN	){
#define ELSE	} else {
#define ELIF	} else if (
#define FI	}

#define FOR	for(
#define WHILE	while(
#define DO	){
#define OD	}
#define REP	do{
#define PER	}while(
#define DONE	);
#define LOOP	for(;;){
#define POOL	}

#define SKIP	;
#define DIV	/
#define REM	%
#define NEQ	^
#define ANDF	&&
#define ORF	||

#define TRUE	 (-1)
#define FALSE	0
#define LOBYTE	0377
#define HIBYTE	0177400
#define STRIP	0177
#define HEXMSK	017

#define SPACE	' '
#define TB	'\t'
#define NL	'\n'

#define DBNAME "adb68\n"
#define LPRMODE "%Q"
#define OFFMODE "+%o"
#define TXTRNDSIZ 8192L

typedef	struct symb	SYMTAB;
typedef struct symb	*SYMPTR;
typedef struct syment	CSYMTAB;
typedef struct syment	*CSYMPTR;
typedef	struct bhdr	BHDR;

struct bhdr			/* Old (5.0) a.out header */
{
	long	fmagic;
	long	tsize;
	long	dsize;
	long	bsize;
	long	ssize;
	long	rtsize;
	long	rdsize;
	long	entry;
};

struct symb
{
	char	symf;		/* symbol type */
	long	vals;		/* symbol value */
	int	smtp;		/* SYMTYPE */
	char	*symc;		/* pointer to symbol name */
};

#define SYMCHK 047
#define SYMTYPE(st)	((st>=041 || (st>=02 && st<=04))\
			? ((st&07)>=3 ? DSYM : (st&07)) : NSYM)
#define MAXCOM	64
#define MAXARG	32
#define LINSIZ	256
TYPE	int		INT;
TYPE	int		VOID;
TYPE	long int	L_INT;
TYPE	float		REAL;
TYPE	double		L_REAL;
TYPE	unsigned	POS;
TYPE	char		BOOL;
TYPE	char		CHAR;
TYPE	char		*STRING;
TYPE	char		MSG[];
TYPE	struct bkpt	BKPT;
TYPE	BKPT		*BKPTR;

L_INT		inkdot();
SYMPTR		lookupsym();
SYMPTR		symget();
POS		get();
POS		chkget();
STRING		exform();
L_INT		round();
BKPTR		scanbkpt();
VOID		fault();

#define NSECT 16

struct bkpt {
	INT	loc;
	INT	ins;
	INT	count;
	INT	initcnt;
	INT	flag;
	CHAR	comm[MAXCOM];
	BKPT	*nxtbkpt;
};

# ifdef M68881
TYPE	struct freglist	FREGLIST;

TYPE	FREGLIST	*FREGPTR;
struct freglist {
	STRING	rname;
	INT	roffs;
	L_INT	rval[3];	/* 96-bit internal format */
};
# endif M68881

TYPE	struct reglist	REGLIST;

TYPE	REGLIST		*REGPTR;
struct reglist {
	STRING	rname;
	INT	roffs;
	L_INT	rval;
};

extern int dotinc, space, dot;
