/* $Id: vaddrs.h,v 1.8 1997/06/27 14:55:13 jj Exp $ */
#ifndef _SPARC64_VADDRS_H
#define _SPARC64_VADDRS_H

/* asm-sparc64/vaddrs.h:  Here will be define the virtual addresses at
 *                      which important I/O addresses will be mapped.
 *                      For instance the timer register virtual address
 *                      is defined here.
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 */

/* I can see only one reason why we should have statically defined
 * mappings for devices and is the speedup improvements of not loading
 * a pointer and then the value in the assembly code
 */
#define  IOBASE_VADDR   0x0000006000000000ULL  /* Base for mapping pages */
#define  IOBASE_LEN     0x0000001000000000ULL  /* Length of the IO area */
#define  IOBASE_END     0x0000007000000000ULL
#define  DVMA_VADDR     0x0000007000000000ULL  /* Base area of the DVMA on suns */
#define  DVMA_LEN       0x0000001000000000ULL  /* Size of the DVMA address space */
#define  DVMA_END       0x0000008000000000ULL
#define  MODULES_VADDR	0x0000000001000000ULL  /* Where to map modules */
#define  MODULES_LEN	0x000000007f000000ULL
#define  MODULES_END	0x0000000080000000ULL

#endif /* !(_SPARC_VADDRS_H) */
