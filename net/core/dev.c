/*
 * 	NET3	Protocol independent device support routines.
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 *	Derived from the non IP parts of dev.c 1.0.19
 * 		Authors:	Ross Biro, <bir7@leland.Stanford.Edu>
 *				Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *				Mark Evans, <evansmp@uhura.aston.ac.uk>
 *
 *	Additional Authors:
 *		Florian la Roche <rzsfl@rz.uni-sb.de>
 *		Alan Cox <gw4pts@gw4pts.ampr.org>
 *		David Hinds <dhinds@allegro.stanford.edu>
 *		Alexey Kuznetsov <kuznet@ms2.inr.ac.ru>
 *		Adam Sulmicki <adam@cfar.umd.edu>
 *
 *	Changes:
 *		Alan Cox	:	device private ioctl copies fields back.
 *		Alan Cox	:	Transmit queue code does relevant stunts to
 *					keep the queue safe.
 *		Alan Cox	:	Fixed double lock.
 *		Alan Cox	:	Fixed promisc NULL pointer trap
 *		????????	:	Support the full private ioctl range
 *		Alan Cox	:	Moved ioctl permission check into drivers
 *		Tim Kordas	:	SIOCADDMULTI/SIOCDELMULTI
 *		Alan Cox	:	100 backlog just doesn't cut it when
 *					you start doing multicast video 8)
 *		Alan Cox	:	Rewrote net_bh and list manager.
 *		Alan Cox	: 	Fix ETH_P_ALL echoback lengths.
 *		Alan Cox	:	Took out transmit every packet pass
 *					Saved a few bytes in the ioctl handler
 *		Alan Cox	:	Network driver sets packet type before calling netif_rx. Saves
 *					a function call a packet.
 *		Alan Cox	:	Hashed net_bh()
 *		Richard Kooijman:	Timestamp fixes.
 *		Alan Cox	:	Wrong field in SIOCGIFDSTADDR
 *		Alan Cox	:	Device lock protection.
 *		Alan Cox	: 	Fixed nasty side effect of device close changes.
 *		Rudi Cilibrasi	:	Pass the right thing to set_mac_address()
 *		Dave Miller	:	32bit quantity for the device lock to make it work out
 *					on a Sparc.
 *		Bjorn Ekwall	:	Added KERNELD hack.
 *		Alan Cox	:	Cleaned up the backlog initialise.
 *		Craig Metz	:	SIOCGIFCONF fix if space for under
 *					1 device.
 *	    Thomas Bogendoerfer :	Return ENODEV for dev_open, if there
 *					is no device open function.
 *		Andi Kleen	:	Fix error reporting for SIOCGIFCONF
 *	    Michael Chastain	:	Fix signed/unsigned for SIOCGIFCONF
 *		Cyrus Durgin	:	Cleaned for KMOD
 *		Adam Sulmicki   :	Bug Fix : Network Device Unload
 *					A network device unload needs to purge
 *					the backlog queue.
 *	Paul Rusty Russell	:	SIOCSIFNAME
 */

#include <asm/uaccess.h>
#include <asm/system.h>
#include <asm/bitops.h>
#include <linux/config.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/socket.h>
#include <linux/sockios.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/if_ether.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/notifier.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <linux/rtnetlink.h>
#include <net/slhc.h>
#include <linux/proc_fs.h>
#include <linux/stat.h>
#include <linux/if_bridge.h>
#include <net/dst.h>
#include <net/pkt_sched.h>
#include <net/profile.h>
#include <linux/init.h>
#include <linux/kmod.h>
#if defined(CONFIG_NET_RADIO) || defined(CONFIG_NET_PCMCIA_RADIO)
#include <linux/wireless.h>		/* Note : will define WIRELESS_EXT */
#endif	/* CONFIG_NET_RADIO || CONFIG_NET_PCMCIA_RADIO */
#ifdef CONFIG_PLIP
extern int plip_init(void);
#endif

NET_PROFILE_DEFINE(dev_queue_xmit)
NET_PROFILE_DEFINE(softnet_process)

const char *if_port_text[] = {
  "unknown",
  "BNC",
  "10baseT",
  "AUI",
  "100baseT",
  "100baseTX",
  "100baseFX"
};

/*
 *	The list of packet types we will receive (as opposed to discard)
 *	and the routines to invoke.
 *
 *	Why 16. Because with 16 the only overlap we get on a hash of the
 *	low nibble of the protocol value is RARP/SNAP/X.25. 
 *
 *		0800	IP
 *		0001	802.3
 *		0002	AX.25
 *		0004	802.2
 *		8035	RARP
 *		0005	SNAP
 *		0805	X.25
 *		0806	ARP
 *		8137	IPX
 *		0009	Localtalk
 *		86DD	IPv6
 */

static struct packet_type *ptype_base[16];		/* 16 way hashed list */
static struct packet_type *ptype_all = NULL;		/* Taps */
static rwlock_t ptype_lock = RW_LOCK_UNLOCKED;

/*
 *	Our notifier list
 */
 
static struct notifier_block *netdev_chain=NULL;

/*
 *	Device drivers call our routines to queue packets here. We empty the
 *	queue in the local softnet handler.
 */
struct softnet_data softnet_data[NR_CPUS] __cacheline_aligned;

#ifdef CONFIG_NET_FASTROUTE
int netdev_fastroute;
int netdev_fastroute_obstacles;
#endif


/******************************************************************************************

		Protocol management and registration routines

*******************************************************************************************/

/*
 *	For efficiency
 */

int netdev_nit=0;

/*
 *	Add a protocol ID to the list. Now that the input handler is
 *	smarter we can dispense with all the messy stuff that used to be
 *	here.
 *
 *	BEWARE!!! Protocol handlers, mangling input packets,
 *	MUST BE last in hash buckets and checking protocol handlers
 *	MUST start from promiscous ptype_all chain in net_bh.
 *	It is true now, do not change it.
 *	Explantion follows: if protocol handler, mangling packet, will
 *	be the first on list, it is not able to sense, that packet
 *	is cloned and should be copied-on-write, so that it will
 *	change it and subsequent readers will get broken packet.
 *							--ANK (980803)
 */
 
void dev_add_pack(struct packet_type *pt)
{
	int hash;

	write_lock_bh(&ptype_lock);

#ifdef CONFIG_NET_FASTROUTE
	/* Hack to detect packet socket */
	if (pt->data) {
		netdev_fastroute_obstacles++;
		dev_clear_fastroute(pt->dev);
	}
#endif
	if (pt->type == htons(ETH_P_ALL)) {
		netdev_nit++;
		pt->next=ptype_all;
		ptype_all=pt;
	} else {
		hash=ntohs(pt->type)&15;
		pt->next = ptype_base[hash];
		ptype_base[hash] = pt;
	}
	write_unlock_bh(&ptype_lock);
}


/*
 *	Remove a protocol ID from the list.
 */
 
void dev_remove_pack(struct packet_type *pt)
{
	struct packet_type **pt1;

	write_lock_bh(&ptype_lock);

	if (pt->type == htons(ETH_P_ALL)) {
		netdev_nit--;
		pt1=&ptype_all;
	} else {
		pt1=&ptype_base[ntohs(pt->type)&15];
	}

	for (; (*pt1) != NULL; pt1 = &((*pt1)->next)) {
		if (pt == (*pt1)) {
			*pt1 = pt->next;
#ifdef CONFIG_NET_FASTROUTE
			if (pt->data)
				netdev_fastroute_obstacles--;
#endif
			write_unlock_bh(&ptype_lock);
			return;
		}
	}
	write_unlock_bh(&ptype_lock);
	printk(KERN_WARNING "dev_remove_pack: %p not found.\n", pt);
}

/*****************************************************************************************

			    Device Interface Subroutines

******************************************************************************************/

/* 
 *	Find an interface by name. May be called under rtnl semaphore
 *	or dev_base_lock.
 */
 

struct net_device *__dev_get_by_name(const char *name)
{
	struct net_device *dev;

	for (dev = dev_base; dev != NULL; dev = dev->next) {
		if (strcmp(dev->name, name) == 0)
			return dev;
	}
	return NULL;
}

/* 
 *	Find an interface by name. Any context, dev_put() to release.
 */

struct net_device *dev_get_by_name(const char *name)
{
	struct net_device *dev;

	read_lock(&dev_base_lock);
	dev = __dev_get_by_name(name);
	if (dev)
		dev_hold(dev);
	read_unlock(&dev_base_lock);
	return dev;
}

/* 
   Return value is changed to int to prevent illegal usage in future.
   It is still legal to use to check for device existance.

   User should understand, that the result returned by this function
   is meaningless, if it was not issued under rtnl semaphore.
 */

int dev_get(const char *name)
{
	struct net_device *dev;

	read_lock(&dev_base_lock);
	dev = __dev_get_by_name(name);
	read_unlock(&dev_base_lock);
	return dev != NULL;
}

/* 
 *	Find an interface by index. May be called under rtnl semaphore
 *	or dev_base_lock.
 */

struct net_device * __dev_get_by_index(int ifindex)
{
	struct net_device *dev;

	for (dev = dev_base; dev != NULL; dev = dev->next) {
		if (dev->ifindex == ifindex)
			return dev;
	}
	return NULL;
}

/* 
 *	Find an interface by index. Any context, dev_put() to release.
 */

struct net_device * dev_get_by_index(int ifindex)
{
	struct net_device *dev;

	read_lock(&dev_base_lock);
	dev = __dev_get_by_index(ifindex);
	if (dev)
		dev_hold(dev);
	read_unlock(&dev_base_lock);
	return dev;
}

/* 
 *	Find an interface by ll addr. May be called only under rtnl semaphore.
 */

struct net_device *dev_getbyhwaddr(unsigned short type, char *ha)
{
	struct net_device *dev;

	ASSERT_RTNL();

	for (dev = dev_base; dev != NULL; dev = dev->next) {
		if (dev->type == type &&
		    memcmp(dev->dev_addr, ha, dev->addr_len) == 0)
			return dev;
	}
	return NULL;
}

/*
 *	Passed a format string - eg "lt%d" it will try and find a suitable
 *	id. Not efficient for many devices, not called a lot..
 */

int dev_alloc_name(struct net_device *dev, const char *name)
{
	int i;
	char buf[32];

	/*
	 *	If you need over 100 please also fix the algorithm...
	 */
	for (i = 0; i < 100; i++) {
		sprintf(buf,name,i);
		if (__dev_get_by_name(buf) == NULL) {
			strcpy(dev->name, buf);
			return i;
		}
	}
	return -ENFILE;	/* Over 100 of the things .. bail out! */
}

struct net_device *dev_alloc(const char *name, int *err)
{
	struct net_device *dev=kmalloc(sizeof(struct net_device)+16, GFP_KERNEL);
	if (dev == NULL) {
		*err = -ENOBUFS;
		return NULL;
	}
	memset(dev, 0, sizeof(struct net_device));
	dev->name = (char *)(dev + 1);	/* Name string space */
	*err = dev_alloc_name(dev, name);
	if (*err < 0) {
		kfree(dev);
		return NULL;
	}
	return dev;
}

void netdev_state_change(struct net_device *dev)
{
	if (dev->flags&IFF_UP) {
		notifier_call_chain(&netdev_chain, NETDEV_CHANGE, dev);
		rtmsg_ifinfo(RTM_NEWLINK, dev, 0);
	}
}


/*
 *	Find and possibly load an interface.
 */
 
#ifdef CONFIG_KMOD

void dev_load(const char *name)
{
	if (!__dev_get_by_name(name) && capable(CAP_SYS_MODULE))
		request_module(name);
}

#else

extern inline void dev_load(const char *unused){;}

#endif

static int default_rebuild_header(struct sk_buff *skb)
{
	printk(KERN_DEBUG "%s: default_rebuild_header called -- BUG!\n", skb->dev ? skb->dev->name : "NULL!!!");
	kfree_skb(skb);
	return 1;
}

/*
 *	Prepare an interface for use. 
 */
 
int dev_open(struct net_device *dev)
{
	int ret = 0;

	/*
	 *	Is it already up?
	 */

	if (dev->flags&IFF_UP)
		return 0;

	/*
	 *	Is it even present?
	 */
	if (!netif_device_present(dev))
		return -ENODEV;

	/*
	 *	Call device private open method
	 */
	 
	if (dev->open) 
  		ret = dev->open(dev);

	/*
	 *	If it went open OK then:
	 */
	 
	if (ret == 0) 
	{
		/*
		 *	Set the flags.
		 */
		dev->flags |= IFF_UP;

		set_bit(__LINK_STATE_START, &dev->state);

		/*
		 *	Initialize multicasting status 
		 */
		dev_mc_upload(dev);

		/*
		 *	Wakeup transmit queue engine
		 */
		dev_activate(dev);

		/*
		 *	... and announce new interface.
		 */
		notifier_call_chain(&netdev_chain, NETDEV_UP, dev);
	}
	return(ret);
}

#ifdef CONFIG_NET_FASTROUTE

static void dev_do_clear_fastroute(struct net_device *dev)
{
	if (dev->accept_fastpath) {
		int i;

		for (i=0; i<=NETDEV_FASTROUTE_HMASK; i++) {
			struct dst_entry *dst;

			write_lock_irq(&dev->fastpath_lock);
			dst = dev->fastpath[i];
			dev->fastpath[i] = NULL;
			write_unlock_irq(&dev->fastpath_lock);

			dst_release(dst);
		}
	}
}

void dev_clear_fastroute(struct net_device *dev)
{
	if (dev) {
		dev_do_clear_fastroute(dev);
	} else {
		read_lock(&dev_base_lock);
		for (dev = dev_base; dev; dev = dev->next)
			dev_do_clear_fastroute(dev);
		read_unlock(&dev_base_lock);
	}
}
#endif

/*
 *	Completely shutdown an interface.
 */
 
int dev_close(struct net_device *dev)
{
	if (!(dev->flags&IFF_UP))
		return 0;

	/*
	 *	Tell people we are going down, so that they can
	 *	prepare to death, when device is still operating.
	 */
	notifier_call_chain(&netdev_chain, NETDEV_GOING_DOWN, dev);

	dev_deactivate(dev);

	clear_bit(__LINK_STATE_START, &dev->state);

	/*
	 *	Call the device specific close. This cannot fail.
	 *	Only if device is UP
	 *
	 *	We allow it to be called even after a DETACH hot-plug
	 *	event.
	 */
	 
	if (dev->stop)
		dev->stop(dev);

	/*
	 *	Device is now down.
	 */

	dev->flags &= ~IFF_UP;
#ifdef CONFIG_NET_FASTROUTE
	dev_clear_fastroute(dev);
#endif

	/*
	 *	Tell people we are down
	 */
	notifier_call_chain(&netdev_chain, NETDEV_DOWN, dev);

	return(0);
}


/*
 *	Device change register/unregister. These are not inline or static
 *	as we export them to the world.
 */

int register_netdevice_notifier(struct notifier_block *nb)
{
	return notifier_chain_register(&netdev_chain, nb);
}

int unregister_netdevice_notifier(struct notifier_block *nb)
{
	return notifier_chain_unregister(&netdev_chain,nb);
}

/*
 *	Support routine. Sends outgoing frames to any network
 *	taps currently in use.
 */

void dev_queue_xmit_nit(struct sk_buff *skb, struct net_device *dev)
{
	struct packet_type *ptype;
	get_fast_time(&skb->stamp);

	read_lock(&ptype_lock);
	for (ptype = ptype_all; ptype!=NULL; ptype = ptype->next) 
	{
		/* Never send packets back to the socket
		 * they originated from - MvS (miquels@drinkel.ow.org)
		 */
		if ((ptype->dev == dev || !ptype->dev) &&
			((struct sock *)ptype->data != skb->sk))
		{
			struct sk_buff *skb2;
			if ((skb2 = skb_clone(skb, GFP_ATOMIC)) == NULL)
				break;

			/* skb->nh should be correctly
			   set by sender, so that the second statement is
			   just protection against buggy protocols.
			 */
			skb2->mac.raw = skb2->data;

			if (skb2->nh.raw < skb2->data || skb2->nh.raw >= skb2->tail) {
				if (net_ratelimit())
					printk(KERN_DEBUG "protocol %04x is buggy, dev %s\n", skb2->protocol, dev->name);
				skb2->nh.raw = skb2->data;
				if (dev->hard_header)
					skb2->nh.raw += dev->hard_header_len;
			}

			skb2->h.raw = skb2->nh.raw;
			skb2->pkt_type = PACKET_OUTGOING;
			skb2->rx_dev = skb->dev;
			dev_hold(skb2->rx_dev);
			ptype->func(skb2, skb->dev, ptype);
		}
	}
	read_unlock(&ptype_lock);
}

/*
 *	Fast path for loopback frames.
 */
 
void dev_loopback_xmit(struct sk_buff *skb)
{
	struct sk_buff *newskb=skb_clone(skb, GFP_ATOMIC);
	if (newskb==NULL)
		return;

	newskb->mac.raw = newskb->data;
	skb_pull(newskb, newskb->nh.raw - newskb->data);
	newskb->pkt_type = PACKET_LOOPBACK;
	newskb->ip_summed = CHECKSUM_UNNECESSARY;
	if (newskb->dst==NULL)
		printk(KERN_DEBUG "BUG: packet without dst looped back 1\n");
	netif_rx(newskb);
}

int dev_queue_xmit(struct sk_buff *skb)
{
	struct net_device *dev = skb->dev;
	struct Qdisc  *q;

	/* Grab device queue */
	spin_lock_bh(&dev->queue_lock);
	q = dev->qdisc;
	if (q->enqueue) {
		int ret = q->enqueue(skb, q);

		qdisc_run(dev);

		spin_unlock_bh(&dev->queue_lock);
		return ret;
	}

	/* The device has no queue. Common case for software devices:
	   loopback, all the sorts of tunnels...

	   Really, it is unlikely that xmit_lock protection is necessary here.
	   (f.e. loopback and IP tunnels are clean ignoring statistics counters.)
	   However, it is possible, that they rely on protection
	   made by us here.

	   Check this and shot the lock. It is not prone from deadlocks.
	   Either shot noqueue qdisc, it is even simpler 8)
	 */
	if (dev->flags&IFF_UP) {
		int cpu = smp_processor_id();

		if (dev->xmit_lock_owner != cpu) {
			spin_unlock(&dev->queue_lock);
			spin_lock(&dev->xmit_lock);
			dev->xmit_lock_owner = cpu;

			if (!netif_queue_stopped(dev)) {
				if (netdev_nit)
					dev_queue_xmit_nit(skb,dev);

				if (dev->hard_start_xmit(skb, dev) == 0) {
					dev->xmit_lock_owner = -1;
					spin_unlock_bh(&dev->xmit_lock);
					return 0;
				}
			}
			dev->xmit_lock_owner = -1;
			spin_unlock_bh(&dev->xmit_lock);
			if (net_ratelimit())
				printk(KERN_DEBUG "Virtual device %s asks to queue packet!\n", dev->name);
			kfree_skb(skb);
			return -ENETDOWN;
		} else {
			/* Recursion is detected! It is possible, unfortunately */
			if (net_ratelimit())
				printk(KERN_DEBUG "Dead loop on virtual device %s, fix it urgently!\n", dev->name);
		}
	}
	spin_unlock_bh(&dev->queue_lock);

	kfree_skb(skb);
	return -ENETDOWN;
}


/*=======================================================================
			Receiver rotutines
  =======================================================================*/

int netdev_max_backlog = 300;

struct netif_rx_stats netdev_rx_stat[NR_CPUS];


#ifdef CONFIG_NET_HW_FLOWCONTROL
static atomic_t netdev_dropping = ATOMIC_INIT(0);
static unsigned long netdev_fc_mask = 1;
unsigned long netdev_fc_xoff = 0;
spinlock_t netdev_fc_lock = SPIN_LOCK_UNLOCKED;

static struct
{
	void (*stimul)(struct net_device *);
	struct net_device *dev;
} netdev_fc_slots[32];

int netdev_register_fc(struct net_device *dev, void (*stimul)(struct net_device *dev))
{
	int bit = 0;
	unsigned long flags;

	spin_lock_irqsave(&netdev_fc_lock, flags);
	if (netdev_fc_mask != ~0UL) {
		bit = ffz(netdev_fc_mask);
		netdev_fc_slots[bit].stimul = stimul;
		netdev_fc_slots[bit].dev = dev;
		set_bit(bit, &netdev_fc_mask);
		clear_bit(bit, &netdev_fc_xoff);
	}
	spin_unlock_irqrestore(&netdev_fc_lock, flags);
	return bit;
}

void netdev_unregister_fc(int bit)
{
	unsigned long flags;

	spin_lock_irqsave(&netdev_fc_lock, flags);
	if (bit > 0) {
		netdev_fc_slots[bit].stimul = NULL;
		netdev_fc_slots[bit].dev = NULL;
		clear_bit(bit, &netdev_fc_mask);
		clear_bit(bit, &netdev_fc_xoff);
	}
	spin_unlock_irqrestore(&netdev_fc_lock, flags);
}

static void netdev_wakeup(void)
{
	unsigned long xoff;

	spin_lock(&netdev_fc_lock);
	xoff = netdev_fc_xoff;
	netdev_fc_xoff = 0;
	while (xoff) {
		int i = ffz(~xoff);
		xoff &= ~(1<<i);
		netdev_fc_slots[i].stimul(netdev_fc_slots[i].dev);
	}
	spin_unlock(&netdev_fc_lock);
}
#endif

/*
 *	Receive a packet from a device driver and queue it for the upper
 *	(protocol) levels.  It always succeeds. 
 */

void netif_rx(struct sk_buff *skb)
{
	int this_cpu = smp_processor_id();
	struct softnet_data *queue;
	unsigned long flags;

	if (skb->stamp.tv_sec == 0)
		get_fast_time(&skb->stamp);

	/* The code is rearranged so that the path is the most
	   short when CPU is congested, but is still operating.
	 */
	queue = &softnet_data[this_cpu];

	local_irq_save(flags);

	netdev_rx_stat[this_cpu].total++;
	if (queue->input_pkt_queue.qlen <= netdev_max_backlog) {
		if (queue->input_pkt_queue.qlen) {
			if (queue->throttle)
				goto drop;

enqueue:
			if (skb->rx_dev)
				dev_put(skb->rx_dev);
			skb->rx_dev = skb->dev;
			dev_hold(skb->rx_dev);
			__skb_queue_tail(&queue->input_pkt_queue,skb);
			__cpu_raise_softirq(this_cpu, NET_RX_SOFTIRQ);
			local_irq_restore(flags);
			return;
		}

		if (queue->throttle) {
			queue->throttle = 0;
#ifdef CONFIG_NET_HW_FLOWCONTROL
			if (atomic_dec_and_test(&netdev_dropping))
				netdev_wakeup();
#endif
		}
		goto enqueue;
	}

	if (queue->throttle == 0) {
		queue->throttle = 1;
		netdev_rx_stat[this_cpu].throttled++;
#ifdef CONFIG_NET_HW_FLOWCONTROL
		atomic_inc(&netdev_dropping);
#endif
	}

drop:
	netdev_rx_stat[this_cpu].dropped++;
	local_irq_restore(flags);

	kfree_skb(skb);
}

/* Deliver skb to an old protocol, which is not threaded well
   or which do not understand shared skbs.
 */
static void deliver_to_old_ones(struct packet_type *pt, struct sk_buff *skb, int last)
{
	static spinlock_t net_bh_lock = SPIN_LOCK_UNLOCKED;

	if (!last) {
		skb = skb_clone(skb, GFP_ATOMIC);
		if (skb == NULL)
			return;
	}

	/* The assumption (correct one) is that old protocols
	   did not depened on BHs different of NET_BH and TIMER_BH.
	 */

	/* Emulate NET_BH with special spinlock */
	spin_lock(&net_bh_lock);

	/* Disable timers and wait for all timers completion */
	tasklet_disable(bh_task_vec+TIMER_BH);

	pt->func(skb, skb->dev, pt);

	tasklet_enable(bh_task_vec+TIMER_BH);
	spin_unlock(&net_bh_lock);
}

/* Reparent skb to master device. This function is called
 * only from net_rx_action under ptype_lock. It is misuse
 * of ptype_lock, but it is OK for now.
 */
static __inline__ void skb_bond(struct sk_buff *skb)
{
	struct net_device *dev = skb->rx_dev;
	
	if (dev->master) {
		dev_hold(dev->master);
		skb->dev = skb->rx_dev = dev->master;
		dev_put(dev);
	}
}

static void net_tx_action(struct softirq_action *h)
{
	int cpu = smp_processor_id();

	if (softnet_data[cpu].completion_queue) {
		struct sk_buff *clist;

		local_irq_disable();
		clist = softnet_data[cpu].completion_queue;
		softnet_data[cpu].completion_queue = NULL;
		local_irq_enable();

		while (clist != NULL) {
			struct sk_buff *skb = clist;
			clist = clist->next;

			BUG_TRAP(atomic_read(&skb->users) == 0);
			__kfree_skb(skb);
		}
	}

	if (softnet_data[cpu].output_queue) {
		struct net_device *head;

		local_irq_disable();
		head = softnet_data[cpu].output_queue;
		softnet_data[cpu].output_queue = NULL;
		local_irq_enable();

		while (head != NULL) {
			struct net_device *dev = head;
			head = head->next_sched;

			clear_bit(__LINK_STATE_SCHED, &dev->state);

			if (spin_trylock(&dev->queue_lock)) {
				qdisc_run(dev);
				spin_unlock(&dev->queue_lock);
			} else {
				netif_schedule(dev);
			}
		}
	}
}

void net_call_rx_atomic(void (*fn)(void))
{
	write_lock_bh(&ptype_lock);
	fn();
	write_unlock_bh(&ptype_lock);
}

#if defined(CONFIG_BRIDGE) || defined(CONFIG_BRIDGE_MODULE)
void (*br_handle_frame_hook)(struct sk_buff *skb) = NULL;
#endif

static void __inline__ handle_bridge(struct sk_buff *skb,
				     struct packet_type *pt_prev)
{
	if (pt_prev)
		deliver_to_old_ones(pt_prev, skb, 0);
	else {
		atomic_inc(&skb->users);
		pt_prev->func(skb, skb->dev, pt_prev);
	}

	br_handle_frame_hook(skb);
}


static void net_rx_action(struct softirq_action *h)
{
	int this_cpu = smp_processor_id();
	struct softnet_data *queue = &softnet_data[this_cpu];
	unsigned long start_time = jiffies;
	int bugdet = netdev_max_backlog;

	read_lock(&ptype_lock);

	for (;;) {
		struct sk_buff *skb;

		local_irq_disable();
		skb = __skb_dequeue(&queue->input_pkt_queue);
		local_irq_enable();

		if (skb == NULL)
			break;

		skb_bond(skb);

#ifdef CONFIG_NET_FASTROUTE
		if (skb->pkt_type == PACKET_FASTROUTE) {
			netdev_rx_stat[this_cpu].fastroute_deferred_out++;
			dev_queue_xmit(skb);
			continue;
		}
#endif
		skb->h.raw = skb->nh.raw = skb->data;
		{
			struct packet_type *ptype, *pt_prev;
			unsigned short type = skb->protocol;

			pt_prev = NULL;
			for (ptype = ptype_all; ptype; ptype = ptype->next) {
				if (!ptype->dev || ptype->dev == skb->dev) {
					if (pt_prev) {
						if (!pt_prev->data) {
							deliver_to_old_ones(pt_prev, skb, 0);
						} else {
							atomic_inc(&skb->users);
							pt_prev->func(skb,
								      skb->dev,
								      pt_prev);
						}
					}
					pt_prev = ptype;
				}
			}

#if defined(CONFIG_BRIDGE) || defined(CONFIG_BRIDGE_MODULE)
			if (skb->dev->br_port != NULL &&
			    br_handle_frame_hook != NULL) {
				handle_bridge(skb, pt_prev);
				continue;
			}
#endif

			for (ptype=ptype_base[ntohs(type)&15];ptype;ptype=ptype->next) {
				if (ptype->type == type &&
				    (!ptype->dev || ptype->dev == skb->dev)) {
					if (pt_prev) {
						if (!pt_prev->data)
							deliver_to_old_ones(pt_prev, skb, 0);
						else {
							atomic_inc(&skb->users);
							pt_prev->func(skb,
								      skb->dev,
								      pt_prev);
						}
					}
					pt_prev = ptype;
				}
			}

			if (pt_prev) {
				if (!pt_prev->data)
					deliver_to_old_ones(pt_prev, skb, 1);
				else
					pt_prev->func(skb, skb->dev, pt_prev);
			} else
				kfree_skb(skb);
		}

		if (bugdet-- < 0 || jiffies - start_time > 1)
			goto softnet_break;
	}
	read_unlock(&ptype_lock);

	local_irq_disable();
	if (queue->throttle) {
		queue->throttle = 0;
#ifdef CONFIG_NET_HW_FLOWCONTROL
		if (atomic_dec_and_test(&netdev_dropping))
			netdev_wakeup();
#endif
	}
	local_irq_enable();

	NET_PROFILE_LEAVE(softnet_process);
	return;

softnet_break:
	read_unlock(&ptype_lock);

	local_irq_disable();
	netdev_rx_stat[this_cpu].time_squeeze++;
	__cpu_raise_softirq(this_cpu, NET_RX_SOFTIRQ);
	local_irq_enable();

	NET_PROFILE_LEAVE(softnet_process);
	return;
}

/* Protocol dependent address dumping routines */

static gifconf_func_t * gifconf_list [NPROTO];

int register_gifconf(unsigned int family, gifconf_func_t * gifconf)
{
	if (family>=NPROTO)
		return -EINVAL;
	gifconf_list[family] = gifconf;
	return 0;
}


/*
 *	Map an interface index to its name (SIOCGIFNAME)
 */

/*
 *	We need this ioctl for efficient implementation of the
 *	if_indextoname() function required by the IPv6 API.  Without
 *	it, we would have to search all the interfaces to find a
 *	match.  --pb
 */

static int dev_ifname(struct ifreq *arg)
{
	struct net_device *dev;
	struct ifreq ifr;

	/*
	 *	Fetch the caller's info block. 
	 */
	
	if (copy_from_user(&ifr, arg, sizeof(struct ifreq)))
		return -EFAULT;

	read_lock(&dev_base_lock);
	dev = __dev_get_by_index(ifr.ifr_ifindex);
	if (!dev) {
		read_unlock(&dev_base_lock);
		return -ENODEV;
	}

	strcpy(ifr.ifr_name, dev->name);
	read_unlock(&dev_base_lock);

	if (copy_to_user(arg, &ifr, sizeof(struct ifreq)))
		return -EFAULT;
	return 0;
}

/*
 *	Perform a SIOCGIFCONF call. This structure will change
 *	size eventually, and there is nothing I can do about it.
 *	Thus we will need a 'compatibility mode'.
 */

static int dev_ifconf(char *arg)
{
	struct ifconf ifc;
	struct net_device *dev;
	char *pos;
	int len;
	int total;
	int i;

	/*
	 *	Fetch the caller's info block. 
	 */
	
	if (copy_from_user(&ifc, arg, sizeof(struct ifconf)))
		return -EFAULT;

	pos = ifc.ifc_buf;
	len = ifc.ifc_len;

	/*
	 *	Loop over the interfaces, and write an info block for each. 
	 */

	total = 0;
	for (dev = dev_base; dev != NULL; dev = dev->next) {
		for (i=0; i<NPROTO; i++) {
			if (gifconf_list[i]) {
				int done;
				if (pos==NULL) {
					done = gifconf_list[i](dev, NULL, 0);
				} else {
					done = gifconf_list[i](dev, pos+total, len-total);
				}
				if (done<0) {
					return -EFAULT;
				}
				total += done;
			}
		}
  	}

	/*
	 *	All done.  Write the updated control block back to the caller. 
	 */
	ifc.ifc_len = total;

	if (copy_to_user(arg, &ifc, sizeof(struct ifconf)))
		return -EFAULT; 

	/* 
	 * 	Both BSD and Solaris return 0 here, so we do too.
	 */
	return 0;
}

/*
 *	This is invoked by the /proc filesystem handler to display a device
 *	in detail.
 */

#ifdef CONFIG_PROC_FS

static int sprintf_stats(char *buffer, struct net_device *dev)
{
	struct net_device_stats *stats = (dev->get_stats ? dev->get_stats(dev): NULL);
	int size;
	
	if (stats)
		size = sprintf(buffer, "%6s:%8lu %7lu %4lu %4lu %4lu %5lu %10lu %9lu %8lu %7lu %4lu %4lu %4lu %5lu %7lu %10lu\n",
 		   dev->name,
		   stats->rx_bytes,
		   stats->rx_packets, stats->rx_errors,
		   stats->rx_dropped + stats->rx_missed_errors,
		   stats->rx_fifo_errors,
		   stats->rx_length_errors + stats->rx_over_errors
		   + stats->rx_crc_errors + stats->rx_frame_errors,
		   stats->rx_compressed, stats->multicast,
		   stats->tx_bytes,
		   stats->tx_packets, stats->tx_errors, stats->tx_dropped,
		   stats->tx_fifo_errors, stats->collisions,
		   stats->tx_carrier_errors + stats->tx_aborted_errors
		   + stats->tx_window_errors + stats->tx_heartbeat_errors,
		   stats->tx_compressed);
	else
		size = sprintf(buffer, "%6s: No statistics available.\n", dev->name);

	return size;
}

/*
 *	Called from the PROCfs module. This now uses the new arbitrary sized /proc/net interface
 *	to create /proc/net/dev
 */
 
static int dev_get_info(char *buffer, char **start, off_t offset, int length)
{
	int len = 0;
	off_t begin = 0;
	off_t pos = 0;
	int size;
	struct net_device *dev;


	size = sprintf(buffer, 
		"Inter-|   Receive                                                |  Transmit\n"
		" face |bytes    packets errs drop fifo frame compressed multicast|bytes    packets errs drop fifo colls carrier compressed\n");
	
	pos += size;
	len += size;
	

	read_lock(&dev_base_lock);
	for (dev = dev_base; dev != NULL; dev = dev->next) {
		size = sprintf_stats(buffer+len, dev);
		len += size;
		pos = begin + len;
				
		if (pos < offset) {
			len = 0;
			begin = pos;
		}
		if (pos > offset + length)
			break;
	}
	read_unlock(&dev_base_lock);

	*start = buffer + (offset - begin);	/* Start of wanted data */
	len -= (offset - begin);		/* Start slop */
	if (len > length)
		len = length;			/* Ending slop */
	if (len < 0)
		len = 0;
	return len;
}

static int dev_proc_stats(char *buffer, char **start, off_t offset,
			  int length, int *eof, void *data)
{
	int i;
	int len=0;

	for (i=0; i<smp_num_cpus; i++) {
		len += sprintf(buffer+len, "%08x %08x %08x %08x %08x %08x %08x %08x %08x\n",
			       netdev_rx_stat[i].total,
			       netdev_rx_stat[i].dropped,
			       netdev_rx_stat[i].time_squeeze,
			       netdev_rx_stat[i].throttled,
			       netdev_rx_stat[i].fastroute_hit,
			       netdev_rx_stat[i].fastroute_success,
			       netdev_rx_stat[i].fastroute_defer,
			       netdev_rx_stat[i].fastroute_deferred_out,
#if 0
			       netdev_rx_stat[i].fastroute_latency_reduction
#else
			       netdev_rx_stat[i].cpu_collision
#endif
			       );
	}

	len -= offset;

	if (len > length)
		len = length;
	if (len < 0)
		len = 0;

	*start = buffer + offset;
	*eof = 1;

	return len;
}

#endif	/* CONFIG_PROC_FS */


#ifdef WIRELESS_EXT
#ifdef CONFIG_PROC_FS

/*
 * Print one entry of /proc/net/wireless
 * This is a clone of /proc/net/dev (just above)
 */
static int sprintf_wireless_stats(char *buffer, struct net_device *dev)
{
	/* Get stats from the driver */
	struct iw_statistics *stats = (dev->get_wireless_stats ?
				       dev->get_wireless_stats(dev) :
				       (struct iw_statistics *) NULL);
	int size;

	if (stats != (struct iw_statistics *) NULL) {
		size = sprintf(buffer,
			       "%6s: %04x  %3d%c  %3d%c  %3d%c  %6d %6d %6d\n",
			       dev->name,
			       stats->status,
			       stats->qual.qual,
			       stats->qual.updated & 1 ? '.' : ' ',
			       stats->qual.level,
			       stats->qual.updated & 2 ? '.' : ' ',
			       stats->qual.noise,
			       stats->qual.updated & 4 ? '.' : ' ',
			       stats->discard.nwid,
			       stats->discard.code,
			       stats->discard.misc);
		stats->qual.updated = 0;
	}
	else
		size = 0;

	return size;
}

/*
 * Print info for /proc/net/wireless (print all entries)
 * This is a clone of /proc/net/dev (just above)
 */
static int dev_get_wireless_info(char * buffer, char **start, off_t offset,
			  int length)
{
	int		len = 0;
	off_t		begin = 0;
	off_t		pos = 0;
	int		size;
	
	struct net_device *	dev;

	size = sprintf(buffer,
		       "Inter-| sta-|   Quality        |   Discarded packets\n"
		       " face | tus | link level noise |  nwid  crypt   misc\n"
			);
	
	pos += size;
	len += size;

	read_lock(&dev_base_lock);
	for (dev = dev_base; dev != NULL; dev = dev->next) {
		size = sprintf_wireless_stats(buffer + len, dev);
		len += size;
		pos = begin + len;

		if (pos < offset) {
			len = 0;
			begin = pos;
		}
		if (pos > offset + length)
			break;
	}
	read_unlock(&dev_base_lock);

	*start = buffer + (offset - begin);	/* Start of wanted data */
	len -= (offset - begin);		/* Start slop */
	if (len > length)
		len = length;			/* Ending slop */
	if (len < 0)
		len = 0;

	return len;
}
#endif	/* CONFIG_PROC_FS */
#endif	/* WIRELESS_EXT */

int netdev_set_master(struct net_device *slave, struct net_device *master)
{
	struct net_device *old = slave->master;

	ASSERT_RTNL();

	if (master) {
		if (old)
			return -EBUSY;
		dev_hold(master);
	}

	write_lock_bh(&ptype_lock);
	slave->master = master;
	write_unlock_bh(&ptype_lock);

	if (old)
		dev_put(old);

	if (master)
		slave->flags |= IFF_SLAVE;
	else
		slave->flags &= ~IFF_SLAVE;

	rtmsg_ifinfo(RTM_NEWLINK, slave, IFF_SLAVE);
	return 0;
}

void dev_set_promiscuity(struct net_device *dev, int inc)
{
	unsigned short old_flags = dev->flags;

	dev->flags |= IFF_PROMISC;
	if ((dev->promiscuity += inc) == 0)
		dev->flags &= ~IFF_PROMISC;
	if (dev->flags^old_flags) {
#ifdef CONFIG_NET_FASTROUTE
		if (dev->flags&IFF_PROMISC) {
			netdev_fastroute_obstacles++;
			dev_clear_fastroute(dev);
		} else
			netdev_fastroute_obstacles--;
#endif
		dev_mc_upload(dev);
		printk(KERN_INFO "device %s %s promiscuous mode\n",
		       dev->name, (dev->flags&IFF_PROMISC) ? "entered" : "left");
	}
}

void dev_set_allmulti(struct net_device *dev, int inc)
{
	unsigned short old_flags = dev->flags;

	dev->flags |= IFF_ALLMULTI;
	if ((dev->allmulti += inc) == 0)
		dev->flags &= ~IFF_ALLMULTI;
	if (dev->flags^old_flags)
		dev_mc_upload(dev);
}

int dev_change_flags(struct net_device *dev, unsigned flags)
{
	int ret;
	int old_flags = dev->flags;

	/*
	 *	Set the flags on our device.
	 */

	dev->flags = (flags & (IFF_DEBUG|IFF_NOTRAILERS|IFF_NOARP|IFF_DYNAMIC|
			       IFF_MULTICAST|IFF_PORTSEL|IFF_AUTOMEDIA)) |
				       (dev->flags & (IFF_UP|IFF_VOLATILE|IFF_PROMISC|IFF_ALLMULTI));

	/*
	 *	Load in the correct multicast list now the flags have changed.
	 */				

	dev_mc_upload(dev);

	/*
	 *	Have we downed the interface. We handle IFF_UP ourselves
	 *	according to user attempts to set it, rather than blindly
	 *	setting it.
	 */

	ret = 0;
	if ((old_flags^flags)&IFF_UP)	/* Bit is different  ? */
	{
		ret = ((old_flags & IFF_UP) ? dev_close : dev_open)(dev);

		if (ret == 0) 
			dev_mc_upload(dev);
	}

	if (dev->flags&IFF_UP &&
	    ((old_flags^dev->flags)&~(IFF_UP|IFF_PROMISC|IFF_ALLMULTI|IFF_VOLATILE)))
		notifier_call_chain(&netdev_chain, NETDEV_CHANGE, dev);

	if ((flags^dev->gflags)&IFF_PROMISC) {
		int inc = (flags&IFF_PROMISC) ? +1 : -1;
		dev->gflags ^= IFF_PROMISC;
		dev_set_promiscuity(dev, inc);
	}

	/* NOTE: order of synchronization of IFF_PROMISC and IFF_ALLMULTI
	   is important. Some (broken) drivers set IFF_PROMISC, when
	   IFF_ALLMULTI is requested not asking us and not reporting.
	 */
	if ((flags^dev->gflags)&IFF_ALLMULTI) {
		int inc = (flags&IFF_ALLMULTI) ? +1 : -1;
		dev->gflags ^= IFF_ALLMULTI;
		dev_set_allmulti(dev, inc);
	}

	if (old_flags^dev->flags)
		rtmsg_ifinfo(RTM_NEWLINK, dev, old_flags^dev->flags);

	return ret;
}

/*
 *	Perform the SIOCxIFxxx calls. 
 */
 
static int dev_ifsioc(struct ifreq *ifr, unsigned int cmd)
{
	struct net_device *dev;
	int err;

	if ((dev = __dev_get_by_name(ifr->ifr_name)) == NULL)
		return -ENODEV;

	switch(cmd) 
	{
		case SIOCGIFFLAGS:	/* Get interface flags */
			ifr->ifr_flags = (dev->flags&~(IFF_PROMISC|IFF_ALLMULTI|IFF_RUNNING))
				|(dev->gflags&(IFF_PROMISC|IFF_ALLMULTI));
			if (netif_running(dev))
				ifr->ifr_flags |= IFF_RUNNING;
			return 0;

		case SIOCSIFFLAGS:	/* Set interface flags */
			return dev_change_flags(dev, ifr->ifr_flags);
		
		case SIOCGIFMETRIC:	/* Get the metric on the interface (currently unused) */
			ifr->ifr_metric = 0;
			return 0;
			
		case SIOCSIFMETRIC:	/* Set the metric on the interface (currently unused) */
			return -EOPNOTSUPP;
	
		case SIOCGIFMTU:	/* Get the MTU of a device */
			ifr->ifr_mtu = dev->mtu;
			return 0;
	
		case SIOCSIFMTU:	/* Set the MTU of a device */
			if (ifr->ifr_mtu == dev->mtu)
				return 0;

			/*
			 *	MTU must be positive.
			 */
			 
			if (ifr->ifr_mtu<0)
				return -EINVAL;

			if (!netif_device_present(dev))
				return -ENODEV;

			if (dev->change_mtu)
				err = dev->change_mtu(dev, ifr->ifr_mtu);
			else {
				dev->mtu = ifr->ifr_mtu;
				err = 0;
			}
			if (!err && dev->flags&IFF_UP)
				notifier_call_chain(&netdev_chain, NETDEV_CHANGEMTU, dev);
			return err;

		case SIOCGIFHWADDR:
			memcpy(ifr->ifr_hwaddr.sa_data,dev->dev_addr, MAX_ADDR_LEN);
			ifr->ifr_hwaddr.sa_family=dev->type;
			return 0;
				
		case SIOCSIFHWADDR:
			if (dev->set_mac_address == NULL)
				return -EOPNOTSUPP;
			if (ifr->ifr_hwaddr.sa_family!=dev->type)
				return -EINVAL;
			if (!netif_device_present(dev))
				return -ENODEV;
			err = dev->set_mac_address(dev, &ifr->ifr_hwaddr);
			if (!err)
				notifier_call_chain(&netdev_chain, NETDEV_CHANGEADDR, dev);
			return err;
			
		case SIOCSIFHWBROADCAST:
			if (ifr->ifr_hwaddr.sa_family!=dev->type)
				return -EINVAL;
			memcpy(dev->broadcast, ifr->ifr_hwaddr.sa_data, MAX_ADDR_LEN);
			notifier_call_chain(&netdev_chain, NETDEV_CHANGEADDR, dev);
			return 0;

		case SIOCGIFMAP:
			ifr->ifr_map.mem_start=dev->mem_start;
			ifr->ifr_map.mem_end=dev->mem_end;
			ifr->ifr_map.base_addr=dev->base_addr;
			ifr->ifr_map.irq=dev->irq;
			ifr->ifr_map.dma=dev->dma;
			ifr->ifr_map.port=dev->if_port;
			return 0;
			
		case SIOCSIFMAP:
			if (dev->set_config) {
				if (!netif_device_present(dev))
					return -ENODEV;
				return dev->set_config(dev,&ifr->ifr_map);
			}
			return -EOPNOTSUPP;
			
		case SIOCADDMULTI:
			if (dev->set_multicast_list == NULL ||
			    ifr->ifr_hwaddr.sa_family != AF_UNSPEC)
				return -EINVAL;
			if (!netif_device_present(dev))
				return -ENODEV;
			dev_mc_add(dev,ifr->ifr_hwaddr.sa_data, dev->addr_len, 1);
			return 0;

		case SIOCDELMULTI:
			if (dev->set_multicast_list == NULL ||
			    ifr->ifr_hwaddr.sa_family!=AF_UNSPEC)
				return -EINVAL;
			if (!netif_device_present(dev))
				return -ENODEV;
			dev_mc_delete(dev,ifr->ifr_hwaddr.sa_data,dev->addr_len, 1);
			return 0;

		case SIOCGIFINDEX:
			ifr->ifr_ifindex = dev->ifindex;
			return 0;

		case SIOCGIFTXQLEN:
			ifr->ifr_qlen = dev->tx_queue_len;
			return 0;

		case SIOCSIFTXQLEN:
			if (ifr->ifr_qlen<0)
				return -EINVAL;
			dev->tx_queue_len = ifr->ifr_qlen;
			return 0;

		case SIOCSIFNAME:
			if (dev->flags&IFF_UP)
				return -EBUSY;
			if (__dev_get_by_name(ifr->ifr_newname))
				return -EEXIST;
			memcpy(dev->name, ifr->ifr_newname, IFNAMSIZ);
			dev->name[IFNAMSIZ-1] = 0;
			notifier_call_chain(&netdev_chain, NETDEV_CHANGENAME, dev);
			return 0;

		/*
		 *	Unknown or private ioctl
		 */

		default:
			if (cmd >= SIOCDEVPRIVATE &&
			    cmd <= SIOCDEVPRIVATE + 15) {
				if (dev->do_ioctl) {
					if (!netif_device_present(dev))
						return -ENODEV;
					return dev->do_ioctl(dev, ifr, cmd);
				}
				return -EOPNOTSUPP;
			}

#ifdef WIRELESS_EXT
			if (cmd >= SIOCIWFIRST && cmd <= SIOCIWLAST) {
				if (dev->do_ioctl) {
					if (!netif_device_present(dev))
						return -ENODEV;
					return dev->do_ioctl(dev, ifr, cmd);
				}
				return -EOPNOTSUPP;
			}
#endif	/* WIRELESS_EXT */

	}
	return -EINVAL;
}


/*
 *	This function handles all "interface"-type I/O control requests. The actual
 *	'doing' part of this is dev_ifsioc above.
 */

int dev_ioctl(unsigned int cmd, void *arg)
{
	struct ifreq ifr;
	int ret;
	char *colon;

	/* One special case: SIOCGIFCONF takes ifconf argument
	   and requires shared lock, because it sleeps writing
	   to user space.
	 */
	   
	if (cmd == SIOCGIFCONF) {
		rtnl_shlock();
		ret = dev_ifconf((char *) arg);
		rtnl_shunlock();
		return ret;
	}
	if (cmd == SIOCGIFNAME) {
		return dev_ifname((struct ifreq *)arg);
	}

	if (copy_from_user(&ifr, arg, sizeof(struct ifreq)))
		return -EFAULT;

	ifr.ifr_name[IFNAMSIZ-1] = 0;

	colon = strchr(ifr.ifr_name, ':');
	if (colon)
		*colon = 0;

	/*
	 *	See which interface the caller is talking about. 
	 */
	 
	switch(cmd) 
	{
		/*
		 *	These ioctl calls:
		 *	- can be done by all.
		 *	- atomic and do not require locking.
		 *	- return a value
		 */
		 
		case SIOCGIFFLAGS:
		case SIOCGIFMETRIC:
		case SIOCGIFMTU:
		case SIOCGIFHWADDR:
		case SIOCGIFSLAVE:
		case SIOCGIFMAP:
		case SIOCGIFINDEX:
		case SIOCGIFTXQLEN:
			dev_load(ifr.ifr_name);
			read_lock(&dev_base_lock);
			ret = dev_ifsioc(&ifr, cmd);
			read_unlock(&dev_base_lock);
			if (!ret) {
				if (colon)
					*colon = ':';
				if (copy_to_user(arg, &ifr, sizeof(struct ifreq)))
					return -EFAULT;
			}
			return ret;

		/*
		 *	These ioctl calls:
		 *	- require superuser power.
		 *	- require strict serialization.
		 *	- do not return a value
		 */
		 
		case SIOCSIFFLAGS:
		case SIOCSIFMETRIC:
		case SIOCSIFMTU:
		case SIOCSIFMAP:
		case SIOCSIFHWADDR:
		case SIOCSIFSLAVE:
		case SIOCADDMULTI:
		case SIOCDELMULTI:
		case SIOCSIFHWBROADCAST:
		case SIOCSIFTXQLEN:
		case SIOCSIFNAME:
			if (!capable(CAP_NET_ADMIN))
				return -EPERM;
			dev_load(ifr.ifr_name);
			rtnl_lock();
			ret = dev_ifsioc(&ifr, cmd);
			rtnl_unlock();
			return ret;
	
		case SIOCGIFMEM:
			/* Get the per device memory space. We can add this but currently
			   do not support it */
		case SIOCSIFMEM:
			/* Set the per device memory buffer space. Not applicable in our case */
		case SIOCSIFLINK:
			return -EINVAL;

		/*
		 *	Unknown or private ioctl.
		 */	
		 
		default:
			if (cmd >= SIOCDEVPRIVATE &&
			    cmd <= SIOCDEVPRIVATE + 15) {
				dev_load(ifr.ifr_name);
				rtnl_lock();
				ret = dev_ifsioc(&ifr, cmd);
				rtnl_unlock();
				if (!ret && copy_to_user(arg, &ifr, sizeof(struct ifreq)))
					return -EFAULT;
				return ret;
			}
#ifdef WIRELESS_EXT
			if (cmd >= SIOCIWFIRST && cmd <= SIOCIWLAST) {
				dev_load(ifr.ifr_name);
				if (IW_IS_SET(cmd)) {
					if (!suser())
						return -EPERM;
				}
				rtnl_lock();
				ret = dev_ifsioc(&ifr, cmd);
				rtnl_unlock();
				if (!ret && IW_IS_GET(cmd) &&
				    copy_to_user(arg, &ifr, sizeof(struct ifreq)))
					return -EFAULT;
				return ret;
			}
#endif	/* WIRELESS_EXT */
			return -EINVAL;
	}
}

int dev_new_index(void)
{
	static int ifindex;
	for (;;) {
		if (++ifindex <= 0)
			ifindex=1;
		if (__dev_get_by_index(ifindex) == NULL)
			return ifindex;
	}
}

static int dev_boot_phase = 1;


int register_netdevice(struct net_device *dev)
{
	struct net_device *d, **dp;

	spin_lock_init(&dev->queue_lock);
	spin_lock_init(&dev->xmit_lock);
	dev->xmit_lock_owner = -1;
#ifdef CONFIG_NET_FASTROUTE
	dev->fastpath_lock=RW_LOCK_UNLOCKED;
#endif

	if (dev_boot_phase) {
		/* This is NOT bug, but I am not sure, that all the
		   devices, initialized before netdev module is started
		   are sane. 

		   Now they are chained to device boot list
		   and probed later. If a module is initialized
		   before netdev, but assumes that dev->init
		   is really called by register_netdev(), it will fail.

		   So that this message should be printed for a while.
		 */
		printk(KERN_INFO "early initialization of device %s is deferred\n", dev->name);

		/* Check for existence, and append to tail of chain */
		for (dp=&dev_base; (d=*dp) != NULL; dp=&d->next) {
			if (d == dev || strcmp(d->name, dev->name) == 0) {
				return -EEXIST;
			}
		}
		dev->next = NULL;
		write_lock_bh(&dev_base_lock);
		*dp = dev;
		dev_hold(dev);
		write_unlock_bh(&dev_base_lock);

		/*
		 *	Default initial state at registry is that the
		 *	device is present.
		 */

		set_bit(__LINK_STATE_PRESENT, &dev->state);

		return 0;
	}

	dev->iflink = -1;

	/* Init, if this function is available */
	if (dev->init && dev->init(dev) != 0)
		return -EIO;

	dev->ifindex = dev_new_index();
	if (dev->iflink == -1)
		dev->iflink = dev->ifindex;

	/* Check for existence, and append to tail of chain */
	for (dp=&dev_base; (d=*dp) != NULL; dp=&d->next) {
		if (d == dev || strcmp(d->name, dev->name) == 0) {
			return -EEXIST;
		}
	}
	/*
	 *	nil rebuild_header routine,
	 *	that should be never called and used as just bug trap.
	 */

	if (dev->rebuild_header == NULL)
		dev->rebuild_header = default_rebuild_header;

	/*
	 *	Default initial state at registry is that the
	 *	device is present.
	 */

	set_bit(__LINK_STATE_PRESENT, &dev->state);

	dev->next = NULL;
	dev_init_scheduler(dev);
	write_lock_bh(&dev_base_lock);
	*dp = dev;
	dev_hold(dev);
	dev->deadbeaf = 0;
	write_unlock_bh(&dev_base_lock);

	/* Notify protocols, that a new device appeared. */
	notifier_call_chain(&netdev_chain, NETDEV_REGISTER, dev);

	return 0;
}

int netdev_finish_unregister(struct net_device *dev)
{
	BUG_TRAP(dev->ip_ptr==NULL);
	BUG_TRAP(dev->ip6_ptr==NULL);
	BUG_TRAP(dev->dn_ptr==NULL);

	if (!dev->deadbeaf) {
		printk("Freeing alive device %p, %s\n", dev, dev->name);
		return 0;
	}
#ifdef NET_REFCNT_DEBUG
	printk(KERN_DEBUG "netdev_finish_unregister: %s%s.\n", dev->name, dev->new_style?"":", old style");
#endif
	if (dev->destructor)
		dev->destructor(dev);
	if (dev->new_style)
		kfree(dev);
	return 0;
}

int unregister_netdevice(struct net_device *dev)
{
	unsigned long now;
	struct net_device *d, **dp;

	/* If device is running, close it first. */
	if (dev->flags & IFF_UP)
		dev_close(dev);

	BUG_TRAP(dev->deadbeaf==0);
	dev->deadbeaf = 1;

	/* And unlink it from device chain. */
	for (dp = &dev_base; (d=*dp) != NULL; dp=&d->next) {
		if (d == dev) {
			write_lock_bh(&dev_base_lock);
			*dp = d->next;
			write_unlock_bh(&dev_base_lock);
			break;
		}
	}
	if (d == NULL) {
		printk(KERN_DEBUG "unregister_netdevice: device %s/%p never was registered\n", dev->name, dev);
		return -ENODEV;
	}

	if (dev_boot_phase == 0) {
#ifdef CONFIG_NET_FASTROUTE
		dev_clear_fastroute(dev);
#endif

		/* Shutdown queueing discipline. */
		dev_shutdown(dev);

		/* Notify protocols, that we are about to destroy
		   this device. They should clean all the things.
		 */
		notifier_call_chain(&netdev_chain, NETDEV_UNREGISTER, dev);

		/*
		 *	Flush the multicast chain
		 */
		dev_mc_discard(dev);
	}

	if (dev->uninit)
		dev->uninit(dev);

	/* Notifier chain MUST detach us from master device. */
	BUG_TRAP(dev->master==NULL);

	if (dev->new_style) {
#ifdef NET_REFCNT_DEBUG
		if (atomic_read(&dev->refcnt) != 1)
			printk(KERN_DEBUG "unregister_netdevice: holding %s refcnt=%d\n", dev->name, atomic_read(&dev->refcnt)-1);
#endif
		dev_put(dev);
		return 0;
	}

	/* Last reference is our one */
	if (atomic_read(&dev->refcnt) == 1) {
		dev_put(dev);
		return 0;
	}

#ifdef NET_REFCNT_DEBUG
	printk("unregister_netdevice: waiting %s refcnt=%d\n", dev->name, atomic_read(&dev->refcnt));
#endif

	/* EXPLANATION. If dev->refcnt is not 1 now (1 is our own reference)
	   it means that someone in the kernel still has reference
	   to this device and we cannot release it.

	   "New style" devices have destructors, hence we can return from this
	   function and destructor will do all the work later.

	   "Old style" devices expect that device is free of any references
	   upon exit from this function. WE CANNOT MAKE such release
	   without delay. Note that it is not new feature. Referencing devices
	   after they are released occured in 2.0 and 2.2.
	   Now we just can know about each fact of illegal usage.

	   So, we linger for 10*HZ (it is an arbitrary number)

	   After 1 second, we start to rebroadcast unregister notifications
	   in hope that careless clients will release the device.

	   If timeout expired, we have no choice how to cross fingers
	   and return. Real alternative would be block here forever
	   and we will make it eventually, when all peaceful citizens
	   will be notified and repaired.
	 */

	now = jiffies;
	while (atomic_read(&dev->refcnt) != 1) {
		if ((jiffies - now) > 1*HZ) {
			/* Rebroadcast unregister notification */
			notifier_call_chain(&netdev_chain, NETDEV_UNREGISTER, dev);
		}
		current->state = TASK_INTERRUPTIBLE;
		schedule_timeout(HZ/4);
		current->state = TASK_RUNNING;
		if ((jiffies - now) > 10*HZ)
			break;
	}

	if (atomic_read(&dev->refcnt) != 1)
		printk("unregister_netdevice: Old style device %s leaked(refcnt=%d). Wait for crash.\n", dev->name, atomic_read(&dev->refcnt)-1);
	dev_put(dev);
	return 0;
}


/*
 *	Initialize the DEV module. At boot time this walks the device list and
 *	unhooks any devices that fail to initialise (normally hardware not 
 *	present) and leaves us with a valid list of present and active devices.
 *
 */

extern void net_device_init(void);
extern void ip_auto_config(void);

int __init net_dev_init(void)
{
	struct net_device *dev, **dp;
	int i;

#ifdef CONFIG_NET_SCHED
	pktsched_init();
#endif

	/*
	 *	Initialise the packet receive queues.
	 */

	for (i = 0; i < NR_CPUS; i++) {
		struct softnet_data *queue;

		queue = &softnet_data[i];
		skb_queue_head_init(&queue->input_pkt_queue);
		queue->throttle = 0;
		queue->completion_queue = NULL;
	}
	
#ifdef CONFIG_NET_PROFILE
	net_profile_init();
	NET_PROFILE_REGISTER(dev_queue_xmit);
	NET_PROFILE_REGISTER(softnet_process);
#endif
	/*
	 *	Add the devices.
	 *	If the call to dev->init fails, the dev is removed
	 *	from the chain disconnecting the device until the
	 *	next reboot.
	 *
	 *	NB At boot phase networking is dead. No locking is required.
	 *	But we still preserve dev_base_lock for sanity.
	 */

	dp = &dev_base;
	while ((dev = *dp) != NULL) {
		spin_lock_init(&dev->queue_lock);
		spin_lock_init(&dev->xmit_lock);
#ifdef CONFIG_NET_FASTROUTE
		dev->fastpath_lock = RW_LOCK_UNLOCKED;
#endif
		dev->xmit_lock_owner = -1;
		dev->iflink = -1;
		dev_hold(dev);
		/*
		 *	We can allocate the name ahead of time. If the
		 *	init fails the name will be reissued correctly.
		 */
		if (strchr(dev->name, '%'))
			dev_alloc_name(dev, dev->name);
		if (dev->init && dev->init(dev)) {
			/*
			 *	It failed to come up. Unhook it.
			 */
			write_lock_bh(&dev_base_lock);
			*dp = dev->next;
			dev->deadbeaf = 1;
			write_unlock_bh(&dev_base_lock);
			dev_put(dev);
		} else {
			dp = &dev->next;
			dev->ifindex = dev_new_index();
			if (dev->iflink == -1)
				dev->iflink = dev->ifindex;
			if (dev->rebuild_header == NULL)
				dev->rebuild_header = default_rebuild_header;
			dev_init_scheduler(dev);
		}
	}

#ifdef CONFIG_PROC_FS
	proc_net_create("dev", 0, dev_get_info);
	create_proc_read_entry("net/softnet_stat", 0, 0, dev_proc_stats, NULL);
#ifdef WIRELESS_EXT
	proc_net_create("wireless", 0, dev_get_wireless_info);
#endif	/* WIRELESS_EXT */
#endif	/* CONFIG_PROC_FS */

	dev_boot_phase = 0;

	open_softirq(NET_TX_SOFTIRQ, net_tx_action, NULL);
	open_softirq(NET_RX_SOFTIRQ, net_rx_action, NULL);

	dst_init();
	dev_mcast_init();

	/*
	 *	Initialise network devices
	 */
	 
	net_device_init();

	return 0;
}
