/*
 *  i2c_adap_pxa.c
 *
 *  I2C adapter for the PXA I2C bus access.
 *
 *  Copyright (C) 2002 Intrinsyc Software Inc.
 *  Copyright (C) 2004-2005 Deep Blue Solutions Ltd.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 *
 *  History:
 *    Apr 2002: Initial version [CS]
 *    Jun 2002: Properly separated algo/adap [FB]
 *    Jan 2003: Fixed several bugs concerning interrupt handling [Kai-Uwe Bloem]
 *    Jan 2003: added limited signal handling [Kai-Uwe Bloem]
 *    Sep 2004: Major rework to ensure efficient bus handling [RMK]
 *    Dec 2004: Added support for PXA27x and slave device probing [Liam Girdwood]
 *    Feb 2005: Rework slave mode handling [RMK]
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/time.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/i2c-pxa.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_i2c.h>
#include <linux/platform_device.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/i2c/pxa-i2c.h>
#include <plat/pm.h>
#include <linux/notifier.h>
#include <linux/reboot.h>

#include <asm/irq.h>
#if defined(CONFIG_MACH_HELANDELOS) || defined(CONFIG_MACH_WILCOX) || defined(CONFIG_MACH_CS02) || defined(CONFIG_MACH_BAFFIN) \
	|| defined(CONFIG_MACH_CT01) || defined(CONFIG_MACH_CS05) || defined(CONFIG_MACH_BAFFINQ) || defined(CONFIG_MACH_GOLDEN)
#include <mach/samsung_camera.h>
#elif CONFIG_MACH_GOYA
#include <mach/samsung_camera_goya.h>
#elif CONFIG_MACH_DEGAS
#include <mach/samsung_camera_degas.h>
#endif
#ifndef CONFIG_HAVE_CLK
#define clk_get(dev, id)	NULL
#define clk_put(clk)		do { } while (0)
#define clk_disable(clk)	do { } while (0)
#define clk_enable(clk)		do { } while (0)
#endif

struct pxa_reg_layout {
	u32 ibmr;
	u32 idbr;
	u32 icr;
	u32 isr;
	u32 isar;
	u32 ilcr;
	u32 iwcr;
	u32 wfifo;
	u32 wfifo_wptr;
	u32 wfifo_rptr;
	u32 rfifo;
	u32 rfifo_wptr;
	u32 rfifo_rptr;
};

enum pxa_i2c_types {
	REGS_PXA2XX,
	REGS_PXA3XX,
	REGS_CE4100,
	REGS_PXA910,
};

/*
 * I2C registers definitions
 */
static struct pxa_reg_layout pxa_reg_layout[] = {
	[REGS_PXA2XX] = {
		.ibmr =	0x00,
		.idbr =	0x08,
		.icr =	0x10,
		.isr =	0x18,
		.isar =	0x20,
	},
	[REGS_PXA3XX] = {
		.ibmr =	0x00,
		.idbr =	0x04,
		.icr =	0x08,
		.isr =	0x0c,
		.isar =	0x10,
	},
	[REGS_CE4100] = {
		.ibmr =	0x14,
		.idbr =	0x0c,
		.icr =	0x00,
		.isr =	0x04,
		/* no isar register */
	},
	[REGS_PXA910] = {
		.ibmr = 0x00,
		.idbr = 0x08,
		.icr =	0x10,
		.isr =	0x18,
		.isar = 0x20,
		.ilcr = 0x28,
		.iwcr = 0x30,
		.wfifo = 0x40,
		.wfifo_wptr = 0x44,
		.wfifo_rptr = 0x48,
		.rfifo = 0x50,
		.rfifo_wptr = 0x54,
		.rfifo_rptr = 0x58,
	},
};

static const struct platform_device_id i2c_pxa_id_table[] = {
	{ "pxa2xx-i2c",		REGS_PXA2XX },
	{ "pxa3xx-pwri2c",	REGS_PXA3XX },
	{ "ce4100-i2c",		REGS_CE4100 },
	{ "pxa910-i2c",		REGS_PXA910 },
	{ },
};
MODULE_DEVICE_TABLE(platform, i2c_pxa_id_table);

/*
 * I2C bit definitions
 */

#define ICR_START	(1 << 0)	   /* start bit */
#define ICR_STOP	(1 << 1)	   /* stop bit */
#define ICR_ACKNAK	(1 << 2)	   /* send ACK(0) or NAK(1) */
#define ICR_TB		(1 << 3)	   /* transfer byte bit */
#define ICR_MA		(1 << 4)	   /* master abort */
#define ICR_SCLE	(1 << 5)	   /* master clock enable */
#define ICR_IUE		(1 << 6)	   /* unit enable */
#define ICR_GCD		(1 << 7)	   /* general call disable */
#define ICR_ITEIE	(1 << 8)	   /* enable tx interrupts */
#define ICR_IRFIE	(1 << 9)	   /* enable rx interrupts */
#define ICR_BEIE	(1 << 10)	   /* enable bus error ints */
#define ICR_SSDIE	(1 << 11)	   /* slave STOP detected int enable */
#define ICR_ALDIE	(1 << 12)	   /* enable arbitration interrupt */
#define ICR_SADIE	(1 << 13)	   /* slave address detected int enable */
#define ICR_UR		(1 << 14)	   /* unit reset */
#define ICR_FM		(1 << 15)	   /* fast mode */
#define ICR_HS		(1 << 16)	   /* High Speed mode */
#define ICR_GPIOEN	(1 << 19)	   /* enable GPIO mode for SCL in HS */
#define ICR_FIFOEN      (1 << 20)	   /* enable FIFO mode */
#define ICR_TXBEGIN     (1 << 21)	   /* transaction begins */
#define ICR_TXDONE_IE   (1 << 22)	   /* EN tx done interrupt */
#define ICR_RXHF_IE     (1 << 23)	   /* EN rx FIFO half full interrupt */
#define ICR_TXE_IE      (1 << 24)	   /* EN tx FIFO empty interrupt */
#define ICR_RXF_IE      (1 << 25)	   /* EN rx FIFO full interrupt */
#define ICR_RXOV_IE     (1 << 26)	   /* EN rx FIFO overrun interrupt */
#define ICR_DMA_EN      (1 << 27)	   /* DMA enable for TX and RX FIFOs */

#define ISR_RWM		(1 << 0)	   /* read/write mode */
#define ISR_ACKNAK	(1 << 1)	   /* ack/nak status */
#define ISR_UB		(1 << 2)	   /* unit busy */
#define ISR_IBB		(1 << 3)	   /* bus busy */
#define ISR_SSD		(1 << 4)	   /* slave stop detected */
#define ISR_ALD		(1 << 5)	   /* arbitration loss detected */
#define ISR_ITE		(1 << 6)	   /* tx buffer empty */
#define ISR_IRF		(1 << 7)	   /* rx buffer full */
#define ISR_GCAD	(1 << 8)	   /* general call address detected */
#define ISR_SAD		(1 << 9)	   /* slave address detected */
#define ISR_BED		(1 << 10)	   /* bus error no ACK/NAK */
#define ISR_EBB		(1 << 11)	   /* early bus busy */
#define ISR_MSD		(1 << 12)	   /* master stop detected */
#define ISR_TXDONE      (1 << 13)	   /* transaction done */
#define ISR_RXHF        (1 << 14)	   /* receive FIFO half full */
#define ISR_TXE         (1 << 15)	   /* transmit FIFO empty */
#define ISR_RXF         (1 << 16)	   /* receive FIFO full */
#define ISR_RXOV        (1 << 17)	   /* receive FIFO overrun */

struct pxa_i2c {
	spinlock_t		lock;
	wait_queue_head_t	wait;
	struct i2c_msg		*msg;
	unsigned int		msg_num;
	unsigned int		msg_idx;
	unsigned int		msg_ptr;
	unsigned int		slave_addr;
	unsigned int		req_slave_addr;

	struct i2c_adapter	adap;
	struct clk		*clk;
#ifdef CONFIG_I2C_PXA_SLAVE
	struct i2c_slave_client *slave;
#endif

	unsigned int		irqlogidx;
	u32			isrlog[32];
	u32			icrlog[32];

	void __iomem		*reg_base;
	void __iomem		*reg_ibmr;
	void __iomem		*reg_idbr;
	void __iomem		*reg_icr;
	void __iomem		*reg_isr;
	void __iomem		*reg_isar;
	void __iomem		*reg_ilcr;
	void __iomem		*reg_iwcr;
	void __iomem		*reg_wfifo;
	void __iomem		*reg_wfifo_wptr;
	void __iomem		*reg_wfifo_rptr;
	void __iomem		*reg_rfifo;
	void __iomem		*reg_rfifo_wptr;
	void __iomem		*reg_rfifo_rptr;

	unsigned long		iobase;
	unsigned long		iosize;

	int			irq;
	unsigned int		use_pio :1;
	unsigned int		fast_mode :1;
	unsigned int		high_mode:1;
	unsigned int		fifo_mode:1;
	bool			highmode_enter;
	bool			fifo_transmit_done;
	unsigned char		master_code;
	unsigned int		ilcr;
	unsigned int		iwcr;
	struct pm_qos_request	qos_idle;
	u32			fifo_write_ptr;
	u32			fifo_read_ptr;
};

#define _IBMR(i2c)	((i2c)->reg_ibmr)
#define _IDBR(i2c)	((i2c)->reg_idbr)
#define _ICR(i2c)	((i2c)->reg_icr)
#define _ISR(i2c)	((i2c)->reg_isr)
#define _ISAR(i2c)	((i2c)->reg_isar)
#define _ILCR(i2c)	((i2c)->reg_ilcr)
#define _IWCR(i2c)	((i2c)->reg_iwcr)
#define _WFIFO(i2c)		((i2c)->reg_wfifo)
#define _WFIFO_WPTR(i2c)	((i2c)->reg_wfifo_wptr)
#define _WFIFO_RPTR(i2c)	((i2c)->reg_wfifo_rptr)
#define _RFIFO(i2c)		((i2c)->reg_rfifo)
#define _RFIFO_WPTR(i2c)	((i2c)->reg_rfifo_wptr)
#define _RFIFO_RPTR(i2c)	((i2c)->reg_rfifo_rptr)

/*
 * FIFO entry information
 */
#define I2C_PXA_FIFO_ENTRY_RX	0x10
#define I2C_PXA_FIFO_ENTRY_TX	0x8

/*
 * I2C Slave mode address
 */
#define I2C_PXA_SLAVE_ADDR      0x1

#define DEBUG 0
static void i2c_pxa_reset(struct pxa_i2c *i2c);

#ifdef DEBUG

struct bits {
	u32	mask;
	const char *set;
	const char *unset;
};
#define PXA_BIT(m, s, u)	{ .mask = m, .set = s, .unset = u }

static inline void
decode_bits(const char *prefix, const struct bits *bits, int num, u32 val)
{
	printk("%s %08x: ", prefix, val);
	while (num--) {
		const char *str = val & bits->mask ? bits->set : bits->unset;
		if (str)
			printk("%s ", str);
		bits++;
	}
}

static const struct bits isr_bits[] = {
	PXA_BIT(ISR_RWM,	"RX",		"TX"),
	PXA_BIT(ISR_ACKNAK,	"NAK",		"ACK"),
	PXA_BIT(ISR_UB,		"Bsy",		"Rdy"),
	PXA_BIT(ISR_IBB,	"BusBsy",	"BusRdy"),
	PXA_BIT(ISR_SSD,	"SlaveStop",	NULL),
	PXA_BIT(ISR_ALD,	"ALD",		NULL),
	PXA_BIT(ISR_ITE,	"TxEmpty",	NULL),
	PXA_BIT(ISR_IRF,	"RxFull",	NULL),
	PXA_BIT(ISR_GCAD,	"GenCall",	NULL),
	PXA_BIT(ISR_SAD,	"SlaveAddr",	NULL),
	PXA_BIT(ISR_BED,	"BusErr",	NULL),
};

static void decode_ISR(unsigned int val)
{
	decode_bits(KERN_DEBUG "ISR", isr_bits, ARRAY_SIZE(isr_bits), val);
	printk("\n");
}

static const struct bits icr_bits[] = {
	PXA_BIT(ICR_START,  "START",	NULL),
	PXA_BIT(ICR_STOP,   "STOP",	NULL),
	PXA_BIT(ICR_ACKNAK, "ACKNAK",	NULL),
	PXA_BIT(ICR_TB,     "TB",	NULL),
	PXA_BIT(ICR_MA,     "MA",	NULL),
	PXA_BIT(ICR_SCLE,   "SCLE",	"scle"),
	PXA_BIT(ICR_IUE,    "IUE",	"iue"),
	PXA_BIT(ICR_GCD,    "GCD",	NULL),
	PXA_BIT(ICR_ITEIE,  "ITEIE",	NULL),
	PXA_BIT(ICR_IRFIE,  "IRFIE",	NULL),
	PXA_BIT(ICR_BEIE,   "BEIE",	NULL),
	PXA_BIT(ICR_SSDIE,  "SSDIE",	NULL),
	PXA_BIT(ICR_ALDIE,  "ALDIE",	NULL),
	PXA_BIT(ICR_SADIE,  "SADIE",	NULL),
	PXA_BIT(ICR_UR,     "UR",		"ur"),
};

#ifdef CONFIG_I2C_PXA_SLAVE
static void decode_ICR(unsigned int val)
{
	decode_bits(KERN_DEBUG "ICR", icr_bits, ARRAY_SIZE(icr_bits), val);
	printk("\n");
}
#endif

static unsigned int i2c_debug = DEBUG;
static struct pxa_i2c *i2c_global;

static void i2c_pxa_show_state(struct pxa_i2c *i2c, int lno, const char *fname)
{
	dev_dbg(&i2c->adap.dev, "state:%s:%d: ISR=%08x, ICR=%08x, IBMR=%02x\n", fname, lno,
		readl(_ISR(i2c)), readl(_ICR(i2c)), readl(_IBMR(i2c)));
}

#define show_state(i2c) i2c_pxa_show_state(i2c, __LINE__, __func__)

static void i2c_pxa_scream_blue_murder(struct pxa_i2c *i2c, const char *why)
{
	unsigned int i;
	struct i2c_pxa_platform_data *plat =
		(i2c->adap.dev.parent)->platform_data;

	printk(KERN_ERR"i2c: <%s> slave_0x%x error: %s\n", i2c->adap.name,
		i2c->req_slave_addr >> 1, why);
	printk(KERN_ERR "i2c: msg_num: %d msg_idx: %d msg_ptr: %d\n",
		i2c->msg_num, i2c->msg_idx, i2c->msg_ptr);
	printk(KERN_ERR "i2c: IBMR: %08x IDBR: %08x ICR: %08x ISR: %08x\n",
		readl(_IBMR(i2c)), readl(_IDBR(i2c)), readl(_ICR(i2c)),
		readl(_ISR(i2c)));
	printk(KERN_DEBUG "i2c: log: ");
	for (i = 0; i < i2c->irqlogidx; i++)
		printk("[%08x:%08x] ", i2c->isrlog[i], i2c->icrlog[i]);
	printk("\n");
	if (strcmp(why, "exhausted retries") != 0) {
		if (plat && plat->i2c_bus_reset)
			plat->i2c_bus_reset(i2c->adap.nr);
		/* reset i2c contorler when it's fail */
		i2c_pxa_reset(i2c);
	}
}

#else /* ifdef DEBUG */

#define i2c_debug	0

#define show_state(i2c) do { } while (0)
#define decode_ISR(val) do { } while (0)
#define decode_ICR(val) do { } while (0)
#define i2c_pxa_scream_blue_murder(i2c, why) do { } while (0)

#endif /* ifdef DEBUG / else */

static void i2c_pxa_master_complete(struct pxa_i2c *i2c, int ret);
static irqreturn_t i2c_pxa_handler(int this_irq, void *dev_id);

static inline int i2c_pxa_is_slavemode(struct pxa_i2c *i2c)
{
	return !(readl(_ICR(i2c)) & ICR_SCLE);
}

static void i2c_pxa_abort(struct pxa_i2c *i2c)
{
	int i = 250;

	if (i2c_pxa_is_slavemode(i2c)) {
		dev_dbg(&i2c->adap.dev, "%s: called in slave mode\n", __func__);
		return;
	}

	while ((i > 0) && (readl(_IBMR(i2c)) & 0x1) == 0) {
		unsigned long icr = readl(_ICR(i2c));

		icr &= ~ICR_START;
		icr |= ICR_ACKNAK | ICR_STOP | ICR_TB;

		writel(icr, _ICR(i2c));

		show_state(i2c);

		mdelay(1);
		i --;
	}

	writel(readl(_ICR(i2c)) & ~(ICR_MA | ICR_START | ICR_STOP),
	       _ICR(i2c));
}

static int i2c_pxa_wait_bus_not_busy(struct pxa_i2c *i2c)
{
	int timeout = DEF_TIMEOUT;

	if (readl(_ISR(i2c)) & (ISR_IBB | ISR_UB)) {
		i2c_pxa_reset(i2c);
		timeout /= 2;
	}

	while (timeout-- && readl(_ISR(i2c)) & (ISR_IBB | ISR_UB)) {
		if ((readl(_ISR(i2c)) & ISR_SAD) != 0)
			timeout += 4;

		msleep(2);
		show_state(i2c);
	}

	if (timeout < 0)
		show_state(i2c);

	return timeout < 0 ? I2C_RETRY : 0;
}

static int i2c_pxa_wait_master(struct pxa_i2c *i2c)
{
	unsigned long timeout = jiffies + HZ*4;

	while (time_before(jiffies, timeout)) {
		if (i2c_debug > 1)
			dev_dbg(&i2c->adap.dev, "%s: %ld: ISR=%08x, ICR=%08x, IBMR=%02x\n",
				__func__, (long)jiffies, readl(_ISR(i2c)), readl(_ICR(i2c)), readl(_IBMR(i2c)));

		if (readl(_ISR(i2c)) & ISR_SAD) {
			if (i2c_debug > 0)
				dev_dbg(&i2c->adap.dev, "%s: Slave detected\n", __func__);
			goto out;
		}

		/* wait for unit and bus being not busy, and we also do a
		 * quick check of the i2c lines themselves to ensure they've
		 * gone high...
		 */
		if ((readl(_ISR(i2c)) & (ISR_UB | ISR_IBB)) == 0 && readl(_IBMR(i2c)) == 3) {
			if (i2c_debug > 0)
				dev_dbg(&i2c->adap.dev, "%s: done\n", __func__);
			return 1;
		}

		msleep(1);
	}

	if (i2c_debug > 0)
		dev_dbg(&i2c->adap.dev, "%s: did not free\n", __func__);
 out:
	return 0;
}

static int i2c_pxa_set_master(struct pxa_i2c *i2c)
{
	if (i2c_debug)
		dev_dbg(&i2c->adap.dev, "setting to bus master\n");

	if ((readl(_ISR(i2c)) & (ISR_UB | ISR_IBB)) != 0) {
		dev_dbg(&i2c->adap.dev, "%s: unit is busy\n", __func__);
		if (!i2c_pxa_wait_master(i2c)) {
			dev_dbg(&i2c->adap.dev, "%s: error: unit busy\n", __func__);
			return I2C_RETRY;
		}
	}

	writel(readl(_ICR(i2c)) | ICR_SCLE, _ICR(i2c));
	return 0;
}

#ifdef CONFIG_I2C_PXA_SLAVE
static int i2c_pxa_wait_slave(struct pxa_i2c *i2c)
{
	unsigned long timeout = jiffies + HZ*1;

	/* wait for stop */

	show_state(i2c);

	while (time_before(jiffies, timeout)) {
		if (i2c_debug > 1)
			dev_dbg(&i2c->adap.dev, "%s: %ld: ISR=%08x, ICR=%08x, IBMR=%02x\n",
				__func__, (long)jiffies, readl(_ISR(i2c)), readl(_ICR(i2c)), readl(_IBMR(i2c)));

		if ((readl(_ISR(i2c)) & (ISR_UB|ISR_IBB)) == 0 ||
		    (readl(_ISR(i2c)) & ISR_SAD) != 0 ||
		    (readl(_ICR(i2c)) & ICR_SCLE) == 0) {
			if (i2c_debug > 1)
				dev_dbg(&i2c->adap.dev, "%s: done\n", __func__);
			return 1;
		}

		msleep(1);
	}

	if (i2c_debug > 0)
		dev_dbg(&i2c->adap.dev, "%s: did not free\n", __func__);
	return 0;
}

/*
 * clear the hold on the bus, and take of anything else
 * that has been configured
 */
static void i2c_pxa_set_slave(struct pxa_i2c *i2c, int errcode)
{
	show_state(i2c);

	if (errcode < 0) {
		udelay(100);   /* simple delay */
	} else {
		/* we need to wait for the stop condition to end */

		/* if we where in stop, then clear... */
		if (readl(_ICR(i2c)) & ICR_STOP) {
			udelay(100);
			writel(readl(_ICR(i2c)) & ~ICR_STOP, _ICR(i2c));
		}

		if (!i2c_pxa_wait_slave(i2c)) {
			dev_err(&i2c->adap.dev, "%s: wait timedout\n",
				__func__);
			return;
		}
	}

	writel(readl(_ICR(i2c)) & ~(ICR_STOP|ICR_ACKNAK|ICR_MA), _ICR(i2c));
	writel(readl(_ICR(i2c)) & ~ICR_SCLE, _ICR(i2c));

	if (i2c_debug) {
		dev_dbg(&i2c->adap.dev, "ICR now %08x, ISR %08x\n", readl(_ICR(i2c)), readl(_ISR(i2c)));
		decode_ICR(readl(_ICR(i2c)));
	}
}
#else
#define i2c_pxa_set_slave(i2c, err)	do { } while (0)
#endif

static void flush_fifo_registers(struct pxa_i2c *i2c)
{
	writel(0, _WFIFO_WPTR(i2c));
	writel(0, _WFIFO_RPTR(i2c));
	writel(0, _RFIFO_WPTR(i2c));
	writel(0, _RFIFO_RPTR(i2c));
}

static void i2c_pxa_reset(struct pxa_i2c *i2c)
{
	pr_debug("Resetting I2C Controller Unit\n");

	/* abort any transfer currently under way */
	i2c_pxa_abort(i2c);

	/* reset according to 9.8 */
	writel(ICR_UR, _ICR(i2c));
	writel(I2C_ISR_INIT, _ISR(i2c));
	writel(readl(_ICR(i2c)) & ~ICR_UR, _ICR(i2c));

#ifdef CONFIG_I2C_PXA_SLAVE
	if (i2c->reg_isar)
		writel(i2c->slave_addr, _ISAR(i2c));
#endif

	/* set control register values */
	writel(I2C_ICR_INIT | (i2c->fast_mode ? ICR_FM : 0), _ICR(i2c));
	writel(readl(_ICR(i2c)) | (i2c->high_mode ? ICR_HS : 0), _ICR(i2c));

	/* There are 2 multi-masters on the I2C bus - APPS and COMM
	 * It is important both uses the standard frequency
	 * Adjust clock by ILCR, IWCR is important
	 */
	if (i2c->ilcr)
		writel(i2c->ilcr, _ILCR(i2c));
	if (i2c->iwcr)
		writel(i2c->iwcr, _IWCR(i2c));
	udelay(2);


#ifdef CONFIG_I2C_PXA_SLAVE
	dev_info(&i2c->adap.dev, "Enabling slave mode\n");
	writel(readl(_ICR(i2c)) | ICR_SADIE | ICR_ALDIE | ICR_SSDIE, _ICR(i2c));
#endif

	i2c_pxa_set_slave(i2c, 0);

	/* enable unit */
	writel(readl(_ICR(i2c)) | ICR_IUE, _ICR(i2c));
	udelay(100);
}


#ifdef CONFIG_I2C_PXA_SLAVE
/*
 * PXA I2C Slave mode
 */

static void i2c_pxa_slave_txempty(struct pxa_i2c *i2c, u32 isr)
{
	if (isr & ISR_BED) {
		/* what should we do here? */
	} else {
		int ret = 0;

		if (i2c->slave != NULL)
			ret = i2c->slave->read(i2c->slave->data);

		writel(ret, _IDBR(i2c));
		writel(readl(_ICR(i2c)) | ICR_TB, _ICR(i2c));   /* allow next byte */
	}
}

static void i2c_pxa_slave_rxfull(struct pxa_i2c *i2c, u32 isr)
{
	unsigned int byte = readl(_IDBR(i2c));

	if (i2c->slave != NULL)
		i2c->slave->write(i2c->slave->data, byte);

	writel(readl(_ICR(i2c)) | ICR_TB, _ICR(i2c));
}

static void i2c_pxa_slave_start(struct pxa_i2c *i2c, u32 isr)
{
	int timeout;

	if (i2c_debug > 0)
		dev_dbg(&i2c->adap.dev, "SAD, mode is slave-%cx\n",
		       (isr & ISR_RWM) ? 'r' : 't');

	if (i2c->slave != NULL)
		i2c->slave->event(i2c->slave->data,
				 (isr & ISR_RWM) ? I2C_SLAVE_EVENT_START_READ : I2C_SLAVE_EVENT_START_WRITE);

	/*
	 * slave could interrupt in the middle of us generating a
	 * start condition... if this happens, we'd better back off
	 * and stop holding the poor thing up
	 */
	writel(readl(_ICR(i2c)) & ~(ICR_START|ICR_STOP), _ICR(i2c));
	writel(readl(_ICR(i2c)) | ICR_TB, _ICR(i2c));

	timeout = 0x10000;

	while (1) {
		if ((readl(_IBMR(i2c)) & 2) == 2)
			break;

		timeout--;

		if (timeout <= 0) {
			dev_err(&i2c->adap.dev, "timeout waiting for SCL high\n");
			break;
		}
	}

	writel(readl(_ICR(i2c)) & ~ICR_SCLE, _ICR(i2c));
}

static void i2c_pxa_slave_stop(struct pxa_i2c *i2c)
{
	if (i2c_debug > 2)
		dev_dbg(&i2c->adap.dev, "ISR: SSD (Slave Stop)\n");

	if (i2c->slave != NULL)
		i2c->slave->event(i2c->slave->data, I2C_SLAVE_EVENT_STOP);

	if (i2c_debug > 2)
		dev_dbg(&i2c->adap.dev, "ISR: SSD (Slave Stop) acked\n");

	/*
	 * If we have a master-mode message waiting,
	 * kick it off now that the slave has completed.
	 */
	if (i2c->msg)
		i2c_pxa_master_complete(i2c, I2C_RETRY);
}
#else
static void i2c_pxa_slave_txempty(struct pxa_i2c *i2c, u32 isr)
{
	if (isr & ISR_BED) {
		/* what should we do here? */
	} else {
		writel(0, _IDBR(i2c));
		writel(readl(_ICR(i2c)) | ICR_TB, _ICR(i2c));
	}
}

static void i2c_pxa_slave_rxfull(struct pxa_i2c *i2c, u32 isr)
{
	writel(readl(_ICR(i2c)) | ICR_TB | ICR_ACKNAK, _ICR(i2c));
}

static void i2c_pxa_slave_start(struct pxa_i2c *i2c, u32 isr)
{
	int timeout;

	/*
	 * slave could interrupt in the middle of us generating a
	 * start condition... if this happens, we'd better back off
	 * and stop holding the poor thing up
	 */
	writel(readl(_ICR(i2c)) & ~(ICR_START|ICR_STOP), _ICR(i2c));
	writel(readl(_ICR(i2c)) | ICR_TB | ICR_ACKNAK, _ICR(i2c));

	timeout = 0x10000;

	while (1) {
		if ((readl(_IBMR(i2c)) & 2) == 2)
			break;

		timeout--;

		if (timeout <= 0) {
			dev_err(&i2c->adap.dev, "timeout waiting for SCL high\n");
			break;
		}
	}

	writel(readl(_ICR(i2c)) & ~ICR_SCLE, _ICR(i2c));
}

static void i2c_pxa_slave_stop(struct pxa_i2c *i2c)
{
	if (i2c->msg)
		i2c_pxa_master_complete(i2c, I2C_RETRY);
}
#endif

/*
 * PXA I2C Master mode
 */

static int i2c_pxa_send_mastercode(struct pxa_i2c *i2c)
{
	u32 icr;
	long timeout;

	spin_lock_irq(&i2c->lock);

	i2c->highmode_enter = true;

	icr = readl(_ICR(i2c));
	icr |= ICR_HS;  /* bit_15 clear or set already done by reset */

	icr |= ICR_GPIOEN;
	writel(i2c->master_code, _IDBR(i2c));

	icr |= ICR_START | ICR_TB | ICR_ITEIE;
	icr &= ~ICR_STOP;
	icr &= ~ICR_ALDIE;
	writel(icr, _ICR(i2c));

	spin_unlock_irq(&i2c->lock);

	timeout = wait_event_timeout(i2c->wait, \
		i2c->highmode_enter == false, HZ * 1);
	i2c->highmode_enter = false;

	if (!timeout)
		return 1;
	else
		return 0;
}

static inline unsigned int i2c_pxa_addr_byte(struct i2c_msg *msg)
{
	unsigned int addr = (msg->addr & 0x7f) << 1;

	if (msg->flags & I2C_M_RD)
		addr |= 1;

	return addr;
}

static inline void i2c_pxa_start_message(struct pxa_i2c *i2c)
{
	u32 icr;

	/*
	 * Step 1: target slave address into IDBR
	 */
	writel(i2c_pxa_addr_byte(i2c->msg), _IDBR(i2c));
	i2c->req_slave_addr = i2c_pxa_addr_byte(i2c->msg);

	/*
	 * Step 2: initiate the write.
	 */
	icr = readl(_ICR(i2c)) & ~(ICR_STOP | ICR_ALDIE);
	writel(icr | ICR_START | ICR_TB, _ICR(i2c));
}

static inline void i2c_pxa_stop_message(struct pxa_i2c *i2c)
{
	u32 icr;

	/*
	 * Clear the STOP and ACK flags
	 */
	icr = readl(_ICR(i2c));
	icr &= ~(ICR_STOP | ICR_ACKNAK);
	writel(icr, _ICR(i2c));
}

static void i2c_pxa_start_fifo(struct pxa_i2c *i2c)
{
	u32 icr;

	icr = readl(_ICR(i2c));
	icr |= ICR_FIFOEN | ICR_TXE_IE | ICR_TXDONE_IE;
	icr |= ICR_RXHF_IE | ICR_RXF_IE | ICR_RXOV_IE;
	icr &= ~(ICR_DMA_EN | ICR_ITEIE | ICR_IRFIE);

	writel(icr, _ICR(i2c));
}

static void i2c_pxa_exit_fifo(struct pxa_i2c *i2c)
{
	u32 icr;

	icr = readl(_ICR(i2c));
	icr &= ~(ICR_FIFOEN | ICR_TXE_IE | ICR_TXDONE_IE);
	icr &= ~(ICR_RXHF_IE | ICR_RXF_IE | ICR_RXOV_IE);
	icr &= ~(ICR_START | ICR_STOP | ICR_ACKNAK | ICR_TB);

	writel(icr, _ICR(i2c));
	writel(0xFFFFF, _ISR(i2c));
}

static inline void i2c_pxa_fifo_line(struct pxa_i2c *i2c, char data, char icr)
{
	u16 wfifo;

	wfifo = readl(_WFIFO(i2c));
	wfifo |= (icr & 0xF) << 8;
	wfifo |= data;
	writel(wfifo, _WFIFO(i2c));
	i2c->fifo_write_ptr++;
}

static void i2c_pxa_fifo_load(struct pxa_i2c *i2c)
{
	char data, icr = 0;

	i2c->fifo_write_ptr = 0;

	if ((i2c->msg_ptr == 0) && (i2c->msg_idx == 0)) {
		if (i2c->high_mode) {
			writel(readl(_ICR(i2c)) | ICR_GPIOEN, _ICR(i2c));

			icr |= ICR_START | ICR_TB;
			icr &= ~(ICR_STOP | ICR_ACKNAK);
			data = i2c->master_code;
			i2c_pxa_fifo_line(i2c, data, icr);
		}

		icr |= ICR_START | ICR_TB;
		icr &= ~(ICR_STOP | ICR_ACKNAK);
		data = i2c_pxa_addr_byte(i2c->msg);
		i2c_pxa_fifo_line(i2c, data, icr);
	}

	while (i2c->fifo_write_ptr < I2C_PXA_FIFO_ENTRY_TX) {
		if (i2c->msg_ptr < i2c->msg->len) {
			if (i2c->msg->buf == NULL) {
				i2c_pxa_scream_blue_murder(i2c, "null buffer");
				i2c->msg_idx = XFER_NAKED;
				i2c->fifo_transmit_done = true;
				wake_up(&i2c->wait);
				return;
			}

			icr |= ICR_TB;
			icr &= ~(ICR_START | ICR_STOP | ICR_ACKNAK);
			data = i2c->msg->buf[i2c->msg_ptr++];

			if ((i2c->msg_ptr == i2c->msg->len) && \
				(i2c->msg_idx == i2c->msg_num - 1)) {
				icr |= ICR_STOP;

				if (i2c->msg->flags & I2C_M_RD)
					icr |= ICR_ACKNAK;

				i2c_pxa_fifo_line(i2c, data, icr);
				writel((readl(_ICR(i2c)) & ~ICR_TXE_IE),
					_ICR(i2c));

				break;
			}

			i2c_pxa_fifo_line(i2c, data, icr);
		} else if (i2c->msg_idx < i2c->msg_num - 1) {
			/*
			 * Next segment of the message.
			 */
			i2c->msg_ptr = 0;
			i2c->msg_idx++;
			i2c->msg++;

			/*
			 * If we aren't doing a repeated start and address,
			 * go back and try to send the next byte.  Note that
			 * we do not support switching the R/W direction here.
			 */
			if (i2c->msg->flags & I2C_M_NOSTART)
				continue;

			/* write the next address and trigger a repeat start */
			icr |= ICR_START | ICR_TB;
			icr &= ~(ICR_STOP | ICR_ACKNAK);
			data = i2c_pxa_addr_byte(i2c->msg);
			i2c_pxa_fifo_line(i2c, data, icr);
		} else {
			if (i2c->msg->len == 0) {
				/*
				 * Device probes have a message length of zero
				 * and need the bus to be reset before it can
				 * be used again.
				 */
				i2c_pxa_reset(i2c);
			}
			return;
		}
	}
}

static int i2c_pxa_pio_set_master(struct pxa_i2c *i2c)
{
	/* make timeout the same as for interrupt based functions */
	long timeout = 2 * DEF_TIMEOUT;

	/*
	 * Wait for the bus to become free.
	 */
	while (timeout-- && readl(_ISR(i2c)) & (ISR_IBB | ISR_UB)) {
		udelay(1000);
		show_state(i2c);
	}

	if (timeout < 0) {
		show_state(i2c);
		dev_err(&i2c->adap.dev,
			"i2c_pxa: timeout waiting for bus free\n");
		return I2C_RETRY;
	}

	/*
	 * Set master mode.
	 */
	writel(readl(_ICR(i2c)) | ICR_SCLE, _ICR(i2c));

	return 0;
}

static int i2c_pxa_do_pio_xfer(struct pxa_i2c *i2c,
			       struct i2c_msg *msg, int num)
{
	unsigned long timeout = 100000; /* 1 seconds */
	int ret = 0;

	ret = i2c_pxa_pio_set_master(i2c);
	if (ret)
		goto out;

	i2c->msg = msg;
	i2c->msg_num = num;
	i2c->msg_idx = 0;
	i2c->msg_ptr = 0;
	i2c->irqlogidx = 0;

	pm_qos_update_request(&i2c->qos_idle, PM_QOS_CPUIDLE_BLOCK_DDR_VALUE);

	i2c_pxa_start_message(i2c);

	while (i2c->msg_num > 0 && --timeout) {
		i2c_pxa_handler(0, i2c);
		udelay(10);
	}

	i2c_pxa_stop_message(i2c);

	pm_qos_update_request(&i2c->qos_idle,
			PM_QOS_CPUIDLE_BLOCK_DEFAULT_VALUE);
	/*
	 * We place the return code in i2c->msg_idx.
	 */
	ret = i2c->msg_idx;

out:
	if (timeout == 0)
		i2c_pxa_scream_blue_murder(i2c, "timeout");

	if (ret < 0)
		i2c_pxa_reset(i2c);

	return ret;
}

/*
 * We are protected by the adapter bus mutex.
 */
static int
i2c_pxa_do_xfer_fifo(struct pxa_i2c *i2c, struct i2c_msg *msg, int num)
{
	long timeout;
	int ret;

	/*
	 * Wait for the bus to become free.
	 */
	ret = i2c_pxa_wait_bus_not_busy(i2c);
	if (ret) {
		dev_err(&i2c->adap.dev, "i2c_pxa: timeout waiting for bus free\n");
		goto out;
	}

	/*
	 * Set master mode.
	 */
	ret = i2c_pxa_set_master(i2c);
	if (ret) {
		dev_err(&i2c->adap.dev, "i2c_pxa_set_master: error %d\n", ret);
		goto out;
	}

	spin_lock_irq(&i2c->lock);

	i2c->msg = msg;
	i2c->msg_num = num;
	i2c->msg_idx = 0;
	i2c->msg_ptr = 0;
	i2c->fifo_write_ptr = 0;
	i2c->fifo_read_ptr = 0;
	i2c->fifo_transmit_done = false;
	i2c->irqlogidx = 0;
	i2c->req_slave_addr = i2c_pxa_addr_byte(i2c->msg);

	pm_qos_update_request(&i2c->qos_idle, PM_QOS_CPUIDLE_BLOCK_DDR_VALUE);

	flush_fifo_registers(i2c);
	i2c_pxa_start_fifo(i2c);

	spin_unlock_irq(&i2c->lock);

	timeout = wait_event_timeout(i2c->wait,
		i2c->fifo_transmit_done == true, HZ * 1);
	i2c_pxa_exit_fifo(i2c);

	pm_qos_update_request(&i2c->qos_idle,
			PM_QOS_CPUIDLE_BLOCK_DEFAULT_VALUE);

	/*
	 * We place the return code in i2c->msg_idx.
	 */
	ret = i2c->msg_idx;

	if (!timeout && i2c->msg_num) {
		i2c_pxa_scream_blue_murder(i2c, "timeout");
		ret = I2C_RETRY;
	}

 out:
	if (ret < 0)
		i2c_pxa_reset(i2c);

	return ret;
}

static int i2c_pxa_do_xfer(struct pxa_i2c *i2c, struct i2c_msg *msg, int num)
{
	long timeout;
	int ret;

	/*
	 * Wait for the bus to become free.
	 */
	ret = i2c_pxa_wait_bus_not_busy(i2c);
	if (ret) {
		dev_err(&i2c->adap.dev, "i2c_pxa: timeout waiting for bus free\n");
		goto out;
	}

	/*
	 * Set master mode.
	 */
	ret = i2c_pxa_set_master(i2c);
	if (ret) {
		dev_err(&i2c->adap.dev, "i2c_pxa_set_master: error %d\n", ret);
		goto out;
	}

	if (i2c->high_mode) {
		ret = i2c_pxa_send_mastercode(i2c);
		if (ret) {
			dev_err(&i2c->adap.dev, "i2c_pxa_send_mastercode timeout\n");
			writel(readl(_ICR(i2c)) & ~(ICR_START | ICR_GPIOEN),
				_ICR(i2c));
			goto out;
		}
	}

	spin_lock_irq(&i2c->lock);

	i2c->msg = msg;
	i2c->msg_num = num;
	i2c->msg_idx = 0;
	i2c->msg_ptr = 0;
	i2c->irqlogidx = 0;

	pm_qos_update_request(&i2c->qos_idle, PM_QOS_CPUIDLE_BLOCK_DDR_VALUE);

	i2c_pxa_start_message(i2c);

	spin_unlock_irq(&i2c->lock);

	/*
	 * The rest of the processing occurs in the interrupt handler.
	 */
	timeout = wait_event_timeout(i2c->wait, i2c->msg_num == 0, HZ * 1);
	i2c_pxa_stop_message(i2c);

	pm_qos_update_request(&i2c->qos_idle,
			PM_QOS_CPUIDLE_BLOCK_DEFAULT_VALUE);

	/*
	 * We place the return code in i2c->msg_idx.
	 */
	ret = i2c->msg_idx;

	if (!timeout && i2c->msg_num) {
		i2c_pxa_scream_blue_murder(i2c, "timeout");
		ret = I2C_RETRY;
	}

 out:
	if (ret < 0)
		i2c_pxa_reset(i2c);

	return ret;
}

static int i2c_pxa_pio_xfer(struct i2c_adapter *adap,
			    struct i2c_msg msgs[], int num)
{
	struct pxa_i2c *i2c = adap->algo_data;
	int ret, i;
	pr_info("--> %s is called to r/w i2c reg\n", __func__);

	/* If the I2C controller is disabled we need to reset it
	  (probably due to a suspend/resume destroying state). We do
	  this here as we can then avoid worrying about resuming the
	  controller before its users. */
	if (!(readl(_ICR(i2c)) & ICR_IUE))
		i2c_pxa_reset(i2c);

	for (i = adap->retries; i >= 0; i--) {
		ret = i2c_pxa_do_pio_xfer(i2c, msgs, num);
		if (ret != I2C_RETRY)
			goto out;

		if (i2c_debug)
			dev_dbg(&adap->dev, "Retrying transmission\n");
		udelay(100);
	}
	i2c_pxa_scream_blue_murder(i2c, "exhausted retries");
	ret = -EREMOTEIO;
 out:
	i2c_pxa_set_slave(i2c, ret);
	return ret;
}

/*
 * i2c_pxa_master_complete - complete the message and wake up.
 */
static void i2c_pxa_master_complete(struct pxa_i2c *i2c, int ret)
{
	i2c->msg_ptr = 0;
	i2c->msg = NULL;
	i2c->msg_idx ++;
	i2c->msg_num = 0;
	if (ret)
		i2c->msg_idx = ret;
	if (!i2c->use_pio)
		wake_up(&i2c->wait);
}

static void i2c_pxa_irq_txempty_fifo(struct pxa_i2c *i2c, u32 isr)
{
	/*
	 * If ISR_ALD is set, we lost arbitration.
	 */
	if (isr & ISR_ALD) {
		/*
		 * Do we need to do anything here?  The PXA docs
		 * are vague about what happens.
		 */
		i2c_pxa_scream_blue_murder(i2c, "ALD set");

		/*
		 * We ignore this error.  We seem to see spurious ALDs
		 * for seemingly no reason.  If we handle them as I think
		 * they should, we end up causing an I2C error, which
		 * is painful for some systems.
		 */
		i2c->fifo_transmit_done = true;
		wake_up(&i2c->wait);
		return; /* ignore */
	}

	if (isr & ISR_BED) {
		/*
		 * I2C bus error - either the device NAK'd us, or
		 * something more serious happened.  If we were NAK'd
		 * on the initial address phase, we can retry.
		 */
		i2c->fifo_transmit_done = true;
		i2c_pxa_master_complete(i2c, BUS_ERROR);
		return;
	}

	if (isr & ISR_RXF) {
		u32 i;

		if (i2c->msg->flags & I2C_M_RD) {
			for (i = 0; ((i < I2C_PXA_FIFO_ENTRY_RX) &&
				(i2c->fifo_read_ptr < i2c->msg->len)); i++)
				i2c->msg->buf[i2c->fifo_read_ptr++] =
					readl(_RFIFO(i2c));
		}
		writel(ISR_RXF, _ISR(i2c));
	}

	if (isr & ISR_RXHF) {
		u32 i;

		/* if receive both RXF & RXHF, read one time in ISR_RXF */
		if (!(isr & ISR_RXF) && (i2c->msg->flags & I2C_M_RD)) {
			for (i = 0; ((i < I2C_PXA_FIFO_ENTRY_RX / 2) &&
				(i2c->fifo_read_ptr < i2c->msg->len)); i++)
				i2c->msg->buf[i2c->fifo_read_ptr++] =
					readl(_RFIFO(i2c));
		}
		writel(ISR_RXHF, _ISR(i2c));
	}

	if (isr & ISR_RXOV) {
		writel(ISR_RXOV, _ISR(i2c));
		i2c_pxa_scream_blue_murder(i2c, "lost data");
	}

	if (isr & ISR_TXDONE) {
		writel(readl(_ICR(i2c)) & ~(ICR_TXE_IE | \
			ICR_RXHF_IE | ICR_RXF_IE), _ICR(i2c));
		writel(0xFFFFF, _ISR(i2c));

		if (i2c->msg->flags & I2C_M_RD)
			while (i2c->fifo_read_ptr < i2c->msg->len)
				i2c->msg->buf[i2c->fifo_read_ptr++] =
					readl(_RFIFO(i2c));

		i2c->fifo_transmit_done = true;
		i2c_pxa_master_complete(i2c, 0);
		return;
	}

	if (isr & ISR_TXE) {
		i2c_pxa_fifo_load(i2c);
		writel(ISR_TXE, _ISR(i2c));
	}

	show_state(i2c);
}

static void i2c_pxa_irq_txempty(struct pxa_i2c *i2c, u32 isr)
{
	u32 icr = readl(_ICR(i2c)) & ~(ICR_START|ICR_STOP|ICR_ACKNAK|ICR_TB);

 again:
	/*
	 * If ISR_ALD is set, we lost arbitration.
	 */
	if (isr & ISR_ALD) {
		/*
		 * Do we need to do anything here?  The PXA docs
		 * are vague about what happens.
		 */
		i2c_pxa_scream_blue_murder(i2c, "ALD set");

		/*
		 * We ignore this error.  We seem to see spurious ALDs
		 * for seemingly no reason.  If we handle them as I think
		 * they should, we end up causing an I2C error, which
		 * is painful for some systems.
		 */
		return; /* ignore */
	}

	if (isr & ISR_BED) {
		int ret = BUS_ERROR;

		/*
		 * I2C bus error - either the device NAK'd us, or
		 * something more serious happened.  If we were NAK'd
		 * on the initial address phase, we can retry.
		 */
		if (isr & ISR_ACKNAK) {
			if (i2c->msg_ptr == 0 && i2c->msg_idx == 0)
				ret = I2C_RETRY;
			else
				ret = XFER_NAKED;
		}
		i2c_pxa_master_complete(i2c, ret);
	} else if (isr & ISR_RWM) {
		/*
		 * Read mode.  We have just sent the address byte, and
		 * now we must initiate the transfer.
		 */
		if (i2c->msg_ptr == i2c->msg->len - 1 &&
		    i2c->msg_idx == i2c->msg_num - 1)
			icr |= ICR_STOP | ICR_ACKNAK;

		icr |= ICR_ALDIE | ICR_TB;
	} else if (i2c->msg_ptr < i2c->msg->len) {
		/*
		 * Write mode.  Write the next data byte.
		 */
		writel(i2c->msg->buf[i2c->msg_ptr++], _IDBR(i2c));

		icr |= ICR_ALDIE | ICR_TB;

		/*
		 * If this is the last byte of the last message, send
		 * a STOP.
		 */
		if (i2c->msg_ptr == i2c->msg->len &&
		    i2c->msg_idx == i2c->msg_num - 1)
			icr |= ICR_STOP;
	} else if (i2c->msg_idx < i2c->msg_num - 1) {
		/*
		 * Next segment of the message.
		 */
		i2c->msg_ptr = 0;
		i2c->msg_idx ++;
		i2c->msg++;

		/*
		 * If we aren't doing a repeated start and address,
		 * go back and try to send the next byte.  Note that
		 * we do not support switching the R/W direction here.
		 */
		if (i2c->msg->flags & I2C_M_NOSTART)
			goto again;

		/*
		 * Write the next address.
		 */
		writel(i2c_pxa_addr_byte(i2c->msg), _IDBR(i2c));
		i2c->req_slave_addr = i2c_pxa_addr_byte(i2c->msg);

		/*
		 * And trigger a repeated start, and send the byte.
		 */
		icr &= ~ICR_ALDIE;
		icr |= ICR_START | ICR_TB;
	} else {
		if (i2c->msg->len == 0) {
			/*
			 * Device probes have a message length of zero
			 * and need the bus to be reset before it can
			 * be used again.
			 */
			i2c_pxa_reset(i2c);
		}
		i2c_pxa_master_complete(i2c, 0);
	}

	i2c->icrlog[i2c->irqlogidx-1] = icr;

	writel(icr, _ICR(i2c));
	show_state(i2c);
}

static void i2c_pxa_irq_rxfull(struct pxa_i2c *i2c, u32 isr)
{
	u32 icr = readl(_ICR(i2c)) & ~(ICR_START|ICR_STOP|ICR_ACKNAK|ICR_TB);

	/*
	 * Read the byte.
	 */
	i2c->msg->buf[i2c->msg_ptr++] = readl(_IDBR(i2c));

	if (i2c->msg_ptr < i2c->msg->len) {
		/*
		 * If this is the last byte of the last
		 * message, send a STOP.
		 */
		if (i2c->msg_ptr == i2c->msg->len - 1)
			icr |= ICR_STOP | ICR_ACKNAK;

		icr |= ICR_ALDIE | ICR_TB;
	} else {
		i2c_pxa_master_complete(i2c, 0);
	}

	i2c->icrlog[i2c->irqlogidx-1] = icr;

	writel(icr, _ICR(i2c));
}

#define VALID_INT_SOURCE	(ISR_SSD | ISR_ALD | ISR_ITE | ISR_IRF | \
				ISR_SAD | ISR_BED)
#define FIFO_INT_SOURCE		(ISR_TXE | ISR_TXDONE | ISR_RXHF | \
				ISR_RXF | ISR_RXOV)
static irqreturn_t i2c_pxa_handler(int this_irq, void *dev_id)
{
	struct pxa_i2c *i2c = dev_id;
	u32 isr = readl(_ISR(i2c));

	if (!(isr & VALID_INT_SOURCE) && !(isr & FIFO_INT_SOURCE))
		return IRQ_NONE;

	if (i2c_debug > 2 && 0) {
		dev_dbg(&i2c->adap.dev, "%s: ISR=%08x, ICR=%08x, IBMR=%02x\n",
			__func__, isr, readl(_ICR(i2c)), readl(_IBMR(i2c)));
		decode_ISR(isr);
	}

	if (i2c->irqlogidx < ARRAY_SIZE(i2c->isrlog))
		i2c->isrlog[i2c->irqlogidx++] = isr;

	show_state(i2c);

	/*
	 * Always clear all pending IRQs.
	 */
	writel(isr & VALID_INT_SOURCE, _ISR(i2c));

	if (isr & ISR_SAD)
		i2c_pxa_slave_start(i2c, isr);
	if (isr & ISR_SSD)
		i2c_pxa_slave_stop(i2c);

	if (i2c_pxa_is_slavemode(i2c)) {
		if (isr & ISR_ITE)
			i2c_pxa_slave_txempty(i2c, isr);
		if (isr & ISR_IRF)
			i2c_pxa_slave_rxfull(i2c, isr);
	} else if (i2c->msg && (!i2c->highmode_enter)) {
		if (i2c->fifo_mode) {
			i2c_pxa_irq_txempty_fifo(i2c, isr);
		} else {
			if (isr & ISR_ITE)
				i2c_pxa_irq_txempty(i2c, isr);
			if (isr & ISR_IRF)
				i2c_pxa_irq_rxfull(i2c, isr);
		}
	} else if ((isr & ISR_ITE) && i2c->highmode_enter) {
		i2c->highmode_enter = false;
		wake_up(&i2c->wait);
	} else {
		i2c_pxa_scream_blue_murder(i2c, "spurious irq");
	}

	return IRQ_HANDLED;
}


static int i2c_pxa_xfer(struct i2c_adapter *adap, struct i2c_msg msgs[], int num)
{
	struct pxa_i2c *i2c = adap->algo_data;
	int ret, i;

	enable_irq(i2c->irq);
	for (i = adap->retries; i >= 0; i--) {
		if (i2c->fifo_mode)
			ret = i2c_pxa_do_xfer_fifo(i2c, msgs, num);
		else
			ret = i2c_pxa_do_xfer(i2c, msgs, num);
		if (ret != I2C_RETRY)
			goto out;

		if (i2c_debug)
			dev_dbg(&adap->dev, "Retrying transmission\n");
		udelay(100);
	}
	i2c_pxa_scream_blue_murder(i2c, "exhausted retries");
	ret = -EREMOTEIO;
 out:
	i2c_pxa_set_slave(i2c, ret);
	disable_irq(i2c->irq);
	return ret;
}

static u32 i2c_pxa_functionality(struct i2c_adapter *adap)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL;
}

static const struct i2c_algorithm i2c_pxa_algorithm = {
	.master_xfer	= i2c_pxa_xfer,
	.functionality	= i2c_pxa_functionality,
};

static const struct i2c_algorithm i2c_pxa_pio_algorithm = {
	.master_xfer	= i2c_pxa_pio_xfer,
	.functionality	= i2c_pxa_functionality,
};

int i2c_set_pio_mode(void)
{
	printk(KERN_EMERG "Set I2C to PIO mode\n");
	i2c_global->adap.algo = &i2c_pxa_pio_algorithm;
	return 0;
}

static int panic_notifier_func(struct notifier_block *this,
		unsigned long code, void *cmd)
{
	i2c_global->adap.algo = &i2c_pxa_pio_algorithm;
	return NOTIFY_DONE;
}

static struct notifier_block panic_block = {
	.notifier_call = panic_notifier_func,
	.next = NULL,
	.priority = INT_MAX,
};
static struct of_device_id i2c_pxa_dt_ids[] = {
	{ .compatible = "mrvl,pxa-i2c", .data = (void *)REGS_PXA2XX },
	{ .compatible = "mrvl,pwri2c", .data = (void *)REGS_PXA3XX },
	{ .compatible = "mrvl,mmp-twsi", .data = (void *)REGS_PXA2XX },
	{}
};
MODULE_DEVICE_TABLE(of, i2c_pxa_dt_ids);

static int i2c_pxa_probe_dt(struct platform_device *pdev, struct pxa_i2c *i2c,
			    enum pxa_i2c_types *i2c_types)
{
	struct device_node *np = pdev->dev.of_node;
	const struct of_device_id *of_id =
			of_match_device(i2c_pxa_dt_ids, &pdev->dev);
	int ret;

	if (!of_id)
		return 1;
	ret = of_alias_get_id(np, "i2c");
	if (ret < 0) {
		dev_err(&pdev->dev, "failed to get alias id, errno %d\n", ret);
		return ret;
	}
	pdev->id = ret;
	if (of_get_property(np, "mrvl,i2c-polling", NULL))
		i2c->use_pio = 1;
	if (of_get_property(np, "mrvl,i2c-fast-mode", NULL))
		i2c->fast_mode = 1;
	*i2c_types = (u32)(of_id->data);
	return 0;
}

static int i2c_pxa_probe_pdata(struct platform_device *pdev,
			       struct pxa_i2c *i2c,
			       enum pxa_i2c_types *i2c_types)
{
	struct i2c_pxa_platform_data *plat = pdev->dev.platform_data;
	const struct platform_device_id *id = platform_get_device_id(pdev);

	*i2c_types = id->driver_data;
	if (plat) {
		i2c->use_pio = plat->use_pio;
		i2c->fast_mode = plat->fast_mode;
		i2c->fifo_mode = plat->fifo_mode;
	}
	return 0;
}

static int i2c_pxa_probe(struct platform_device *dev)
{
	struct i2c_pxa_platform_data *plat = dev->dev.platform_data;
	enum pxa_i2c_types i2c_type;
	struct pxa_i2c *i2c;
	struct resource *res = NULL;
	int ret, irq;

	int pm_qos_class = PM_QOS_CPUIDLE_BLOCK;

	i2c = kzalloc(sizeof(struct pxa_i2c), GFP_KERNEL);
	if (!i2c) {
		ret = -ENOMEM;
		goto emalloc;
	}

	ret = i2c_pxa_probe_dt(dev, i2c, &i2c_type);
	if (ret > 0)
		ret = i2c_pxa_probe_pdata(dev, i2c, &i2c_type);
	if (ret < 0)
		goto eres;

	res = platform_get_resource(dev, IORESOURCE_MEM, 0);
	irq = platform_get_irq(dev, 0);
	if (res == NULL || irq < 0) {
		ret = -ENODEV;
		goto eres;
	}

	if (!request_mem_region(res->start, resource_size(res), res->name)) {
		ret = -ENOMEM;
		goto eres;
	}

	i2c->adap.owner   = THIS_MODULE;
	i2c->adap.retries = 3;

	i2c->qos_idle.name = i2c->adap.name;
	pm_qos_add_request(&i2c->qos_idle, pm_qos_class,
			PM_QOS_CPUIDLE_BLOCK_DEFAULT_VALUE);

	spin_lock_init(&i2c->lock);
	init_waitqueue_head(&i2c->wait);

	/*
	 * If "dev->id" is negative we consider it as zero.
	 * The reason to do so is to avoid sysfs names that only make
	 * sense when there are multiple adapters.
	 */
	i2c->adap.nr = dev->id;
	snprintf(i2c->adap.name, sizeof(i2c->adap.name), "pxa_i2c-i2c.%u",
		 i2c->adap.nr);

	i2c->clk = clk_get(&dev->dev, NULL);
	if (IS_ERR(i2c->clk)) {
		ret = PTR_ERR(i2c->clk);
		goto eclk;
	}

	i2c->reg_base = ioremap(res->start, resource_size(res));
	if (!i2c->reg_base) {
		ret = -EIO;
		goto eremap;
	}

	i2c->reg_ibmr = i2c->reg_base + pxa_reg_layout[i2c_type].ibmr;
	i2c->reg_idbr = i2c->reg_base + pxa_reg_layout[i2c_type].idbr;
	i2c->reg_icr = i2c->reg_base + pxa_reg_layout[i2c_type].icr;
	i2c->reg_isr = i2c->reg_base + pxa_reg_layout[i2c_type].isr;
	if (i2c_type != REGS_CE4100)
		i2c->reg_isar = i2c->reg_base + pxa_reg_layout[i2c_type].isar;
	i2c->reg_ilcr = i2c->reg_base + pxa_reg_layout[i2c_type].ilcr;
	i2c->reg_iwcr = i2c->reg_base + pxa_reg_layout[i2c_type].iwcr;

	if (i2c->fifo_mode) {
		i2c->reg_wfifo = i2c->reg_base + pxa_reg_layout[i2c_type].wfifo;
		i2c->reg_wfifo_wptr = i2c->reg_base +
				pxa_reg_layout[i2c_type].wfifo_wptr;
		i2c->reg_wfifo_rptr = i2c->reg_base +
				pxa_reg_layout[i2c_type].wfifo_rptr;
		i2c->reg_rfifo = i2c->reg_base + pxa_reg_layout[i2c_type].rfifo;
		i2c->reg_rfifo_wptr = i2c->reg_base +
				pxa_reg_layout[i2c_type].rfifo_wptr;
		i2c->reg_rfifo_rptr = i2c->reg_base +
				pxa_reg_layout[i2c_type].rfifo_rptr;
	}

	i2c->iobase = res->start;
	i2c->iosize = resource_size(res);

	i2c->irq = irq;

	i2c->slave_addr = I2C_PXA_SLAVE_ADDR;
	i2c->highmode_enter = false;

	if (plat) {
#ifdef CONFIG_I2C_PXA_SLAVE
		i2c->slave_addr = plat->slave_addr;
		i2c->slave = plat->slave;
#endif
		i2c->adap.class = plat->class;
		i2c->adap.hardware_lock = plat->hardware_lock;
		i2c->adap.hardware_unlock = plat->hardware_unlock;
		i2c->adap.hardware_trylock = plat->hardware_trylock;
		i2c->high_mode = plat->high_mode;
		i2c->master_code = plat->master_code;
		i2c->ilcr = plat->ilcr;
		i2c->iwcr = plat->iwcr;
	}

	if (i2c->high_mode) {
		clk_set_rate(i2c->clk, 62400000);
		printk(KERN_INFO "i2c: <%s> set rate to %ld\n", \
			i2c->adap.name, clk_get_rate(i2c->clk));

	}

	clk_enable(i2c->clk);

	if (i2c->use_pio) {
		i2c->adap.algo = &i2c_pxa_pio_algorithm;
	} else {
		i2c->adap.algo = &i2c_pxa_algorithm;
		ret = request_irq(irq, i2c_pxa_handler,
				IRQF_SHARED | IRQF_NO_SUSPEND,
				i2c->adap.name, i2c);
		if (ret)
			goto ereqirq;
	}

	i2c_pxa_reset(i2c);
#if defined(CONFIG_MACH_HELANDELOS) || defined(CONFIG_MACH_WILCOX) || defined(CONFIG_MACH_CS02) || defined(CONFIG_MACH_LT02) \
	|| defined(CONFIG_MACH_BAFFIN) || defined(CONFIG_MACH_CT01) || defined(CONFIG_MACH_CS05) || defined(CONFIG_MACH_BAFFINQ) \
	|| defined(CONFIG_MACH_GOLDEN) ||defined(CONFIG_MACH_GOYA) || defined(CONFIG_MACH_DEGAS)
	/* For samsung aruba, camera maybe pull down all i2c pins which leads to wrong state of AP side*/
	if (i2c->adap.nr == 0x0) {
		samsung_camera.i2c_pxa_reset = i2c_pxa_reset;
		samsung_camera.i2c = i2c;
	}
#endif 
	disable_irq(i2c->irq);

	i2c->adap.algo_data = i2c;
	i2c->adap.dev.parent = &dev->dev;
#ifdef CONFIG_OF
	i2c->adap.dev.of_node = dev->dev.of_node;
#endif

	ret = i2c_add_numbered_adapter(&i2c->adap);
	if (ret < 0) {
		printk(KERN_INFO "I2C: Failed to add bus\n");
		goto eadapt;
	}
	of_i2c_register_devices(&i2c->adap);

	platform_set_drvdata(dev, i2c);

	i2c_global = i2c;

#ifdef CONFIG_I2C_PXA_SLAVE
	printk(KERN_INFO "I2C: %s: PXA I2C adapter, slave address %d\n",
	       dev_name(&i2c->adap.dev), i2c->slave_addr);
#else
	printk(KERN_INFO "I2C: %s: PXA I2C adapter\n",
	       dev_name(&i2c->adap.dev));
#endif
	return 0;

eadapt:
	if (!i2c->use_pio)
		free_irq(irq, i2c);
ereqirq:
	clk_disable(i2c->clk);
	iounmap(i2c->reg_base);
eremap:
	clk_put(i2c->clk);
eclk:
	pm_qos_remove_request(&i2c->qos_idle);
	release_mem_region(res->start, resource_size(res));
eres:
	kfree(i2c);
emalloc:
	return ret;
}

static int __exit i2c_pxa_remove(struct platform_device *dev)
{
	struct pxa_i2c *i2c = platform_get_drvdata(dev);

	platform_set_drvdata(dev, NULL);
	pm_qos_remove_request(&i2c->qos_idle);

	i2c_del_adapter(&i2c->adap);
	if (!i2c->use_pio)
		free_irq(i2c->irq, i2c);

	clk_disable(i2c->clk);
	clk_put(i2c->clk);

	iounmap(i2c->reg_base);
	release_mem_region(i2c->iobase, i2c->iosize);
	kfree(i2c);

	return 0;
}

static struct platform_driver i2c_pxa_driver = {
	.probe		= i2c_pxa_probe,
	.remove		= __exit_p(i2c_pxa_remove),
	.driver		= {
		.name	= "pxa2xx-i2c",
		.owner	= THIS_MODULE,
		.of_match_table = i2c_pxa_dt_ids,
	},
	.id_table	= i2c_pxa_id_table,
};

static int __init i2c_adap_pxa_init(void)
{
	atomic_notifier_chain_register(&panic_notifier_list,
				       &panic_block);
	return platform_driver_register(&i2c_pxa_driver);
}

static void __exit i2c_adap_pxa_exit(void)
{
	platform_driver_unregister(&i2c_pxa_driver);
}

MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:pxa2xx-i2c");

subsys_initcall(i2c_adap_pxa_init);
module_exit(i2c_adap_pxa_exit);
