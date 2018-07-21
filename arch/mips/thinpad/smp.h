#ifndef __THINPAD_SMP_H_
#define __THINPAD_SMP_H_

#if defined(CONFIG_SMP)

#define SMP_BASE 0xbff01000

/* 2 cores in each group(node) */
#define SMP_CORE0_OFFSET  0x00
#define SMP_CORE1_OFFSET  0x20

/* ipi registers offsets */
#define STATUS0  0x00
#define EN0      0x04
#define SET0     0x08
#define CLEAR0   0x0c
#define BUF      0x10

void thinpad_ipi_interrupt(void);

extern struct plat_smp_ops thinpad_smp_ops;

#endif // CONFIG_SMP

#endif // __THINPAD_SMP_H_

