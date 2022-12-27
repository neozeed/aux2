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


dbgputc(c)
register c;
{
	scputchar(c);
}

/*
 * read a character from the usart
 */

dbggetc()
{
	register unsigned int ch;
	ch = scgetchar();
	if (ch == '\r')
		ch = '\n';
	dbgputc(ch);
	return(ch);
}

/*
 * return true if there is a character ready to be input
 */

dbgwaitc()
{

	return(scwaitchar());
}

/*
 * Print an unsigned integer in base b.
 */

dbgpn(n, b)
unsigned long n;
{
	register unsigned long a;

	if(a = n/b)
		dbgpn(a, b);
	dbgputc("0123456789ABCDEF"[(int)(n%b)]);
}

/*
 * Various utility functions used by the debugger
 */


#define STATIC

/* Maximum number of digits in any integer representation */
#define MAXDIGS 11

/* Data type for flags */
typedef char bool;

/* Convert a digit character to the corresponding number */
#define tonumber(x) ((x)-'0')

/* Convert a number between 0 and 9 to the corresponding digit */
#define todigit(x) ((x)+'0')

/* Max and Min macros */
#define max(a,b) ((a) > (b)? (a): (b))

#define PUT(p, n)     { register unsigned char *newbufptr; \
                        newbufptr = bufptr + n; \
			(void) memcpy((char *) bufptr, p, n); \
			bufptr = newbufptr; \
                      }

#define PAD(s, n)     { register int nn; \
			for (nn = n; nn > 20; nn -= 20) \
				PUT(s, 20); \
                        PUT(s, nn); \
                      }

/* bit positions for flags used in doprnt */
#define LENGTH  1       /* l */
#define FPLUS   2       /* + */
#define FMINUS  4       /* - */
#define FBLANK  8       /* blank */
#define FSHARP  16      /* # */
#define PADZERO 32      /* padding zeroes requested via '0' */
#define DOTSEEN 64      /* dot appeared in format specification */
#define SUFFIX  128     /* a suffix is to appear in the output */
#define RZERO   256     /* there will be trailing zeros in output */
#define LZERO   512     /* there will be leading zeroes in output */

char *memchr(), *memcpy();
int dbglen();

STATIC unsigned char doprntbuf[128];  /* buffer for use by doprnt and friends */

STATIC int
_lowdigit(valptr)
long *valptr;
{       /* This function computes the decimal low-order digit of the number */
        /* pointed to by valptr, and returns this digit after dividing   */
        /* *valptr by ten.  This function is called ONLY to compute the */
        /* low-order digit of a long whose high-order bit is set. */

        int lowbit = *valptr & 1;
        long value = (*valptr >> 1) & ~HIBITL;

        *valptr = value / 5;
        return(value % 5 * 2 + lowbit + '0');
}

STATIC int
doprnt(format, ap)
char   *format;
va_list ap;
{

        static char _blanks[] = "                    ";
        static char _zeroes[] = "00000000000000000000";

        unsigned char *bufptr;

        /* This variable counts output characters. */
        int     count = 0;

        /* Starting and ending points for value to be printed */
        register char   *bp;
        char *p;

        /* Field width and precision */
        int     width, prec;

        /* Format code */
        register int    fcode;

        /* Number of padding zeroes required on the left and right */
        int     lzero, rzero;

        /* Flags - bit positions defined by LENGTH, FPLUS, FMINUS, FBLANK, */
        /* and FSHARP are set if corresponding character is in format */
        /* Bit position defined by PADZERO means extra space in the field */
        /* should be padded with leading zeroes rather than with blanks */
        register int    flagword;

        /* Values are developed in this buffer */
        char    buf[MAXDIGS];

        /* Pointer to sign, "0x", "0X", or empty */
        char    *prefix;

        /* Exponent or empty */
        char    *suffix;

        /* Length of prefix and of suffix */
        int     prefixlength, suffixlength;

        /* combined length of leading zeroes, trailing zeroes, and suffix */
        int     otherlength;

        /* The value being converted, if integer */
        long    val;

        /* The value being converted, if real */
        double  dval;

        /* Output values from fcvt and ecvt */
        int     decpt, sign;

        /* Pointer to a translate table for digits of whatever radix */
        char    *tab;

        /* Work variables */
        int     k, lradix, mradix;


        bufptr = doprntbuf;

        /*
         *      The main loop -- this loop goes through one iteration
         *      for each string of ordinary characters or format specification.
         */
        for ( ; ; ) {
                register int n;

                if ((fcode = *format) != '\0' && fcode != '%') {
                        bp = format;
                        do {
                                format++;
                        } while ((fcode = *format) != '\0' && fcode != '%');
                
                        count += (n = format - bp); /* n = no. of non-% chars */
                        PUT(bp, n);
                }
                if (fcode == '\0') {  /* end of format; return */
			doprntbuf[count] = '\0';
                        return(count);
                }

                /*
                 *      % has been found.
                 *      The following switch is used to parse the format
                 *      specification and to perform the operation specified
                 *      by the format letter.  The program repeatedly goes
                 *      back to this switch until the format letter is
                 *      encountered.
                 */
                width = prefixlength = otherlength = flagword = 0;
                format++;

        charswitch:

                switch (fcode = *format++) {

                case '+':
                        flagword |= FPLUS;
                        goto charswitch;
                case '-':
                        flagword |= FMINUS;
                        goto charswitch;
                case ' ':
                        flagword |= FBLANK;
                        goto charswitch;
                case '#':
                        flagword |= FSHARP;
                        goto charswitch;

                /* Scan the field width and precision */
                case '.':
                        flagword |= DOTSEEN;
                        prec = 0;
                        goto charswitch;

                case '*':
                        if (!(flagword & DOTSEEN)) {
                                width = va_arg(ap, int);
                                if (width < 0) {
                                        width = -width;
                                        flagword ^= FMINUS;
                                }
                        } else {
                                prec = va_arg(ap, int);
                                if (prec < 0)
                                        prec = 0;
                        }
                        goto charswitch;

                case '0':       /* obsolescent spec:  leading zero in width */
                                /* means pad with leading zeros */
                        if (!(flagword & (DOTSEEN | FMINUS)))
                                flagword |= PADZERO;
                case '1':
                case '2':
                case '3':
                case '4':
                case '5':
                case '6':
                case '7':
                case '8':
                case '9':
                      { register num = fcode - '0';
                        while (isdigit(fcode = *format)) {
                                num = num * 10 + fcode - '0';
                                format++;
                        }
                        if (flagword & DOTSEEN)
                                prec = num;
                        else
                                width = num;
                        goto charswitch;
                      }

                /* Scan the length modifier */
                case 'l':
                        flagword |= LENGTH;
                        /* No break */
                case 'h':
                        goto charswitch;

                /*
                 *      The character addressed by format must be
                 *      the format letter -- there is nothing
                 *      left for it to be.
                 *
                 *      The status of the +, -, #, and blank
                 *      flags are reflected in the variable
                 *      "flagword".  "width" and "prec" contain
                 *      numbers corresponding to the digit
                 *      strings before and after the decimal
                 *      point, respectively. If there was no
                 *      decimal point, then flagword & DOTSEEN
                 *      is false and the value of prec is meaningless.
                 *
                 *      The following switch cases set things up
                 *      for printing.  What ultimately gets
                 *      printed will be padding blanks, a
                 *      prefix, left padding zeroes, a value,
                 *      right padding zeroes, a suffix, and
                 *      more padding blanks.  Padding blanks
                 *      will not appear simultaneously on both
                 *      the left and the right.  Each case in
                 *      this switch will compute the value, and
                 *      leave in several variables the informa-
                 *      tion necessary to construct what is to
                 *      be printed.  
                 *
                 *      The prefix is a sign, a blank, "0x",
                 *      "0X", or null, and is addressed by
                 *      "prefix".
                 *
                 *      The suffix is either null or an
                 *      exponent, and is addressed by "suffix".
                 *      If there is a suffix, the flagword bit
                 *      SUFFIX will be set.
                 *
                 *      The value to be printed starts at "bp"
                 *      and continues up to and not including
                 *      "p".
                 *
                 *      "lzero" and "rzero" will contain the
                 *      number of padding zeroes required on
                 *      the left and right, respectively.
                 *      The flagword bits LZERO and RZERO tell
                 *      whether padding zeros are required.
                 *
                 *      The number of padding blanks, and
                 *      whether they go on the left or the
                 *      right, will be computed on exit from
                 *      the switch.
                 */



                
                /*
                 *      decimal fixed point representations
                 *
                 *      HIBITL is 100...000
                 *      binary, and is equal to the maximum
                 *      negative number.
                 *      We assume a 2's complement machine
                 */

		case 'D':
                case 'd':
		case 'r':
                        /* Fetch the argument to be printed */
                        if (flagword & LENGTH)
                                val = va_arg(ap, long);
                        else
                                val = va_arg(ap, int);

                        /* Set buffer pointer to last digit */
                        p = bp = buf + MAXDIGS;

                        /* If signed conversion, make sign */
                        if (val < 0) {
                                prefix = "-";
                                prefixlength = 1;
                                /*
                                 * Negate, checking in
                                 * advance for possible
                                 * overflow.
                                 */
                                if (val != HIBITL)
                                        val = -val;
                                else     /* number is -HIBITL; convert last */
                                         /* digit now and get positive number */
                                        *--bp = _lowdigit(&val);
                        } else if (flagword & FPLUS) {
                                prefix = "+";
                                prefixlength = 1;
                        } else if (flagword & FBLANK) {
                                prefix = " ";
                                prefixlength = 1;
                        }

                decimal:
                        { register long qval = val;
                                if (qval <= 9) {
                                        if (qval != 0 || !(flagword & DOTSEEN))
                                                *--bp = qval + '0';
                                } else {
                                        do {
                                                n = qval;
                                                qval /= 10;
                                                *--bp = n - qval * 10 + '0';
                                        } while (qval > 9);
                                        *--bp = qval + '0';
                                }
                        }

                        /* Calculate minimum padding zero requirement */
                        if (flagword & DOTSEEN) {
                                register leadzeroes = prec - (p - bp);
                                if (leadzeroes > 0) {
                                        otherlength = lzero = leadzeroes;
                                        flagword |= LZERO;
                                }
                        }

                        break;

                case 'u':
                        /* Fetch the argument to be printed */
                        if (flagword & LENGTH)
                                val = va_arg(ap, long);
                        else
                                val = va_arg(ap, unsigned);

                        p = bp = buf + MAXDIGS;

                        if (val & HIBITL)
                                *--bp = _lowdigit(&val);

                        goto decimal;

                /*
                 *      non-decimal fixed point representations
                 *      for radix equal to a power of two
                 *
                 *      "mradix" is one less than the radix for the conversion.
                 *      "lradix" is one less than the base 2 log
                 *      of the radix for the conversion. Conversion is unsigned.
                 *      HIBITL is 100...000
                 *      binary, and is equal to the maximum
                 *      negative number.
                 *      We assume a 2's complement machine
                 */

                case 'o':
                        mradix = 7;
                        lradix = 2;
                        goto fixed;

                case 'X':
                case 'x':
                        mradix = 15;
                        lradix = 3;

                fixed:
                        /* Fetch the argument to be printed */
                        if (flagword & LENGTH)
                                val = va_arg(ap, long);
                        else
                                val = va_arg(ap, unsigned);

                        /* Set translate table for digits */
                        tab = (fcode == 'X') ?
                            "0123456789ABCDEF" : "0123456789abcdef";

                        /* Develop the digits of the value */
                        p = bp = buf + MAXDIGS;
                        { register long qval = val;
                                if (qval == 0) {
                                        if (!(flagword & DOTSEEN)) {
                                                otherlength = lzero = 1;
                                                flagword |= LZERO;
                                        }
                                } else
                                        do {
                                                *--bp = tab[qval & mradix];
                                                qval = ((qval >> 1) & ~HIBITL)
                                                                 >> lradix;
                                        } while (qval != 0);
                        }

                        /* Calculate minimum padding zero requirement */
                        if (flagword & DOTSEEN) {
                                register leadzeroes = prec - (p - bp);
                                if (leadzeroes > 0) {
                                        otherlength = lzero = leadzeroes;
                                        flagword |= LZERO;
                                }
                        }

                        /* Handle the # flag */
                        if (flagword & FSHARP && val != 0)
                                switch (fcode) {
                                case 'o':
                                        if (!(flagword & LZERO)) {
                                                otherlength = lzero = 1;
                                                flagword |= LZERO;
                                        }
                                        break;
                                case 'x':
                                        prefix = "0x";
                                        prefixlength = 2;
                                        break;
                                case 'X':
                                        prefix = "0X";
                                        prefixlength = 2;
                                        break;
                                }

                        break;

                case '%':
                        buf[0] = fcode;
                        goto c_merge;

		case 't':
			buf[0] = '\t';
			goto c_merge;

                case 'c':
                        buf[0] = va_arg(ap, int);
                c_merge:
                        p = (bp = &buf[0]) + 1;
                        break;

                case 's':
                        bp = va_arg(ap, char *);
                        if (!(flagword & DOTSEEN))
                                p = bp + dbglen(bp);
                        else { /* a strnlen function would  be useful here! */
                                register char *qp = bp;
                                while (*qp++ != '\0' && --prec >= 0)
                                        ;
                                p = qp - 1;
                        }
                        break;

                default: /* this is technically an error; what we do is to */
                        /* back up the format pointer to the offending char */
                        /* and continue with the format scan */
                        format--;
                        continue;

                }

                /* Calculate number of padding blanks */
                k = (n = p - bp) + prefixlength + otherlength;
                if (width <= k)
                        count += k;
                else {
                        count += width;

                        /* Set up for padding zeroes if requested */
                        /* Otherwise emit padding blanks unless output is */
                        /* to be left-justified.  */

                        if (flagword & PADZERO) {
                                if (!(flagword & LZERO)) {
                                        flagword |= LZERO;
                                        lzero = width - k;
                                }
                                else
                                        lzero += width - k;
                                k = width; /* cancel padding blanks */
                        } else
                                /* Blanks on left if required */
                                if (!(flagword & FMINUS))
                                        PAD(_blanks, width - k);
                }

                /* Prefix, if any */
                if (prefixlength != 0)
                        PUT(prefix, prefixlength);

                /* Zeroes on the left */
                if (flagword & LZERO)
                        PAD(_zeroes, lzero);
                
                /* The value itself */
                if (n > 0)
                        PUT(bp, n);

                if (flagword & (RZERO | SUFFIX | FMINUS)) {
                        /* Zeroes on the right */
                        if (flagword & RZERO)
                                PAD(_zeroes, rzero);

                        /* The suffix */
                        if (flagword & SUFFIX)
                                PUT(suffix, suffixlength);

                        /* Blanks on the right if required */
                        if (flagword & FMINUS && width > k)
                                PAD(_blanks, width - k);
                }
        }
}

int
sprintf(buf, fmt, va_alist)
char *buf;
char *fmt;
va_dcl
{
	va_list ap;
	int len;

	va_start(ap);
	len = doprnt(fmt, ap);
	memcpy(buf, doprntbuf, len+1);
	va_end(ap);
	return(len);
}

int
dbgprintf(fmt, va_alist)
char *fmt;
va_dcl
{
	va_list ap;
	register unsigned char *p;

	va_start(ap);
	doprnt(fmt, ap);
	for (p = doprntbuf; *p != '\0'; p++) {
		dbgputc(*p);
	}
	va_end(ap);
}
