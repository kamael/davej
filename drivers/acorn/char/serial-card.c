/*
 * linux/arch/arm/drivers/char/serial-module.c
 *
 * Copyright (c) 1996 Russell King.
 *
 * A generic handler of serial expansion cards that use 16550s or
 * the like.
 *
 * Definitions:
 *  MY_PRODS		Product numbers to identify this card by
 *  MY_MANUS		Manufacturer numbers to identify this card by
 *  MY_NUMPORTS		Number of ports per card
 *  MY_BAUD_BASE	Baud base for the card
 *  MY_INIT		Initialisation routine name
 *  MY_BASE_ADDRESS(ec)	Return base address for ports
 *  MY_PORT_ADDRESS
 *	(port,cardaddr)	Return address for port using base address
 *			from above.
 *
 * Changelog:
 *  30-07-1996	RMK	Created
 *  22-04-1998	RMK	Removed old register_pre_init_serial
 */
#include <linux/module.h>
#include <linux/serial.h>
#include <linux/errno.h>
#include <asm/ecard.h>

#ifndef NUM_SERIALS
#define NUM_SERIALS	MY_NUMPORTS * MAX_ECARDS
#endif

#ifdef MODULE
static int __serial_ports[NUM_SERIALS];
static int __serial_pcount;
static struct expansion_card *expcard[MAX_ECARDS];
#define ADD_ECARD(ec,card) expcard[(card)] = (ec)
#define ADD_PORT(port) __serial_ports[__serial_pcount++] = (port)
#undef MY_INIT
#define MY_INIT init_module
#else
#define ADD_ECARD(ec,card)
#define ADD_PORT(port)
#endif

static const card_ids serial_cids[] = { MY_CARD_LIST, { 0xffff, 0xffff } };

static inline int serial_register_onedev (unsigned long port, int irq)
{
    struct serial_struct req;

    req.baud_base = MY_BAUD_BASE;
    req.irq = irq;
    req.port = port;

    return register_serial(&req);
}

int MY_INIT (void)
{
    int card = 0;

    ecard_startfind ();

    do {
	struct expansion_card *ec;
	unsigned long cardaddr;
	int port;

	ec = ecard_find (0, serial_cids);
	if (!ec)
	    break;

	cardaddr = MY_BASE_ADDRESS(ec);

	for (port = 0; port < MY_NUMPORTS; port ++) {
	    int line;

	    line = serial_register_onedev (MY_PORT_ADDRESS(port, cardaddr), ec->irq);
	    if (line < 0)
		break;
	    ADD_PORT(line);
	}

	if (port) {
	    ecard_claim (ec);
	    ADD_ECARD(ec, card);
	} else
	    break;
    } while (++card < MAX_ECARDS);
    return card ? 0 : -ENODEV;
}

#ifdef MODULE
void cleanup_module (void)
{
    int i;

    for (i = 0; i < __serial_pcount; i++)
	unregister_serial (__serial_ports[i]);

    for (i = 0; i < MAX_ECARDS; i++)
	if (expcard[i])
	    ecard_release (expcard[i]);
}
#endif