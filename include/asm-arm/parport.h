/*
 * parport.h: ARM-specific parport initialisation
 *
 * Copyright (C) 1999, 2000  Tim Waugh <tim@cyberelk.demon.co.uk>
 *
 * This file should only be included by drivers/parport/parport_pc.c.
 */

#ifndef __ASMARM_PARPORT_H
#define __ASMARM_PARPORT_H

#include <linux/config.h>

/* Maximum number of ports to support.  It is useless to set this greater
   than PARPORT_MAX (in <linux/parport.h>).  */
#define PARPORT_PC_MAX_PORTS  8

/* If parport_cs (PCMCIA) is managing ports for us, we'll need the
 * probing routines forever; otherwise we can lose them at boot time. */
#ifdef CONFIG_PARPORT_PC_PCMCIA
#define __maybe_initdata
#define __maybe_init
#else
#define __maybe_initdata __initdata
#define __maybe_init __init
#endif

static int user_specified __maybe_initdata = 0;


int __init
parport_pc_init(int *io, int *io_hi, int *irq, int *dma)
{
	int count = 0, i = 0;

#ifndef MODULE
	detect_and_report_winbond();
	detect_and_report_smsc();

	count += parport_pc_init_superio ();
#endif

	if (io && *io) {
		/* Only probe the ports we were given. */
		user_specified = 1;
		do {
			if (!*io_hi) *io_hi = 0x400 + *io;
			if (parport_pc_probe_port(*(io++), *(io_hi++),
						  *(irq++), *(dma++), NULL))
				count++;
		} while (*io && (++i < PARPORT_PC_MAX_PORTS));
	} else {
		/* Probe all the likely ports. */
		if (parport_pc_probe_port(0x3bc, 0x7bc, irq[0], dma[0], NULL))
			count++;
		if (parport_pc_probe_port(0x378, 0x778, irq[0], dma[0], NULL))
			count++;
		if (parport_pc_probe_port(0x278, 0x678, irq[0], dma[0], NULL))
			count++;
	}

        return count;
}

#endif /* !(_ASMARM_PARPORT_H) */
