/*
 *	X.25 Packet Layer release 001
 *
 *	This is ALPHA test software. This code may break your machine, randomly fail to work with new 
 *	releases, misbehave and/or generally screw up. It might even work. 
 *
 *	This code REQUIRES 2.1.15 or higher
 *
 *	This module:
 *		This module is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 *	History
 *	X.25 001	Jonathan Naylor	Started coding.
 */

#include <linux/config.h>
#if defined(CONFIG_X25) || defined(CONFIG_X25_MODULE)
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/socket.h>
#include <linux/in.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/string.h>
#include <linux/sockios.h>
#include <linux/net.h>
#include <linux/inet.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <asm/segment.h>
#include <asm/system.h>
#include <linux/fcntl.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <net/x25.h>

/*
 *	This routine purges all of the queues of frames.
 */
void x25_clear_queues(struct sock *sk)
{
	struct sk_buff *skb;

	while ((skb = skb_dequeue(&sk->write_queue)) != NULL)
		kfree_skb(skb, FREE_WRITE);

	while ((skb = skb_dequeue(&sk->protinfo.x25->interrupt_in_queue)) != NULL)
		kfree_skb(skb, FREE_READ);

	while ((skb = skb_dequeue(&sk->protinfo.x25->interrupt_out_queue)) != NULL)
		kfree_skb(skb, FREE_WRITE);

	while ((skb = skb_dequeue(&sk->protinfo.x25->fragment_queue)) != NULL)
		kfree_skb(skb, FREE_READ);
}

/*
 *	Validate that the value of nr is between va and vs. Return true or
 *	false for testing.
 */
int x25_validate_nr(struct sock *sk, unsigned short nr)
{
	unsigned short vc = sk->protinfo.x25->va;
	int modulus;

	modulus = (sk->protinfo.x25->neighbour->extended) ? X25_EMODULUS : X25_SMODULUS;

	while (vc != sk->protinfo.x25->vs) {
		if (nr == vc) return 1;
		vc = (vc + 1) % modulus;
	}

	if (nr == sk->protinfo.x25->vs) return 1;

	return 0;
}

/* 
 *  This routine is called when the packet layer internally generates a
 *  control frame.
 */
void x25_write_internal(struct sock *sk, int frametype)
{
	struct sk_buff *skb;
	unsigned char  *dptr;
	unsigned char  facilities[X25_MAX_FAC_LEN];
	unsigned char  addresses[1 + X25_ADDR_LEN];
	unsigned char  lci1, lci2;
	int len;

	/*
	 *	Default safe frame size.
	 */
	len = X25_MAX_L2_LEN + X25_EXT_MIN_LEN;

	/*
	 *	Adjust frame size.
	 */
	switch (frametype) {
		case X25_CALL_REQUEST:
			len += 1 + X25_ADDR_LEN + X25_MAX_FAC_LEN + X25_MAX_CUD_LEN;
			break;
		case X25_CALL_ACCEPTED:
			len += 1 + X25_MAX_FAC_LEN + X25_MAX_CUD_LEN;
			break;
		case X25_CLEAR_REQUEST:
		case X25_RESET_REQUEST:
			len += 2;
			break;
		case X25_RR:
		case X25_RNR:
		case X25_REJ:
		case X25_CLEAR_CONFIRMATION:
		case X25_INTERRUPT_CONFIRMATION:
		case X25_RESET_CONFIRMATION:
			break;
		default:
			printk(KERN_ERR "X.25: invalid frame type %02X\n", frametype);
			return;
	}

	if ((skb = alloc_skb(len, GFP_ATOMIC)) == NULL)
		return;

	/*
	 *	Space for Ethernet and 802.2 LLC headers.
	 */
	skb_reserve(skb, X25_MAX_L2_LEN);

	/*
	 *	Make space for the GFI and LCI, and fill them in.
	 */
	dptr = skb_put(skb, 2);

	lci1 = (sk->protinfo.x25->lci >> 8) & 0x0F;
	lci2 = (sk->protinfo.x25->lci >> 0) & 0xFF;

	if (sk->protinfo.x25->neighbour->extended) {
		*dptr++ = lci1 | X25_GFI_EXTSEQ;
		*dptr++ = lci2;
	} else {
		*dptr++ = lci1 | X25_GFI_STDSEQ;
		*dptr++ = lci2;
	}

	/*
	 *	Now fill in the frame type specific information.
	 */
	switch (frametype) {

		case X25_CALL_REQUEST:
			dptr    = skb_put(skb, 1);
			*dptr++ = X25_CALL_REQUEST;
			len     = x25_addr_aton(addresses, &sk->protinfo.x25->dest_addr, &sk->protinfo.x25->source_addr);
			dptr    = skb_put(skb, len);
			memcpy(dptr, addresses, len);
			len     = x25_create_facilities(facilities, &sk->protinfo.x25->facilities);
			dptr    = skb_put(skb, len);
			memcpy(dptr, facilities, len);
			dptr = skb_put(skb, sk->protinfo.x25->calluserdata.cudlength);
			memcpy(dptr, sk->protinfo.x25->calluserdata.cuddata, sk->protinfo.x25->calluserdata.cudlength);
			sk->protinfo.x25->calluserdata.cudlength = 0;
			break;

		case X25_CALL_ACCEPTED:
			dptr    = skb_put(skb, 2);
			*dptr++ = X25_CALL_ACCEPTED;
			*dptr++ = 0x00;		/* Address lengths */
			len     = x25_create_facilities(facilities, &sk->protinfo.x25->facilities);
			dptr    = skb_put(skb, len);
			memcpy(dptr, facilities, len);
			dptr = skb_put(skb, sk->protinfo.x25->calluserdata.cudlength);
			memcpy(dptr, sk->protinfo.x25->calluserdata.cuddata, sk->protinfo.x25->calluserdata.cudlength);
			sk->protinfo.x25->calluserdata.cudlength = 0;
			break;

		case X25_CLEAR_REQUEST:
		case X25_RESET_REQUEST:
			dptr    = skb_put(skb, 3);
			*dptr++ = frametype;
			*dptr++ = 0x00;		/* XXX */
			*dptr++ = 0x00;		/* XXX */
			break;

		case X25_RR:
		case X25_RNR:
		case X25_REJ:
			if (sk->protinfo.x25->neighbour->extended) {
				dptr     = skb_put(skb, 2);
				*dptr++  = frametype;
				*dptr++  = (sk->protinfo.x25->vr << 1) & 0xFE;
			} else {
				dptr     = skb_put(skb, 1);
				*dptr    = frametype;
				*dptr++ |= (sk->protinfo.x25->vr << 5) & 0xE0;
			}
			break;

		case X25_CLEAR_CONFIRMATION:
		case X25_INTERRUPT_CONFIRMATION:
		case X25_RESET_CONFIRMATION:
			dptr  = skb_put(skb, 1);
			*dptr = frametype;
			break;
	}

	x25_transmit_link(skb, sk->protinfo.x25->neighbour);
}

/*
 *	Unpick the contents of the passed X.25 Packet Layer frame.
 */
int x25_decode(struct sock *sk, struct sk_buff *skb, int *ns, int *nr, int *q, int *d, int *m)
{
	unsigned char *frame;

	frame = skb->data;

	*ns = *nr = *q = *d = *m = 0;

	switch (frame[2]) {
		case X25_CALL_REQUEST:
		case X25_CALL_ACCEPTED:
		case X25_CLEAR_REQUEST:
		case X25_CLEAR_CONFIRMATION:
		case X25_INTERRUPT:
		case X25_INTERRUPT_CONFIRMATION:
		case X25_RESET_REQUEST:
		case X25_RESET_CONFIRMATION:
		case X25_RESTART_REQUEST:
		case X25_RESTART_CONFIRMATION:
		case X25_REGISTRATION_REQUEST:
		case X25_REGISTRATION_CONFIRMATION:
		case X25_DIAGNOSTIC:
			return frame[2];
	}

	if (sk->protinfo.x25->neighbour->extended) {
		if (frame[2] == X25_RR  ||
		    frame[2] == X25_RNR ||
		    frame[2] == X25_REJ) {
			*nr = (frame[3] >> 1) & 0x7F;
			return frame[2];
		}
	} else {
		if ((frame[2] & 0x1F) == X25_RR  ||
		    (frame[2] & 0x1F) == X25_RNR ||
		    (frame[2] & 0x1F) == X25_REJ) {
			*nr = (frame[2] >> 5) & 0x07;
			return frame[2] & 0x1F;
		}
	}

	if (sk->protinfo.x25->neighbour->extended) {
		if ((frame[2] & 0x01) == X25_DATA) {
			*q  = (frame[0] & X25_Q_BIT) == X25_Q_BIT;
			*d  = (frame[0] & X25_D_BIT) == X25_D_BIT;
			*m  = (frame[3] & X25_EXT_M_BIT) == X25_EXT_M_BIT;
			*nr = (frame[3] >> 1) & 0x7F;
			*ns = (frame[2] >> 1) & 0x7F;
			return X25_DATA;
		}
	} else {
		if ((frame[2] & 0x01) == X25_DATA) {
			*q  = (frame[0] & X25_Q_BIT) == X25_Q_BIT;
			*d  = (frame[0] & X25_D_BIT) == X25_D_BIT;
			*m  = (frame[2] & X25_STD_M_BIT) == X25_STD_M_BIT;
			*nr = (frame[2] >> 5) & 0x07;
			*ns = (frame[2] >> 1) & 0x07;
			return X25_DATA;
		}
	}

	printk(KERN_DEBUG "X.25: invalid PLP frame %02X %02X %02X\n", frame[0], frame[1], frame[2]);

	return X25_ILLEGAL;
}

/*
 *	Parse a set of facilities into the facilities structure. Unrecognised
 *	facilities are written to the debug log file.
 */
int x25_parse_facilities(struct sk_buff *skb, struct x25_facilities *facilities)
{
	unsigned int len;
	unsigned char *p = skb->data;

	memset(facilities, 0x00, sizeof(struct x25_facilities));

	len = *p++;

	while (len > 0) {
		switch (*p & X25_FAC_CLASS_MASK) {
			case X25_FAC_CLASS_A:
				switch (*p) {
					case X25_FAC_REVERSE:
						facilities->reverse = (p[1] & 0x01);
						break;
					case X25_FAC_THROUGHPUT:
						facilities->throughput = p[1];
						break;
					default:
						printk(KERN_DEBUG "X.25: unknown facility %02X, value %02X\n", p[0], p[1]);
						break;
				}
				p   += 2;
				len -= 2;
				break;

			case X25_FAC_CLASS_B:
				switch (*p) {
					case X25_FAC_PACKET_SIZE:
						facilities->pacsize_in  = p[1];
						facilities->pacsize_out = p[2];
						break;
					case X25_FAC_WINDOW_SIZE:
						facilities->winsize_in  = p[1];
						facilities->winsize_out = p[2];
						break;
					default:
						printk(KERN_DEBUG "X.25: unknown facility %02X, values %02X, %02X\n", p[0], p[1], p[2]);
						break;
				}
				p   += 3;
				len -= 3;
				break;

			case X25_FAC_CLASS_C:
				printk(KERN_DEBUG "X.25: unknown facility %02X, values %02X, %02X, %02X\n", p[0], p[1], p[2], p[3]);
				p   += 4;
				len -= 4;
				break;

			case X25_FAC_CLASS_D:
				printk(KERN_DEBUG "X.25: unknown facility %02X, length %d, values %02X, %02X, %02X, %02X\n", p[0], p[1], p[2], p[3], p[4], p[5]);
				p   += p[1] + 2;
				len -= p[1] + 2;
				break;
		}
	}

	return p - skb->data;
}

/*
 *	Create a set of facilities.
 */
int x25_create_facilities(unsigned char *buffer, struct x25_facilities *facilities)
{
	unsigned char *p = buffer + 1;
	int len;

	if (facilities->reverse != 0) {
		*p++ = X25_FAC_REVERSE;
		*p++ = (facilities->reverse) ? 0x01 : 0x00;
	}

	if (facilities->throughput != 0) {
		*p++ = X25_FAC_THROUGHPUT;
		*p++ = facilities->throughput;
	}

	if (facilities->pacsize_in != 0 || facilities->pacsize_out != 0) {
		*p++ = X25_FAC_PACKET_SIZE;
		*p++ = (facilities->pacsize_in  == 0) ? facilities->pacsize_out : facilities->pacsize_in;
		*p++ = (facilities->pacsize_out == 0) ? facilities->pacsize_in  : facilities->pacsize_out;
	}

	if (facilities->winsize_in != 0 || facilities->winsize_out != 0) {
		*p++ = X25_FAC_WINDOW_SIZE;
		*p++ = (facilities->winsize_in  == 0) ? facilities->winsize_out : facilities->winsize_in;
		*p++ = (facilities->winsize_out == 0) ? facilities->winsize_in  : facilities->winsize_out;
	}

	len       = p - buffer;
	buffer[0] = len - 1;

	return len;
}

#endif