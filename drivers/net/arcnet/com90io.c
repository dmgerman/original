/*
 * Linux ARCnet driver - COM90xx chipset (IO-mapped buffers)
 * 
 * Written 1997 by David Woodhouse.
 * Written 1994-1999 by Avery Pennarun.
 * Written 1999-2000 by Martin Mares <mj@suse.cz>.
 * Derived from skeleton.c by Donald Becker.
 *
 * Special thanks to Contemporary Controls, Inc. (www.ccontrols.com)
 *  for sponsoring the further development of this driver.
 *
 * **********************
 *
 * The original copyright of skeleton.c was as follows:
 *
 * skeleton.c Written 1993 by Donald Becker.
 * Copyright 1993 United States Government as represented by the
 * Director, National Security Agency.  This software may only be used
 * and distributed according to the terms of the GNU Public License as
 * modified by SRC, incorporated herein by reference.
 *
 * **********************
 *
 * For more details, see drivers/net/arcnet.c
 *
 * **********************
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/ioport.h>
#include <linux/malloc.h>
#include <linux/delay.h>
#include <linux/netdevice.h>
#include <linux/bootmem.h>
#include <linux/init.h>
#include <asm/io.h>
#include <linux/arcdevice.h>


#define VERSION "arcnet: COM90xx IO-mapped mode support (by David Woodhouse et el.)\n"


/* Internal function declarations */

static int com90io_found(struct net_device *dev);
static void com90io_command(struct net_device *dev, int command);
static int com90io_status(struct net_device *dev);
static void com90io_setmask(struct net_device *dev, int mask);
static int com90io_reset(struct net_device *dev, int really_reset);
static void com90io_openclose(struct net_device *dev, bool open);
static void com90io_copy_to_card(struct net_device *dev, int bufnum, int offset,
				 void *buf, int count);
static void com90io_copy_from_card(struct net_device *dev, int bufnum, int offset,
				   void *buf, int count);


/* Handy defines for ARCnet specific stuff */

/* The number of low I/O ports used by the card. */
#define ARCNET_TOTAL_SIZE 16

/* COM 9026 controller chip --> ARCnet register addresses */
#define _INTMASK (ioaddr+0)	/* writable */
#define _STATUS  (ioaddr+0)	/* readable */
#define _COMMAND (ioaddr+1)	/* writable, returns random vals on read (?) */
#define _RESET  (ioaddr+8)	/* software reset (on read) */
#define _MEMDATA  (ioaddr+12)	/* Data port for IO-mapped memory */
#define _ADDR_HI  (ioaddr+15)	/* Control registers for said */
#define _ADDR_LO  (ioaddr+14)
#define _CONFIG  (ioaddr+2)	/* Configuration register */

#undef ASTATUS
#undef ACOMMAND
#undef AINTMASK

#define ASTATUS()	inb(_STATUS)
#define ACOMMAND(cmd) outb((cmd),_COMMAND)
#define AINTMASK(msk)	outb((msk),_INTMASK)
#define SETCONF() 	outb((lp->config),_CONFIG)


/****************************************************************************
 *                                                                          *
 * IO-mapped operation routines                                             *
 *                                                                          *
 ****************************************************************************/

#undef ONE_AT_A_TIME_TX
#undef ONE_AT_A_TIME_RX

static u_char get_buffer_byte(struct net_device *dev, unsigned offset)
{
	int ioaddr = dev->base_addr;

	outb(offset >> 8, _ADDR_HI);
	outb(offset & 0xff, _ADDR_LO);

	return inb(_MEMDATA);
}

#ifdef ONE_AT_A_TIME_TX
static void put_buffer_byte(struct net_device *dev, unsigned offset, u_char datum)
{
	int ioaddr = dev->base_addr;

	outb(offset >> 8, _ADDR_HI);
	outb(offset & 0xff, _ADDR_LO);

	outb(datum, _MEMDATA);
}

#endif


static void get_whole_buffer(struct net_device *dev, unsigned offset, unsigned length, char *dest)
{
	int ioaddr = dev->base_addr;

	outb((offset >> 8) | AUTOINCflag, _ADDR_HI);
	outb(offset & 0xff, _ADDR_LO);

	while (length--)
#ifdef ONE_AT_A_TIME_RX
		*(dest++) = get_buffer_byte(dev, offset++);
#else
		*(dest++) = inb(_MEMDATA);
#endif
}

static void put_whole_buffer(struct net_device *dev, unsigned offset, unsigned length, char *dest)
{
	int ioaddr = dev->base_addr;

	outb((offset >> 8) | AUTOINCflag, _ADDR_HI);
	outb(offset & 0xff, _ADDR_LO);

	while (length--)
#ifdef ONE_AT_A_TIME_TX
		put_buffer_byte(dev, offset++, *(dest++));
#else
		outb(*(dest++), _MEMDATA);
#endif
}

/*
 * We cannot probe for an IO mapped card either, although we can check that
 * it's where we were told it was, and even autoirq
 */
static int __init com90io_probe(struct net_device *dev)
{
	int ioaddr = dev->base_addr, status;
	unsigned long airqmask;

#ifndef MODULE
	arcnet_init();
#endif

	BUGLVL(D_NORMAL) printk(VERSION);
	BUGLVL(D_NORMAL) printk("E-mail me if you actually test this driver, please!\n");

	if (!ioaddr) {
		BUGMSG(D_NORMAL, "No autoprobe for IO mapped cards; you "
		       "must specify the base address!\n");
		return -ENODEV;
	}
	if (check_region(ioaddr, ARCNET_TOTAL_SIZE)) {
		BUGMSG(D_INIT_REASONS, "IO check_region %x-%x failed.\n",
		       ioaddr, ioaddr + ARCNET_TOTAL_SIZE - 1);
		return -ENXIO;
	}
	if (ASTATUS() == 0xFF) {
		BUGMSG(D_INIT_REASONS, "IO address %x empty\n", ioaddr);
		return -ENODEV;
	}
	inb(_RESET);
	mdelay(RESETtime);

	status = ASTATUS();

	if ((status & 0x9D) != (NORXflag | RECONflag | TXFREEflag | RESETflag)) {
		BUGMSG(D_INIT_REASONS, "Status invalid (%Xh).\n", status);
		return -ENODEV;
	}
	BUGMSG(D_INIT_REASONS, "Status after reset: %X\n", status);

	ACOMMAND(CFLAGScmd | RESETclear | CONFIGclear);

	BUGMSG(D_INIT_REASONS, "Status after reset acknowledged: %X\n", status);

	status = ASTATUS();

	if (status & RESETflag) {
		BUGMSG(D_INIT_REASONS, "Eternal reset (status=%Xh)\n", status);
		return -ENODEV;
	}
	outb((0x16 | IOMAPflag) & ~ENABLE16flag, _CONFIG);

	/* Read first loc'n of memory */

	outb(AUTOINCflag, _ADDR_HI);
	outb(0, _ADDR_LO);

	if ((status = inb(_MEMDATA)) != 0xd1) {
		BUGMSG(D_INIT_REASONS, "Signature byte not found"
		       " (%Xh instead).\n", status);
		return -ENODEV;
	}
	if (!dev->irq) {
		/*
		 * if we do this, we're sure to get an IRQ since the
		 * card has just reset and the NORXflag is on until
		 * we tell it to start receiving.
		 */

		airqmask = probe_irq_on();
		outb(NORXflag, _INTMASK);
		udelay(1);
		outb(0, _INTMASK);
		dev->irq = probe_irq_off(airqmask);

		if (dev->irq <= 0) {
			BUGMSG(D_INIT_REASONS, "Autoprobe IRQ failed\n");
			return -ENODEV;
		}
	}
	return com90io_found(dev);
}


/* Set up the struct net_device associated with this card.  Called after
 * probing succeeds.
 */
static int __init com90io_found(struct net_device *dev)
{
	struct arcnet_local *lp;
	int ioaddr = dev->base_addr;

	/* Reserve the irq */
	if (request_irq(dev->irq, &arcnet_interrupt, 0, "arcnet (COM90xx-IO)", dev)) {
		BUGMSG(D_NORMAL, "Can't get IRQ %d!\n", dev->irq);
		return -ENODEV;
	}
	/* Reserve the I/O region - guaranteed to work by check_region */
	request_region(dev->base_addr, ARCNET_TOTAL_SIZE, "arcnet (COM90xx-IO)");

	/* Initialize the rest of the device structure. */
	dev->priv = kmalloc(sizeof(struct arcnet_local), GFP_KERNEL);
	if (!dev->priv) {
		free_irq(dev->irq, dev);
		release_region(dev->base_addr, ARCNET_TOTAL_SIZE);
		return -ENOMEM;
	}
	memset(dev->priv, 0, sizeof(struct arcnet_local));

	lp = (struct arcnet_local *) (dev->priv);
	lp->hw.command = com90io_command;
	lp->hw.status = com90io_status;
	lp->hw.intmask = com90io_setmask;
	lp->hw.reset = com90io_reset;
	lp->hw.open_close = com90io_openclose;
	lp->hw.copy_to_card = com90io_copy_to_card;
	lp->hw.copy_from_card = com90io_copy_from_card;

	/*
	 * Fill in the fields of the device structure with generic
	 * values.
	 */
	arcdev_setup(dev);

	lp->config = (0x16 | IOMAPflag) & ~ENABLE16flag;
	SETCONF();

	/* get and check the station ID from offset 1 in shmem */

	dev->dev_addr[0] = get_buffer_byte(dev, 1);

	BUGMSG(D_NORMAL, "COM90IO: station %02Xh found at %03lXh, IRQ %d.\n",
	       dev->dev_addr[0], dev->base_addr, dev->irq);

	return 0;
}


/*
 * Do a hardware reset on the card, and set up necessary registers.
 *
 * This should be called as little as possible, because it disrupts the
 * token on the network (causes a RECON) and requires a significant delay.
 *
 * However, it does make sure the card is in a defined state.
 */
static int com90io_reset(struct net_device *dev, int really_reset)
{
	struct arcnet_local *lp = (struct arcnet_local *) dev->priv;
	short ioaddr = dev->base_addr;

	BUGMSG(D_INIT, "Resetting %s (status=%02Xh)\n", dev->name, ASTATUS());

	if (really_reset) {
		/* reset the card */
		inb(_RESET);
		mdelay(RESETtime);
	}
	/* Set the thing to IO-mapped, 8-bit  mode */
	lp->config = (0x1C | IOMAPflag) & ~ENABLE16flag;
	SETCONF();

	ACOMMAND(CFLAGScmd | RESETclear);	/* clear flags & end reset */
	ACOMMAND(CFLAGScmd | CONFIGclear);

	/* verify that the ARCnet signature byte is present */
	if (get_buffer_byte(dev, 0) != TESTvalue) {
		BUGMSG(D_NORMAL, "reset failed: TESTvalue not present.\n");
		return 1;
	}
	/* enable extended (512-byte) packets */
	ACOMMAND(CONFIGcmd | EXTconf);

	/* done!  return success. */
	return 0;
}


static void com90io_command(struct net_device *dev, int cmd)
{
	short ioaddr = dev->base_addr;

	ACOMMAND(cmd);
}


static int com90io_status(struct net_device *dev)
{
	short ioaddr = dev->base_addr;

	return ASTATUS();
}


static void com90io_setmask(struct net_device *dev, int mask)
{
	short ioaddr = dev->base_addr;

	AINTMASK(mask);
}

static void com90io_openclose(struct net_device *dev, int open)
{
	if (open)
		MOD_INC_USE_COUNT;
	else
		MOD_DEC_USE_COUNT;
}

static void com90io_copy_to_card(struct net_device *dev, int bufnum, int offset,
				 void *buf, int count)
{
	TIME("put_whole_buffer", count, put_whole_buffer(dev, bufnum * 512 + offset, count, buf));
}


static void com90io_copy_from_card(struct net_device *dev, int bufnum, int offset,
				   void *buf, int count)
{
	TIME("get_whole_buffer", count, get_whole_buffer(dev, bufnum * 512 + offset, count, buf));
}


#ifdef MODULE

static struct net_device *my_dev;

/* Module parameters */

static int io = 0x0;		/* use the insmod io= irq= shmem= options */
static int irq = 0;
static char *device;		/* use eg. device=arc1 to change name */

MODULE_PARM(io, "i");
MODULE_PARM(irq, "i");
MODULE_PARM(device, "s");

int init_module(void)
{
	struct net_device *dev;
	int err;

	dev = dev_alloc(device ? : "arc%d", &err);
	if (!dev)
		return err;

	dev->base_addr = io;
	dev->irq = irq;
	if (dev->irq == 2)
		dev->irq = 9;

	if (com90io_probe(dev))
		return -EIO;

	my_dev = dev;
	return 0;
}

void cleanup_module(void)
{
	struct net_device *dev = my_dev;
	int ioaddr = dev->base_addr;

	unregister_netdev(dev);

	/* Set the thing back to MMAP mode, in case the old driver is loaded later */
	outb((inb(_CONFIG) & ~IOMAPflag), _CONFIG);

	free_irq(dev->irq, dev);
	release_region(dev->base_addr, ARCNET_TOTAL_SIZE);
	kfree(dev->priv);
	kfree(dev);
}

#else

static int __init com90io_setup(char *s)
{
	struct net_device *dev;
	int ints[4];

	s = get_options(s, 4, ints);
	if (!ints[0])
		return 1;
	dev = alloc_bootmem(sizeof(struct net_device) + 10);
	memset(dev, 0, sizeof(struct net_device) + 10);
	dev->name = (char *) (dev + 1);
	dev->init = com90io_probe;

	switch (ints[0]) {
	default:		/* ERROR */
		printk("com90io: Too many arguments.\n");
	case 2:		/* IRQ */
		dev->irq = ints[2];
	case 1:		/* IO address */
		dev->base_addr = ints[1];
	}
	if (*s)
		strncpy(dev->name, s, 9);
	else
		strcpy(dev->name, "arc%d");
	if (register_netdev(dev))
		printk(KERN_ERR "com90io: Cannot register arcnet device\n");

	return 1;
}

__setup("com90io=", com90io_setup);

#endif				/* MODULE */
