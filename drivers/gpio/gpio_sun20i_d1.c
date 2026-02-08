/*
 * Copyright (c) 2025
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT allwinner_sun20i_d1_gpio

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_utils.h>
#include <zephyr/irq.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/util.h>

#define PIO_CFG_OFFSET  0x00
#define PIO_DAT_OFFSET  0x10
#define PIO_DRV_OFFSET  0x14
#define PIO_PULL_OFFSET 0x24

#define PIO_CFG_INPUT   0x0
#define PIO_CFG_OUTPUT  0x1

struct gpio_sun20i_d1_config {
	/* gpio_driver_config must be first */
	struct gpio_driver_config common;
	uintptr_t base;
};

struct gpio_sun20i_d1_data {
	/* gpio_driver_data must be first */
	struct gpio_driver_data common;
};

static inline uintptr_t gpio_sun20i_d1_reg(const struct gpio_sun20i_d1_config *cfg,
					      uint32_t offset)
{
	return cfg->base + offset;
}

static int gpio_sun20i_d1_pin_configure(const struct device *dev,
					   gpio_pin_t pin,
					   gpio_flags_t flags)
{
	const struct gpio_sun20i_d1_config *cfg = dev->config;
	uint32_t pin_mask = BIT(pin);
	uintptr_t reg;
	uint32_t val;
	uint32_t shift;
	unsigned int key;
	uint32_t func;
	uint32_t pull;

	if ((cfg->common.port_pin_mask & pin_mask) == 0U) {
		return -EINVAL;
	}

	if ((flags & GPIO_SINGLE_ENDED) != 0U) {
		return -ENOTSUP;
	}

	if ((flags & GPIO_PULL_UP) && (flags & GPIO_PULL_DOWN)) {
		return -EINVAL;
	}

	if ((flags & GPIO_OUTPUT_INIT_HIGH) && (flags & GPIO_OUTPUT_INIT_LOW)) {
		return -EINVAL;
	}

	if ((flags & GPIO_OUTPUT) != 0U) {
		func = PIO_CFG_OUTPUT;
	} else {
		func = PIO_CFG_INPUT;
	}

	if ((flags & GPIO_PULL_UP) != 0U) {
		pull = 1U;
	} else if ((flags & GPIO_PULL_DOWN) != 0U) {
		pull = 2U;
	} else {
		pull = 0U;
	}

	key = irq_lock();

	if ((flags & GPIO_OUTPUT) != 0U) {
		reg = gpio_sun20i_d1_reg(cfg, PIO_DAT_OFFSET);
		val = sys_read32(reg);

		if ((flags & GPIO_OUTPUT_INIT_HIGH) != 0U) {
			val |= pin_mask;
		} else if ((flags & GPIO_OUTPUT_INIT_LOW) != 0U) {
			val &= ~pin_mask;
		}

		sys_write32(val, reg);
	}

	reg = gpio_sun20i_d1_reg(cfg, PIO_CFG_OFFSET + ((pin / 8U) * 4U));
	shift = (pin % 8U) * 4U;
	val = sys_read32(reg);
	val = (val & ~(0xFU << shift)) | (func << shift);
	sys_write32(val, reg);

	reg = gpio_sun20i_d1_reg(cfg, PIO_PULL_OFFSET + ((pin / 16U) * 4U));
	shift = (pin % 16U) * 2U;
	val = sys_read32(reg);
	val = (val & ~(0x3U << shift)) | (pull << shift);
	sys_write32(val, reg);

	irq_unlock(key);

	return 0;
}

static int gpio_sun20i_d1_port_get_raw(const struct device *dev, uint32_t *value)
{
	const struct gpio_sun20i_d1_config *cfg = dev->config;

	*value = sys_read32(gpio_sun20i_d1_reg(cfg, PIO_DAT_OFFSET)) &
		 cfg->common.port_pin_mask;
	return 0;
}

static int gpio_sun20i_d1_port_set_masked_raw(const struct device *dev,
						 uint32_t mask,
						 uint32_t value)
{
	const struct gpio_sun20i_d1_config *cfg = dev->config;
	uintptr_t reg = gpio_sun20i_d1_reg(cfg, PIO_DAT_OFFSET);
	unsigned int key;
	uint32_t val;

	mask &= cfg->common.port_pin_mask;
	value &= mask;

	key = irq_lock();
	val = sys_read32(reg);
	val = (val & ~mask) | value;
	sys_write32(val, reg);
	irq_unlock(key);

	return 0;
}

static int gpio_sun20i_d1_port_set_bits_raw(const struct device *dev, uint32_t mask)
{
	const struct gpio_sun20i_d1_config *cfg = dev->config;
	uintptr_t reg = gpio_sun20i_d1_reg(cfg, PIO_DAT_OFFSET);
	unsigned int key;
	uint32_t val;

	mask &= cfg->common.port_pin_mask;

	key = irq_lock();
	val = sys_read32(reg) | mask;
	sys_write32(val, reg);
	irq_unlock(key);

	return 0;
}

static int gpio_sun20i_d1_port_clear_bits_raw(const struct device *dev, uint32_t mask)
{
	const struct gpio_sun20i_d1_config *cfg = dev->config;
	uintptr_t reg = gpio_sun20i_d1_reg(cfg, PIO_DAT_OFFSET);
	unsigned int key;
	uint32_t val;

	mask &= cfg->common.port_pin_mask;

	key = irq_lock();
	val = sys_read32(reg) & ~mask;
	sys_write32(val, reg);
	irq_unlock(key);

	return 0;
}

static int gpio_sun20i_d1_port_toggle_bits(const struct device *dev, uint32_t mask)
{
	const struct gpio_sun20i_d1_config *cfg = dev->config;
	uintptr_t reg = gpio_sun20i_d1_reg(cfg, PIO_DAT_OFFSET);
	unsigned int key;
	uint32_t val;

	mask &= cfg->common.port_pin_mask;

	key = irq_lock();
	val = sys_read32(reg) ^ mask;
	sys_write32(val, reg);
	irq_unlock(key);

	return 0;
}

static int gpio_sun20i_d1_pin_interrupt_configure(const struct device *dev,
						     gpio_pin_t pin,
						     enum gpio_int_mode mode,
						     enum gpio_int_trig trig)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(pin);
	ARG_UNUSED(mode);
	ARG_UNUSED(trig);

	return -ENOTSUP;
}

#ifdef CONFIG_GPIO_GET_DIRECTION
static int gpio_sun20i_d1_port_get_direction(const struct device *dev,
						gpio_port_pins_t map,
						gpio_port_pins_t *inputs,
						gpio_port_pins_t *outputs)
{
	const struct gpio_sun20i_d1_config *cfg = dev->config;
	gpio_port_pins_t in_mask = 0U;
	gpio_port_pins_t out_mask = 0U;
	gpio_port_pins_t allowed = cfg->common.port_pin_mask & map;

	for (uint32_t pin = 0U; pin < 32U; pin++) {
		uint32_t func;
		uint32_t reg;
		uint32_t shift;

		if ((allowed & BIT(pin)) == 0U) {
			continue;
		}

		reg = gpio_sun20i_d1_reg(cfg, PIO_CFG_OFFSET + ((pin / 8U) * 4U));
		shift = (pin % 8U) * 4U;
		func = (sys_read32(reg) >> shift) & 0xFU;

		if (func == PIO_CFG_OUTPUT) {
			out_mask |= BIT(pin);
		} else {
			in_mask |= BIT(pin);
		}
	}

	if (inputs != NULL) {
		*inputs = in_mask;
	}
	if (outputs != NULL) {
		*outputs = out_mask;
	}

	return 0;
}
#endif

static DEVICE_API(gpio, gpio_sun20i_d1_api) = {
	.pin_configure = gpio_sun20i_d1_pin_configure,
	.port_get_raw = gpio_sun20i_d1_port_get_raw,
	.port_set_masked_raw = gpio_sun20i_d1_port_set_masked_raw,
	.port_set_bits_raw = gpio_sun20i_d1_port_set_bits_raw,
	.port_clear_bits_raw = gpio_sun20i_d1_port_clear_bits_raw,
	.port_toggle_bits = gpio_sun20i_d1_port_toggle_bits,
	.pin_interrupt_configure = gpio_sun20i_d1_pin_interrupt_configure,
#ifdef CONFIG_GPIO_GET_DIRECTION
	.port_get_direction = gpio_sun20i_d1_port_get_direction,
#endif
};

static int gpio_sun20i_d1_init(const struct device *dev)
{
	ARG_UNUSED(dev);
	return 0;
}

#define GPIO_SUN20I_D1_INIT(n)						\
	static struct gpio_sun20i_d1_data gpio_sun20i_d1_data_##n;	\
									\
	static const struct gpio_sun20i_d1_config gpio_sun20i_d1_cfg_##n = { \
		.common = {						\
			.port_pin_mask = GPIO_PORT_PIN_MASK_FROM_DT_INST(n), \
		},							\
		.base = DT_INST_REG_ADDR(n),				\
	};								\
									\
	DEVICE_DT_INST_DEFINE(n,					\
			      gpio_sun20i_d1_init,			\
			      NULL,					\
			      &gpio_sun20i_d1_data_##n,		\
			      &gpio_sun20i_d1_cfg_##n,		\
			      PRE_KERNEL_1,				\
			      CONFIG_GPIO_INIT_PRIORITY,		\
			      &gpio_sun20i_d1_api);

DT_INST_FOREACH_STATUS_OKAY(GPIO_SUN20I_D1_INIT)
