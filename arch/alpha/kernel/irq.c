/*
 *	linux/arch/alpha/kernel/irq.c
 *
 *	Copyright (C) 1995 Linus Torvalds
 *
 * This file contains the code used by various IRQ handling routines:
 * asking for different IRQ's should be done through these routines
 * instead of just grabbing them. Thus setups with different IRQ numbers
 * shouldn't result in any weird surprises, and installing new handlers
 * should be easier.
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/ptrace.h>
#include <linux/errno.h>
#include <linux/kernel_stat.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/malloc.h>
#include <linux/random.h>
#include <linux/init.h>
#include <linux/delay.h>

#include <asm/system.h>
#include <asm/io.h>
#include <asm/dma.h>
#include <asm/bitops.h>
#include <asm/machvec.h>

#include "proto.h"
#include "irq_impl.h"

#define vulp	volatile unsigned long *
#define vuip	volatile unsigned int *

/* Only uniprocessor needs this IRQ/BH locking depth, on SMP it lives
   in the per-cpu structure for cache reasons.  */
#ifndef CONFIG_SMP
int __local_irq_count;
int __local_bh_count;
unsigned long __irq_attempt[NR_IRQS];
#endif

#ifdef CONFIG_ALPHA_GENERIC
#define ACTUAL_NR_IRQS	alpha_mv.nr_irqs
#else
#define ACTUAL_NR_IRQS	NR_IRQS
#endif

/* Hack minimum IPL during interupt processing for broken hardware.  */

#ifdef CONFIG_ALPHA_BROKEN_IRQ_MASK
int __min_ipl;
#endif

/*
 * Performance counter hook.  A module can override this to
 * do something useful.
 */

static void
dummy_perf(unsigned long vector, struct pt_regs *regs)
{
        printk(KERN_CRIT "Performance counter interrupt!\n");
}

void (*perf_irq)(unsigned long, struct pt_regs *) = dummy_perf;

/*
 * Dispatch device interrupts.
 */

/*
 * Handle ISA interrupt via the PICs.
 */

#if defined(CONFIG_ALPHA_GENERIC)
# define IACK_SC	alpha_mv.iack_sc
#elif defined(CONFIG_ALPHA_APECS)
# define IACK_SC	APECS_IACK_SC
#elif defined(CONFIG_ALPHA_LCA)
# define IACK_SC	LCA_IACK_SC
#elif defined(CONFIG_ALPHA_CIA)
# define IACK_SC	CIA_IACK_SC
#elif defined(CONFIG_ALPHA_PYXIS)
# define IACK_SC	PYXIS_IACK_SC
#elif defined(CONFIG_ALPHA_TSUNAMI)
# define IACK_SC	TSUNAMI_IACK_SC
#elif defined(CONFIG_ALPHA_POLARIS)
# define IACK_SC	POLARIS_IACK_SC
#elif defined(CONFIG_ALPHA_IRONGATE)
# define IACK_SC        IRONGATE_IACK_SC
#endif

#if defined(IACK_SC)
void
isa_device_interrupt(unsigned long vector, struct pt_regs *regs)
{
	/*
	 * Generate a PCI interrupt acknowledge cycle.  The PIC will
	 * respond with the interrupt vector of the highest priority
	 * interrupt that is pending.  The PALcode sets up the
	 * interrupts vectors such that irq level L generates vector L.
	 */
	int j = *(vuip) IACK_SC;
	j &= 0xff;
	if (j == 7) {
		if (!(inb(0x20) & 0x80)) {
			/* It's only a passive release... */
			return;
		}
	}
	handle_irq(j, regs);
}
#endif
#if defined(CONFIG_ALPHA_GENERIC) || !defined(IACK_SC)
void
isa_no_iack_sc_device_interrupt(unsigned long vector, struct pt_regs *regs)
{
	unsigned long pic;

	/*
	 * It seems to me that the probability of two or more *device*
	 * interrupts occurring at almost exactly the same time is
	 * pretty low.  So why pay the price of checking for
	 * additional interrupts here if the common case can be
	 * handled so much easier?
	 */
	/* 
	 *  The first read of gives you *all* interrupting lines.
	 *  Therefore, read the mask register and and out those lines
	 *  not enabled.  Note that some documentation has 21 and a1 
	 *  write only.  This is not true.
	 */
	pic = inb(0x20) | (inb(0xA0) << 8);	/* read isr */
	pic &= 0xFFFB;				/* mask out cascade & hibits */

	while (pic) {
		int j = ffz(~pic);
		pic &= pic - 1;
		handle_irq(j, regs);
	}
}
#endif

/*
 * Handle interrupts from the SRM, assuming no additional weirdness.
 */

static inline void
srm_enable_irq(unsigned int irq)
{
	cserve_ena(irq - 16);
}

static void
srm_disable_irq(unsigned int irq)
{
	cserve_dis(irq - 16);
}

static unsigned int
srm_startup_irq(unsigned int irq)
{
	srm_enable_irq(irq);
	return 0;
}

static struct hw_interrupt_type srm_irq_type = {
	typename:	"SRM",
	startup:	srm_startup_irq,
	shutdown:	srm_disable_irq,
	enable:		srm_enable_irq,
	disable:	srm_disable_irq,
	ack:		srm_disable_irq,
	end:		srm_enable_irq,
};

void 
srm_device_interrupt(unsigned long vector, struct pt_regs * regs)
{
	int irq = (vector - 0x800) >> 4;
	handle_irq(irq, regs);
}

void __init
init_srm_irqs(long max, unsigned long ignore_mask)
{
	long i;

	for (i = 16; i < max; ++i) {
		if (i < 64 && ((ignore_mask >> i) & 1))
			continue;
		irq_desc[i].status = IRQ_DISABLED;
		irq_desc[i].handler = &srm_irq_type;
	}
}

/*
 * The not-handled irq handler.
 */

static void
noirq_enable_disable(unsigned int irq)
{
}

static unsigned int
noirq_startup(unsigned int irq)
{
	return 0;
}

static void
noirq_ack(unsigned int irq)
{
	printk(KERN_CRIT "Unexpected IRQ %u\n", irq);
}

static struct hw_interrupt_type no_irq_type = {
	typename:	"none",
	startup:	noirq_startup,
	shutdown:	noirq_enable_disable,
	enable:		noirq_enable_disable,
	disable:	noirq_enable_disable,
	ack:		noirq_ack,
	end:		noirq_enable_disable,
};

/*
 * The special RTC interrupt type.  The interrupt itself was
 * processed by PALcode, and comes in via entInt vector 1.
 */

static struct hw_interrupt_type rtc_irq_type = {
	typename:	"RTC",
	startup:	noirq_startup,
	shutdown:	noirq_enable_disable,
	enable:		noirq_enable_disable,
	disable:	noirq_enable_disable,
	ack:		noirq_enable_disable,
	end:		noirq_enable_disable,
};

void __init
init_rtc_irq(void)
{
	irq_desc[RTC_IRQ].status = IRQ_DISABLED;
	irq_desc[RTC_IRQ].handler = &rtc_irq_type;
}

/*
 * Special irq handlers.
 */

void
no_action(int cpl, void *dev_id, struct pt_regs *regs)
{
}

/* 
 * Common irq handlers.
 */

struct irqaction isa_cascade_irqaction = {
	handler:	no_action,
	name:		"isa-cascade"
};

struct irqaction timer_cascade_irqaction = {
	handler:	no_action,
	name:		"timer-cascade"
};

struct irqaction halt_switch_irqaction = {
	handler:	no_action,
	name:		"halt-switch"
};


spinlock_t irq_controller_lock = SPIN_LOCK_UNLOCKED;
irq_desc_t irq_desc[NR_IRQS] __cacheline_aligned = {
	[0 ... NR_IRQS-1] = { 0, &no_irq_type, }
};

int
handle_IRQ_event(unsigned int irq, struct pt_regs *regs,
		 struct irqaction *action)
{
	int status, cpu = smp_processor_id();
	int old_ipl, ipl;

	kstat.irqs[cpu][irq]++;
	irq_enter(cpu, irq);

	status = 1;	/* Force the "do bottom halves" bit */

	old_ipl = ipl = getipl();
	do {
		int new_ipl = IPL_MIN;
		if (action->flags & SA_INTERRUPT)
			new_ipl = IPL_MAX;
		if (new_ipl != ipl) {
			setipl(new_ipl);
			ipl = new_ipl;
		}

		status |= action->flags;
		action->handler(irq, action->dev_id, regs);
		action = action->next;
	} while (action);
	if (ipl != old_ipl)
		setipl(old_ipl);

	if (status & SA_SAMPLE_RANDOM)
		add_interrupt_randomness(irq);
	irq_exit(cpu, irq);

	return status;
}

/*
 * Generic enable/disable code: this just calls
 * down into the PIC-specific version for the actual
 * hardware disable after having gotten the irq
 * controller lock. 
 */
void
disable_irq_nosync(unsigned int irq)
{
	unsigned long flags;

	spin_lock_irqsave(&irq_controller_lock, flags);
	if (!irq_desc[irq].depth++) {
		irq_desc[irq].status |= IRQ_DISABLED | IRQ_MASKED;
		irq_desc[irq].handler->disable(irq);
	}
	spin_unlock_irqrestore(&irq_controller_lock, flags);
}

/*
 * Synchronous version of the above, making sure the IRQ is
 * no longer running on any other IRQ..
 */
void
disable_irq(unsigned int irq)
{
	disable_irq_nosync(irq);

	if (!local_irq_count(smp_processor_id())) {
		do {
			barrier();
		} while (irq_desc[irq].status & IRQ_INPROGRESS);
	}
}

void
enable_irq(unsigned int irq)
{
	unsigned long flags;

	spin_lock_irqsave(&irq_controller_lock, flags);
	switch (irq_desc[irq].depth) {
	case 1:
	  {
		unsigned int status = irq_desc[irq].status;

		status &= ~(IRQ_DISABLED | IRQ_MASKED);
		if ((status & (IRQ_PENDING | IRQ_REPLAY)) == IRQ_PENDING) {
			status |= IRQ_REPLAY;
			/* ??? We can't re-send on (most?) alpha hw.
			   hw_resend_irq(irq_desc[irq].handler,irq); */
		}
		irq_desc[irq].status = status;
		irq_desc[irq].handler->enable(irq);
		/* fall-through */
	  }
	default:
		irq_desc[irq].depth--;
		break;
	case 0:
		printk(KERN_ERR "enable_irq() unbalanced from %p\n",
		       __builtin_return_address(0));
	}
	spin_unlock_irqrestore(&irq_controller_lock, flags);
}

int
setup_irq(unsigned int irq, struct irqaction * new)
{
	int shared = 0;
	struct irqaction *old, **p;
	unsigned long flags;

	/*
	 * Some drivers like serial.c use request_irq() heavily,
	 * so we have to be careful not to interfere with a
	 * running system.
	 */
	if (new->flags & SA_SAMPLE_RANDOM) {
		/*
		 * This function might sleep, we want to call it first,
		 * outside of the atomic block.
		 * Yes, this might clear the entropy pool if the wrong
		 * driver is attempted to be loaded, without actually
		 * installing a new handler, but is this really a problem,
		 * only the sysadmin is able to do this.
		 */
		rand_initialize_irq(irq);
	}

	/*
	 * The following block of code has to be executed atomically
	 */
	spin_lock_irqsave(&irq_controller_lock,flags);
	p = &irq_desc[irq].action;
	if ((old = *p) != NULL) {
		/* Can't share interrupts unless both agree to */
		if (!(old->flags & new->flags & SA_SHIRQ)) {
			spin_unlock_irqrestore(&irq_controller_lock,flags);
			return -EBUSY;
		}

		/* add new interrupt at end of irq queue */
		do {
			p = &old->next;
			old = *p;
		} while (old);
		shared = 1;
	}

	*p = new;

	if (!shared) {
		irq_desc[irq].depth = 0;
		irq_desc[irq].status &= ~(IRQ_DISABLED | IRQ_MASKED);
		irq_desc[irq].handler->startup(irq);
	}
	spin_unlock_irqrestore(&irq_controller_lock,flags);
	return 0;
}

int
request_irq(unsigned int irq, void (*handler)(int, void *, struct pt_regs *),
	    unsigned long irqflags, const char * devname, void *dev_id)
{
	int retval;
	struct irqaction * action;

	if (irq >= ACTUAL_NR_IRQS)
		return -EINVAL;
	if (!handler)
		return -EINVAL;

#if 1
	/*
	 * Sanity-check: shared interrupts should REALLY pass in
	 * a real dev-ID, otherwise we'll have trouble later trying
	 * to figure out which interrupt is which (messes up the
	 * interrupt freeing logic etc).
	 */
	if ((irqflags & SA_SHIRQ) && !dev_id) {
		printk(KERN_ERR
		       "Bad boy: %s (at %p) called us without a dev_id!\n",
		       devname, __builtin_return_address(0));
	}
#endif

	action = (struct irqaction *)
			kmalloc(sizeof(struct irqaction), GFP_KERNEL);
	if (!action)
		return -ENOMEM;

	action->handler = handler;
	action->flags = irqflags;
	action->mask = 0;
	action->name = devname;
	action->next = NULL;
	action->dev_id = dev_id;

	retval = setup_irq(irq, action);
	if (retval)
		kfree(action);
	return retval;
}

void
free_irq(unsigned int irq, void *dev_id)
{
	struct irqaction **p;
	unsigned long flags;

	if (irq >= ACTUAL_NR_IRQS) {
		printk(KERN_CRIT "Trying to free IRQ%d\n", irq);
		return;
	}

	spin_lock_irqsave(&irq_controller_lock,flags);
	p = &irq_desc[irq].action;
	for (;;) {
		struct irqaction * action = *p;
		if (action) {
			struct irqaction **pp = p;
			p = &action->next;
			if (action->dev_id != dev_id)
				continue;

			/* Found - now remove it from the list of entries.  */
			*pp = action->next;
			if (!irq_desc[irq].action) {
				irq_desc[irq].status |= IRQ_DISABLED|IRQ_MASKED;
				irq_desc[irq].handler->shutdown(irq);
			}
			spin_unlock_irqrestore(&irq_controller_lock,flags);

			/* Wait to make sure it's not being used on
			   another CPU.  */
			while (irq_desc[irq].status & IRQ_INPROGRESS)
				barrier();
			kfree(action);
			return;
		}
		printk(KERN_ERR "Trying to free free IRQ%d\n",irq);
		spin_unlock_irqrestore(&irq_controller_lock,flags);
		return;
	}
}

int get_irq_list(char *buf)
{
	int i, j;
	struct irqaction * action;
	char *p = buf;

#ifdef CONFIG_SMP
	p += sprintf(p, "           ");
	for (i = 0; i < smp_num_cpus; i++)
		p += sprintf(p, "CPU%d       ", i);
	for (i = 0; i < smp_num_cpus; i++)
		p += sprintf(p, "TRY%d       ", i);
	*p++ = '\n';
#endif

	for (i = 0; i < NR_IRQS; i++) {
		action = irq_desc[i].action;
		if (!action) 
			continue;
		p += sprintf(p, "%3d: ",i);
#ifndef CONFIG_SMP
		p += sprintf(p, "%10u ", kstat_irqs(i));
#else
		for (j = 0; j < smp_num_cpus; j++)
			p += sprintf(p, "%10u ",
				     kstat.irqs[cpu_logical_map(j)][i]);
		for (j = 0; j < smp_num_cpus; j++)
			p += sprintf(p, "%10lu ",
				     irq_attempt(cpu_logical_map(j), i));
#endif
		p += sprintf(p, " %14s", irq_desc[i].handler->typename);
		p += sprintf(p, "  %c%s",
			     (action->flags & SA_INTERRUPT)?'+':' ',
			     action->name);

		for (action=action->next; action; action = action->next) {
			p += sprintf(p, ", %c%s",
				     (action->flags & SA_INTERRUPT)?'+':' ',
				     action->name);
		}
		*p++ = '\n';
	}
#if CONFIG_SMP
	p += sprintf(p, "LOC: ");
	for (j = 0; j < smp_num_cpus; j++)
		p += sprintf(p, "%10lu ",
			     cpu_data[cpu_logical_map(j)].smp_local_irq_count);
	p += sprintf(p, "\n");
#endif
	return p - buf;
}

#ifdef CONFIG_SMP
/* Who has global_irq_lock. */
int global_irq_holder = NO_PROC_ID;

/* This protects IRQ's. */
spinlock_t global_irq_lock = SPIN_LOCK_UNLOCKED;

/* Global IRQ locking depth. */
atomic_t global_irq_count = ATOMIC_INIT(0);

static void *previous_irqholder = NULL;

#define MAXCOUNT 100000000

static void show(char * str, void *where);

static inline void
wait_on_irq(int cpu, void *where)
{
	int count = MAXCOUNT;

	for (;;) {

		/*
		 * Wait until all interrupts are gone. Wait
		 * for bottom half handlers unless we're
		 * already executing in one..
		 */
		if (!atomic_read(&global_irq_count)) {
			if (local_bh_count(cpu)
			    || !spin_is_locked(&global_bh_lock))
				break;
		}

		/* Duh, we have to loop. Release the lock to avoid deadlocks */
		spin_unlock(&global_irq_lock);

		for (;;) {
			if (!--count) {
				show("wait_on_irq", where);
				count = MAXCOUNT;
			}
			__sti();
			udelay(1); /* make sure to run pending irqs */
			__cli();

			if (atomic_read(&global_irq_count))
				continue;
			if (spin_is_locked(&global_irq_lock))
				continue;
			if (!local_bh_count(cpu)
			    && spin_is_locked(&global_bh_lock))
				continue;
			if (spin_trylock(&global_irq_lock))
				break;
		}
	}
}

static inline void
get_irqlock(int cpu, void* where)
{
	if (!spin_trylock(&global_irq_lock)) {
		/* Do we already hold the lock?  */
		if (cpu == global_irq_holder)
			return;
		/* Uhhuh.. Somebody else got it.  Wait.  */
		spin_lock(&global_irq_lock);
	}

	/*
	 * Ok, we got the lock bit.
	 * But that's actually just the easy part.. Now
	 * we need to make sure that nobody else is running
	 * in an interrupt context. 
	 */
	wait_on_irq(cpu, where);

	/*
	 * Finally.
	 */
#if DEBUG_SPINLOCK
	global_irq_lock.task = current;
	global_irq_lock.previous = where;
#endif
	global_irq_holder = cpu;
	previous_irqholder = where;
}

void
__global_cli(void)
{
	int cpu = smp_processor_id();
	void *where = __builtin_return_address(0);

	/*
	 * Maximize ipl.  If ipl was previously 0 and if this thread
	 * is not in an irq, then take global_irq_lock.
	 */
	if (swpipl(IPL_MAX) == IPL_MIN && !local_irq_count(cpu))
		get_irqlock(cpu, where);
}

void
__global_sti(void)
{
        int cpu = smp_processor_id();

        if (!local_irq_count(cpu))
		release_irqlock(cpu);
	__sti();
}

/*
 * SMP flags value to restore to:
 * 0 - global cli
 * 1 - global sti
 * 2 - local cli
 * 3 - local sti
 */
unsigned long
__global_save_flags(void)
{
        int retval;
        int local_enabled;
        unsigned long flags;
	int cpu = smp_processor_id();

        __save_flags(flags);
        local_enabled = (!(flags & 7));
        /* default to local */
        retval = 2 + local_enabled;

        /* Check for global flags if we're not in an interrupt.  */
        if (!local_irq_count(cpu)) {
                if (local_enabled)
                        retval = 1;
                if (global_irq_holder == cpu)
                        retval = 0;
	}
	return retval;
}

void
__global_restore_flags(unsigned long flags)
{
        switch (flags) {
        case 0:
                __global_cli();
                break;
        case 1:
                __global_sti();
                break;
        case 2:
                __cli();
                break;
        case 3:
                __sti();
                break;
        default:
                printk(KERN_ERR "global_restore_flags: %08lx (%p)\n",
                        flags, __builtin_return_address(0));
        }
}

static void
show(char * str, void *where)
{
#if 0
	int i;
        unsigned long *stack;
#endif
        int cpu = smp_processor_id();

        printk("\n%s, CPU %d: %p\n", str, cpu, where);
        printk("irq:  %d [%d %d]\n",
	       atomic_read(&global_irq_count),
               cpu_data[0].irq_count,
	       cpu_data[1].irq_count);

        printk("bh:   %d [%d %d]\n",
	       spin_is_locked(&global_bh_lock) ? 1 : 0,
	       cpu_data[0].bh_count,
	       cpu_data[1].bh_count);
#if 0
        stack = (unsigned long *) &str;
        for (i = 40; i ; i--) {
		unsigned long x = *++stack;
                if (x > (unsigned long) &init_task_union &&
		    x < (unsigned long) &vsprintf) {
			printk("<[%08lx]> ", x);
                }
        }
#endif
}
        
/*
 * From its use, I infer that synchronize_irq() stalls a thread until
 * the effects of a command to an external device are known to have
 * taken hold.  Typically, the command is to stop sending interrupts.
 * The strategy here is wait until there is at most one processor
 * (this one) in an irq.  The memory barrier serializes the write to
 * the device and the subsequent accesses of global_irq_count.
 * --jmartin
 */
#define DEBUG_SYNCHRONIZE_IRQ 0

void
synchronize_irq(void)
{
#if 0
	/* Joe's version.  */
	int cpu = smp_processor_id();
	int local_count;
	int global_count;
	int countdown = 1<<24;
	void *where = __builtin_return_address(0);

	mb();
	do {
		local_count = local_irq_count(cpu);
		global_count = atomic_read(&global_irq_count);
		if (DEBUG_SYNCHRONIZE_IRQ && (--countdown == 0)) {
			printk("%d:%d/%d\n", cpu, local_count, global_count);
			show("synchronize_irq", where);
			break;
		}
	} while (global_count != local_count);
#else
	/* Jay's version.  */
	if (atomic_read(&global_irq_count)) {
		cli();
		sti();
	}
#endif
}
#endif /* CONFIG_SMP */

/*
 * do_IRQ handles all normal device IRQ's (the special
 * SMP cross-CPU interrupts have their own specific
 * handlers).
 */
void
handle_irq(int irq, struct pt_regs * regs)
{	
	/* 
	 * We ack quickly, we don't want the irq controller
	 * thinking we're snobs just because some other CPU has
	 * disabled global interrupts (we have already done the
	 * INT_ACK cycles, it's too late to try to pretend to the
	 * controller that we aren't taking the interrupt).
	 *
	 * 0 return value means that this irq is already being
	 * handled by some other CPU. (or is disabled)
	 */
	int cpu = smp_processor_id();
	irq_desc_t *desc;
	struct irqaction * action;
	unsigned int status;

	if ((unsigned) irq > ACTUAL_NR_IRQS) {
		printk(KERN_CRIT "device_interrupt: illegal interrupt %d\n",
		       irq);
		return;
	}

	irq_attempt(cpu, irq)++;
	desc = irq_desc + irq;
	spin_lock_irq(&irq_controller_lock); /* mask also the RTC */
	desc->handler->ack(irq);
	status = desc->status;

	/* Look for broken irq masking.  */
	if (status & IRQ_MASKED) {
		static unsigned long last_printed;
		if (time_after(jiffies, last_printed+HZ)) {
			printk(KERN_CRIT "Mask didn't work for irq %d!\n", irq);
			last_printed = jiffies;
		}
	}

	/*
	 * REPLAY is when Linux resends an IRQ that was dropped earlier.
	 * WAITING is used by probe to mark irqs that are being tested.
	 */
	status &= ~(IRQ_REPLAY | IRQ_WAITING);
	status |= IRQ_PENDING | IRQ_MASKED; /* we _want_ to handle it */

	/*
	 * If the IRQ is disabled for whatever reason, we cannot
	 * use the action we have.
	 */
	action = NULL;
	if (!(status & (IRQ_DISABLED | IRQ_INPROGRESS))) {
		action = desc->action;
		status &= ~IRQ_PENDING; /* we commit to handling */
		status |= IRQ_INPROGRESS; /* we are handling it */
	}
	desc->status = status;
	spin_unlock(&irq_controller_lock);

	/*
	 * If there is no IRQ handler or it was disabled, exit early.
	 * Since we set PENDING, if another processor is handling
	 * a different instance of this same irq, the other processor
	 * will take care of it.
	 */
	if (!action)
		return;

	/*
	 * Edge triggered interrupts need to remember pending events.
	 * This applies to any hw interrupts that allow a second
	 * instance of the same irq to arrive while we are in do_IRQ
	 * or in the handler. But the code here only handles the _second_
	 * instance of the irq, not the third or fourth. So it is mostly
	 * useful for irq hardware that does not mask cleanly in an
	 * SMP environment.
	 */
	for (;;) {
		handle_IRQ_event(irq, regs, action);
		spin_lock(&irq_controller_lock);
		
		if (!(desc->status & IRQ_PENDING)
		    || (desc->status & IRQ_LEVEL))
			break;
		desc->status &= ~IRQ_PENDING;
		spin_unlock(&irq_controller_lock);
	}
	status = desc->status & ~IRQ_INPROGRESS;
	if (!(status & IRQ_DISABLED)) {
		status &= ~IRQ_MASKED;
		desc->handler->end(irq);
	}
	desc->status = status;
	spin_unlock(&irq_controller_lock);
}

/*
 * IRQ autodetection code..
 *
 * This depends on the fact that any interrupt that
 * comes in on to an unassigned handler will get stuck
 * with "IRQ_WAITING" cleared and the interrupt
 * disabled.
 */
unsigned long
probe_irq_on(void)
{
	int i;
	unsigned long delay;
	unsigned long val;

	/* Something may have generated an irq long ago and we want to
	   flush such a longstanding irq before considering it as spurious. */
	spin_lock_irq(&irq_controller_lock);
	for (i = NR_IRQS-1; i >= 0; i--)
		if (!irq_desc[i].action) 
			if(irq_desc[i].handler->startup(i))
				irq_desc[i].status |= IRQ_PENDING;
	spin_unlock_irq(&irq_controller_lock);

	/* Wait for longstanding interrupts to trigger. */
	for (delay = jiffies + HZ/50; time_after(delay, jiffies); )
		/* about 20ms delay */ synchronize_irq();

	/* enable any unassigned irqs (we must startup again here because
	   if a longstanding irq happened in the previous stage, it may have
	   masked itself) first, enable any unassigned irqs. */
	spin_lock_irq(&irq_controller_lock);
	for (i = NR_IRQS-1; i >= 0; i--) {
		if (!irq_desc[i].action) {
			irq_desc[i].status |= IRQ_AUTODETECT | IRQ_WAITING;
			if(irq_desc[i].handler->startup(i))
				irq_desc[i].status |= IRQ_PENDING;
		}
	}
	spin_unlock_irq(&irq_controller_lock);

	/*
	 * Wait for spurious interrupts to trigger
	 */
	for (delay = jiffies + HZ/10; time_after(delay, jiffies); )
		/* about 100ms delay */ synchronize_irq();

	/*
	 * Now filter out any obviously spurious interrupts
	 */
	val = 0;
	spin_lock_irq(&irq_controller_lock);
	for (i=0; i<NR_IRQS; i++) {
		unsigned int status = irq_desc[i].status;

		if (!(status & IRQ_AUTODETECT))
			continue;
		
		/* It triggered already - consider it spurious. */
		if (!(status & IRQ_WAITING)) {
			irq_desc[i].status = status & ~IRQ_AUTODETECT;
			irq_desc[i].handler->shutdown(i);
			continue;
		}

		if (i < 64)
			val |= 1 << i;
	}
	spin_unlock_irq(&irq_controller_lock);

	return val;
}

/*
 * Return a mask of triggered interrupts (this
 * can handle only legacy ISA interrupts).
 */
unsigned int probe_irq_mask(unsigned long val)
{
	int i;
	unsigned int mask;

	mask = 0;
	spin_lock_irq(&irq_controller_lock);
	for (i = 0; i < 16; i++) {
		unsigned int status = irq_desc[i].status;

		if (!(status & IRQ_AUTODETECT))
			continue;

		if (!(status & IRQ_WAITING))
			mask |= 1 << i;

		irq_desc[i].status = status & ~IRQ_AUTODETECT;
		irq_desc[i].handler->shutdown(i);
	}
	spin_unlock_irq(&irq_controller_lock);

	return mask & val;
}

/*
 * Get the result of the IRQ probe.. A negative result means that
 * we have several candidates (but we return the lowest-numbered
 * one).
 */

int
probe_irq_off(unsigned long val)
{
	int i, irq_found, nr_irqs;

	nr_irqs = 0;
	irq_found = 0;
	spin_lock_irq(&irq_controller_lock);
	for (i=0; i<NR_IRQS; i++) {
		unsigned int status = irq_desc[i].status;

		if (!(status & IRQ_AUTODETECT))
			continue;

		if (!(status & IRQ_WAITING)) {
			if (!nr_irqs)
				irq_found = i;
			nr_irqs++;
		}
		irq_desc[i].status = status & ~IRQ_AUTODETECT;
		irq_desc[i].handler->shutdown(i);
	}
	spin_unlock_irq(&irq_controller_lock);

	if (nr_irqs > 1)
		irq_found = -irq_found;
	return irq_found;
}


/*
 * The main interrupt entry point.
 */

asmlinkage void 
do_entInt(unsigned long type, unsigned long vector, unsigned long la_ptr,
	  unsigned long a3, unsigned long a4, unsigned long a5,
	  struct pt_regs regs)
{
	switch (type) {
	case 0:
#ifdef CONFIG_SMP
		handle_ipi(&regs);
		return;
#else
		printk(KERN_CRIT "Interprocessor interrupt? "
		       "You must be kidding!\n");
#endif
		break;
	case 1:
#ifdef CONFIG_SMP
		cpu_data[smp_processor_id()].smp_local_irq_count++;
		smp_percpu_timer_interrupt(&regs);
		if (smp_processor_id() == smp_boot_cpuid)
#endif
			handle_irq(RTC_IRQ, &regs);
		return;
	case 2:
		alpha_mv.machine_check(vector, la_ptr, &regs);
		return;
	case 3:
		alpha_mv.device_interrupt(vector, &regs);
		return;
	case 4:
		perf_irq(vector, &regs);
		return;
	default:
		printk(KERN_CRIT "Hardware intr %ld %lx? Huh?\n",
		       type, vector);
	}
	printk("PC = %016lx PS=%04lx\n", regs.pc, regs.ps);
}

void __init
common_init_isa_dma(void)
{
	outb(0, DMA1_RESET_REG);
	outb(0, DMA2_RESET_REG);
	outb(0, DMA1_CLR_MASK_REG);
	outb(0, DMA2_CLR_MASK_REG);
}

void __init
init_IRQ(void)
{
	wrent(entInt, 0);
	alpha_mv.init_irq();
}


/*
 */
#define MCHK_K_TPERR           0x0080
#define MCHK_K_TCPERR          0x0082
#define MCHK_K_HERR            0x0084
#define MCHK_K_ECC_C           0x0086
#define MCHK_K_ECC_NC          0x0088
#define MCHK_K_OS_BUGCHECK     0x008A
#define MCHK_K_PAL_BUGCHECK    0x0090

#ifndef CONFIG_SMP
struct mcheck_info __mcheck_info;
#endif

void
process_mcheck_info(unsigned long vector, unsigned long la_ptr,
		    struct pt_regs *regs, const char *machine,
		    int expected)
{
	struct el_common *mchk_header;
	const char *reason;

	/*
	 * See if the machine check is due to a badaddr() and if so,
	 * ignore it.
	 */

#if DEBUG_MCHECK > 0
	 printk(KERN_CRIT "%s machine check %s\n", machine,
	        expected ? "expected." : "NOT expected!!!");
#endif

	if (expected) {
		int cpu = smp_processor_id();
		mcheck_expected(cpu) = 0;
		mcheck_taken(cpu) = 1;
		return;
	}

	mchk_header = (struct el_common *)la_ptr;

	printk(KERN_CRIT "%s machine check: vector=0x%lx pc=0x%lx code=0x%lx\n",
	       machine, vector, regs->pc, mchk_header->code);

	switch ((unsigned int) mchk_header->code) {
	/* Machine check reasons.  Defined according to PALcode sources.  */
	case 0x80: reason = "tag parity error"; break;
	case 0x82: reason = "tag control parity error"; break;
	case 0x84: reason = "generic hard error"; break;
	case 0x86: reason = "correctable ECC error"; break;
	case 0x88: reason = "uncorrectable ECC error"; break;
	case 0x8A: reason = "OS-specific PAL bugcheck"; break;
	case 0x90: reason = "callsys in kernel mode"; break;
	case 0x96: reason = "i-cache read retryable error"; break;
	case 0x98: reason = "processor detected hard error"; break;
	
	/* System specific (these are for Alcor, at least): */
	case 0x202: reason = "system detected hard error"; break;
	case 0x203: reason = "system detected uncorrectable ECC error"; break;
	case 0x204: reason = "SIO SERR occurred on PCI bus"; break;
	case 0x205: reason = "parity error detected by CIA"; break;
	case 0x206: reason = "SIO IOCHK occurred on ISA bus"; break;
	case 0x207: reason = "non-existent memory error"; break;
	case 0x208: reason = "MCHK_K_DCSR"; break;
	case 0x209: reason = "PCI SERR detected"; break;
	case 0x20b: reason = "PCI data parity error detected"; break;
	case 0x20d: reason = "PCI address parity error detected"; break;
	case 0x20f: reason = "PCI master abort error"; break;
	case 0x211: reason = "PCI target abort error"; break;
	case 0x213: reason = "scatter/gather PTE invalid error"; break;
	case 0x215: reason = "flash ROM write error"; break;
	case 0x217: reason = "IOA timeout detected"; break;
	case 0x219: reason = "IOCHK#, EISA add-in board parity or other catastrophic error"; break;
	case 0x21b: reason = "EISA fail-safe timer timeout"; break;
	case 0x21d: reason = "EISA bus time-out"; break;
	case 0x21f: reason = "EISA software generated NMI"; break;
	case 0x221: reason = "unexpected ev5 IRQ[3] interrupt"; break;
	default: reason = "unknown"; break;
	}

	printk(KERN_CRIT "machine check type: %s%s\n",
	       reason, mchk_header->retry ? " (retryable)" : "");

	dik_show_regs(regs, NULL);

#if DEBUG_MCHECK > 1
	{
		/* Dump the logout area to give all info.  */
		unsigned long *ptr = (unsigned long *)la_ptr;
		long i;
		for (i = 0; i < mchk_header->size / sizeof(long); i += 2) {
			printk(KERN_CRIT "   +%8lx %016lx %016lx\n",
			       i*sizeof(long), ptr[i], ptr[i+1]);
		}
	}
#endif
}
