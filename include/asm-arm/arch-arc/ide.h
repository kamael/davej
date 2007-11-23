/*
 * linux/include/asm-arm/arch-arc/ide.h
 *
 * Copyright (c) 1997,1998 Russell King
 */

static __inline__ int
ide_default_irq(ide_ioreg_t base)
{
	return 0;
}

static __inline__ ide_ioreg_t
ide_default_io_base(int index)
{
	return 0;
}

static __inline__ int
ide_default_stepping(int index)
{
	return 0;
}

static __inline__ void
ide_init_hwif_ports (ide_ioreg_t *p, ide_ioreg_t base, int stepping, int *irq)
{
	ide_ioreg_t port = base;
	ide_ioreg_t ctrl = base + 0x206;
	int i;

	i = 8;
	while (i--) {
		*p++ = port;
		port += 1 << stepping;
	}
	*p++ = ctrl;
	if (irq != NULL)
		irq = 0;
}