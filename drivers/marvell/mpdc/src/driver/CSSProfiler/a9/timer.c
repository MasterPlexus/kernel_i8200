/*
** (C) Copyright 2009 Marvell International Ltd.
**  		All Rights Reserved

** This software file (the "File") is distributed by Marvell International Ltd.
** under the terms of the GNU General Public License Version 2, June 1991 (the "License").
** You may use, redistribute and/or modify this File in accordance with the terms and
** conditions of the License, a copy of which is available along with the File in the
** license.txt file or by writing to the Free Software Foundation, Inc., 59 Temple Place,
** Suite 330, Boston, MA 02111-1307 or on the worldwide web at http://www.gnu.org/licenses/gpl.txt.
** THE FILE IS DISTRIBUTED AS-IS, WITHOUT WARRANTY OF ANY KIND, AND THE IMPLIED WARRANTIES
** OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE ARE EXPRESSLY DISCLAIMED.
** The License provides additional details about this warranty disclaimer.
*/

/*
 * ARM A9 CSS Timer implementation
 */

#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/version.h>
#include <linux/profile.h>
#include <linux/sched.h>
#include <linux/cpu.h>
#include <linux/hrtimer.h>
#include <asm/io.h>

#include "PXD_css.h"
#include "CSSProfilerSettings.h"
#include "css_drv.h"
#include "common.h"

static bool tbs_running;

static irqreturn_t px_timer_isr(unsigned int pid,
                                unsigned int tid,
                                struct pt_regs * const regs,
                                unsigned int cpu,
                                unsigned long long ts)
{
	bool buffer_full = false;

	if (tbs_running)
	{
		char ** bt_buffer = &per_cpu(g_bt_buffer, cpu);
		PXD32_CSS_Call_Stack_V2 *css_data = (PXD32_CSS_Call_Stack_V2 *)*bt_buffer;

		backtrace(regs, cpu, pid, tid, css_data);
		
		fill_css_data_head(css_data, pid, tid, COUNTER_A9_OS_TIMER, ts);

		buffer_full = write_css_data(cpu, css_data);
	
		if (buffer_full)
		{
			tbs_running = false;
		}
		else
		{
			tbs_running = true;
		}
	}

	return IRQ_HANDLED;
}

static DEFINE_PER_CPU(struct hrtimer, hrtimer_for_tbs);

static enum hrtimer_restart hrtimer_func(struct hrtimer *hrtimer)
{
	unsigned int pid;
	unsigned int tid;
	unsigned int cpu;
	unsigned long long ts;
	struct pt_regs *regs;

	unsigned long long delay_in_ns;

	delay_in_ns = g_tbs_settings.interval * NSEC_PER_USEC;

	regs = get_irq_regs();
	
	if (regs != NULL)
	{
		pid = current->tgid;
		tid = current->pid;
		cpu = smp_processor_id();
		ts  = get_timestamp();

		px_timer_isr(pid, tid, regs, cpu, ts);
	}

	hrtimer_forward_now(hrtimer, ns_to_ktime(delay_in_ns));

	return HRTIMER_RESTART;
}

static void __start_tbs(void * data)
{
	ktime_t ktime;
	unsigned long long delay_in_ns = g_tbs_settings.interval * NSEC_PER_USEC;
	struct hrtimer *hrtimer = &__get_cpu_var(hrtimer_for_tbs);

	ktime = ktime_set(0, delay_in_ns);

	hrtimer_init(hrtimer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	hrtimer->function = hrtimer_func;

	hrtimer_start(hrtimer, ns_to_ktime(delay_in_ns), HRTIMER_MODE_REL_PINNED);
}

static void __stop_tbs(unsigned int cpu)
{
	struct hrtimer *hrtimer = &per_cpu(hrtimer_for_tbs, cpu);

	hrtimer_cancel(hrtimer);
}

/*
static int __cpuinit cpu_notify(struct notifier_block *self, unsigned long action, void *hcpu)
{
	long cpu = (long) hcpu;

	switch (action) {
	case CPU_ONLINE:
	case CPU_ONLINE_FROZEN:
		smp_call_function_single(cpu, __start_tbs, NULL, 1);
		break;
	case CPU_DEAD:
	case CPU_DEAD_FROZEN:
		__stop_tbs(cpu);

		break;
	}
	return NOTIFY_OK;
}

struct notifier_block __refdata cpu_notifier_for_timer = {
	.notifier_call = cpu_notify,
};
*/

int start_tbs(bool is_start_paused)
{
	int ret;

//	ret = register_hotcpu_notifier(&cpu_notifier_for_timer);
//	if (ret != 0)
//		return ret;

	if (!is_start_paused)
	{
		tbs_running = true;
	}
	else
	{
		tbs_running = false;
	}
	
	on_each_cpu(__start_tbs, NULL, 1);

	return 0;
}

int stop_tbs(void)
{
	unsigned int cpu;

	tbs_running = false;

	for_each_online_cpu(cpu)
	{
		__stop_tbs(cpu);
	}
	
//	unregister_hotcpu_notifier(&cpu_notifier_for_timer);
	
	return 0;
}

int pause_tbs(void)
{
	tbs_running = false;

	return 0;
}

int resume_tbs(void)
{
	tbs_running = true;
	return 0;
}

