/*
 *  Code extracted from
 *  linux/kernel/hd.c
 *
 *  Copyright (C) 1991-1998  Linus Torvalds
 *
 *  Moved partition checking code to fs/partitions* - Russell King
 *  (linux@arm.uk.linux.org)
 */

#include <linux/config.h>
#include <linux/fs.h>
#include <linux/genhd.h>
#include <linux/kernel.h>
#include <linux/blk.h>
#include <linux/init.h>

extern int parport_init(void);
extern int chr_dev_init(void);
extern int blk_dev_init(void);
extern int scsi_dev_init(void);
extern int net_dev_init(void);
extern void console_map_init(void);
extern int soc_probe(void);

void __init device_init(void)
{
#ifdef CONFIG_PARPORT
	parport_init();
#endif
	/*
	 *	I2O must come before block and char as the I2O layer may
	 *	in future claim devices that block/char most not touch.
	 */
#ifdef CONFIG_I2O
	i2o_init();
#endif
	chr_dev_init();
	blk_dev_init();
	sti();
#ifdef CONFIG_FC4_SOC
	/* This has to be done before scsi_dev_init */
	soc_probe();
#endif
#ifdef CONFIG_SCSI
	scsi_dev_init();
#endif
#ifdef CONFIG_BLK_CPQ_DA
	cpqarray_init();
#endif
#ifdef CONFIG_INET
	net_dev_init();
#endif
#ifdef CONFIG_VT
	console_map_init();
#endif
}
