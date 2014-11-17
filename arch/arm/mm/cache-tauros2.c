/*
 * arch/arm/mm/cache-tauros2.c - Tauros2 L2 cache controller support
 *
 * Copyright (C) 2008 Marvell Semiconductor
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 *
 * References:
 * - PJ1 CPU Core Datasheet,
 *   Document ID MV-S104837-01, Rev 0.7, January 24 2008.
 * - PJ4 CPU Core Datasheet,
 *   Document ID MV-S105190-00, Rev 0.7, March 14 2008.
 */

#include <linux/init.h>
#include <linux/io.h>
#include <asm/cacheflush.h>
#include <asm/cp15.h>
#include <asm/cputype.h>
#include <asm/hardware/cache-tauros2.h>

#ifdef CONFIG_ARCH_PXA
#include <mach/hardware.h>
#endif

/*
 * When Tauros2 is used on a CPU that supports the v7 hierarchical
 * cache operations, the cache handling code in proc-v7.S takes care
 * of everything, including handling DMA coherency.
 *
 * So, we only need to register outer cache operations here if we're
 * being used on a pre-v7 CPU, and we only need to build support for
 * outer cache operations into the kernel image if the kernel has been
 * configured to support a pre-v7 CPU.
 */
#if __LINUX_ARM_ARCH__ < 7
/*
 * Low-level cache maintenance operations.
 */
static inline void tauros2_clean_pa(unsigned long addr)
{
	__asm__("mcr p15, 1, %0, c7, c11, 3" : : "r" (addr));
}

static inline void tauros2_clean_inv_pa(unsigned long addr)
{
	__asm__("mcr p15, 1, %0, c7, c15, 3" : : "r" (addr));
}

static inline void tauros2_inv_pa(unsigned long addr)
{
	__asm__("mcr p15, 1, %0, c7, c7, 3" : : "r" (addr));
}


/*
 * Linux primitives.
 *
 * Note that the end addresses passed to Linux primitives are
 * noninclusive.
 */
#define CACHE_LINE_SIZE		32

static void tauros2_inv_range(unsigned long start, unsigned long end)
{
	/*
	 * Clean and invalidate partial first cache line.
	 */
	if (start & (CACHE_LINE_SIZE - 1)) {
		tauros2_clean_inv_pa(start & ~(CACHE_LINE_SIZE - 1));
		start = (start | (CACHE_LINE_SIZE - 1)) + 1;
	}

	/*
	 * Clean and invalidate partial last cache line.
	 */
	if (end & (CACHE_LINE_SIZE - 1)) {
		tauros2_clean_inv_pa(end & ~(CACHE_LINE_SIZE - 1));
		end &= ~(CACHE_LINE_SIZE - 1);
	}

	/*
	 * Invalidate all full cache lines between 'start' and 'end'.
	 */
	while (start < end) {
		tauros2_inv_pa(start);
		start += CACHE_LINE_SIZE;
	}

	dsb();
}

static void tauros2_clean_range(unsigned long start, unsigned long end)
{
	start &= ~(CACHE_LINE_SIZE - 1);
	while (start < end) {
		tauros2_clean_pa(start);
		start += CACHE_LINE_SIZE;
	}

	dsb();
}

static void tauros2_flush_range(unsigned long start, unsigned long end)
{
	start &= ~(CACHE_LINE_SIZE - 1);
	while (start < end) {
		tauros2_clean_inv_pa(start);
		start += CACHE_LINE_SIZE;
	}

	dsb();
}
#endif

static inline u32 __init read_extra_features(void)
{
	u32 u;

	__asm__("mrc p15, 1, %0, c15, c1, 0" : "=r" (u));

	return u;
}

static inline void __init write_extra_features(u32 u)
{
	__asm__("mcr p15, 1, %0, c15, c1, 0" : : "r" (u));
}

/* disable L2 prefetch */
static void __init tauros2_disable_prefetch(void)
{
	u32 u;

	/*
	 * Read the CPU Extra Features register and verify that the
	 * Disable L2 Prefetch bit is set.
	 */
	u = read_extra_features();
	if (!(u & 0x01000000)) {
		write_extra_features(u | 0x01000000);
	}
	printk(KERN_INFO "Tauros2: Disabling L2 prefetch.\n");
}

/* enable L2 prefetch */
#ifndef CONFIG_CACHE_TAUROS2_PREFETCH_OFF
static void __init tauros2_enable_prefetch(void)
{
	u32 u;

	/*
	 * Read the CPU Extra Features register and verify that the
	 * Disable L2 Prefetch bit is set.
	 */
	u = read_extra_features();
	if ((u & 0x01000000)) {
		write_extra_features(u & 0xfeffffff);
	}
	printk(KERN_INFO "Tauros2: Enable L2 prefetch. \n");
}
#endif

#ifdef CONFIG_CACHE_TAUROS2_LINEFILL_BURST8
static void __init tauros2_enable_burst8(void)
{
	u32 u;

	/*
	 * Read the CPU Extra Features register and verify that the
	 * Enable L2 burst lenght 8 bit is clear.
	 */
	u = read_extra_features();
	if (!(u & 0x00100000)) {
		write_extra_features(u | 0x00100000);
	}
	printk(KERN_INFO "Tauros2: Enable L2 Burst Length 8\n");
}
#endif

static void __init tauros2_disable_burst8(void)
{
	u32 u;

	/*
	 * Read the CPU Extra Features register and verify that the
	 * Enable L2 burst lenght 8 bit is set.
	 */
	u = read_extra_features();
	if (u & 0x00100000) {
		write_extra_features(u & 0xffefffff);
	}
	printk(KERN_INFO "Tauros2: Disable L2 Burst Length 8\n");
}

#ifndef CONFIG_CACHE_TAUROS2_WRITEBUFFER_COALESCING_OFF
static void __init tauros2_enable_writebuffer_coalescing(void)
{
	u32 u;

	/*
	 * Read the CPU Extra Features register and verify that the
	 * Enable write buffer coalescing bit is set.
	 */
	u = read_extra_features();
	if (!(u & 0x100)) {
		write_extra_features(u | 0x100);
	}
	printk(KERN_INFO "Tauros2: Enable L2 write buffer coalescing \n");
}
#else

static void __init tauros2_disable_writebuffer_coalescing(void)
{
	u32 u;

	/*
	 * Read the CPU Extra Features register and verify that the
	 * Enable write buffer coalescing bit is clear.
	 */
	u = read_extra_features();
	if (u & 0x100) {
		write_extra_features(u & ~(0x100));
	}
	printk(KERN_INFO "Tauros2: Disabling L2 write buffer coalescing \n");
}
#endif

static inline int __init cpuid_scheme(void)
{
	return !!((processor_id & 0x000f0000) == 0x000f0000);
}

static inline u32 __init read_mmfr3(void)
{
	u32 mmfr3;

	__asm__("mrc p15, 0, %0, c0, c1, 7\n" : "=r" (mmfr3));

	return mmfr3;
}

static inline u32 __init read_actlr(void)
{
	u32 actlr;

	__asm__("mrc p15, 0, %0, c1, c0, 1\n" : "=r" (actlr));

	return actlr;
}

static inline void __init write_actlr(u32 actlr)
{
	__asm__("mcr p15, 0, %0, c1, c0, 1\n" : : "r" (actlr));
}

static void enable_extra_feature(void)
{
#ifndef CONFIG_CACHE_TAUROS2_PREFETCH_OFF
	tauros2_enable_prefetch();
#else
	tauros2_disable_prefetch();
#endif

#ifdef CONFIG_CACHE_TAUROS2_LINEFILL_BURST8
	tauros2_enable_burst8();
#else
	tauros2_disable_burst8();
#endif

#ifndef CONFIG_CACHE_TAUROS2_WRITEBUFFER_COALESCING_OFF
	tauros2_enable_writebuffer_coalescing();
#else
	tauros2_disable_writebuffer_coalescing();
#endif
}

#ifdef CONFIG_ARCH_PXA
static void disable_extra_feature(void)
{
	tauros2_disable_prefetch();
	tauros2_disable_burst8();
}
#endif

void __init tauros2_init(void)
{
	char *mode = NULL;

	enable_extra_feature();

#ifdef CONFIG_CPU_32v5
	if ((processor_id & 0xff0f0000) == 0x56050000) {
		/*
		 * When Tauros2 is used in an ARMv5 system, the L2
		 * enable bit is in the ARMv5 ARM-mandated position
		 * (bit [26] of the System Control Register).
		 */
		if (!(get_cr() & 0x04000000)) {
			printk(KERN_INFO "Tauros2: Enabling L2 cache.\n");
			adjust_cr(0x04000000, 0x04000000);
		}

		mode = "ARMv5";
		outer_cache.inv_range = tauros2_inv_range;
		outer_cache.clean_range = tauros2_clean_range;
		outer_cache.flush_range = tauros2_flush_range;
	}
#endif

#ifdef CONFIG_CPU_32v6
	/*
	 * Check whether this CPU lacks support for the v7 hierarchical
	 * cache ops.  (PJ4 is in its v6 personality mode if the MMFR3
	 * register indicates no support for the v7 hierarchical cache
	 * ops.)
	 */
	if (cpuid_scheme() && (read_mmfr3() & 0xf) == 0) {
		/*
		 * When Tauros2 is used in an ARMv6 system, the L2
		 * enable bit is in the ARMv6 ARM-mandated position
		 * (bit [26] of the System Control Register).
		 */
		if (!(get_cr() & 0x04000000)) {
			printk(KERN_INFO "Tauros2: Enabling L2 cache.\n");
			adjust_cr(0x04000000, 0x04000000);
		}

		mode = "ARMv6";
		outer_cache.inv_range = tauros2_inv_range;
		outer_cache.clean_range = tauros2_clean_range;
		outer_cache.flush_range = tauros2_flush_range;
	}
#endif

#ifdef CONFIG_CPU_32v7
	/*
	 * Check whether this CPU has support for the v7 hierarchical
	 * cache ops.  (PJ4 is in its v7 personality mode if the MMFR3
	 * register indicates support for the v7 hierarchical cache
	 * ops.)
	 *
	 * (Although strictly speaking there may exist CPUs that
	 * implement the v7 cache ops but are only ARMv6 CPUs (due to
	 * not complying with all of the other ARMv7 requirements),
	 * there are no real-life examples of Tauros2 being used on
	 * such CPUs as of yet.)
	 */
	if (cpuid_scheme() && (read_mmfr3() & 0xf) == 1) {
		u32 actlr;

		/*
		 * When Tauros2 is used in an ARMv7 system, the L2
		 * enable bit is located in the Auxiliary System Control
		 * Register (which is the only register allowed by the
		 * ARMv7 spec to contain fine-grained cache control bits).
		 */
		actlr = read_actlr();
		if (!(actlr & 0x00000002)) {
			printk(KERN_INFO "Tauros2: Enabling L2 cache.\n");
			write_actlr(actlr | 0x00000002);
		}

		mode = "ARMv7";
	}
#endif

	if (mode == NULL) {
		printk(KERN_CRIT "Tauros2: Unable to detect CPU mode.\n");
		return;
	}

	printk(KERN_INFO "Tauros2: L2 cache support initialised "
			 "in %s mode.\n", mode);
}
