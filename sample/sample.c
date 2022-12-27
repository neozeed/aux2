#if !defined(_NO_IDENTS) && defined(_MAIN_IDENTS)
# pragma ident "@(#)ddk:2.0/sample/sample.c"
#endif

/*
 * Copyright 1990 Apple Computer, Inc.
 * All Rights Reserved.
 *
 * This sample driver may be modified, given away, sold or whatever as long
 * as the Apple Computer, Inc. copyright notices remain intact and that the
 * modified driver is intended for an A/UX-licensed user.
 */

/*
 * sample.c
 */

#include <sys/types.h>
#include <sys/sysmacros.h>
#include <sys/reg.h>
#include <sys/uio.h>

#define MAX_CARDS 6	     /* Maximum number of cards this driver supports */

extern  int sample_cnt;      /* Number of cards assigned to this driver */
extern  int sample_addr[];   /* Slot number of each card */          

/*
 * Initialization routine.
 * 
 * This routine is called during the kernel boot process and    
 * before any calls to other routines in this driver.
 *
 * Arguments:    none
 * Return Code:  none
 */

void
sample_init()
{
	if(sample_cnt) {
	    int i;

	    if(sample_cnt > MAX_CARDS) {
		printf("sample_init:  too many cards (%d) assigned!\n",
		    sample_cnt);

		/*
		 * This driver has been assigned more cards than it can handle,
		 * so the safe thing to do is panic now while the system is
		 * still booting and we have less to lose.
		 */

		panic("sample_init");
	    }

	    printf("sample_init:  Cards assigned to this driver are in slots:");
	    for(i=0; i < sample_cnt; ++i)
	        printf(" %d", sample_addr[i]);
	    printf("\n");
	} else
	    printf("sample_init:  No cards were assigned to this driver.\n");
}

/*
 * Open routine.
 *
 * Arguments:
 *   dev	major/minor device number
 *   flag	bit values as defined in <sys/file.h>
 *   newdev	If your driver is a Streams device driver, you should compare
 *		'newdev' with the contents of 'dev'. If the two values are the
 *		same, your driver can change the value of 'newdev' so that a
 *		different major/minor number than that specified by 'dev' is
 *		opened. This strategy used to open target clone devices, as
 *		described in Chapter 6, "Streams Device Drivers."
 *
 * Return Code:
 *   0		no error
 *   n		one of the errno values in <sys/errno.h>
 */

int
sample_open(dev, flag, newdev)
dev_t dev;
int   flag;
dev_t *newdev;
{
	printf("sample_open:  maj=%d, min=%d, flag=0x%x, nmaj=%d, nmin=%d\n",
	    major(dev), minor(dev), flag, major(*newdev), minor(*newdev));

	return(0);
}

/*
 * Close routine.
 *
 * Arguments:
 *   dev	major/minor device number
 *   flag	bit values as defined in <sys/file.h>
 *
 * Return Code:  none
 */

int
sample_close(dev, flag)
dev_t dev;
int   flag;
{
	printf("sample_close: maj=%d, min=%d, flag=0x%x\n",
	    major(dev), minor(dev), flag);
}

/*
 * Read routine.
 *
 * The sample_read and sample_write routines modify the 
 * iovec buffers directly, but you might want to use the   
 * iomove() routine to do this work.
 *
 * Arguments:
 *   dev	major/minor device number
 *   uio	pointer to a uio structure, defined in <sys/uio.h> as follows:
 *		struct uio {
 *		    struct iovec {
 *			caddr_t iov_base;	buffer start address
 *			int     iov_len;	buffer length
 *		    } *uio_iov;
 *		    int uio_iovcnt;		number of uio_iov structures
 *		    int uio_offset;		current read offset
 *		    int uio_seg;		UIOSEG_USER or UIOSEG_KERNEL
 *		    int uio_resid;		total bytes requested to read 
 *		};
 *
 * Return Code:
 *   0		no error
 *   n		one of the errno values in <sys/errno.h>
 */

int
sample_read(dev, uio)
dev_t  dev;
struct uio *uio;
{
	register struct iovec *iov;
	register int cnt;

	printf("sample_read:  maj=%d, min=%d, uio=0x%x, cnt=%d, off=%d",
	    major(dev), minor(dev), uio, uio->uio_iovcnt, uio->uio_offset);
	printf(", seg=%d, res=%d\n", uio->uio_seg, uio->uio_resid);

	while(uio->uio_resid) {
    	    iov = uio->uio_iov;
	    cnt = iov->iov_len;

	    if (cnt == 0) {		    /* empty buffer? */               
		uio->uio_iov++;		    /* get next buffer and decrement */
		uio->uio_iovcnt--;	    /* the count of available buffers */
		if (uio->uio_iovcnt < 0)    /* iovcnt can't be less than 0 */
		    panic("sample_read");
		continue;		    /* setup for the next round */
	    }

           iov->iov_base   += cnt;	    /* Handle iov_base, */
           iov->iov_len    -= cnt;	    /* iov_len, uio_offset, */
           uio->uio_offset += cnt;	    /* and uio_resid as for */
           uio->uio_resid  -= cnt;	    /* a real read. */
	}

	return(0);
}

/*
 * Write routine.
 *
 * Arguments:
 *   dev	major/minor device number
 *   uio	pointer to a uio structure, defined in <sys/uio.h> as follows:
 *		struct uio {
 *		    struct iovec {
 *		    	caddr_t iov_base;	buffer start address
 *		    	int     iov_len;	buffer length
 *		    } *uio_iov;
 *		    int uio_iovcnt;		number of uio_iov structures
 *		    int uio_offset;		current read offset
 *		    int uio_seg;		UIOSEG_USER or UIOSEG_KERNEL
 *		    int uio_resid;		total bytes requested to write 
 *		};
 *
 * Return Code:
 *   0		no error
 *   n		one of the errno values in <sys/errno.h>
 */

int
sample_write(dev, uio)
dev_t  dev;
struct uio *uio;
{
	register struct iovec *iov;
	register int cnt;

	printf("sample_write: maj=%d, min=%d, uio=0x%x, cnt=%d, off=%d",
	    major(dev), minor(dev), uio, uio->uio_iovcnt, uio->uio_offset);
	printf(", seg=%d, res=%d\n", uio->uio_seg, uio->uio_resid);

	while(uio->uio_resid) {
    	    iov = uio->uio_iov;
	    cnt = iov->iov_len;

	    if (cnt == 0) {		    /* empty buffer? */               
		uio->uio_iov++;		    /* get next buffer and decrement */
		uio->uio_iovcnt--;	    /* the count of available buffers */
		if (uio->uio_iovcnt < 0)    /* iovcnt can't be less than 0 */
		    panic("sample_write");
		continue;		    /* setup for the next round */
	    }

           iov->iov_base   += cnt;	    /* Handle iov_base, */
           iov->iov_len    -= cnt;	    /* iov_len, uio_offset, */
           uio->uio_offset += cnt;	    /* and uio_resid as for */
           uio->uio_resid  -= cnt;	    /* a real write. */
	}

	return(0);
}

/*
 * Interrupt routine.
 *
 * Unless there is a card in a slot handled by this device 
 * driver, this routine is not called.
 *
 * If a card is plugged in and generates an interrupt on a  
 * slot for which this device driver is responsible, this 
 * routine is called and the 'a_dev' member contains the slot  
 * number of the card that interrupted. If the card handles 
 * more than one device, the driver has to check the card 
 * itself to determine the device for which the interrupt was
 * generated.
 *
 * The slot numbers for cards that plug into the Macintosh II NuBus slots
 * start at $9 (9) and continue up to $E (14). The Macintosh itself is slot $0.
 *
 * Arguments:
 *   args	pointer to interrupt arguments structure, defined in <sys/reg.h>
 *		as follows:
 *		struct args {
 *		    int     a_regs[16];		saved registers
 *		    dev_t   a_dev;		slot number
 *		    int     (*a_faddr)();	called routine address    
 *		    short   a_ps;		original status register 
 *		    caddr_t a_pc;		original pc
 *		};
 *
 * Return Code:  none
 */

void
sample_int(args)
struct args *args;
{
	printf("sample_int:   slot=%d, args=0x%x\n", args->a_dev, args);
}

/*
 * Ioctl routine.
 *
 * Arguments:
 *   dev	major/minor device number
 *   cmd	command
 *   arg	pointer to arguments
 *   flag	bit values as defined in <sys/file.h>
 *
 * Return Code:
 *   0		no error
 *   n		one of the errno values in <sys/errno.h>
 */

int
sample_ioctl(dev, cmd, arg, flag)
dev_t   dev;
int     cmd;
caddr_t *arg;
int     flag;
{
	printf("sample_ioctl: maj=%d, min=%d, cmd=0x%x, arg=0x%x, flag=0x%x\n",
	    major(dev), minor(dev), cmd, arg, flag);

	return(0);
}
