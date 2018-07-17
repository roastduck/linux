/*
 * NaiveMIPS interrupt controller setup
 *
 * Copyright (C) 2017 Tsinghua Univ.
 * Author: Yuxiang Zhang
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 */

#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/irq.h>
#include <linux/irqchip.h>

#include <asm/irq_cpu.h>

#if defined(CONFIG_SMP)
static void ipi_interrupt()
{
	// TODO
}
#endif

asmlinkage void plat_irq_dispatch(void)
{
	unsigned long pending = read_c0_cause() & read_c0_status() & ST0_IM;
	int irq;

	if (!pending) {
		spurious_interrupt();
		return;
	}

#if defined(CONFIG_SMP)
	if (pending & CAUSEF_IP6) {
		ipi_interrupt();
		pending = read_c0_cause() & read_c0_status() & ST0_IM;
	}
#endif

	pending >>= CAUSEB_IP;
	while (pending) {
		irq = fls(pending) - 1;
		do_IRQ(MIPS_CPU_IRQ_BASE + irq);
		pending &= ~BIT(irq);
	}
}

void __init arch_init_irq(void)
{
	pr_devel("arch_init_irq\n");
	irqchip_init();

#if defined(CONFIG_SMP)
	/*
	 * IPI IRQ
	 */
	set_c0_status(STATUSF_IP6);
#endif
}

IRQCHIP_DECLARE(mips_cpu_intc, "mti,cpu-interrupt-controller",
         mips_cpu_irq_of_init);
