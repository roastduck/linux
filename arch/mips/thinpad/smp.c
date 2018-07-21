#if defined(CONFIG_SMP)

#include <linux/init.h>
#include <linux/cpu.h>
#include <linux/sched.h>
#include <linux/smp.h>
#include <linux/cpufreq.h>
#include <asm/processor.h>
#include <asm/time.h>
#include <asm/clock.h>
#include <asm/tlbflush.h>
#include <asm/cacheflush.h>

#include "smp.h"

DEFINE_PER_CPU(int, cpu_state);

static void *ipi_set0_regs[2];
static void *ipi_clear0_regs[2];
static void *ipi_status0_regs[2];
static void *ipi_en0_regs[2];
static void *ipi_mailbox_buf[2];
static uint32_t core0_c0count[NR_CPUS];

/* read a 32bit value from ipi register */
#define thinpad_ipi_read32(addr) readl(addr)
/* write a 32bit value to ipi register */
#define thinpad_ipi_write32(action, addr) writel(action, addr)

static void ipi_regs_init(void)
{
	ipi_set0_regs[0] = (void *)
		(SMP_BASE + SMP_CORE0_OFFSET + SET0);
	ipi_set0_regs[1] = (void *)
		(SMP_BASE + SMP_CORE1_OFFSET + SET0);
	ipi_clear0_regs[0] = (void *)
		(SMP_BASE + SMP_CORE0_OFFSET + CLEAR0);
	ipi_clear0_regs[1] = (void *)
		(SMP_BASE + SMP_CORE1_OFFSET + CLEAR0);
	ipi_status0_regs[0] = (void *)
		(SMP_BASE + SMP_CORE0_OFFSET + STATUS0);
	ipi_status0_regs[1] = (void *)
		(SMP_BASE + SMP_CORE1_OFFSET + STATUS0);
	ipi_en0_regs[0] = (void *)
		(SMP_BASE + SMP_CORE0_OFFSET + EN0);
	ipi_en0_regs[1] = (void *)
		(SMP_BASE + SMP_CORE1_OFFSET + EN0);
	ipi_mailbox_buf[0] = (void *)
		(SMP_BASE + SMP_CORE0_OFFSET + BUF);
	ipi_mailbox_buf[1] = (void *)
		(SMP_BASE + SMP_CORE1_OFFSET + BUF);
}

/*
 * Simple enough, just poke the appropriate ipi register
 */
static void thinpad_send_ipi_single(int cpu, unsigned int action)
{
	thinpad_ipi_write32((u32)action, ipi_set0_regs[cpu_logical_map(cpu)]);
}

static void
thinpad_send_ipi_mask(const struct cpumask *mask, unsigned int action)
{
	unsigned int i;

	for_each_cpu(i, mask)
		thinpad_ipi_write32((u32)action, ipi_set0_regs[cpu_logical_map(i)]);
}

void thinpad_ipi_interrupt(void)
{
	int i, cpu = smp_processor_id();
	unsigned int action, c0count;

	/* Load the ipi register to figure out what we're supposed to do */
	action = thinpad_ipi_read32(ipi_status0_regs[cpu_logical_map(cpu)]);

	/* Clear the ipi register to clear the interrupt */
	thinpad_ipi_write32((u32)action, ipi_clear0_regs[cpu_logical_map(cpu)]);

	if (action & SMP_RESCHEDULE_YOURSELF)
		scheduler_ipi();

	if (action & SMP_CALL_FUNCTION) {
		irq_enter();
		generic_smp_call_function_interrupt();
		irq_exit();
	}

	if (action & SMP_ASK_C0COUNT) {
		BUG_ON(cpu != 0);
		c0count = read_c0_count();
		c0count = c0count ? c0count : 1;
		for (i = 1; i < nr_cpu_ids; i++)
			core0_c0count[i] = c0count;
	}
}

#define MAX_LOOPS 800
/*
 * SMP init and finish on secondary CPUs
 */
static void thinpad_init_secondary(void)
{
	int i;
	uint32_t initcount;
	unsigned int cpu = smp_processor_id();
	unsigned int imask = STATUSF_IP7 | STATUSF_IP6 | STATUSF_IP4; // FIXME Why set in smp.c?

	/* Set interrupt mask, but don't enable */
	change_c0_status(ST0_IM, imask);

	for (i = 0; i < num_possible_cpus(); i++)
		thinpad_ipi_write32(0xffffffff, ipi_en0_regs[cpu_logical_map(i)]);

	per_cpu(cpu_state, cpu) = CPU_ONLINE;
	cpu_data[cpu].core = cpu_logical_map(cpu);
	cpu_data[cpu].package = 0;

	i = 0;
	core0_c0count[cpu] = 0;
	thinpad_send_ipi_single(0, SMP_ASK_C0COUNT);
	while (!core0_c0count[cpu]) {
		i++;
		cpu_relax();
	}

	if (i > MAX_LOOPS)
		i = MAX_LOOPS;
    initcount = core0_c0count[cpu] + i;

	write_c0_count(initcount);
}

static void thinpad_smp_finish(void)
{
	int cpu = smp_processor_id();

	write_c0_compare(read_c0_count() + mips_hpt_frequency / HZ);
	local_irq_enable();
	thinpad_ipi_write32(0,
			(void *)(ipi_mailbox_buf[cpu_logical_map(cpu)] + 0x0));
	pr_info("CPU#%d finished, CP0_ST=%x\n",
			smp_processor_id(), read_c0_status());
}

static void __init thinpad_smp_setup(void)
{
	int i = 0;

	init_cpu_possible(cpu_none_mask);

    for (i = 0; i < 2; i++) {
        __cpu_number_map[i] = i;
        __cpu_logical_map[i] = i;
        set_cpu_possible(i, true);
    }
	pr_info("2 available CPUs\n");

	ipi_regs_init();
	cpu_data[0].core = 0;
	cpu_data[0].package = 0;
}

static void __init thinpad_prepare_cpus(unsigned int max_cpus)
{
	init_cpu_present(cpu_possible_mask);
	per_cpu(cpu_state, smp_processor_id()) = CPU_ONLINE;
}

/*
 * Setup the PC, SP, and GP of a secondary processor and start it runing!
 */
static void thinpad_boot_secondary(int cpu, struct task_struct *idle)
{
	unsigned long startargs[4];

	pr_info("Booting CPU#%d...\n", cpu);

	/* startargs[] are initial PC, SP and GP for secondary CPU */
	startargs[0] = (unsigned long)&smp_bootstrap;
	startargs[1] = (unsigned long)__KSTK_TOS(idle);
	startargs[2] = (unsigned long)task_thread_info(idle);
	startargs[3] = 0;

	pr_debug("CPU#%d, func_pc=%lx, sp=%lx, gp=%lx\n",
			cpu, startargs[0], startargs[1], startargs[2]);

	thinpad_ipi_write32(startargs[3],
			(void *)(ipi_mailbox_buf[cpu_logical_map(cpu)] + 0xc));
	thinpad_ipi_write32(startargs[2],
			(void *)(ipi_mailbox_buf[cpu_logical_map(cpu)] + 0x8));
	thinpad_ipi_write32(startargs[1],
			(void *)(ipi_mailbox_buf[cpu_logical_map(cpu)] + 0x4));
	thinpad_ipi_write32(startargs[0],
			(void *)(ipi_mailbox_buf[cpu_logical_map(cpu)] + 0x0));
}

struct plat_smp_ops thinpad_smp_ops = {
	.send_ipi_single = thinpad_send_ipi_single,
	.send_ipi_mask = thinpad_send_ipi_mask,
	.init_secondary = thinpad_init_secondary,
	.smp_finish = thinpad_smp_finish,
	.boot_secondary = thinpad_boot_secondary,
	.smp_setup = thinpad_smp_setup,
	.prepare_cpus = thinpad_prepare_cpus,
};

#endif // CONFIG_SMP
