/*
 * Copyright (c) 2025
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT allwinner_sun20i_d1_timer

#include <zephyr/arch/cpu.h>
#include <zephyr/arch/riscv/sys_io.h>
#include <zephyr/init.h>
#include <zephyr/irq.h>
#include <zephyr/kernel.h>
#include <zephyr/spinlock.h>
#include <zephyr/drivers/timer/system_timer.h>
#include <zephyr/sys/sys_io.h>

#define TIMER_IRQ_EN_REG        0x00
#define TIMER_IRQ_EN(val)       BIT(val)
#define TIMER_IRQ_ST_REG        0x04
#define TIMER_IRQ_CLEAR(val)    BIT(val)
#define TIMER_CTL_REG(val)      (0x10 * (val) + 0x10)
#define TIMER_CTL_ENABLE        BIT(0)
#define TIMER_CTL_RELOAD        BIT(1)
#define TIMER_CTL_CLK_SRC(val)  (((val) & 0x3) << 2)
#define TIMER_CTL_CLK_SRC_OSC24M 1U
#define TIMER_CTL_ONESHOT       BIT(7)
#define TIMER_INTVAL_REG(val)   (0x10 * (val) + 0x14)
#define TIMER_CNTVAL_REG(val)   (0x10 * (val) + 0x18)

#define TIMER_BASE              DT_INST_REG_ADDR(0)
#define TIMER_IRQ               DT_INST_IRQN(0)
#define TIMER_IRQ_PRIO          DT_INST_IRQ(0, priority)

static uint32_t cycles_per_tick;
static uint64_t cycle_base;
static struct k_spinlock timer_lock;

static void timer_clear_irq(void)
{
	sys_write32(TIMER_IRQ_CLEAR(0), TIMER_BASE + TIMER_IRQ_ST_REG);
}

static void timer_irq_handler(const void *arg)
{
	ARG_UNUSED(arg);

	k_spinlock_key_t key = k_spin_lock(&timer_lock);

	timer_clear_irq();
	cycle_base += cycles_per_tick;
	sys_clock_announce(1);

	k_spin_unlock(&timer_lock, key);
}

uint32_t sys_clock_cycle_get_32(void)
{
	uint32_t cnt;
	uint64_t cycles;
	k_spinlock_key_t key = k_spin_lock(&timer_lock);

	cnt = sys_read32(TIMER_BASE + TIMER_CNTVAL_REG(0));
	cycles = cycle_base + (cycles_per_tick - cnt);

	k_spin_unlock(&timer_lock, key);

	return (uint32_t)cycles;
}

uint64_t sys_clock_cycle_get_64(void)
{
	uint32_t cnt;
	uint64_t cycles;
	k_spinlock_key_t key = k_spin_lock(&timer_lock);

	cnt = sys_read32(TIMER_BASE + TIMER_CNTVAL_REG(0));
	cycles = cycle_base + (cycles_per_tick - cnt);

	k_spin_unlock(&timer_lock, key);

	return cycles;
}

uint32_t sys_clock_elapsed(void)
{
	return 0;
}

void sys_clock_set_timeout(int32_t ticks, bool idle)
{
	ARG_UNUSED(ticks);
	ARG_UNUSED(idle);
}

static int sys_clock_driver_init(void)
{
	uint32_t val;

	cycles_per_tick = k_ticks_to_cyc_floor32(1);

	IRQ_CONNECT(TIMER_IRQ, TIMER_IRQ_PRIO, timer_irq_handler, NULL, 0);
	irq_enable(TIMER_IRQ);

	sys_write32(cycles_per_tick, TIMER_BASE + TIMER_INTVAL_REG(0));
	sys_write32(0, TIMER_BASE + TIMER_CNTVAL_REG(0));

	val = TIMER_CTL_CLK_SRC(TIMER_CTL_CLK_SRC_OSC24M);
	val |= TIMER_CTL_RELOAD | TIMER_CTL_ENABLE;
	sys_write32(val, TIMER_BASE + TIMER_CTL_REG(0));

	timer_clear_irq();

	val = sys_read32(TIMER_BASE + TIMER_IRQ_EN_REG);
	sys_write32(val | TIMER_IRQ_EN(0), TIMER_BASE + TIMER_IRQ_EN_REG);

	return 0;
}

SYS_INIT(sys_clock_driver_init, PRE_KERNEL_2, CONFIG_SYSTEM_CLOCK_INIT_PRIORITY);
