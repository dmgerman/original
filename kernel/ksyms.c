/* 
 * Herein lies all the functions/variables that are "exported" for linkage
 * with dynamically loaded kernel modules.
 *			Jon.
 *
 * - Stacked module support and unified symbol table added (June 1994)
 * - External symbol table support added (December 1994)
 * - Versions on symbols added (December 1994)
 * by Bjorn Ekwall <bj0rn@blox.se>
 */

#include <linux/module.h>
#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/smp.h>
#include <linux/fs.h>
#include <linux/blkdev.h>
#include <linux/sched.h>
#include <linux/kernel_stat.h>
#include <linux/mm.h>
#include <linux/malloc.h>
#include <linux/ptrace.h>
#include <linux/sys.h>
#include <linux/utsname.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/timer.h>
#include <linux/binfmts.h>
#include <linux/personality.h>
#include <linux/termios.h>
#include <linux/tqueue.h>
#include <linux/tty.h>
#include <linux/serial.h>
#include <linux/locks.h>
#include <linux/string.h>
#include <linux/delay.h>
#include <linux/sem.h>
#include <linux/minix_fs.h>
#include <linux/ext2_fs.h>
#include <linux/random.h>
#include <linux/mount.h>
#include <linux/pagemap.h>

extern unsigned char aux_device_present, kbd_read_mask;

#ifdef __alpha__
# include <asm/io.h>
# include <asm/hwrpb.h>

extern void bcopy (const char *src, char *dst, int len);
extern struct hwrpb_struct *hwrpb;

/* these are C runtime functions with special calling conventions: */
extern void __divl (void);
extern void __reml (void);
extern void __divq (void);
extern void __remq (void);
extern void __divlu (void);
extern void __remlu (void);
extern void __divqu (void);
extern void __remqu (void);

#endif

#ifdef CONFIG_NET
#include <linux/in.h>
#include <linux/net.h>
#include <linux/netdevice.h>
#include <linux/firewall.h>

#include <linux/trdevice.h>

#ifdef CONFIG_AX25
#include <net/ax25.h>
#endif
#ifdef CONFIG_INET
#include <linux/ip.h>
#include <linux/etherdevice.h>
#include <net/protocol.h>
#include <net/arp.h>
#include <net/ip.h>
#include <net/udp.h>
#include <net/tcp.h>
#include <net/route.h>
#include <linux/net_alias.h>
#if defined(CONFIG_PPP) || defined(CONFIG_SLIP)
#include "../drivers/net/slhc.h"
#endif
#endif
#ifdef CONFIG_NET_ALIAS
#include <linux/net_alias.h>
#endif
#endif
#ifdef CONFIG_PCI
#include <linux/bios32.h>
#include <linux/pci.h>
#endif
#if defined(CONFIG_MSDOS_FS) && !defined(CONFIG_UMSDOS_FS)
#include <linux/msdos_fs.h>
#endif

#if defined(CONFIG_PROC_FS)
#include <linux/proc_fs.h>
#endif

#include <asm/irq.h>
#ifdef __SMP__
#include <linux/smp.h>
#endif

extern char *get_options(char *str, int *ints);
extern void set_device_ro(int dev,int flag);
extern struct file_operations * get_blkfops(unsigned int);

extern void *sys_call_table;

#if	defined(CONFIG_ULTRA)	||	defined(CONFIG_WD80x3)		|| \
	defined(CONFIG_EL2)	||	defined(CONFIG_NE2000)		|| \
	defined(CONFIG_E2100)	||	defined(CONFIG_HPLAN_PLUS)	|| \
	defined(CONFIG_HPLAN)	||	defined(CONFIG_AC3200)		
#include "../drivers/net/8390.h"
#endif

#ifdef CONFIG_SCSI
#include "../drivers/scsi/scsi.h"
#include "../drivers/scsi/scsi_ioctl.h"
#include "../drivers/scsi/hosts.h"
#include "../drivers/scsi/constants.h"
#include "../drivers/scsi/sd.h"
#include <linux/scsicam.h>

extern int generic_proc_info(char *, char **, off_t, int, int, int);
#endif

extern int sys_tz;
extern int request_dma(unsigned int dmanr, char * deviceID);
extern void free_dma(unsigned int dmanr);
extern int (*rarp_ioctl_hook)(int,void*);

extern void (* iABI_hook)(struct pt_regs * regs);

#ifdef CONFIG_BINFMT_ELF
#include <linux/elfcore.h>
extern int dump_fpu(elf_fpregset_t *);
#endif

struct symbol_table symbol_table = {
#include <linux/symtab_begin.h>
#ifdef MODVERSIONS
	{ (void *)1 /* Version version :-) */,
		SYMBOL_NAME_STR (Using_Versions) },
#endif

	/* platform dependent support */
#ifdef __alpha__
	X(_inb),
	X(_inw),
	X(_inl),
	X(_outb),
	X(_outw),
	X(_outl),
	X(bcopy),	/* generated by gcc-2.7.0 for string assignments */
	X(hwrpb),
	X(__divl),
	X(__reml),
	X(__divq),
	X(__remq),
	X(__divlu),
	X(__remlu),
	X(__divqu),
	X(__remqu),
	X(strlen),	/* used by ftape */
	X(memcmp),
	X(memmove),
	X(__constant_c_memset),
#endif

	/* stackable module support */
	X(rename_module_symbol),
	X(register_symtab),
	X(get_options),

	/* system info variables */
	/* These check that they aren't defines (0/1) */
#ifndef EISA_bus__is_a_macro
	X(EISA_bus),
#endif
#ifndef MCA_bus__is_a_macro
	X(MCA_bus),
#endif
#ifndef wp_works_ok__is_a_macro
	X(wp_works_ok),
#endif

#ifdef CONFIG_PCI
	/* PCI BIOS support */
	X(pcibios_present),
	X(pcibios_find_class),
	X(pcibios_find_device),
	X(pcibios_read_config_byte),
	X(pcibios_read_config_word),
	X(pcibios_read_config_dword),
    	X(pcibios_strerror),
	X(pcibios_write_config_byte),
	X(pcibios_write_config_word),
	X(pcibios_write_config_dword),
#endif

	/* process memory management */
	X(verify_area),
	X(do_mmap),
	X(do_munmap),
	X(insert_vm_struct),
	X(merge_segments),

	/* internal kernel memory management */
	X(__get_free_pages),
	X(free_pages),
	X(kmalloc),
	X(kfree),
	X(vmalloc),
	X(vremap),
	X(vfree),
 	X(mem_map),
 	X(remap_page_range),
	X(high_memory),
	X(update_vm_cache),

	/* filesystem internal functions */
	X(getname),
	X(putname),
	X(__iget),
	X(iput),
	X(namei),
	X(lnamei),
	X(open_namei),
	X(close_fp),
	X(check_disk_change),
	X(invalidate_buffers),
	X(invalidate_inodes),
	X(fsync_dev),
	X(permission),
	X(inode_setattr),
	X(inode_change_ok),
	X(generic_mmap),
	X(set_blocksize),
	X(getblk),
	X(bread),
	X(breada),
	X(__brelse),
	X(__bforget),
	X(ll_rw_block),
	X(__wait_on_buffer),
	X(dcache_lookup),
	X(dcache_add),
	X(aout_core_dump),
	X(add_blkdev_randomness),
	X(generic_file_read),
	X(generic_readpage),

	/* device registration */
	X(register_chrdev),
	X(unregister_chrdev),
	X(register_blkdev),
	X(unregister_blkdev),
	X(tty_register_driver),
	X(tty_unregister_driver),
	X(tty_std_termios),

	/* block device driver support */
	X(block_read),
	X(block_write),
	X(block_fsync),
	X(wait_for_request),
	X(blksize_size),
	X(hardsect_size),
	X(blk_size),
	X(blk_dev),
	X(is_read_only),
	X(set_device_ro),
	X(bmap),
	X(sync_dev),
	X(get_blkfops),
	
	/* Module creation of serial units */
	X(register_serial),
	X(unregister_serial),

	/* tty routines */
	X(tty_hangup),
	X(tty_wait_until_sent),
	X(tty_check_change),
	X(tty_hung_up_p),
	X(do_SAK),
	X(console_print),

	/* filesystem registration */
	X(register_filesystem),
	X(unregister_filesystem),

	/* executable format registration */
	X(register_binfmt),
	X(unregister_binfmt),

	/* execution environment registration */
	X(lookup_exec_domain),
	X(register_exec_domain),
	X(unregister_exec_domain),

	/* interrupt handling */
	X(request_irq),
	X(free_irq),
	X(enable_irq),
	X(disable_irq),
	X(probe_irq_on),
	X(probe_irq_off),
	X(bh_active),
	X(bh_mask),
	X(bh_base),
	X(add_timer),
	X(del_timer),
	X(tq_timer),
	X(tq_immediate),
	X(tq_scheduler),
	X(tq_last),
	X(timer_active),
	X(timer_table),
 	X(intr_count),

	/* autoirq from  drivers/net/auto_irq.c */
	X(autoirq_setup),
	X(autoirq_report),

	/* dma handling */
	X(request_dma),
	X(free_dma),
#ifdef HAVE_DISABLE_HLT
	X(disable_hlt),
	X(enable_hlt),
#endif

	/* IO port handling */
	X(check_region),
	X(request_region),
	X(release_region),

	/* process management */
	X(wake_up),
	X(wake_up_interruptible),
	X(sleep_on),
	X(interruptible_sleep_on),
	X(schedule),
	X(current_set),
#if defined(__i386__) && defined(__SMP__)
	X(apic_reg),		/* Needed internally for the I386 inlines */
#endif	
	X(jiffies),
	X(xtime),
	X(do_gettimeofday),
	X(loops_per_sec),
	X(need_resched),
	X(kstat),
	X(kill_proc),
	X(kill_pg),
	X(kill_sl),

	/* misc */
	X(panic),
	X(printk),
	X(sprintf),
	X(vsprintf),
	X(kdevname),
	X(simple_strtoul),
	X(system_utsname),
	X(sys_call_table),

	/* Signal interfaces */
	X(send_sig),

	/* Program loader interfaces */
	X(setup_arg_pages),
	X(copy_strings),
	X(create_tables),
	X(do_execve),
	X(flush_old_exec),
	X(open_inode),
	X(read_exec),

	/* Miscellaneous access points */
	X(si_meminfo),
#ifdef CONFIG_NET
	/* Socket layer registration */
	X(sock_register),
	X(sock_unregister),
#ifdef CONFIG_FIREWALL
	/* Firewall registration */
	X(register_firewall),
	X(unregister_firewall),
#endif
#ifdef CONFIG_INET	
	/* Internet layer registration */
	X(inet_add_protocol),
	X(inet_del_protocol),
	X(rarp_ioctl_hook),
	X(init_etherdev),
	X(ip_rt_route),
	X(ip_rt_put),
	X(arp_send),
#ifdef CONFIG_IP_FORWARD
	X(ip_forward),
#endif
#if	defined(CONFIG_ULTRA)	||	defined(CONFIG_WD80x3)		|| \
	defined(CONFIG_EL2)	||	defined(CONFIG_NE2000)		|| \
	defined(CONFIG_E2100)	||	defined(CONFIG_HPLAN_PLUS)	|| \
	defined(CONFIG_HPLAN)	||	defined(CONFIG_AC3200)
	/* If 8390 NIC support is built in, we will need these. */
	X(ei_open),
	X(ei_close),
	X(ei_debug),
	X(ei_interrupt),
	X(ethdev_init),
	X(NS8390_init),
#endif
#if defined(CONFIG_PPP) || defined(CONFIG_SLIP)
    	/* VJ header compression */
	X(slhc_init),
	X(slhc_free),
	X(slhc_remember),
	X(slhc_compress),
	X(slhc_uncompress),
	X(slhc_toss),
#endif
#ifdef CONFIG_NET_ALIAS
#include <linux/net_alias.h>
#endif
#endif
	/* Device callback registration */
	X(register_netdevice_notifier),
	X(unregister_netdevice_notifier),
#ifdef CONFIG_NET_ALIAS
	X(register_net_alias_type),
	X(unregister_net_alias_type),
#endif
#endif

	/* support for loadable net drivers */
#ifdef CONFIG_AX25
	X(ax25_encapsulate),
	X(ax25_rebuild_header),	
#endif	
#ifdef CONFIG_INET
	X(register_netdev),
	X(unregister_netdev),
	X(ether_setup),
	X(eth_type_trans),
	X(eth_copy_and_sum),
	X(alloc_skb),
	X(kfree_skb),
	X(dev_alloc_skb),
	X(dev_kfree_skb),
	X(netif_rx),
	X(dev_rint),
	X(dev_tint),
	X(irq2dev_map),
	X(dev_add_pack),
	X(dev_remove_pack),
	X(dev_get),
	X(dev_ioctl),
	X(dev_queue_xmit),
	X(dev_base),
	X(dev_close),
	X(arp_find),
	X(n_tty_ioctl),
	X(tty_register_ldisc),
	X(kill_fasync),
#endif
#ifdef CONFIG_SCSI
	/* Supports loadable scsi drivers 
 	 * technically some of this stuff could be moved to scsi.c, but
 	 * scsi.c is initialized before the memory manager is set up.
 	 * So we add it here too.  There is a duplicate set in scsi.c
 	 * that is used when the entire scsi subsystem is a loadable
 	 * module.
	 */
	X(scsi_register_module),
	X(scsi_unregister_module),
	X(scsi_free),
	X(scsi_malloc),
	X(scsi_register),
	X(scsi_unregister),
	X(scsicam_bios_param),
 	X(allocate_device),
 	X(scsi_do_cmd),
 	X(scsi_command_size),
 	X(scsi_init_malloc),
 	X(scsi_init_free),
 	X(scsi_ioctl),
	X(scsi_mark_host_bus_reset),
  	X(print_command),
      	X(print_msg),
  	X(print_status),
 	X(print_sense),
 	X(dma_free_sectors),
 	X(kernel_scsi_ioctl),
 	X(need_isa_buffer),
 	X(request_queueable),
	X(generic_proc_info),
 	X(scsi_devices),
	X(gendisk_head), /* Needed for sd.c */
	X(resetup_one_dev), /* Needed for sd.c */
#if defined(CONFIG_PROC_FS)
	X(proc_print_scsidevice),
#endif
#else
	/*
	 * With no scsi configured, we still need to export a few
	 * symbols so that scsi can be loaded later via insmod.
	 */
	X(gendisk_head),
	X(resetup_one_dev),
#endif
	/* Added to make file system as module */
	X(set_writetime),
	X(sys_tz),
	X(__wait_on_super),
	X(file_fsync),
	X(clear_inode),
	X(refile_buffer),
	X(___strtok),
	X(init_fifo),
	X(super_blocks),
	X(chrdev_inode_operations),
	X(blkdev_inode_operations),
	X(read_ahead),
	X(get_hash_table),
	X(get_empty_inode),
	X(insert_inode_hash),
	X(event),
	X(__down),
#if defined(CONFIG_MSDOS_FS) && !defined(CONFIG_UMSDOS_FS)
	/* support for umsdos fs */
	X(msdos_bmap),
	X(msdos_create),
	X(msdos_file_read),
	X(msdos_file_write),
	X(msdos_lookup),
	X(msdos_mkdir),
	X(msdos_mmap),
	X(msdos_put_inode),
	X(msdos_put_super),
	X(msdos_read_inode),
	X(msdos_read_super),
	X(msdos_readdir),
	X(msdos_rename),
	X(msdos_rmdir),
	X(msdos_smap),
	X(msdos_statfs),
	X(msdos_truncate),
	X(msdos_unlink),
	X(msdos_unlink_umsdos),
	X(msdos_write_inode),
#endif
#ifdef CONFIG_PROC_FS
	X(proc_register),
	X(proc_unregister),
	X(in_group_p),
	X(generate_cluster),
	X(proc_scsi),
	X(proc_net_inode_operations),
	X(proc_net),
#endif
/* all busmice */
	X(add_mouse_randomness),
	X(fasync_helper),
/* psaux mouse */
	X(aux_device_present),
	X(kbd_read_mask),

#ifdef CONFIG_TR
	X(tr_setup),
	X(tr_type_trans),
#endif

#ifdef CONFIG_BINFMT_ELF
	X(dump_fpu),
#endif

	/********************************************************
	 * Do not add anything below this line,
	 * as the stacked modules depend on this!
	 */
#include <linux/symtab_end.h>
};

/*
int symbol_table_size = sizeof (symbol_table) / sizeof (symbol_table[0]);
*/
