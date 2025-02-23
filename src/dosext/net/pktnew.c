/*
 * (C) Copyright 1992, ..., 2014 the "DOSEMU-Development-Team".
 *
 * for details see file COPYING.DOSEMU in the DOSEMU distribution
 */

/*
 *
 *     Packet driver emulation for the Linux DOS emulator.
 *
 *     Written almost from scratch by Rob Janssen, PE1CHL
 *     (pe1chl@rabo.nl, PE1CHL@PI8UTR.#UTR.NLD.EU, pe1chl@ko23)
 *
 *     Now it is entirely contained in dosemu, no driver needs to
 *     be loaded in DOS anymore.
 *     (it looks like the PC's BIOS supports the packet driver interface)
 *
 *     Also added support for multiple handles, and checking the
 *     actual type passed to access_type.  This means other protocols
 *     than Novell 802.3 are now supported.
 *
 *     21/05/97 Gloriano Frisoni (gfrisoni@hi-net.it)
 *              Now we can run tcp/ip stuff (netscape eudora etc) in
 *              "dosemu windoze".  See README.winnet.
 *
 *     - merged the virtual net code with the original code
 *     - integrated asm helper routine into bios.S
 *     - removed most of the global data from the DOS address space
 *                                    Marcus.Better@abc.se
 *
 */

#include <unistd.h>
#include "emu.h"
#include "pktdrvr.h"
#include "memory.h"
#include "int.h"
#include "bios.h"
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/if_ether.h>
#include "libpacket.h"
#include "virq.h"
#include "coopth.h"
#include "hlt.h"
#include "utilities.h"
#include "emudpmi.h"

#ifndef ETH_FRAME_LEN
#define ETH_FRAME_LEN   1514
#define ETH_ALEN        6               /* Octets in one ethernet addr   */
#define ETH_ZLEN        60              /* Min. octets in frame sans FCS */
#define ETH_P_ALL       0x0003          /* Every packet (be careful!!!) */
#define ETH_P_802_3     0x0001          /* Dummy type for 802.3 frames  */
#define ETH_P_IPX       0x8137          /* IPX over DIX                 */
#endif

#ifndef __linux__
struct ethhdr {
        unsigned char   h_dest[ETH_ALEN];       /* destination eth addr */
        unsigned char   h_source[ETH_ALEN];     /* source ether addr    */
        uint16_t        h_proto;                /* packet type ID field */
} __attribute__((packed));
#endif

static void pkt_hlt(Bit16u idx, HLT_ARG(arg));
static int Insert_Type(int, int, Bit8u *);
static int Remove_Type(int);
int Find_Handle(u_char *buf);
static void printbuf(const char *, struct ethhdr *);
static int pkt_receive(void);
static enum VirqHwRet pkt_virq_receive(void *arg);
static enum VirqSwRet pkt_receiver_callback(void *arg);
static void pkt_receiver_callback_thr(void *arg);
static void pkt_register_net_fd_and_mode(int fd, int mode);
static Bit32u PKTRcvCall_TID;
static Bit16u pkt_hlt_off;

static unsigned short receive_mode;
static unsigned short local_receive_mode;
static int pkt_fd = -1;

/* array used by virtual net to keep track of packet types */
#define MAX_PKT_TYPE_SIZE 10
struct pkt_type {
    int handle;
    int pkt_type_len;
    unsigned char pkt_type[MAX_PKT_TYPE_SIZE];
    int count;       /* To sort the array based on counts of packet type */
} pkt_type_array[MAX_HANDLE];
int max_pkt_type_array=0;

#define PKT_BUF_SIZE (ETH_FRAME_LEN+32)

/* flags from config file, for pkt_globs.flags */
#define FLAG_NOVELL 0x01                /* Novell 802.3 <-> 8137 translation */

/* global data common to all interfaces (there is only one interface
   for the moment...) */

struct per_handle
{
	char in_use;			/* this handle in use? */
	char cls;			/* class it was access_type'd with */
	short packet_type_len;		/* length of packet type */
	int flags;			/* per-packet-type flags */
	int sock;			/* fd for the socket */
	Bit16u rcvr_cs, rcvr_ip;	/* receive handler */
	Bit8u packet_type[16];		/* packet type for this handle */
};

struct pkt_globs
{
    unsigned char classes[4];		/* supported classes */
    unsigned char hw_address[8];	/* our hardware address */
    short type;				/* our type */
    int flags;				/* configuration flags */
    int nfds;				/* number of fd's for select() */
    fd_set sockset;			/* set of sockets for select() */
    struct per_handle  handle[MAX_HANDLE];
} pg;

unsigned char pkt_buf[PKT_BUF_SIZE];

short p_helper_size;
Bit16u p_helper_receiver_cs, p_helper_receiver_ip;
short p_helper_handle;
struct pkt_param *p_param;
struct pkt_statistics *p_stats;

/************************************************************************/

void
pkt_init(void)
{
    int ret;
    emu_hlt_t hlt_hdlr = HLT_INITIALIZER;
    if (!config.pktdrv)
      return;

    hlt_hdlr.name       = "pkt callout";
    hlt_hdlr.func       = pkt_hlt;
    pkt_hlt_off = hlt_register_handler_vm86(hlt_hdlr);

    ret = OpenNetworkLink(pkt_register_net_fd_and_mode);
    if (ret < 0) {
      config.pktdrv = 0;
      return;
    }

    p_param = MK_FP32(BIOSSEG, PKTDRV_param);
    p_stats = MK_FP32(BIOSSEG, PKTDRV_stats);
    pd_printf("PKT: VNET mode is %i\n", config.vnet);

    virq_register(VIRQ_PKT, pkt_virq_receive, pkt_receiver_callback, NULL);

    /* fill other global data */

    GetDeviceHardwareAddress(pg.hw_address);
    pg.classes[0] = ETHER_CLASS;  /* == 1. */
    pg.classes[1] = IEEE_CLASS;   /* == 11. */
    pg.type = 12;			/* dummy type (3c503) */
    pg.flags = config.pktflags;	/* global config flags */

    p_param->major_rev = 1;		/* pkt driver spec */
    p_param->minor_rev = 9;
    p_param->length = sizeof(struct pkt_param);
    p_param->addr_len = ETH_ALEN;
    p_param->mtu = GetDeviceMTU();
    p_param->rcv_bufs = 8 - 1;		/* a guess */
    p_param->xmt_bufs = 2 - 1;

    PKTRcvCall_TID = coopth_create("PKT_receiver_call",
	pkt_receiver_callback_thr);
}

void
pkt_reset(void)
{
    int handle;
    if (!config.pktdrv)
      return;
    WRITE_WORD(SEGOFF2LINEAR(PKTDRV_SEG, PKTDRV_driver_entry_ip), pkt_hlt_off);
    WRITE_WORD(SEGOFF2LINEAR(PKTDRV_SEG, PKTDRV_driver_entry_cs), BIOS_HLT_BLK_SEG);

    max_pkt_type_array = 0;
    for (handle = 0; handle < MAX_HANDLE; handle++)
        pg.handle[handle].in_use = 0;
}

void pkt_term(void)
{
    if (!config.pktdrv)
      return;
    remove_from_io_select(pkt_fd);
    CloseNetworkLink(pkt_fd);
}

/* this is the handler for INT calls from DOS to the packet driver */
static int pkt_int(void)
{
    struct per_handle *hdlp;
    int hdlp_handle=-1;

    /* If something went wrong in pkt_init, pretend we are not there. */
    if (!config.pktdrv)
	return 0;

#if 1
  if (debug_level('P') > 8) {
    pd_printf("NPKT: AX=%04x BX=%04x CX=%04x DX=%04x FLAGS=%08x\n",
		LWORD(eax),LWORD(ebx),LWORD(ecx),LWORD(edx),REG(eflags));
    pd_printf("      SI=%04x DI=%04x BP=%04x SP=%04x CS=%04x DS=%04x ES=%04x SS=%04x\n",
		LWORD(esi),LWORD(edi),LWORD(ebp),LWORD(esp),
		SREG(cs),SREG(ds),SREG(es),SREG(ss));
  }
#endif

    /* first clear CARRY flag that we may later set (on error) */

    NOCARRY;

    /* when BX seems like a valid handle, set the handle info pointer.
       Actually, BX could be  zero in some "default cases". For e.g.
       if it is zero when getting the driver info, we can't say if it is
       the "real" handle or general default value.
     */

    if ((LWORD(ebx) < MAX_HANDLE)) {
	hdlp = &pg.handle[LWORD(ebx)];
        hdlp_handle=LWORD(ebx);
    }
    else
	hdlp = NULL;

    /* switch on the packet driver function code */

    switch (HI(ax))
    {
    case F_DRIVER_INFO:
        pd_printf("Driver info called ...\n");

	if( LO(ax) != 255 ) {
	   /* entry condition AL=255 not given
	    *         Frank <molzahn@cc.umanitoba.ca>, 2000/02/18
	    */
	   HI(dx) = E_BAD_COMMAND;
	   break;
	}
	REG(eax) = 2;				/* basic+extended functions */
	REG(ebx) = 1;				/* version */

        /* If  hdlp_handle == 0,  it is not always a valid handle.
	   At least in case of CUTCP it sets it to zero for the first
	   time.
	*/
	if (hdlp_handle !=0 && hdlp != NULL && hdlp->in_use)
	    REG(ecx) = (hdlp->cls << 8) + 1;	/* class, number */
	else
	    REG(ecx) = (pg.classes[0] << 8) + 1;
	REG(edx) = pg.type;			/* type (dummy) */
	SREG(ds) = PKTDRV_SEG;			/* driver name */
	REG(esi) = PKTDRV_driver_name;
        pd_printf("Class returned = %d, handle=%d, pg.classes[0]=%d \n",
		  REG(ecx)>>8, hdlp_handle, pg.classes[0] );
	return 1;

    case F_ACCESS_TYPE:
	if (memchr(pg.classes,LO(ax),sizeof(pg.classes)) == NULL) { /* check if_class */
	    HI(dx) = E_NO_CLASS;
	    break;
	}
	if (LWORD(ebx) != 0xffff && LWORD(ebx) != pg.type) {
            /* check if_type */
	    HI(dx) = E_NO_TYPE;
	    break;
	}
	if (LO(dx) != 0 && LO(dx) != 1) {	/* check if_number */
	    HI(dx) = E_NO_NUMBER;
	    break;
	}
	if (LWORD(ecx) > sizeof(pg.handle[0].packet_type)) { /* check len */
	    HI(dx) = E_BAD_TYPE;
	    break;
	}
	/* now check if the type is not already in use, and find a free */
	/* handle to use for this request */
	{
	    int free_handle = -1;
	    int handle;
	    unsigned short type;

	    if (LWORD(ecx) == 0)		/* pass-all type? */
		handle = MAX_HANDLE - 1;	/* always put it last */
	    else
		handle = 0;			/* else search from 1st */

	    for (; handle < MAX_HANDLE; handle++) {
		hdlp = &pg.handle[handle];

		if (hdlp->in_use) {
		    if (hdlp->cls == LO(ax) && /* same class? */
			!memcmp(hdlp->packet_type, /* same type? (prefix) */
				SEG_ADR((char *),ds,si),
				_min(LWORD(ecx), hdlp->packet_type_len)))
		    {
			HI(dx) = E_TYPE_INUSE;
			CARRY;
			return 1;
		    }
		} else {
		    if (free_handle == -1)
			free_handle = handle;	/* remember free handle */
		}
	    }

	    /* now see if we found a free handle, and allocate it */
	    if (free_handle == -1) {
		HI(dx) = E_NO_SPACE;
		break;
	    }

	    hdlp = &pg.handle[free_handle];
	    memset(hdlp, 0, sizeof(struct per_handle));
	    hdlp->in_use = 1;
	    hdlp->rcvr_cs = SREG(es);
	    hdlp->rcvr_ip = LWORD(edi);
	    hdlp->packet_type_len = LWORD(ecx);
	    memcpy(hdlp->packet_type, SEG_ADR((char *),ds,si), LWORD(ecx));
	    hdlp->cls = LO(ax);

	    if (hdlp->cls == IEEE_CLASS)
		type = ETH_P_802_3;
	    else {
		if (hdlp->packet_type_len < 2)
		    type = ETH_P_ALL;		/* get all packet types */
		else {
		    type = (hdlp->packet_type[0] << 8) |
			((unsigned char) hdlp->packet_type[1]);
		    switch (type) {
		    case 0:		    /* sometimes this bogus type */
			type = ETH_P_802_3; /* is used for status calls.. */
			break;

		    case ETH_P_IPX:
			if (hdlp->packet_type_len == 2 &&
			    (pg.flags & FLAG_NOVELL))
			{
			    hdlp->flags |= FLAG_NOVELL;
			    hdlp->packet_type[0] = hdlp->packet_type[1] = 0xff;
			    hdlp->cls = IEEE_CLASS;
			    type = ETH_P_802_3;
			}
			break;
		    }
		}
	    }
	    Insert_Type(free_handle, hdlp->packet_type_len,
			    hdlp->packet_type);
	    REG(eax) = free_handle;		/* return the handle */
	}
	return 1;

    case F_RELEASE_TYPE:
	if (hdlp == NULL) {
	    HI(dx) = E_BAD_HANDLE;
	    break;
	}

	Remove_Type(hdlp_handle);
	hdlp->in_use = 0;	/* no longer in use */
	return 1;

    case F_SEND_PKT: {
	p_stats->packets_out++;
	p_stats->bytes_out += LWORD(ecx);

	pd_printf("========Sending packet======\n");
	printbuf("packet to send:", SEG_ADR((struct ethhdr *), ds, si));
	if (pg.flags & FLAG_NOVELL)	/* Novell hack? */
	{
		    char *p;
		    short len;

		    p = SEG_ADR((char *),ds,si);
		    p += 2 * ETH_ALEN;	/* point to protocol type */

		    if (p[0] == (char)(ETH_P_IPX >> 8) &&
			p[1] == (char)ETH_P_IPX &&
			p[2] == (char)0xff && p[3] == (char)0xff)
		    {
			/* it is a Novell Ethernet-II packet, make it */
			/* "raw 802.3" by overwriting type with length */

			len = (p[4] << 8) | (unsigned char)p[5];
			len = (len + 1) & ~1; /* make length even */
			p[0] = len >> 8;
			p[1] = (char)len;
		    }
	}

	if (pkt_write(pkt_fd, SEG_ADR((char *), ds, si), LWORD(ecx)) >= 0) {
	    pd_printf("Write to net was ok\n");
	    return 1;
	}

	warn("WriteToNetwork(len=%u): error %d\n", LWORD(ecx), errno);
	p_stats->errors_out++;
	HI(dx) = E_CANT_SEND;
    }
    break;

    case F_TERMINATE:
	if (hdlp == NULL || !hdlp->in_use)
	    HI(dx) = E_BAD_HANDLE;
	else
	    HI(dx) = E_CANT_TERM;

	break;

    case F_GET_ADDRESS:
	if (LWORD(ecx) < ETH_ALEN) {
	    HI(dx) = E_NO_SPACE;
	    break;
	}
	REG(ecx) = ETH_ALEN;
	memcpy(SEG_ADR((char *),es,di), pg.hw_address, ETH_ALEN);
	return 1;

    case F_RESET_IFACE:
	if (hdlp == NULL || !hdlp->in_use)
	    HI(dx) = E_BAD_HANDLE;
	else
	    HI(dx) = E_CANT_RESET;

	break;

    case F_GET_PARAMS:
	SREG(es) = PKTDRV_SEG;
	REG(edi) = PKTDRV_param;
	return 1;

    case F_SET_RCV_MODE:
	if (hdlp == NULL || !hdlp->in_use) {
	    HI(dx) = E_BAD_HANDLE;
	    break;
	}
	if (LWORD(ecx) != receive_mode && LWORD(ecx) != 1) {
	    HI(dx) = E_BAD_MODE;
	    break;
	}
	local_receive_mode = LWORD(ecx);
	return 1;

    case F_GET_RCV_MODE:
	if (hdlp == NULL || !hdlp->in_use) {
	    HI(dx) = E_BAD_HANDLE;
	    break;
	}
	REG(eax) = local_receive_mode;
	return 1;

    case F_GET_STATS:
	if (hdlp == NULL || !hdlp->in_use) {
	    HI(dx) = E_BAD_HANDLE;
	    break;
	}
	SREG(ds) = PKTDRV_SEG;
	REG(esi) = PKTDRV_stats;
	return 1;

    default:
	/* unhandled function, indicate an error */

	HI(dx) = E_BAD_COMMAND;
	break;
    }

    /* fell through switch, indicate an error (DH set above) */
    CARRY;

    pd_printf("PD ERR:  AX=%04x BX=%04x CX=%04x DX=%04x FLAGS=%08x\n",
		LWORD(eax),LWORD(ebx),LWORD(ecx),LWORD(edx),REG(eflags));
    pd_printf("      SI=%04x DI=%04x BP=%04x SP=%04x CS=%04x DS=%04x ES=%04x SS=%04x\n",
		LWORD(esi),LWORD(edi),LWORD(ebp),LWORD(esp),
		SREG(cs),SREG(ds),SREG(es),SREG(ss));

    return 1;
}

static void pkt_hlt(Bit16u idx, HLT_ARG(arg))
{
    fake_iret();
    pkt_int();
}

static void pkt_receive_req_async(int fd, void *arg)
{
    virq_raise(VIRQ_PKT);
}

static void pkt_register_net_fd_and_mode(int fd, int mode)
{
    pkt_fd = fd;
    add_to_io_select_masked(pkt_fd, pkt_receive_req_async, NULL);
    receive_mode = mode;
    local_receive_mode = mode;
    pd_printf("PKT: detected receive mode %i\n", mode);
}

/* register a new packet type */
static int
Insert_Type(int handle, int pkt_type_len, Bit8u *pkt_type)
{
    int i, nchars;
    if(pkt_type_len > MAX_PKT_TYPE_SIZE) return -1;
    if (max_pkt_type_array >= MAX_HANDLE) return -1;

    pd_printf("Trying to insert: handle %d, pkt_type_len=%d\n ",
	      handle, pkt_type_len);

    /* Find if the type or handle is already present */
    for(i=0; i < max_pkt_type_array; i++) {
	nchars = pkt_type_array[i].pkt_type_len;
	if ((pkt_type_array[i].handle == handle) ||
	    ((nchars > 1)
	     && !memcmp(&pkt_type_array[i].pkt_type, pkt_type, nchars)))
	    return  -1;
    }
    pkt_type_array[max_pkt_type_array].pkt_type_len = pkt_type_len;
    pkt_type_array[max_pkt_type_array].handle = handle;
    pkt_type_array[max_pkt_type_array].count = 0;
    memcpy(&pkt_type_array[max_pkt_type_array].pkt_type,
	   pkt_type, pkt_type_len);
    for(i=0; i<pkt_type_len; i++)
	pd_printf(" %.2x", pkt_type_array[max_pkt_type_array].pkt_type[i]);
    pd_printf("\n");
    pd_printf("Succeeded: inserted at %d\n", max_pkt_type_array);
    max_pkt_type_array++;
    return 0;
}

static int
Remove_Type(int handle)
{
    int i, shift_up;
    shift_up=0;
    for( i=0; i<max_pkt_type_array; i++) {
	if( shift_up )
            pkt_type_array[i-1]=pkt_type_array[i];
	else if (pkt_type_array[i].handle == handle)
	    shift_up=1;
    }
    if (shift_up)
	max_pkt_type_array--;
    return 0;
}

static enum VirqSwRet pkt_receiver_callback(void *arg)
{
    if (p_helper_size) {
        coopth_start(PKTRcvCall_TID, NULL);
        return VIRQ_SWRET_BH;
    }
    return VIRQ_SWRET_DONE;
}

static void pkt_receiver_callback_thr(void *arg)
{
    struct vm86_regs rcv_saved_regs;
    rcv_saved_regs = REGS;
    _AX = 0;
    _BX = p_helper_handle;
    _CX = p_helper_size;
    _DX = 0;	// no lookahead buffer
    _DI = 0;	// no error
    do_call_back(p_helper_receiver_cs, p_helper_receiver_ip);
    if ((_ES == 0 && _DI == 0) || (_CX && _CX < p_helper_size))
      goto out;
    MEMCPY_2DOS(SEGOFF2LINEAR(_ES, _DI), pkt_buf, p_helper_size);
    _DS = _ES;
    _SI = _DI;
    _AX = 1;
    _BX = p_helper_handle;
    _CX = p_helper_size;
    do_call_back(p_helper_receiver_cs, p_helper_receiver_ip);

out:
    p_helper_size = 0;
    REGS = rcv_saved_regs;
}

static int pkt_receive(void)
{
    int size, handle;
    struct per_handle *hdlp;

    if (!config.pktdrv) {
        pd_printf("Driver not initialized ...\n");
	return 0;
    }
    if (local_receive_mode == 1)
	return 0;

    size = pkt_read(pkt_fd, pkt_buf, PKT_BUF_SIZE);
    if (size < 0) {
        p_stats->errors_in++;		/* select() somehow lied */
        return 0;
    }
    if (size == 0)
	return 0;

    pd_printf("========Processing New packet======\n");
    handle = Find_Handle(pkt_buf);
    if (handle == -1)
        return 0;
    pd_printf("Found handle %d\n", handle);

    hdlp = &pg.handle[handle];
    if (hdlp->in_use) {
	    /* No need to hack the incoming packets it seems. */
#if 0
	    if (pg.flags & FLAG_NOVELL)	{ /* Novell hack? */
		char *p;
		/* check if the packet's type matches the specified type */
		/* in the ACCESS_TYPE call.  the position depends on the */
		/* driver class! */

		if (hdlp->cls == ETHER_CLASS)
		    p = pkt_buf + 2 * ETH_ALEN;		/* Ethernet-II */
		else
		    p = pkt_buf + 2 * ETH_ALEN + 2;	/* IEEE 802.3 */

		*--p = (char)ETH_P_IPX; /* overwrite length with type */
		*--p = (char)(ETH_P_IPX >> 8);
	    }
#endif

	    /* The TAP driver can send ethernet frames shorter than 60 bytes
	     * that may be dropped for the DOS
	     * stack implementation. This will fix the packet size
	     * adding a trailer at the end. by almauri@cs.com.uy
	     */
	    if (size < ETH_ZLEN) {
		pd_printf("Fixing packet padding. Actual length: %d\n", size);
		memset(pkt_buf + size, 0, ETH_ZLEN - size);
		size = ETH_ZLEN;
	    }

	    p_stats->packets_in++;
	    p_stats->bytes_in += size;

	    printbuf("received packet:", (struct ethhdr *)pkt_buf);
	    /* stuff things in global vars and queue a hardware */
	    /* interrupt which will perform the upcall */
	    if (p_helper_size)
		error("PKT: Receiver is not ready, packet dropped (size=%i)\n",
		  p_helper_size);
	    p_helper_size = size;
	    p_helper_receiver_cs = hdlp->rcvr_cs;
	    p_helper_receiver_ip = hdlp->rcvr_ip;
	    p_helper_handle = handle;
	    pd_printf("Called the helpvector ... \n");
	    return 1;
    } else {
        p_stats->packets_lost++;	/* not really lost... */
        pd_printf("Handle not in use, ignored this packet\n");
        return 0;
    }
    return 0;
}

static enum VirqHwRet pkt_virq_receive(void *arg)
{
    int rc = pkt_receive();
    if (rc)
        return VIRQ_HWRET_CONT;
    ioselect_complete(pkt_fd);
    return VIRQ_HWRET_DONE;
}

/*  Find_Handle does type demultiplexing.
    Given a packet, it compares the type fields and finds out which
    handle should take the given packet.
    An array of types is maintained.
*/
int
Find_Handle(u_char *buf)
{
    int i, nchars;
    struct ethhdr *eth = (struct ethhdr *) buf;
    u_char *p;

    /* find this packet's frame type, and hence position to compare the type */
    if (ntohs(eth->h_proto) >= 1536)
	p = buf + 2 * ETH_ALEN;		/* Ethernet-II */
    else
	p = buf + 2 * ETH_ALEN + 2;     /* All the rest frame types. */
    pd_printf("Received packet type: 0x%x\n", ntohs(eth->h_proto));

    for(i=0; i<max_pkt_type_array; i++) {
	nchars = pkt_type_array[i].pkt_type_len;
	if ( /* nchars < 2 || */
	     !memcmp(&pkt_type_array[i].pkt_type, p, nchars) )
	    return pkt_type_array[i].handle;
    }
    return -1;
}

static void printbuf(const char *mesg, struct ethhdr *buf)
{
  int i;
  u_char *p;

  pd_printf("%s :\n Dest.=", mesg);
  for (i = 0; i < 6; i++)
    pd_printf("%x:", buf->h_dest[i]);
  pd_printf(" Source=");
  for (i = 0; i < 6; i++)
    pd_printf("%x:", buf->h_source[i]);
  if (ntohs(buf->h_proto) >= 1536) {
    p = (u_char *)buf + 2 * ETH_ALEN; /* Ethernet-II */
    pd_printf(" Ethernet-II;");
  } else {
    p = (u_char *)buf + 2 * ETH_ALEN + 2; /* All the rest frame types. */
    pd_printf(" 802.3;");
  }
  pd_printf(" Type= 0x%x \n", ntohs(*(u_short *)p));
}
