#ifndef lint	/* .../sys/psn/io/if_ae6.c */
#define _AC_NAME if_ae6_c
#define _AC_NO_MAIN "@(#) Copyright (c) 1987 Apple Computer, Inc., All Rights Reserved.  {Apple version 2.1 89/10/13 15:54:12}"
#include <apple_notice.h>

#ifdef _AC_HISTORY
  static char *sccsid = "@(#)Copyright Apple Computer 1987\tVersion 2.1 of if_ae6.c on 89/10/13 15:54:12";
#endif		/* _AC_HISTORY */
#endif		/* lint */

#define _AC_MODS

/*
 * (C) 1986 Apple Computer Co. 
 *
 */
#define	NAE6	6
#define AT2.0

#if	NAE6 > 0
#include "sys/types.h"
#include "sys/param.h"
#include "sys/uconfig.h"
#include "sys/sysmacros.h"
#include "sys/reg.h"
#include "sys/mmu.h"
#include "sys/page.h"
#include "sys/systm.h"
#include "sys/mbuf.h"
#include "sys/socket.h"
#include "sys/socketvar.h"
#include "sys/var.h"
#include "sys/ioctl.h"
#include "sys/time.h"
#include "sys/user.h"
#include "sys/errno.h"

#include "net/if.h"
#include "net/route.h"
#include "net/netisr.h"
#include "net/raw_cb.h"

#include "netinet/in.h"
#include "netinet/in_systm.h"
#include "netinet/in_var.h"
#include "netinet/ip.h"
#include "netinet/ip_var.h"
#include "netinet/if_ether.h"
#include "vaxuba/ubavar.h"
#include "sys/if_ae6.h"
#include "sys/protosw.h"

extern int ae6cnt;
extern int ae6addr[];

#ifdef AT2.0
#define LEN_802_3	1494
#endif

/*
 * Ae6 routine declarations.
 */
int	ae6_probe(), ae6_init(), ae6_attach(), ae6_output(), ae6_ioctl(),
    	ae6int(), ae6_rint(), ae6_tint(), ae6_timeout();
struct 	mbuf *ae6_get();
u8 	get_boundary_page(), get_current_page();

#ifdef ETHERLINK
/*
 * Raw ethernet support routines and structures.
 */
int ren_output();
extern struct protosw ensw[];
#endif

int ae6_trans[16];

/*
 * Ae6 global structure declarations.
 */
struct	uba_device *ae6info[NAE6];
struct	uba_driver ae6driver = {
	ae6_probe, ae6_attach, (u_short *) 0, ae6info
};
struct 	ae6 ae6[NAE6];
/*
 * Ae6 external structure references.
 */
extern	struct 	ifnet loif;

/*
 * Probe for device.
 */
static ae6_probe(ui)
    struct uba_device *ui;
{
    struct ae6 *ae6p = &ae6[ui->ui_unit];

    if (ui->ui_unit < ae6cnt) {
	ae6_map(ui);
	if (iocheck((caddr_t) &ae6p->ae6_cmd->ae6_csr) == 0) {
	    ae6_unmap(ui);
	    return (0);
	}
	else {
	    size_ram(ae6p);
	    printf("ae%d: ram size %d bytes\n", ui->ui_unit, 
		   (ae6p->ae6_recv_stop + 1) * AE6_PAGESIZE);
	    return (1);
	}
    }
    else
	return (0);
}

/*
 * Set up the control, ram and rom pointers for the ethernet board.
 */

static ae6_map(ui)
    struct uba_device *ui;
{
    struct ae6 *ae6p = &ae6[ui->ui_unit];
    int ind;
    int i;
    
    ind = ae6addr[ui->ui_unit] - SLOT_LO;
    ae6_trans[ind] = ui->ui_unit;
    ae6p->ae6_base = (char *)(AE6_BASE(ae6addr[ui->ui_unit]) + RAM);
    ae6p->ae6_cmd = (struct ae6_regs *)(AE6_BASE(ae6addr[ui->ui_unit]) + CTRL);
    ae6p->ae6_rom = (char *)(AE6_BASE(ae6addr[ui->ui_unit]) + ROM);
#ifdef AT2.0
    bzero(ae6p->ae6_mar, 8);
#else
    for (i = 0; i < 8; i++)
	ae6p->ae6_mar[i] = 0xff;
#endif

}

/*
 * Zero the control, ram and rom pointers for the ethernet board.
 */
static ae6_unmap(ui)
    struct uba_device *ui;
{
    struct ae6 *ae6p = &ae6[ui->ui_unit];
    
    ae6p->ae6_base = 0;
    ae6p->ae6_cmd = 0;
    ae6p->ae6_rom = 0;
    ae6p->ae6_recv_stop = 0;
}

/*
 * Interface exists: make it available by inializing the network visible
 * interface structure.  The system will initalize the interface when it is
 * ready to accept packets.
 */
static ae6_attach(ui)
    struct uba_device *ui;
{
    register struct ae6 *ae6p = &ae6[ui->ui_unit];
    register struct ifnet *ifp = &ae6p->ae6_if;

    ifp->if_unit = ui->ui_unit;
    ifp->if_name = "ae";
    ifp->if_mtu = ETHERMTU;
    
    ifp->if_init = ae6_init;
    ifp->if_ioctl = ae6_ioctl;

    /*
     *  Return the maximum (read) transfer size for this interface.
     *  We should be able to handle four back-to-back packets of
     *  this size.
     */
    ifp->if_output = ae6_output;
    ifp->if_flags = IFF_BROADCAST;
    if_attach(ifp);
#ifdef ETHERLINK
    /*
     * Initialize the raw output routine in the ETHERLINK protocol structure.
     */
    ensw[0].pr_output = ren_output;
#endif
    bzero(ae6p->ae6_mcast_usage, 64);
}

/*
 * Initalize the hardware.
 */
static ae6_init(unit)
    int	unit;
{
    register int i;
    register struct ae6 *ae6p = &ae6[unit];
    register struct ae6_regs *rp = ae6p->ae6_cmd;
    struct ifnet *ifp = &ae6p->ae6_if;
    register struct sockaddr_in *sin;
    int s;

    /* not yet, if address still unknown */
    if (ifp->if_addrlist == (struct ifaddr *)0)
	return;

    s = splimp();
    ae6p->ae6_oactive = 0;
    /*
     * Initialize the NIC as specified in board documentation. 
     */
    rp->ae6_csr = CR_STP;
    rp->ae6_rbcr0 = 0;
    rp->ae6_rbcr1 = 0;
    /*
     * Loop until the interrupt status register indicates that the board
     * has been reset. 
     */
    for (i = 0; i < 1000; i++)
	if (rp->ae6_isr & ISR_RST)
	    break;
    if ((rp->ae6_isr & ISR_RST) == 0) {
	splx(s);
	printf("warning, ae%d:  init failed\n", unit);
    }
    rp->ae6_dcr = DCR_FT_4 | DCR_BMS | DCR_WTS;
    rp->ae6_rcr =  RCR_AM | RCR_AB;
    rp->ae6_tcr = TCR_LOO_EXT;
    rp->ae6_pstart = RECV_START;
    /*
     * Point Pstop to the first byte of the page outside the receive
     * ring. 
     */
    rp->ae6_pstop = ae6p->ae6_recv_stop + 1;

    /*
     * Set the boundary page to be the one "behind" RECV_START 
     */
    rp->ae6_bnry = ae6p->ae6_recv_stop;
    rp->ae6_isr = ISR_ALL;

    /*
     * Get the six bytes of ethernet address from ROM, into the enaddr
     * structure in arpcom.
     */
    if(slot_ether_addr(ae6p->ae6_rom, ae6p->ae6_enaddr) == -1) {
	splx(s);
	printf("ae%d:  ethernet address not found\n", unit);
	return;
    }
    /*
     * Store our ethernet address for REVARP.
     */
    localetheraddr(ae6p->ae6_enaddr, NULL);

    /*
     * Set the csr to point to page 1 of registers 
     */
    rp->ae6_csr = CR_PS1 | CR_STP;
    rp->ae6_curr = RECV_START;
    
    /*
     * Set up the page address registers from the arpcom structure.
     */
    rp->ae6_par0 = ae6p->ae6_enaddr[0];
    rp->ae6_par1 = ae6p->ae6_enaddr[1];
    rp->ae6_par2 = ae6p->ae6_enaddr[2];
    rp->ae6_par3 = ae6p->ae6_enaddr[3];
    rp->ae6_par4 = ae6p->ae6_enaddr[4];
    rp->ae6_par5 = ae6p->ae6_enaddr[5];

    ae6_mar_set(ae6p);

    /*
     * Enable interrupts
     */
    rp->ae6_imr = 0x1f;
    rp->ae6_csr = CR_STA;
    rp->ae6_tcr = TCR_LOO_NON;
    
    ifp->if_flags |=  IFF_RUNNING;
    if (ifp->if_snd.ifq_head)
	ae6_start(unit);
    splx(s);
}

ae6_mar_set(ae6p)
    struct ae6 *ae6p;
{
    register struct ae6_regs *rp = ae6p->ae6_cmd;
    int s;

    /*
     * Set up the Multicast address on the board from the stored
     * multicast array.
     */
    s = splimp();

    rp->ae6_csr = CR_STP | CR_PS1;

    rp->ae6_mar0 = ae6p->ae6_mar[0];
    rp->ae6_mar1 = ae6p->ae6_mar[1];
    rp->ae6_mar2 = ae6p->ae6_mar[2];
    rp->ae6_mar3 = ae6p->ae6_mar[3];
    rp->ae6_mar4 = ae6p->ae6_mar[4];
    rp->ae6_mar5 = ae6p->ae6_mar[5];
    rp->ae6_mar6 = ae6p->ae6_mar[6];
    rp->ae6_mar7 = ae6p->ae6_mar[7];

    rp->ae6_csr = CR_STP;
    splx(s);
}

ae6_ovrecover(unit)
    int unit;
{
    register struct ae6 *ae6p = &ae6[unit];
    register struct ae6_regs *rp = ae6p->ae6_cmd;
    register int i;
    struct ifnet *ifp = &ae6p->ae6_if;
    int s = splimp();
    
    ae6p->ae6_oactive = 0;
    /*
     * Reset the NIC as specified in board documentation in the case
     * of an overflow warning interrupt. 
     */
    rp->ae6_csr = CR_STP;
    /*
     * Try to remove packets from the receive buffer ring.
     */
    ae6_rintr(unit);

    rp->ae6_csr = CR_STP;    
    rp->ae6_rbcr0 = 0;
    rp->ae6_rbcr1 = 0;
    /*
     * Loop until the interrupt status register indicates that the board
     * has been reset. 
     */
    for (i = 0; i < 1000; i++)
	if (rp->ae6_isr & ISR_RST)
	    break;
    if ((rp->ae6_isr & ISR_RST) == 0) {
	splx(s);
	printf("warning, ae%d:  overflow NIC reset failed\n", unit);
    }
    rp->ae6_tcr = TCR_LOO_EXT;
    rp->ae6_csr = CR_STA;
    rp->ae6_tcr = TCR_LOO_NON;
    splx(s);
}

/*
 * Start a packet transmission.
 */
static ae6_start(unit)
    int	unit;
{
    int len;
    register struct ae6 *ae6p = &ae6[unit];
    register struct ae6_regs *rp = ae6p->ae6_cmd;
    struct mbuf *m;
    
    if (ae6p->ae6_oactive)
	goto restart;
    
    IF_DEQUEUE(&ae6p->ae6_if.if_snd, m);
    if (m == 0) {
	ae6p->ae6_oactive = 0;
	return;
    }
    len = ae6_put(m, ae6p->ae6_base);
    if (len < ETHERMIN + sizeof(struct ether_header)) 
	len = ETHERMIN + sizeof(struct ether_header);
    /*
     * Set the packet length and the transmit start address to the
     * requested values. 
     */
restart:
    rp->ae6_tbcr1 = (len >> 8) & 0x7;
    rp->ae6_tbcr0 = (len & 0xff);
    rp->ae6_tpsr = 0;
    /*
     * ... on your mark, get set, go ... 
     */
    rp->ae6_csr = CR_TXP | CR_STA;
    ae6p->ae6_oactive = 1;
    /*
     * Schedule a timeout to guard against trasmitter freeze-up, and
     * missed interrupts.  This timeout is cancelled when we get the
     * transmit interrupt for the outgoing packet.  
     */
    ae6p->ae6_flags |= AE6_TOPENDING;
    timeout(ae6_timeout, unit, v.v_hz * 60 * 2);
}

static ae6_timeout(unit)
    int unit;
{
    register struct ae6 *ae6p = &ae6[unit];
    
    ae6p->ae6_flags &= ~AE6_TOPENDING;
    printf("ae%d transmitter frozen -- resetting\n", unit);
    ae6_init(unit);
}

static ae6_output(ifp, m0, dst)
    struct ifnet *ifp;
    struct mbuf *m0;
    struct sockaddr *dst;
{
    int type, s, error;
    u_char edst[6];
    struct in_addr idst;
    register struct ae6 *ae6p = &ae6[ifp->if_unit];
    register struct mbuf *m = m0;
    struct mbuf *mcopy = (struct mbuf *) 0;
    register struct ether_header *e, *e2;
    register int off;
    int usetrailers = 0;
    
    if ((ifp->if_flags & (IFF_UP|IFF_RUNNING)) != (IFF_UP|IFF_RUNNING)) {
	error = ENETDOWN;
	goto bad;
    }
    switch (dst->sa_family) {
#ifdef INET
    case AF_INET:
	idst = ((struct sockaddr_in *) dst)->sin_addr;
	if (!arpresolve(&ae6p->ae6_ac, m, &idst, edst, &usetrailers))
	    return (0);	/* if not yet resolved */
	off = ntohs((u_short)mtod(m, struct ip *)->ip_len) - m->m_len;
	if (usetrailers && off > 0 && (off & 0x1ff) == 0 &&
	    m->m_off >= MMINOFF + 2 * sizeof (u_short)) {
	    type = ETHERTYPE_TRAIL + (off>>9);
	    m->m_off -= 2 * sizeof (u_short);
	    m->m_len += 2 * sizeof (u_short);
	    *mtod(m, u_short *) = htons((u_short)ETHERTYPE_IP);
	    *(mtod(m, u_short *) + 1) = htons((u_short)m->m_len);
	    /*
	     * Packet to be sent as trailer: move first packet
	     * (control information) to end of chain.
	     */
	    while (m->m_next)
		m = m->m_next;
	    m->m_next = m0;
	    m = m0->m_next;
	    m0->m_next = 0;
	    m0 = m;
	} else {
	    type = ETHERTYPE_IP;
	    off = 0;
	    if ((in_lnaof(idst) == INADDR_ANY) || in_broadcast(idst) ||
		(in_lnaof(idst) == INADDR_BROADCAST))
		mcopy = m_copy(m, 0, (int) M_COPYALL);
	}
	break;
#endif
    case AF_UNSPEC:
	e = (struct ether_header *)dst->sa_data;
	bcopy((caddr_t)e->ether_dhost, (caddr_t)edst, sizeof(edst));
	type = e->ether_type;
	break;
#ifdef AT2.0
    case AF_APPLETALK:
	/*
	 * Sending 802.3 packet from ethertalk.
	 */
	e = mtod(m, struct ether_header *);
	type = e->ether_type;
	if (type >= 0 && type <= LEN_802_3)
	    goto gotheader;
	else {
	    error = EINVAL;
	    goto bad;
	}
#endif
    case AF_ETHERLINK:
	e = mtod(m, struct ether_header *);
	goto gotheader;

    default:
	printf("ae%d: can't handle af%d\n", ifp->if_unit, dst->sa_family);
	error = EAFNOSUPPORT;
	goto bad;
    }

    /*
     * Add local net header.  If no space in first mbuf, allocate
     * another. 
     */
    if (m->m_off > MMAXOFF ||
	MMINOFF + sizeof(struct ether_header) > m->m_off) {
	m = m_get(M_DONTWAIT, MT_HEADER);
	if (m == 0) {
	    error = ENOBUFS;
	    goto bad;
	}
	m->m_next = m0;
	m->m_off = MMINOFF;
	m->m_len = sizeof(struct ether_header);
    } else {
	m->m_off -= sizeof(struct ether_header);
	m->m_len += sizeof(struct ether_header);
    }

    e = mtod(m, struct ether_header *);
    e->ether_type = htons((u_short) type);
    bcopy((caddr_t)edst, (caddr_t)e->ether_dhost, sizeof(edst));

gotheader:
    /*
     * Fill in the source address.
     */
    bcopy((caddr_t)ae6p->ae6_enaddr, (caddr_t)e->ether_shost, 
	  sizeof(e->ether_shost)); 


    /*
     * Queue message on interface, and start output if interface not yet
     * active. 
     */
    s = splimp();
    if (IF_QFULL(&ifp->if_snd)) {
	IF_DROP(&ifp->if_snd);
	splx(s);
	m_freem(m);
	return (ENOBUFS);
    }
    IF_ENQUEUE(&ifp->if_snd, m);
    if (ae6p->ae6_oactive == 0)
	ae6_start(ifp->if_unit);
    splx(s);
    return (mcopy ? looutput(&loif, mcopy, dst) : 0);
    
bad:
    m_freem(m0);
    if (mcopy)
	m_freem(mcopy);
    return (error);
}

/*
 * ae6 interrupt handlers 
 */

static ae6_xintr(unit)
    int unit;
{
    register struct ae6 *ae6p = &ae6[unit];
    struct ae6_regs *rp = ae6p->ae6_cmd;
    int s;

    if (ae6p->ae6_oactive == 0)
	return;

    if(ae6p->ae6_flags & AE6_TOPENDING) {
	untimeout(ae6_timeout, unit);
	ae6p->ae6_flags &= ~AE6_TOPENDING;
    }
    ae6p->ae6_oactive = 0;
    s = splimp();
    if (ae6p->ae6_if.if_snd.ifq_head)
	ae6_start(unit);
    splx(s);
}


static ae6_rintr(unit)
    int unit;
{
    register struct ae6 *ae6p = &ae6[unit];
    struct ae6_regs *rp = ae6p->ae6_cmd;
    u8 gc, gb;

    /*
     * Check for a received packet 
     */
next:
    gb = get_boundary_page(ae6p);
    gc = get_current_page(ae6p);
    
    /* If there is one, then get it. */
    if (gb != gc) {
	ae6_rpkt(ae6p);
	update_boundary_page(ae6p);
	goto next;
    }
}

/*ARGSUSED*/
static ae6int(args)
    struct args *args;
{
    int unit = ae6_trans[args->a_dev - SLOT_LO];
    register struct ae6 *ae6p = &ae6[unit];
    register struct ae6_regs *rp = ae6p->ae6_cmd;
    struct ifnet *ifp = &ae6p->ae6_if;
    register int isr, sr = 0;

    if (unit >= NAE6) {
	printf("ae6int:  interrupt from slot %d\n", unit);
	panic("ae6int");
	/*NOTREACHED*/
    }

    /* 
     * Read interrupt status register 
     */
    if(rp->ae6_isr == 0) {
	printf("ae%d spurious interrupt\n", unit);
	return;
    }
    /*
     * Handle transmit interrupts and transmit errors.
     */
    if (isr = (rp->ae6_isr & (ISR_PTX | ISR_TXE))) {
	rp->ae6_isr = isr;
	if (isr & ISR_PTX)
	    ifp->if_opackets++;
	else if (rp->ae6_isr & ISR_TXE) {
	    ifp->if_oerrors++;
	    sr = rp->ae6_rsr;
	    if (sr & TSR_COL)
		ae6p->ae6_stats.scol_err++;
	    if (sr & TSR_ABT)
		ae6p->ae6_stats.sabt_err++;
	    if (sr & TSR_CRS)
		ae6p->ae6_stats.scrs_err++;
	    if (sr & TSR_FU)
		ae6p->ae6_stats.sfu_err++;
	    if (sr & TSR_CDH)
		ae6p->ae6_stats.scdh_err++;
	    if (sr & TSR_OWC)
		ae6p->ae6_stats.sowc_err++;
	}
	ae6_xintr(unit);
    }
    /*
     * Handle receive interrupts and receive errors.
     */
    if (isr = rp->ae6_isr & (ISR_PRX | ISR_OVW | ISR_RXE)) {
	rp->ae6_isr = isr;
	ae6_rintr(unit);
	if (isr & ISR_OVW) {
	    ae6_ovrecover(unit);
	    printf("ae%d: Receive overflow warning\n", unit); 
	}
	if (isr & ISR_RXE) {
	    ifp->if_ierrors++;
	    sr = rp->ae6_rsr;
	    if (sr & RSR_CRC) {
		if (sr & RSR_FAE)
		    ae6p->ae6_stats.rfae_err++;
		else
		    ae6p->ae6_stats.rcrc_err++;
	    }
	    if (sr & RSR_FO)
		ae6p->ae6_stats.rfo_err++;
	    if (sr & RSR_MPA) {
		ae6p->ae6_stats.rmiss_err++;
		printf("ae%d: Rcv overflow, lost %d packets\n", unit, 
		   rp->ae6_cntr2); 
	    }
	}
    }
}

struct	sockaddr redst = { AF_ETHERLINK };
struct	sockaddr resrc = { AF_ETHERLINK };
struct	sockproto reproto = { PF_ETHERLINK };
#define STOPADDR ((unsigned)base+(unsigned) ((ae6p->ae6_recv_stop+1)*AE6_PAGESIZE))
#define STARTADDR ((unsigned)base+(unsigned)(RECV_START*AE6_PAGESIZE))

static ae6_rpkt(ae6p)
    register struct ae6 *ae6p;
{
    struct rcv_pkt_hdr *rcvp;
    register short len;
    int page;
    int off;
    int resid;
    u_char *base = (u_char *)ae6p->ae6_base;
    u_short *sp;
    register struct mbuf *m;
    struct mbuf *m0;
    struct ifqueue *inq;
    int s;
    register int type;
    u_char *buf;

    ae6p->ae6_if.if_ipackets++;
    /*
     * Set the page to point to the page boundary of the received packet.
     * Then create a pointer into memory based on the page, the page size
     * and the location in memory of the ethernet ram. 
     */
    page = (int) (get_boundary_page(ae6p) & 0xff);
    rcvp = (struct rcv_pkt_hdr *) ((unsigned) (page * AE6_PAGESIZE) +
				   (unsigned) ae6p->ae6_base);
    /*
     * The input packet length is swapped, so swap it into a valid value
     * to count down. 
     */
    len = (u16) ((rcvp->rph_hbc << 8) | rcvp->rph_lbc);
    
    if (len == 0)
	return;
    type = rcvp->rph_type;
    if ((type >= ETHERTYPE_TRAIL) &&
	(type < ETHERTYPE_TRAIL + ETHERTYPE_NTRAILER)) {
	off = (type - ETHERTYPE_TRAIL) * 512;
	if (off >= ETHERMTU)
	    return;

	sp = (u_short *)((unsigned)(rcvp + 1) + off);
	if ((unsigned)sp >= STOPADDR) {
	    sp  = (u_short *)(STARTADDR + (unsigned)sp - STOPADDR);
	}
	rcvp->rph_type = ntohs(*sp++);
	resid = ntohs(*sp);
	
	if (off + resid > len)
	    return;
	len = off + resid;
	buf = (u_char *)rcvp;
    } else {
	off = 0;
	len -= 4;		/* remove checksum */
#ifdef AT2.0
	if (type >= 0 && type <= LEN_802_3) {
	    buf = (u_char *)rcvp + sizeof (struct rcv_pkt_hdr) -
		sizeof (struct ether_header);
	} else {
#endif
	    buf = (u_char *)rcvp + sizeof(struct rcv_pkt_hdr);
	    len -= sizeof(struct ether_header);
#ifdef AT2.0
	}
#endif
    }
    m = ae6_get(buf, len, off, ae6p, &ae6p->ae6_if);
    if (m == 0) {
	return;
    }
    /*
     * Pull packet off interface.  Off is nonzero if packet
     * has trailing header; ae6_get will then force this header
     * information to be at the front, but we still have to drop
     * the type and length which are at the front of any trailer data.
     */
    if (off) {
	struct ifnet *ifp;
	
	ifp = *(mtod(m, struct ifnet **));
	m->m_off += 2 * sizeof (u_short);
	m->m_len -= 2 * sizeof (u_short);
	*(mtod(m, struct ifnet **)) = ifp;
    }

    switch (rcvp->rph_type) {

#ifdef INET
    case ETHERTYPE_IP:
	schednetisr(NETISR_IP);
	inq = &ipintrq;
	break;

    case ETHERTYPE_ARP:
	arpinput(&ae6p->ae6_ac, m);
	return;
	
    case ETHERTYPE_REVARP:
	revarpinput(&ae6p->ae6_ac, m);
	return;
#endif
    default:
#ifdef AT2.0
	if (NETISR_ET != NULL) {
	    type = rcvp->rph_type;
	    if (type >= 0 && type <= LEN_802_3) {
		if (type < 60)
		    /*
		     * Dont copy pad bytes.  Just len + 802.3 hdr + *ifp
		     */
		    m->m_len = type + 14 + 4;
		schednetisr(*NETISR_ET); /* indirect through etintr */
		inq = &etintrq;
		break;
	    }
	} else {
	    m_freem(m);
	    return;
	}

#endif	
#ifdef ETHERLINK
	{
	    register int i;
	    register char *p = (char *)rcvp->rph_dest;
	    
	    reproto.sp_protocol = rcvp->rph_type;
	    /*
	     * The source is the entire ethernet header.
	     */
	    wordcopy((caddr_t)p, (caddr_t)resrc.sa_data, 
		  sizeof(struct ether_header));
	    /*
	     * The destination is the real packet destination.
	     */
	    wordcopy((caddr_t)rcvp->rph_dest, (caddr_t)redst.sa_data, 
		  sizeof(rcvp->rph_dest));

	    /*
	     * remove the ifp pointer from the data.
	     */
	    m->m_off += 2 * sizeof (u_short);
	    m->m_len -= 2 * sizeof (u_short);

	    raw_input(m, &reproto, (struct sockaddr *)&resrc,
		      (struct sockaddr *)&redst);
	    return;
	}
#else
	m_freem(m);
	return;
#endif
    }
    s = splimp();
    if (IF_QFULL(inq)) {
	IF_DROP(inq);
	splx(s);
	m_freem(m);
	return;
    }
    IF_ENQUEUE(inq, m);
    splx(s);
}

/*
 * Ae6 page manipulation routines. 
 */

static u8 get_current_page(ae6p)
    struct ae6 *ae6p;
{
    u8 page;
    register struct ae6_regs *rp = ae6p->ae6_cmd;
    
    rp->ae6_csr = CR_PS1;
    page = rp->ae6_curr;
    rp->ae6_csr = CR_PS0;
    return (page);
}

static u8 get_boundary_page(ae6p)
    struct ae6 *ae6p;
{
    u8 page;
    register struct ae6_regs *rp = ae6p->ae6_cmd;
    
    page = rp->ae6_bnry + 1;
    if (page > ae6p->ae6_recv_stop)
	page = RECV_START;
    return (page);
}

static update_boundary_page(ae6p)
    struct ae6 *ae6p;
{
    register struct ae6_regs *rp = ae6p->ae6_cmd;
    struct rcv_pkt_hdr *rcvp;
    u8 new_bnry, page;

    page = (get_boundary_page(ae6p) & 0xff);
    rcvp = (struct rcv_pkt_hdr *) ((unsigned) (page * AE6_PAGESIZE) +
				   (unsigned) ae6p->ae6_base);

    /*
     * Get the new boundary out of the packet header, and subtract one
     * because the boundary must always be one behind the current packet
     * pointer. 
     */
    new_bnry = rcvp->rph_next - 1;

    /*
     * If the subtraction causes the page to be out of the receive area,
     * then wrap it around. 
     */
    if (new_bnry < RECV_START) {
	new_bnry = ae6p->ae6_recv_stop;
    }
    rp->ae6_bnry = new_bnry;
}

/*
 * Ae6 ram read/write routines.	 Ram is read and written from/to mbufs. 
 */

static ae6_put(m, base)
    struct mbuf *m;
    char *base;
{
    register struct mbuf *mp;
    register int off;
    register u_char *bp;
    u16 temp;
    u16 *tp;

    for (bp = (u_char *) base, mp = m, off = 0; mp; mp = mp->m_next) {
	register unsigned len = mp->m_len;
	u_char *mcp;
	
	off += mp->m_len;
	if (len == 0)
	    continue;
	mcp = mtod(mp, u_char *);
	/*
	 * If the ram pointer (bp) is left on a byte address,
	 * then adjust the pointer back one byte, create a
	 * 16 bit quantity from the odd byte in card ram, and
	 * the first byte in the next mbuf, store it, and
	 * leave bp pointing to a 16 bit word address, and
	 * mcp pointing to the next byte (which is byte
	 * aligned.  This only works for 68020s!)
	 */
	if(((unsigned)bp & 0x1) != 0) {
	    bp--;
	    tp = (u16 *)bp;
	    temp = *tp;
	    *tp++ = ((temp & 0xff00) | (*mcp++ & 0xff)) & 0xffff;
	    len--;
	    bp = (u_char *)tp;
	}
	wordcopy((u16 *) mcp, (u16 *) bp, (int) len);
	bp += len;
    }
    if (off & 01) {
	off++;
    }
    m_freem(m);
    return (off);
}


static struct mbuf *ae6_get(buf, totlen, off0, ae6p, ifp)
    u_char *buf;
    int totlen, off0;
    struct ae6 *ae6p;
    struct ifnet *ifp;
{
    u_char *base = (u_char *)ae6p->ae6_base;
    register struct mbuf *m;
    struct mbuf *top = 0, **mp = &top;
    register int off = off0, len;
    register u_char *cp;
    int templen;
    u16 *bufp;

    cp = buf;
    while (totlen > 0) {

	MGET(m, M_DONTWAIT, MT_DATA);
	if (m == 0) {
	    m_freem(top);
	    top = 0;
	    goto out;
	}
	if (off) {
	    len = totlen - off;
	    templen = (unsigned)buf + sizeof(struct rcv_pkt_hdr) + off;
	    if (templen >= STOPADDR) {
		cp = (u_char *)(STARTADDR + templen - STOPADDR);
	    } else 
		cp = (u_char *)templen;
	} else
	    len = totlen;

	if (len > MCLBYTES/2) {
	    /*
	     * If doing the first mbuf and
	     * the interface pointer hasn't been put in,
	     * put it in a separate mbuf to preserve alignment.
	     */
	    if (ifp) {
		len = 0;
		goto nopage;
	    }
	    MCLGET(m);
	    if (m->m_len != MCLBYTES)
		goto nopage;
	    m->m_len = MIN(len, MCLBYTES);
	    goto copy;
	}
nopage:
	if (ifp) {
	    /*
	     * Leave room for ifp.
	     */
	    m->m_len = MIN(MLEN - sizeof(ifp), len);
	    m->m_off += sizeof(ifp);
	} else
	    m->m_len = MIN(MLEN, len);
copy:
	/*
	 * Copy stuff from the card to memory, watching for recieve
	 * ring end.
	 */
	bufp = mtod(m, u16 *);
	if (((unsigned)cp + m->m_len) >= STOPADDR) {
	    templen = STOPADDR - (unsigned) cp;
	    wordcopy((u16 *) cp, bufp, templen);
	    cp = (u_char *) STARTADDR;
	    bufp = (u16 *)((unsigned)bufp + templen);
	    wordcopy((u16 *) cp, bufp, 
		     (unsigned) m->m_len - templen);
	    cp += (m->m_len - templen);
	} else {
	    wordcopy((u16 *) cp, bufp, (unsigned) m->m_len);
	    cp += m->m_len;
	}
	*mp = m;
	mp = &m->m_next;
	if (off) {
	    off += m->m_len;
	    if (off == totlen) {
		cp = buf + sizeof(struct rcv_pkt_hdr);
		off = 0;
		totlen = off0;
	    }
	}
	else
	    totlen -= m->m_len;
	if (ifp) {
	    /*
	     * Prepend interface pointer to first mbuf.
	     */
	    m->m_len += sizeof(ifp);
	    m->m_off -= sizeof(ifp);
	    *(mtod(m, struct ifnet **)) = ifp;
	    ifp = (struct ifnet *)0;
	}
    }
out:
    return (top);
}

/*
 * Process an ioctl request. 
 */
static ae6_ioctl(ifp, cmd, data)
    register struct ifnet *ifp;
    int	cmd;
    caddr_t data;
{
    register struct ifaddr *ifa = (struct ifaddr *)data;
    register struct sockaddr *sa = (struct sockaddr *)data;
    struct ae6 *ae6p = &ae6[ifp->if_unit];
    int s = splimp(), error = 0;
    
    switch (cmd) {
	
    case SIOCSIFADDR:
	ifp->if_flags |= IFF_UP;
	switch (ifa->ifa_addr.sa_family) {
	case AF_INET:
	    /* 
	     * Initalize the interface.  This includes setting the
	     * ethernet address for the interface.
	     */
	    ae6_init(ifp->if_unit);
	    ((struct arpcom *)ifp)->ac_ipaddr = IA_SIN(ifa)->sin_addr;
	    arpwhohas((struct arpcom *)ifp, &IA_SIN(ifa)->sin_addr);
	    break;
	    
	default:
	    error = EINVAL;
	    break;
	}
	break;
    case SIOCSIFFLAGS:
	if ((ifp->if_flags & IFF_UP) == 0 &&
	    ifp->if_flags & IFF_RUNNING) {
	    ifp->if_flags &= ~IFF_RUNNING;
	} else if (ifp->if_flags & IFF_UP &&
		   (ifp->if_flags & IFF_RUNNING) == 0)
	    ae6_init(ifp->if_unit);
	break;
#ifdef AT2.0
    case SIOCSMAR:
	if (!(suser())) {
	    splx(s);
	    return(u.u_error);
	}
	ae6_reg_mcast(ae6p, sa->sa_data);
	break;
    case SIOCUMAR:
	if (!(suser())) {
	    splx(s);
	    return(u.u_error);
	}
	ae6_unreg_mcast(ae6p, sa->sa_data);
	break;
    case SIOCGMAR:
	bcopy(ae6p->ae6_mar, sa->sa_data, 8);
	break;
#endif
    default:
	error = EINVAL;
	break;
    }
    splx(s);
    return (error);
}

static wordcopy(from, to, len)
    register unsigned short *from, *to;
    register unsigned int len;
{
    register unsigned int i;
    if (i = (len >> 1)) {
	do
	    *to++ = *from++;
	while(--i);
    }
    if (len & 0x01)
	    *to++ = *from++ & 0xff00;
}

/*
 * Figure out the size of memory on the ethernet board.
 */
#define MAGIC 0x1234
size_ram(ae6p)
    struct ae6 *ae6p;
{
    register short *p;
    register int i = 0;

    p = (short *)ae6p->ae6_base;
    *p = MAGIC;
    do {
	p += ((0x40 * AE6_PAGESIZE) >> 1);
	i++;
    } while ((*p != MAGIC) && (i < 2));

    if (*p == MAGIC)
	ae6p->ae6_recv_stop = 0x3f;
    else
	ae6p->ae6_recv_stop = 0xfe;
}
/*
 * Apple talk support code, 802.3 and multicast routines.
 */
#ifdef AT2.0
#define CRCPOLY 0x04c11db7

u_int ae6_crc(addr)
    char *addr;			/* 6 bytes of ethernet address */
{
    u_int crc;
    u_char byte;
    register int i, j;

    crc = -1;
    for (i = 0; i < 6; i++) {
	byte = addr[i];
	for (j = 0; j < 8; j++) {
	    if ((byte & 1) ^ (crc >> 31)) {
		crc <<= 1;
		crc ^= CRCPOLY;
	    } else
		crc <<= 1;
	    byte >>= 1;
	}
    }
    return(crc);
}

ae6_reg_mcast(ae6p, addr)
    struct ae6 *ae6p;
    char *addr;			/* 6 bytes of ethernet address */
{
    register struct ae6_regs *rp = ae6p->ae6_cmd;
    u_int crc;
    u_char mask;

    crc = ae6_crc(addr) >> 26;
    if (ae6p->ae6_mcast_usage[crc]++)
	return;			/* This bit is already set */
    mask = crc % 8;
    mask = (u_char)1 << mask;
    ae6p->ae6_mar[crc/8] |= mask;
    ae6_mar_set(ae6p);
    rp->ae6_csr = CR_STA;
}

ae6_unreg_mcast(ae6p, addr)
    struct ae6 *ae6p;
    char *addr;
{
    register struct ae6_regs *rp = ae6p->ae6_cmd;
    u_int crc;
    u_char mask;

    crc = ae6_crc(addr) >> 26;
    if (ae6p->ae6_mcast_usage[crc] == 0)
	return;			/* That bit wasn't in use! */
    if (--ae6p->ae6_mcast_usage[crc])
	return;			/* That bit is still in use */
    mask = crc % 8;
    mask = ((u_char)1 << mask) ^ 0xff;
    ae6p->ae6_mar[crc/8] &= mask;
    ae6_mar_set(ae6p);
    rp->ae6_csr = CR_STA;
}
#endif
/*
 * Etherlink support code.
 */
ren_output(m0, so)
    struct mbuf *m0;
    struct socket *so;
{
    struct rawcb *rp = sotorawcb(so);
    struct ifaddr *ifa;
    struct ifnet *ifp;
    int error;

    if(rp == (struct rawcb *)0) {
	error = ENOPROTOOPT;
	goto ren_bad;
    }
    switch(rp->rcb_proto.sp_family) {
#ifdef ETHERLINK
    case AF_ETHERLINK: {
	/* 
	 * Not great.  A raw ethernet socket must have a bound internet
	 * address to find the interface... but this seems better than
	 * a fixed interface, or the first broadcast interface.
	 */
	struct sockaddr_in inether, *sin;
	
	if((rp->rcb_flags & RAW_LADDR) == 0) {
	    error = EADDRNOTAVAIL;
	    goto ren_bad;
	}
	bzero((caddr_t)&inether, (unsigned)sizeof(inether));
	inether.sin_family = AF_INET;
	sin = (struct sockaddr_in *)&rp->rcb_laddr;
	inether.sin_addr = sin->sin_addr;
	if (inether.sin_addr.s_addr &&
	    (ifa = ifa_ifwithaddr((struct sockaddr *)&inether)) == 0) {
	    error = EADDRNOTAVAIL;
	    goto ren_bad;
	}
	break;
    }
	    
#endif
    default:
	error = EPROTOTYPE;
	goto ren_bad;
    }
    if(ifp = ifa->ifa_ifp)
	return((*ifp->if_output)(ifp, m0, &rp->rcb_laddr));
    error = EADDRNOTAVAIL;
ren_bad:
    if(m0 != 0) 
	m_freem(m0);
    return(error);
}

#endif	/* NAE6 */
