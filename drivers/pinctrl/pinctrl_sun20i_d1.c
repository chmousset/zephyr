/*
 * Copyright (c) 2025
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT allwinner_sun20i_d1_pinctrl

#include <zephyr/arch/riscv/sys_io.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/sys/sys_io.h>


#define PIO_PORT_STRIDE 0x30
#define PIO_CFG_OFFSET  0x00
#define PIO_DRV_OFFSET  0x14
#define PIO_PULL_OFFSET 0x24

static uintptr_t pinctrl_base_addr(uintptr_t reg)
{
	if (reg != 0) {
		return reg;
	}

	return DT_INST_REG_ADDR(0);
}

static int pinctrl_configure_pin(pinctrl_soc_pin_t pin, uintptr_t base)
{
	uint32_t port = ALLWINNER_D1_PIN_PORT(pin);
	uint32_t pin_num = ALLWINNER_D1_PIN_NUM(pin);
	uint32_t func = ALLWINNER_D1_PIN_FUNC(pin);
	uint32_t pull = ALLWINNER_D1_PIN_PULL(pin);
	uint32_t drive = ALLWINNER_D1_PIN_DRV(pin);
	uintptr_t port_base;
	uintptr_t reg;
	uint32_t val;
	uint32_t shift;

	if (port > ALLWINNER_D1_PORT_G || pin_num > 31 || func > 0xF) {
		return -EINVAL;
	}

	port_base = base + (port * PIO_PORT_STRIDE);

	reg = port_base + PIO_CFG_OFFSET + ((pin_num / 8U) * 4U);
	shift = (pin_num % 8U) * 4U;
	val = sys_read32(reg);
	val = (val & ~(0xFU << shift)) | (func << shift);
	sys_write32(val, reg);

	/* Drive strength uses packed 4-bit fields. */
	reg = port_base + PIO_DRV_OFFSET + ((pin_num / 8U) * 4U);
	shift = (pin_num % 8U) * 4U;
	val = sys_read32(reg);
	val = (val & ~(0xFU << shift)) | ((drive & 0xFU) << shift);
	sys_write32(val, reg);

	/* Pull selection uses packed 2-bit fields without padding. */
	reg = port_base + PIO_PULL_OFFSET + ((pin_num / 16U) * 4U);
	shift = (pin_num % 16U) * 2U;
	val = sys_read32(reg);
	val = (val & ~(0x3U << shift)) | ((pull & 0x3U) << shift);
	sys_write32(val, reg);

	return 0;
}

int pinctrl_configure_pins(const pinctrl_soc_pin_t *pins, uint8_t pin_cnt, uintptr_t reg)
{
	uintptr_t base = pinctrl_base_addr(reg);

	for (uint8_t i = 0U; i < pin_cnt; i++) {
		int ret = pinctrl_configure_pin(pins[i], base);

		if (ret != 0) {
			return ret;
		}
	}

	return 0;
}
