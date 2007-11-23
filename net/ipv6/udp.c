/*
 *	UDP over IPv6
 *	Linux INET6 implementation 
 *
 *	Authors:
 *	Pedro Roque		<roque@di.fc.ul.pt>	
 *
 *	Based on linux/ipv4/udp.c
 *
 *	$Id: udp.c,v 1.8 1997/02/28 09:56:35 davem Exp $
 *
 *	This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#include <linux/errno.h>
#include <linux/types.h>
#include <linux/socket.h>
#include <linux/sockios.h>
#include <linux/sched.h>
#include <linux/net.h>
#include <linux/in6.h>
#include <linux/netdevice.h>
#include <linux/if_arp.h>
#include <linux/ipv6.h>
#include <linux/icmpv6.h>

#include <net/sock.h>
#include <net/snmp.h>

#include <net/ipv6.h>
#include <net/ndisc.h>
#include <net/protocol.h>
#include <net/transp_v6.h>
#include <net/ipv6_route.h>
#include <net/addrconf.h>
#include <net/ip.h>
#include <net/udp.h>

#include <net/checksum.h>

struct udp_mib udp_stats_in6;

/* Grrr, addr_type already calculated by caller, but I don't want
 * to add some silly "cookie" argument to this method just for that.
 */
static int udp_v6_verify_bind(struct sock *sk, unsigned short snum)
{
	struct sock *sk2;
	int addr_type = ipv6_addr_type(&sk->net_pinfo.af_inet6.rcv_saddr);
	int retval = 0, sk_reuse = sk->reuse;

	SOCKHASH_LOCK();
	for(sk2 = udp_hash[snum & (UDP_HTABLE_SIZE - 1)]; sk2 != NULL; sk2 = sk2->next) {
		if((sk2->num == snum) && (sk2 != sk)) {
			unsigned char state = sk2->state;
			int sk2_reuse = sk2->reuse;
			if(addr_type == IPV6_ADDR_ANY || (!sk2->rcv_saddr)) {
				if((!sk2_reuse)			||
				   (!sk_reuse)			||
				   (state != TCP_LISTEN)) {
					retval = 1;
					break;
				}
			} else if(!ipv6_addr_cmp(&sk->net_pinfo.af_inet6.rcv_saddr,
						 &sk2->net_pinfo.af_inet6.rcv_saddr)) {
				if((!sk_reuse)			||
				   (!sk2_reuse)			||
				   (state == TCP_LISTEN)) {
					retval = 1;
					break;
				}
			}
		}
	}
	SOCKHASH_UNLOCK();
	return retval;
}

static void udp_v6_hash(struct sock *sk)
{
	struct sock **skp;
	int num = sk->num;

	num &= (UDP_HTABLE_SIZE - 1);
	skp = &udp_hash[num];

	SOCKHASH_LOCK();
	sk->next = *skp;
	*skp = sk;
	sk->hashent = num;
	SOCKHASH_UNLOCK();
}

static void udp_v6_unhash(struct sock *sk)
{
	struct sock **skp;
	int num = sk->num;

	num &= (UDP_HTABLE_SIZE - 1);
	skp = &udp_hash[num];

	SOCKHASH_LOCK();
	while(*skp != NULL) {
		if(*skp == sk) {
			*skp = sk->next;
			break;
		}
		skp = &((*skp)->next);
	}
	SOCKHASH_UNLOCK();
}

static void udp_v6_rehash(struct sock *sk)
{
	struct sock **skp;
	int num = sk->num;
	int oldnum = sk->hashent;

	num &= (UDP_HTABLE_SIZE - 1);
	skp = &udp_hash[oldnum];

	SOCKHASH_LOCK();
	while(*skp != NULL) {
		if(*skp == sk) {
			*skp = sk->next;
			break;
		}
		skp = &((*skp)->next);
	}
	sk->next = udp_hash[num];
	udp_hash[num] = sk;
	sk->hashent = num;
	SOCKHASH_UNLOCK();
}

static struct sock *udp_v6_lookup(struct in6_addr *saddr, u16 sport,
				  struct in6_addr *daddr, u16 dport)
{
	struct sock *sk, *result = NULL;
	unsigned short hnum = ntohs(dport);
	int badness = -1;

	for(sk = udp_hash[hnum & (UDP_HTABLE_SIZE - 1)]; sk != NULL; sk = sk->next) {
		if((sk->num == hnum)		&&
		   (sk->family == AF_INET6)	&&
		   !(sk->dead && (sk->state == TCP_CLOSE))) {
			struct ipv6_pinfo *np = &sk->net_pinfo.af_inet6;
			int score = 0;
			if(sk->dummy_th.dest) {
				if(sk->dummy_th.dest != sport)
					continue;
				score++;
			}
			if(!ipv6_addr_any(&np->rcv_saddr)) {
				if(ipv6_addr_cmp(&np->rcv_saddr, daddr))
					continue;
				score++;
			}
			if(!ipv6_addr_any(&np->daddr)) {
				if(ipv6_addr_cmp(&np->daddr, saddr))
					continue;
				score++;
			}
			if(score == 3) {
				result = sk;
				break;
			} else if(score > badness) {
				result = sk;
				badness = score;
			}
		}
	}
	return result;
}

/*
 *
 */

int udpv6_connect(struct sock *sk, struct sockaddr *uaddr, int addr_len)
{
	struct sockaddr_in6	*usin = (struct sockaddr_in6 *) uaddr;
	struct in6_addr		*daddr;
	struct dest_entry	*dest;
	struct ipv6_pinfo      	*np;
	struct inet6_ifaddr	*ifa;
	int			addr_type;

	if (addr_len < sizeof(*usin)) 
	  	return(-EINVAL);

	if (usin->sin6_family && usin->sin6_family != AF_INET6) 
	  	return(-EAFNOSUPPORT);

	addr_type = ipv6_addr_type(&usin->sin6_addr);
	np = &sk->net_pinfo.af_inet6;

	if (addr_type == IPV6_ADDR_ANY)
	{
		/*
		 *	connect to self
		 */
		usin->sin6_addr.s6_addr[15] = 0x01;
	}

	daddr = &usin->sin6_addr;

	if (addr_type == IPV6_ADDR_MAPPED)
	{
		struct sockaddr_in sin;
		int err;

		sin.sin_family = AF_INET;
		sin.sin_addr.s_addr = daddr->s6_addr32[3];

		err = udp_connect(sk, (struct sockaddr*) &sin, sizeof(sin));
		
		if (err < 0)
		{
			return err;
		}
		
		ipv6_addr_copy(&np->daddr, daddr);
		
		if(ipv6_addr_any(&np->saddr))
		{
			ipv6_addr_set(&np->saddr, 0, 0, 
				      __constant_htonl(0x0000ffff),
				      sk->saddr);

		}

		if(ipv6_addr_any(&np->rcv_saddr))
		{
			ipv6_addr_set(&np->rcv_saddr, 0, 0, 
				      __constant_htonl(0x0000ffff),
				      sk->rcv_saddr);
		}

	}

	ipv6_addr_copy(&np->daddr, daddr);

	/*
	 *	Check for a route to destination an obtain the
	 *	destination cache for it.
	 */

	dest = ipv6_dst_route(daddr, NULL, sk->localroute ? RTI_GATEWAY : 0);

	np->dest = dest;

	if (dest == NULL)
		return -ENETUNREACH;

	/* get the source adddress used in the apropriate device */

	ifa = ipv6_get_saddr((struct rt6_info *) dest, daddr);

	if(ipv6_addr_any(&np->saddr))
	{
		ipv6_addr_copy(&np->saddr, &ifa->addr);
	}

	if(ipv6_addr_any(&np->rcv_saddr))
	{
		ipv6_addr_copy(&np->rcv_saddr, &ifa->addr);
		sk->rcv_saddr = 0xffffffff;
	}

	sk->dummy_th.dest = usin->sin6_port;

	sk->state = TCP_ESTABLISHED;

	return(0);
}

static void udpv6_close(struct sock *sk, unsigned long timeout)
{
	struct ipv6_pinfo *np = &sk->net_pinfo.af_inet6;

	lock_sock(sk);
	sk->state = TCP_CLOSE;

	if (np->dest)
	{
		ipv6_dst_unlock(np->dest);
	}

	release_sock(sk);
	destroy_sock(sk);
}

/*
 * 	This should be easy, if there is something there we
 * 	return it, otherwise we block.
 */

int udpv6_recvmsg(struct sock *sk, struct msghdr *msg, int len,
		  int noblock, int flags, int *addr_len)
{
  	int copied = 0;
  	int truesize;
  	struct sk_buff *skb;
  	int err;
  	

	/*
	 *	Check any passed addresses
	 */
	 
  	if (addr_len) 
  		*addr_len=sizeof(struct sockaddr_in6);
  
	/*
	 *	From here the generic datagram does a lot of the work. Come
	 *	the finished NET3, it will do _ALL_ the work!
	 */
	 	
	skb = skb_recv_datagram(sk, flags, noblock, &err);
	if(skb==NULL)
  		return err;
  
  	truesize = skb->tail - skb->h.raw - sizeof(struct udphdr);
  	
  	copied=truesize;
  	if(copied>len)
  	{
  		copied=len;
  		msg->msg_flags|=MSG_TRUNC;
  	}

  	/*
  	 *	FIXME : should use udp header size info value 
  	 */
  	 
	err = skb_copy_datagram_iovec(skb, sizeof(struct udphdr), 
				      msg->msg_iov, copied);
	if (err)
		return err; 
	
	sk->stamp=skb->stamp;

	/* Copy the address. */
	if (msg->msg_name) 
	{
		struct sockaddr_in6 *sin6;
	  
		sin6 = (struct sockaddr_in6 *) msg->msg_name;
		
		sin6->sin6_family = AF_INET6;
		sin6->sin6_port = skb->h.uh->source;

		if (skb->protocol == __constant_htons(ETH_P_IP))
		{
			ipv6_addr_set(&sin6->sin6_addr, 0, 0,
				      __constant_htonl(0xffff), skb->nh.iph->daddr);
		}
		else
		{
			memcpy(&sin6->sin6_addr, &skb->nh.ipv6h->saddr,
			       sizeof(struct in6_addr));

			if (msg->msg_controllen)
				datagram_recv_ctl(sk, msg, skb);
		}
  	}
	
  	skb_free_datagram(sk, skb);
  	return(copied);
}

void udpv6_err(int type, int code, unsigned char *buff, __u32 info,
	       struct in6_addr *saddr, struct in6_addr *daddr,
	       struct inet6_protocol *protocol)
{
	struct sock *sk;
	struct udphdr *uh;
	int err;
	
	uh = (struct udphdr *) buff;

	sk = udp_v6_lookup(saddr, uh->source, daddr, uh->dest);
   
	if (sk == NULL)
	{
		printk(KERN_DEBUG "icmp for unkown sock\n");
		return;
	}

	if (icmpv6_err_convert(type, code, &err))
	{
		if(sk->bsdism && sk->state!=TCP_ESTABLISHED)
			return;
		
		sk->err = err;
		sk->error_report(sk);
	}
	else
		sk->err_soft = err;
}

static inline int udpv6_queue_rcv_skb(struct sock * sk, struct sk_buff *skb)
{

	if (sock_queue_rcv_skb(sk,skb)<0) {
		udp_stats_in6.UdpInErrors++;
		ipv6_statistics.Ip6InDiscards++;
		ipv6_statistics.Ip6InDelivers--;
		skb->sk = NULL;
		kfree_skb(skb, FREE_WRITE);
		return 0;
	}
	udp_stats_in6.UdpInDatagrams++;
	return 0;
}

static int __inline__ inet6_mc_check(struct sock *sk, struct in6_addr *addr)
{
	struct ipv6_mc_socklist *mc;
		
	for (mc = sk->net_pinfo.af_inet6.ipv6_mc_list; mc; mc=mc->next) {
		if (ipv6_addr_cmp(&mc->addr, addr) == 0)
			return 1;
	}

	return 0;
}

static struct sock *udp_v6_mcast_next(struct sock *sk,
				      u16 loc_port, struct in6_addr *loc_addr,
				      u16 rmt_port, struct in6_addr *rmt_addr)
{
	struct sock *s = sk;
	unsigned short num = ntohs(loc_port);
	for(; s; s = s->next) {
		if((s->num == num)		&&
		   !(s->dead && (s->state == TCP_CLOSE))) {
			struct ipv6_pinfo *np = &s->net_pinfo.af_inet6;
			if(s->dummy_th.dest) {
				if(s->dummy_th.dest != rmt_port)
					continue;
			}
			if(!ipv6_addr_any(&np->daddr) &&
			   ipv6_addr_cmp(&np->daddr, rmt_addr))
				continue;

			if(!ipv6_addr_any(&np->rcv_saddr)) {
				if(ipv6_addr_cmp(&np->rcv_saddr, loc_addr) == 0)
					return s;
			}
			if(!inet6_mc_check(s, loc_addr))
				continue;
			return s;
		}
	}
	return NULL;
}

static void udpv6_mcast_deliver(struct udphdr *uh,
				struct in6_addr *saddr, struct in6_addr *daddr,
				struct sk_buff *skb)
{
	struct sock *sk, *sk2;

	sk = udp_hash[ntohs(uh->dest) & (UDP_HTABLE_SIZE - 1)];
	sk = udp_v6_mcast_next(sk, uh->dest, daddr, uh->source, saddr);
	if(sk) {
		sk2 = sk;
		while((sk2 = udp_v6_mcast_next(sk2->next,
					       uh->dest, saddr,
					       uh->source, daddr))) {
			struct sk_buff *buff = skb_clone(skb, GFP_ATOMIC);
			if(sock_queue_rcv_skb(sk, buff) < 0) {
				buff->sk = NULL;
				kfree_skb(buff, FREE_READ);
			}
		}
	}
	if(!sk || sock_queue_rcv_skb(sk, skb) < 0) {
		skb->sk = NULL;
		kfree_skb(skb, FREE_READ);
	}
}

int udpv6_rcv(struct sk_buff *skb, struct device *dev,
	      struct in6_addr *saddr, struct in6_addr *daddr,
	      struct ipv6_options *opt, unsigned short len,
	      int redo, struct inet6_protocol *protocol)
{
	struct sock *sk;
  	struct udphdr *uh;
	int ulen;

	/*
	 *	check if the address is ours...
	 *	I believe that this is being done in IP layer
	 */

	uh = (struct udphdr *) skb->h.uh;
  	
  	ipv6_statistics.Ip6InDelivers++;

	ulen = ntohs(uh->len);
	
	if (ulen > len || len < sizeof(*uh))
	{
		printk(KERN_DEBUG "UDP: short packet: %d/%d\n", ulen, len);
		udp_stats_in6.UdpInErrors++;
		kfree_skb(skb, FREE_READ);
		return(0);
	}

	if (uh->check == 0)
	{
		printk(KERN_DEBUG "IPv6: udp checksum is 0\n");
		goto discard;
	}

	switch (skb->ip_summed) {
	case CHECKSUM_NONE:
		skb->csum = csum_partial((char*)uh, len, 0);
	case CHECKSUM_HW:
		if (csum_ipv6_magic(saddr, daddr, len, IPPROTO_UDP, skb->csum))
		{
			printk(KERN_DEBUG "IPv6: udp checksum error\n");
			goto discard;
		}
	}
	
	len = ulen;

	/* 
	 *	Multicast receive code 
	 */
	if (ipv6_addr_type(daddr) & IPV6_ADDR_MULTICAST) {
		udpv6_mcast_deliver(uh, saddr, daddr, skb);
		return 0;
	}

	/* Unicast */
	
	/* 
	 * check socket cache ... must talk to Alan about his plans
	 * for sock caches... i'll skip this for now.
	 */

	sk = udp_v6_lookup(saddr, uh->source, daddr, uh->dest);

	if (sk == NULL)
	{
		udp_stats_in6.UdpNoPorts++;

		icmpv6_send(skb, ICMPV6_DEST_UNREACH, ICMPV6_PORT_UNREACH,
			    0, dev);
		
		kfree_skb(skb, FREE_READ);
		return(0);
	}

	/* deliver */

	if (sk->users)
	{
		__skb_queue_tail(&sk->back_log, skb);
	}
	else
	{
		udpv6_queue_rcv_skb(sk, skb);
	}
	
	return(0);

  discard:
	udp_stats_in6.UdpInErrors++;
	kfree_skb(skb, FREE_READ);
	return(0);	
}

/*
 *	Sending
 */

struct udpv6fakehdr 
{
	struct udphdr	uh;
	struct iovec	*iov;
	__u32		wcheck;
	__u32		pl_len;
	struct in6_addr *daddr;
};

/*
 *	with checksum
 */

static int udpv6_getfrag(const void *data, struct in6_addr *addr,
			 char *buff, unsigned int offset, unsigned int len)
{
	struct udpv6fakehdr *udh = (struct udpv6fakehdr *) data;
	char *dst;
	int final = 0;
	int clen = len;

	dst = buff;

	if (offset)
	{
		offset -= sizeof(struct udphdr);
	}
	else
	{
		dst += sizeof(struct udphdr);
		final = 1;
		clen -= sizeof(struct udphdr);
	}

	udh->wcheck = csum_partial_copy_fromiovecend(dst, udh->iov, offset,
						     clen, udh->wcheck);

	if (final)
	{
		struct in6_addr *daddr;
		
		udh->wcheck = csum_partial((char *)udh, sizeof(struct udphdr),
					   udh->wcheck);

		if (udh->daddr)
		{
			daddr = udh->daddr;
		}
		else
		{
			/*
			 *	use packet destination address
			 *	this should improve cache locality
			 */
			daddr = addr + 1;
		}
		udh->uh.check = csum_ipv6_magic(addr, daddr,
						udh->pl_len, IPPROTO_UDP,
						udh->wcheck);
		if (udh->uh.check == 0)
			udh->uh.check = -1;

		memcpy(buff, udh, sizeof(struct udphdr));
	}
	return 0;
}

static int udpv6_sendmsg(struct sock *sk, struct msghdr *msg, int ulen)
{
	
	struct ipv6_options opt_space;
	struct udpv6fakehdr udh;
	struct ipv6_pinfo *np = &sk->net_pinfo.af_inet6;
	struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *) msg->msg_name;
	struct ipv6_options *opt = NULL;
	struct device *dev = NULL;
	int addr_len = msg->msg_namelen;
	struct in6_addr *daddr;
	struct in6_addr *saddr = NULL;
	int len = ulen + sizeof(struct udphdr);
	int addr_type;
	int hlimit = 0;
	int err;

	
	if (msg->msg_flags & ~(MSG_DONTROUTE|MSG_DONTWAIT))
		return(-EINVAL);

	if (sin6)
	{
		if (addr_len < sizeof(*sin6))
			return(-EINVAL);
		
		if (sin6->sin6_family && sin6->sin6_family != AF_INET6)
			return(-EINVAL);

		if (sin6->sin6_port == 0)
			return(-EINVAL);
	       
		udh.uh.dest = sin6->sin6_port;
		daddr = &sin6->sin6_addr;

		if (np->dest && ipv6_addr_cmp(daddr, &np->daddr))
		{
			ipv6_dst_unlock(np->dest);
			np->dest = NULL;
		}
	}
	else
	{
		if (sk->state != TCP_ESTABLISHED)
			return(-EINVAL);
		
		udh.uh.dest = sk->dummy_th.dest;
		daddr = &sk->net_pinfo.af_inet6.daddr;
	}

	addr_type = ipv6_addr_type(daddr);

	if (addr_type == IPV6_ADDR_MAPPED)
	{
		struct sockaddr_in sin;
		
		sin.sin_family = AF_INET;
		sin.sin_addr.s_addr = daddr->s6_addr32[3];

		return udp_sendmsg(sk, msg, len);
	}

	udh.daddr = NULL;
	
	if (msg->msg_controllen)
	{
		opt = &opt_space;
		memset(opt, 0, sizeof(struct ipv6_options));

		err = datagram_send_ctl(msg, &dev, &saddr, opt, &hlimit);
		if (err < 0)
		{
			printk(KERN_DEBUG "invalid msg_control\n");
			return err;
		}
		
		if (opt->srcrt)
		{			
			udh.daddr = daddr;
		}
	}
	
	udh.uh.source = sk->dummy_th.source;
	udh.uh.len = htons(len);
	udh.uh.check = 0;
	udh.iov = msg->msg_iov;
	udh.wcheck = 0;
	udh.pl_len = len;
	
	err = ipv6_build_xmit(sk, udpv6_getfrag, &udh, daddr, len,
			      saddr, dev, opt, IPPROTO_UDP, hlimit,
			      msg->msg_flags&MSG_DONTWAIT);
	
	if (err < 0)
		return err;

	udp_stats_in6.UdpOutDatagrams++;
	return ulen;
}

static struct inet6_protocol udpv6_protocol = 
{
	udpv6_rcv,		/* UDP handler		*/
	udpv6_err,		/* UDP error control	*/
	NULL,			/* next			*/
	IPPROTO_UDP,		/* protocol ID		*/
	0,			/* copy			*/
	NULL,			/* data			*/
	"UDPv6"			/* name			*/
};


struct proto udpv6_prot = {
	(struct sock *)&udpv6_prot,	/* sklist_next */
	(struct sock *)&udpv6_prot,	/* sklist_prev */
	udpv6_close,			/* close */
	udpv6_connect,			/* connect */
	NULL,				/* accept */
	NULL,				/* retransmit */
	NULL,				/* write_wakeup */
	NULL,				/* read_wakeup */
	datagram_poll,			/* poll */
	udp_ioctl,			/* ioctl */
	NULL,				/* init */
	NULL,				/* destroy */
	NULL,				/* shutdown */
	ipv6_setsockopt,		/* setsockopt */
	ipv6_getsockopt,		/* getsockopt */
	udpv6_sendmsg,			/* sendmsg */
	udpv6_recvmsg,			/* recvmsg */
	NULL,				/* bind */
	udpv6_queue_rcv_skb,		/* backlog_rcv */
	udp_v6_hash,			/* hash */
	udp_v6_unhash,			/* unhash */
	udp_v6_rehash,			/* rehash */
	udp_good_socknum,		/* good_socknum */
	udp_v6_verify_bind,		/* verify_bind */
	128,				/* max_header */
	0,				/* retransmits */
	"UDP",				/* name */
	0,				/* inuse */
	0				/* highestinuse */
};

void udpv6_init(void)
{
	inet6_add_protocol(&udpv6_protocol);
}