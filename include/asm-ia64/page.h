#ifndef _ASM_IA64_PAGE_H
#define _ASM_IA64_PAGE_H
/*
 * Pagetable related stuff.
 *
 * Copyright (C) 1998, 1999 Hewlett-Packard Co
 * Copyright (C) 1998, 1999 David Mosberger-Tang <davidm@hpl.hp.com>
 */

#include <linux/config.h>

#include <asm/types.h>

/*
 * PAGE_SHIFT determines the actual kernel page size.
 */
#if defined(CONFIG_IA64_PAGE_SIZE_4KB)
# define PAGE_SHIFT	12
#elif defined(CONFIG_IA64_PAGE_SIZE_8KB)
# define PAGE_SHIFT	13
#elif defined(CONFIG_IA64_PAGE_SIZE_16KB)
# define PAGE_SHIFT	14
#elif defined(CONFIG_IA64_PAGE_SIZE_64KB)
# define PAGE_SHIFT	16
#else
# error Unsupported page size!
#endif

#define PAGE_SIZE		(__IA64_UL_CONST(1) << PAGE_SHIFT)
#define PAGE_MASK		(~(PAGE_SIZE - 1))
#define PAGE_ALIGN(addr)	(((addr) + PAGE_SIZE - 1) & PAGE_MASK)

#ifdef __ASSEMBLY__
# define __pa(x)		((x) - PAGE_OFFSET)
# define __va(x)		((x) + PAGE_OFFSET)
#else /* !__ASSEMBLY */
# ifdef __KERNEL__
#  define STRICT_MM_TYPECHECKS

extern void clear_page (void *page);
extern void copy_page (void *to, void *from);

#  ifdef STRICT_MM_TYPECHECKS
/*
 * These are used to make use of C type-checking..
 */
typedef struct { unsigned long pte; } pte_t;
typedef struct { unsigned long pmd; } pmd_t;
typedef struct { unsigned long pgd; } pgd_t;
typedef struct { unsigned long pgprot; } pgprot_t;

#define pte_val(x)	((x).pte)
#define pmd_val(x)	((x).pmd)
#define pgd_val(x)	((x).pgd)
#define pgprot_val(x)	((x).pgprot)

#define __pte(x)	((pte_t) { (x) } )
#define __pgd(x)	((pgd_t) { (x) } )
#define __pgprot(x)	((pgprot_t) { (x) } )

#  else /* !STRICT_MM_TYPECHECKS */
/*
 * .. while these make it easier on the compiler
 */
typedef unsigned long pte_t;
typedef unsigned long pmd_t;
typedef unsigned long pgd_t;
typedef unsigned long pgprot_t;

#define pte_val(x)	(x)
#define pmd_val(x)	(x)
#define pgd_val(x)	(x)
#define pgprot_val(x)	(x)

#define __pte(x)	(x)
#define __pgd(x)	(x)
#define __pgprot(x)	(x)

#  endif /* !STRICT_MM_TYPECHECKS */

/*
 * Note: the MAP_NR() macro can't use __pa() because MAP_NR(X) MUST
 * map to something >= max_mapnr if X is outside the identity mapped
 * kernel space.
 */

/*
 * The dense variant can be used as long as the size of memory holes isn't
 * very big.
 */
#define MAP_NR_DENSE(addr)	(((unsigned long) (addr) - PAGE_OFFSET) >> PAGE_SHIFT)

/*
 * This variant works well for the SGI SN1 architecture (which does have huge
 * holes in the memory address space).
 */
#define MAP_NR_SN1(addr)	(((unsigned long) (addr) - PAGE_OFFSET) >> PAGE_SHIFT)

#ifdef CONFIG_IA64_GENERIC
# define MAP_NR(addr)	platform_map_nr(addr)
#elif defined (CONFIG_IA64_SN_SN1_SIM)
# define MAP_NR(addr)	MAP_NR_SN1(addr)
#else
# define MAP_NR(addr)	MAP_NR_DENSE(addr)
#endif

# endif /* __KERNEL__ */

typedef union ia64_va {
	struct {
		unsigned long off : 61;		/* intra-region offset */
		unsigned long reg :  3;		/* region number */
	} f;
	unsigned long l;
	void *p;
} ia64_va;

/*
 * Note: These macros depend on the fact that PAGE_OFFSET has all
 * region bits set to 1 and all other bits set to zero.  They are
 * expressed in this way to ensure they result in a single "dep"
 * instruction.
 */
#define __pa(x)		({ia64_va _v; _v.l = (long) (x); _v.f.reg = 0; _v.l;})
#define __va(x)		({ia64_va _v; _v.l = (long) (x); _v.f.reg = -1; _v.p;})

#define BUG() do { printk("kernel BUG at %s:%d!\n", __FILE__, __LINE__); *(int *)0=0; } while (0)
#define PAGE_BUG(page) do { BUG(); } while (0)

#endif /* !ASSEMBLY */

#define PAGE_OFFSET		0xe000000000000000

#endif /* _ASM_IA64_PAGE_H */
