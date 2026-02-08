/*
 * Copyright (c) 2025
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT allwinner_sun20i_d1_ccu

#include <zephyr/arch/riscv/sys_io.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/sys_io.h>

#include <zephyr/dt-bindings/clock/allwinner-d1-ccu.h>

LOG_MODULE_REGISTER(sun20i_d1_ccu, CONFIG_CLOCK_CONTROL_LOG_LEVEL);

struct sun20i_d1_ccu_config {
	uintptr_t base;
	uint32_t hosc_rate;
};

struct sun20i_d1_ccu_clk {
	uint16_t id;
	uint16_t gate_offset;
	uint8_t gate_bit;
	uint16_t reset_offset;
	uint8_t reset_bit;
};

#define SUN20I_D1_CCU_NO_RESET 0xFF

static const struct sun20i_d1_ccu_clk sun20i_d1_ccu_clks[] = {
	{CLK_BUS_UART0, 0x090C, 0, 0x090C, 16},
	{CLK_BUS_UART1, 0x090C, 1, 0x090C, 17},
	{CLK_BUS_UART2, 0x090C, 2, 0x090C, 18},
	{CLK_BUS_UART3, 0x090C, 3, 0x090C, 19},
	{CLK_BUS_UART4, 0x090C, 4, 0x090C, 20},
	{CLK_BUS_UART5, 0x090C, 5, 0x090C, 21},

	{CLK_BUS_I2C0, 0x091C, 0, 0x091C, 16},
	{CLK_BUS_I2C1, 0x091C, 1, 0x091C, 17},
	{CLK_BUS_I2C2, 0x091C, 2, 0x091C, 18},
	{CLK_BUS_I2C3, 0x091C, 3, 0x091C, 19},

	{CLK_SPI0, 0x0940, 31, 0, SUN20I_D1_CCU_NO_RESET},
	{CLK_SPI1, 0x0944, 31, 0, SUN20I_D1_CCU_NO_RESET},

	{CLK_BUS_SPI0, 0x096C, 0, 0x096C, 16},
	{CLK_BUS_SPI1, 0x096C, 1, 0x096C, 17},

	{CLK_BUS_PWM, 0x07AC, 0, 0x07AC, 16},
};

static const struct sun20i_d1_ccu_clk *sun20i_d1_ccu_clk_get(uint32_t id)
{
	for (size_t i = 0; i < ARRAY_SIZE(sun20i_d1_ccu_clks); i++) {
		if (sun20i_d1_ccu_clks[i].id == id) {
			return &sun20i_d1_ccu_clks[i];
		}
	}

	return NULL;
}

static inline void sun20i_d1_ccu_set_bit(uintptr_t addr, uint8_t bit, bool set)
{
	uint32_t val = sys_read32(addr);

	if (set) {
		val |= BIT(bit);
	} else {
		val &= ~BIT(bit);
	}

	sys_write32(val, addr);
}

static enum clock_control_status sun20i_d1_ccu_get_status(const struct device *dev,
							     clock_control_subsys_t sys)
{
	const struct sun20i_d1_ccu_config *config = dev->config;
	uint32_t clkid = (uint32_t)(uintptr_t)sys;
	const struct sun20i_d1_ccu_clk *clk = sun20i_d1_ccu_clk_get(clkid);

	if (clk == NULL) {
		return CLOCK_CONTROL_STATUS_UNKNOWN;
	}

	return (sys_read32(config->base + clk->gate_offset) & BIT(clk->gate_bit)) ?
		CLOCK_CONTROL_STATUS_ON : CLOCK_CONTROL_STATUS_OFF;
}

static int sun20i_d1_ccu_on(const struct device *dev, clock_control_subsys_t sys)
{
	const struct sun20i_d1_ccu_config *config = dev->config;
	uint32_t clkid = (uint32_t)(uintptr_t)sys;
	const struct sun20i_d1_ccu_clk *clk = sun20i_d1_ccu_clk_get(clkid);
	uint32_t val;

	if (clk == NULL) {
		return -ENOTSUP;
	}

	/*
	 * Some CCU gate/reset registers appear write-1-to-set; ensure both bits
	 * are set with a single write so one update doesn't clear the other.
	 */
	val = sys_read32(config->base + clk->gate_offset);
	val |= BIT(clk->gate_bit);
	if (clk->reset_bit != SUN20I_D1_CCU_NO_RESET &&
	    clk->reset_offset == clk->gate_offset) {
		val |= BIT(clk->reset_bit);
	}
	sys_write32(val, config->base + clk->gate_offset);

	if (clk->reset_bit != SUN20I_D1_CCU_NO_RESET &&
	    clk->reset_offset != clk->gate_offset) {
		val = sys_read32(config->base + clk->reset_offset);
		val |= BIT(clk->reset_bit);
		sys_write32(val, config->base + clk->reset_offset);
	}

	LOG_DBG("on clkid=%u gate@0x%04x reset@0x%04x", clkid,
		clk->gate_offset, clk->reset_offset);

	return 0;
}

static int sun20i_d1_ccu_off(const struct device *dev, clock_control_subsys_t sys)
{
	const struct sun20i_d1_ccu_config *config = dev->config;
	uint32_t clkid = (uint32_t)(uintptr_t)sys;
	const struct sun20i_d1_ccu_clk *clk = sun20i_d1_ccu_clk_get(clkid);

	if (clk == NULL) {
		return -ENOTSUP;
	}

	sun20i_d1_ccu_set_bit(config->base + clk->gate_offset, clk->gate_bit, false);

	return 0;
}

static int sun20i_d1_ccu_get_rate(const struct device *dev,
				     clock_control_subsys_t sys,
				     uint32_t *rate)
{
	const struct sun20i_d1_ccu_config *config = dev->config;
	uint32_t clkid = (uint32_t)(uintptr_t)sys;
	const struct sun20i_d1_ccu_clk *clk = sun20i_d1_ccu_clk_get(clkid);

	if (clk == NULL) {
		return -ENOTSUP;
	}

	if (clkid == CLK_SPI0 || clkid == CLK_SPI1) {
		uint32_t reg = sys_read32(config->base + clk->gate_offset);
		uint32_t m = (reg & 0x0FU) + 1U;
		uint32_t p = 1U << ((reg >> 8) & 0x3U);

		*rate = config->hosc_rate / (m * p);
		return 0;
	}

	*rate = config->hosc_rate;
	return 0;
}

static int sun20i_d1_ccu_set_rate(const struct device *dev,
				     clock_control_subsys_t sys,
				     clock_control_subsys_rate_t rate)
{
	const struct sun20i_d1_ccu_config *config = dev->config;
	uint32_t clkid = (uint32_t)(uintptr_t)sys;
	const struct sun20i_d1_ccu_clk *clk = sun20i_d1_ccu_clk_get(clkid);
	uint32_t req_rate;
	uint32_t parent_rate;
	uint32_t best_rate = 0U;
	uint32_t best_m = 0U;
	uint32_t best_p = 0U;

	if ((clk == NULL) || (rate == NULL)) {
		return -ENOTSUP;
	}

	if ((clkid != CLK_SPI0) && (clkid != CLK_SPI1)) {
		return -ENOTSUP;
	}

	req_rate = *(uint32_t *)rate;
	if (req_rate == 0U) {
		return -EINVAL;
	}

	/* Only HOSC is supported for now. */
	parent_rate = config->hosc_rate;

	for (uint32_t p = 0U; p < 4U; p++) {
		for (uint32_t m = 0U; m < 16U; m++) {
			uint32_t div = (m + 1U) * (1U << p);
			uint32_t cur = parent_rate / div;

			if (cur <= req_rate && cur > best_rate) {
				best_rate = cur;
				best_m = m;
				best_p = p;
			}
		}
	}

	if (best_rate == 0U) {
		best_m = 15U;
		best_p = 3U;
	}

	uint32_t val = sys_read32(config->base + clk->gate_offset);

	val &= ~0x0FU;
	val &= ~(0x3U << 8);
	val &= ~(0x7U << 24);
	val |= (best_m & 0x0FU);
	val |= ((best_p & 0x3U) << 8);
	val |= BIT(31); /* gate */
	/* CLK_SRC_SEL = 0 (HOSC) */

	sys_write32(val, config->base + clk->gate_offset);

	LOG_DBG("set_rate clkid=%u req=%uHz best=%uHz m=%u p=%u",
		clkid, req_rate, best_rate, best_m, best_p);

	return 0;
}

static DEVICE_API(clock_control, sun20i_d1_ccu_api) = {
	.on = sun20i_d1_ccu_on,
	.off = sun20i_d1_ccu_off,
	.get_status = sun20i_d1_ccu_get_status,
	.get_rate = sun20i_d1_ccu_get_rate,
	.set_rate = sun20i_d1_ccu_set_rate,
};

static int sun20i_d1_ccu_init(const struct device *dev)
{
	ARG_UNUSED(dev);
	return 0;
}

static const struct sun20i_d1_ccu_config sun20i_d1_ccu_config = {
	.base = DT_INST_REG_ADDR(0),
	.hosc_rate = DT_INST_PROP_OR(0, clock_frequency, 24000000U),
};

DEVICE_DT_INST_DEFINE(0, sun20i_d1_ccu_init, NULL,
		      NULL, &sun20i_d1_ccu_config,
		      PRE_KERNEL_1, CONFIG_CLOCK_CONTROL_INIT_PRIORITY,
		      &sun20i_d1_ccu_api);
