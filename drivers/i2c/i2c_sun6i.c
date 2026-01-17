/*
 * Copyright (c) 2025
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT allwinner_sun20i_d1_i2c

#include <zephyr/device.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/util.h>

#include "i2c-priv.h"

LOG_MODULE_REGISTER(i2c_sun6i, CONFIG_I2C_LOG_LEVEL);

#define TWI_ADDR 0x00
#define TWI_XADDR 0x04
#define TWI_DATA 0x08
#define TWI_CNTR 0x0C
#define TWI_STAT 0x10
#define TWI_CCR 0x14
#define TWI_SRST 0x18
#define TWI_EFR 0x1C
#define TWI_LCR 0x20

#define TWI_CNTR_INT_EN   BIT(7)
#define TWI_CNTR_BUS_EN   BIT(6)
#define TWI_CNTR_M_STA    BIT(5)
#define TWI_CNTR_M_STP    BIT(4)
#define TWI_CNTR_INT_FLAG BIT(3)
#define TWI_CNTR_A_ACK    BIT(2)

#define TWI_STAT_START            0x08
#define TWI_STAT_RESTART          0x10
#define TWI_STAT_ADDR_W_ACK       0x18
#define TWI_STAT_ADDR_W_NACK      0x20
#define TWI_STAT_DATA_W_ACK       0x28
#define TWI_STAT_DATA_W_NACK      0x30
#define TWI_STAT_ARB_LOST         0x38
#define TWI_STAT_ADDR_R_ACK       0x40
#define TWI_STAT_ADDR_R_NACK      0x48
#define TWI_STAT_DATA_R_ACK       0x50
#define TWI_STAT_DATA_R_NACK      0x58

#define TWI_TIMEOUT_US 1000000U

struct i2c_allwinner_d1_config {
	uintptr_t base;
	const struct pinctrl_dev_config *pcfg;
	const struct device *clock_dev;
	clock_control_subsys_t clock_id;
	uint32_t input_clk;
	uint32_t bus_freq;
};

struct i2c_allwinner_d1_data {
	uint32_t bus_freq;
	uint32_t input_clk;
};

static inline uint32_t twi_read(const struct i2c_allwinner_d1_config *cfg, uint32_t reg)
{
	return sys_read32(cfg->base + reg);
}

static inline void twi_write(const struct i2c_allwinner_d1_config *cfg,
			     uint32_t reg, uint32_t val)
{
	sys_write32(val, cfg->base + reg);
}

static void twi_write_cntr(const struct i2c_allwinner_d1_config *cfg,
			   bool ack, bool start, bool stop)
{
	uint32_t cntr = TWI_CNTR_INT_EN | TWI_CNTR_BUS_EN | TWI_CNTR_INT_FLAG;

	if (ack) {
		cntr |= TWI_CNTR_A_ACK;
	}
	if (start) {
		cntr |= TWI_CNTR_M_STA;
	}
	if (stop) {
		cntr |= TWI_CNTR_M_STP;
	}

	twi_write(cfg, TWI_CNTR, cntr);
}

static int twi_wait_irq(const struct i2c_allwinner_d1_config *cfg)
{
	uint32_t timeout = TWI_TIMEOUT_US;

	while (timeout-- != 0U) {
		if ((twi_read(cfg, TWI_CNTR) & TWI_CNTR_INT_FLAG) != 0U) {
			return 0;
		}
		k_busy_wait(1);
	}

	return -ETIMEDOUT;
}

static int twi_send_start(const struct i2c_allwinner_d1_config *cfg)
{
	twi_write_cntr(cfg, true, true, false);
	if (twi_wait_irq(cfg) != 0) {
		return -ETIMEDOUT;
	}

	switch (twi_read(cfg, TWI_STAT) & 0xFFU) {
	case TWI_STAT_START:
	case TWI_STAT_RESTART:
		return 0;
	default:
		return -EIO;
	}
}

static void twi_send_stop(const struct i2c_allwinner_d1_config *cfg)
{
	uint32_t timeout = TWI_TIMEOUT_US;

	twi_write_cntr(cfg, true, false, true);

	while (timeout-- != 0U) {
		if ((twi_read(cfg, TWI_CNTR) & TWI_CNTR_M_STP) == 0U) {
			return;
		}
		k_busy_wait(1);
	}
}

static int twi_send_addr(const struct i2c_allwinner_d1_config *cfg,
			 uint16_t addr, bool read)
{
	uint8_t status;

	twi_write(cfg, TWI_DATA, (addr << 1) | (read ? 1U : 0U));
	twi_write_cntr(cfg, true, false, false);

	if (twi_wait_irq(cfg) != 0) {
		return -ETIMEDOUT;
	}

	status = twi_read(cfg, TWI_STAT) & 0xFFU;
	if (read) {
		if (status == TWI_STAT_ADDR_R_ACK) {
			return 0;
		}
	} else {
		if (status == TWI_STAT_ADDR_W_ACK) {
			return 0;
		}
	}

	return -EIO;
}

static int twi_write_data(const struct i2c_allwinner_d1_config *cfg,
			  const uint8_t *buf, size_t len)
{
	for (size_t i = 0U; i < len; i++) {
		uint8_t status;

		twi_write(cfg, TWI_DATA, buf[i]);
		twi_write_cntr(cfg, true, false, false);

		if (twi_wait_irq(cfg) != 0) {
			return -ETIMEDOUT;
		}

		status = twi_read(cfg, TWI_STAT) & 0xFFU;
		if (status == TWI_STAT_DATA_W_ACK) {
			continue;
		}
		if (status == TWI_STAT_ARB_LOST) {
			return -EAGAIN;
		}

		return -EIO;
	}

	return 0;
}

static int twi_read_data(const struct i2c_allwinner_d1_config *cfg,
			 uint8_t *buf, size_t len)
{
	for (size_t i = 0U; i < len; i++) {
		bool ack = (i + 1U) < len;
		uint8_t status;

		twi_write_cntr(cfg, ack, false, false);
		if (twi_wait_irq(cfg) != 0) {
			return -ETIMEDOUT;
		}

		status = twi_read(cfg, TWI_STAT) & 0xFFU;
		if ((ack && status != TWI_STAT_DATA_R_ACK) ||
		    (!ack && status != TWI_STAT_DATA_R_NACK)) {
			return -EIO;
		}

		buf[i] = (uint8_t)twi_read(cfg, TWI_DATA);
	}

	return 0;
}

static uint32_t twi_calc_ccr(uint32_t input_clk, uint32_t bus_freq)
{
	uint32_t best_rate = 0U;
	uint32_t best_m = 0U;
	uint32_t best_n = 0U;

	for (uint32_t n = 0U; n < 8U; n++) {
		for (uint32_t m = 0U; m < 16U; m++) {
			uint32_t rate = input_clk / ((1U << n) * (m + 1U) * 10U);

			if ((rate <= bus_freq) && (rate > best_rate)) {
				best_rate = rate;
				best_m = m;
				best_n = n;
			}
		}
	}

	if (best_rate == 0U) {
		best_m = 15U;
		best_n = 7U;
	}

	return (best_m << 3) | best_n;
}

static int i2c_allwinner_d1_configure(const struct device *dev, uint32_t dev_config)
{
	const struct i2c_allwinner_d1_config *cfg = dev->config;
	struct i2c_allwinner_d1_data *data = dev->data;
	uint32_t bus_freq;

	if ((dev_config & I2C_MODE_CONTROLLER) == 0U) {
		return -ENOTSUP;
	}

	if ((dev_config & I2C_ADDR_10_BITS) != 0U) {
		return -ENOTSUP;
	}

	switch (I2C_SPEED_GET(dev_config)) {
	case I2C_SPEED_STANDARD:
		bus_freq = 100000U;
		break;
	case I2C_SPEED_FAST:
		bus_freq = 400000U;
		break;
	default:
		return -ENOTSUP;
	}

	if (data->bus_freq != bus_freq) {
		uint32_t ccr = twi_calc_ccr(data->input_clk, bus_freq);

		twi_write(cfg, TWI_CCR, ccr);
		data->bus_freq = bus_freq;
	}

	return 0;
}

static int i2c_allwinner_d1_transfer(const struct device *dev,
				     struct i2c_msg *msgs,
				     uint8_t num_msgs,
				     uint16_t addr)
{
	const struct i2c_allwinner_d1_config *cfg = dev->config;
	int ret = 0;

	i2c_dump_msgs(dev, msgs, num_msgs, addr);

	for (uint8_t i = 0U; i < num_msgs; i++) {
		struct i2c_msg *msg = &msgs[i];
		bool start = (i == 0U) || ((msg->flags & I2C_MSG_RESTART) != 0U);
		bool stop = (msg->flags & I2C_MSG_STOP) != 0U;
		bool read = (msg->flags & I2C_MSG_READ) != 0U;

		if (start) {
			ret = twi_send_start(cfg);
			if (ret != 0) {
				goto out_stop;
			}
		}

		ret = twi_send_addr(cfg, addr, read);
		if (ret != 0) {
			goto out_stop;
		}

		if (read) {
			ret = twi_read_data(cfg, msg->buf, msg->len);
		} else {
			ret = twi_write_data(cfg, msg->buf, msg->len);
		}

		if (ret != 0) {
			goto out_stop;
		}

		if (stop) {
			twi_send_stop(cfg);
		}
	}

	return 0;

out_stop:
	twi_send_stop(cfg);
	return ret;
}

static int i2c_allwinner_d1_init(const struct device *dev)
{
	const struct i2c_allwinner_d1_config *cfg = dev->config;
	struct i2c_allwinner_d1_data *data = dev->data;
	uint32_t dev_config;
	int ret;

	if (cfg->clock_dev != NULL) {
		if (!device_is_ready(cfg->clock_dev)) {
			return -ENODEV;
		}

		ret = clock_control_on(cfg->clock_dev, cfg->clock_id);
		if (ret != 0) {
			return ret;
		}

		ret = clock_control_get_rate(cfg->clock_dev, cfg->clock_id, &data->input_clk);
		if (ret != 0) {
			data->input_clk = cfg->input_clk;
		}
	} else {
		data->input_clk = cfg->input_clk;
	}

	ret = pinctrl_apply_state(cfg->pcfg, PINCTRL_STATE_DEFAULT);
	if (ret < 0) {
		return ret;
	}

	twi_write(cfg, TWI_SRST, 1U);
	twi_write(cfg, TWI_CNTR, TWI_CNTR_INT_EN | TWI_CNTR_BUS_EN);
	twi_write(cfg, TWI_ADDR, 0U);
	twi_write(cfg, TWI_XADDR, 0U);

	dev_config = I2C_MODE_CONTROLLER |
		     i2c_map_dt_bitrate(cfg->bus_freq);

	return i2c_allwinner_d1_configure(dev, dev_config);
}

static DEVICE_API(i2c, i2c_allwinner_d1_api) = {
	.configure = i2c_allwinner_d1_configure,
	.transfer = i2c_allwinner_d1_transfer,
#ifdef CONFIG_I2C_RTIO
	.iodev_submit = i2c_iodev_submit_fallback,
#endif
};

#define I2C_SUN6I_INIT(n)								\
	PINCTRL_DT_INST_DEFINE(n);							\
										\
	static struct i2c_allwinner_d1_data i2c_allwinner_d1_data_##n;		\
										\
	static const struct i2c_allwinner_d1_config i2c_allwinner_d1_cfg_##n = {	\
		.base = DT_INST_REG_ADDR(n),					\
		.pcfg = PINCTRL_DT_INST_DEV_CONFIG_GET(n),			\
		.clock_dev = COND_CODE_1(DT_INST_NODE_HAS_PROP(n, clocks),	\
					 (DEVICE_DT_GET(DT_INST_CLOCKS_CTLR(n))), \
					 (NULL)),				\
		.clock_id = (clock_control_subsys_t)COND_CODE_1(		\
			DT_INST_NODE_HAS_PROP(n, clocks),			\
			(DT_INST_CLOCKS_CELL(n, clkid)),			\
			(0)),							\
		.input_clk = 24000000U,					\
		.bus_freq = DT_INST_PROP_OR(n, clock_frequency, I2C_BITRATE_STANDARD), \
	};									\
										\
	I2C_DEVICE_DT_INST_DEFINE(n,						\
				  i2c_allwinner_d1_init,			\
				  NULL,						\
				  &i2c_allwinner_d1_data_##n,			\
				  &i2c_allwinner_d1_cfg_##n,			\
				  POST_KERNEL,					\
				  CONFIG_I2C_INIT_PRIORITY,			\
				  &i2c_allwinner_d1_api);

DT_INST_FOREACH_STATUS_OKAY(I2C_SUN6I_INIT)
