/*
 * Copyright (c) 2025
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT allwinner_sun20i_d1_pwm

#include <zephyr/device.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(pwm_sun20i_d1, CONFIG_PWM_LOG_LEVEL);

#define PWM_PCCR_BASE 0x20
#define PWM_PCGR      0x40
#define PWM_PER       0x80

#define PWM_PCR_BASE   0x100
#define PWM_PPR_BASE   0x104
#define PWM_CH_STRIDE  0x20

#define PWM_CHANNELS 8U
#define PWM_PERIOD_MAX 65536U

#define PCCR_CLK_SRC_SHIFT 7
#define PCCR_CLK_SRC_MASK  (0x3U << PCCR_CLK_SRC_SHIFT)
#define PCCR_CLK_SRC_HOSC  0x0U
#define PCCR_CLK_DIV_MASK  0xFU

#define PCGR_GATE_BIT(ch)   BIT(ch)
#define PCGR_BYPASS_BIT(ch) BIT(16U + (ch))

#define PCR_PRESCAL_MASK 0xFFU
#define PCR_ACT_STA      BIT(8)
#define PCR_MODE         BIT(9)
#define PCR_PUL_START    BIT(10)

struct pwm_sun20i_d1_config {
	uintptr_t base;
	const struct pinctrl_dev_config *pcfg;
	const struct device *clock_dev;
	clock_control_subsys_t clock_id;
	uint32_t input_clk;
};

struct pwm_sun20i_d1_data {
	uint32_t input_clk;
};

static inline uintptr_t pwm_sun20i_d1_reg(const struct pwm_sun20i_d1_config *cfg,
					      uint32_t offset)
{
	return cfg->base + offset;
}

static inline uintptr_t pwm_sun20i_d1_pcr(uint32_t channel)
{
	return PWM_PCR_BASE + (channel * PWM_CH_STRIDE);
}

static inline uintptr_t pwm_sun20i_d1_ppr(uint32_t channel)
{
	return PWM_PPR_BASE + (channel * PWM_CH_STRIDE);
}

static inline uintptr_t pwm_sun20i_d1_pccr(uint32_t channel)
{
	return PWM_PCCR_BASE + ((channel / 2U) * 4U);
}

static int pwm_sun20i_d1_set_cycles(const struct device *dev, uint32_t channel,
				       uint32_t period_cycles, uint32_t pulse_cycles,
				       pwm_flags_t flags)
{
	const struct pwm_sun20i_d1_config *cfg = dev->config;
	uint32_t prescaler_k;
	uint32_t period_pwm;
	uint32_t pulse_pwm;
	uint32_t pccr;
	uint32_t pcr;
	uint32_t ppr;
	uint32_t pcgr;
	uint32_t per;

	if (channel >= PWM_CHANNELS) {
		return -EINVAL;
	}

	if (period_cycles == 0U || pulse_cycles > period_cycles) {
		return -EINVAL;
	}

	if ((flags & ~(PWM_POLARITY_INVERTED)) != 0U) {
		return -ENOTSUP;
	}

	prescaler_k = DIV_ROUND_UP(period_cycles, PWM_PERIOD_MAX);
	if ((prescaler_k == 0U) || (prescaler_k > 256U)) {
		return -EINVAL;
	}

	period_pwm = DIV_ROUND_UP(period_cycles, prescaler_k);
	if ((period_pwm == 0U) || (period_pwm > PWM_PERIOD_MAX)) {
		return -EINVAL;
	}

	pulse_pwm = pulse_cycles / prescaler_k;
	if (pulse_pwm > period_pwm) {
		pulse_pwm = period_pwm;
	}
	if (pulse_pwm > 0xFFFFU) {
		return -EINVAL;
	}

	pccr = sys_read32(pwm_sun20i_d1_reg(cfg, pwm_sun20i_d1_pccr(channel)));
	pccr &= ~(PCCR_CLK_SRC_MASK | PCCR_CLK_DIV_MASK);
	pccr |= (PCCR_CLK_SRC_HOSC << PCCR_CLK_SRC_SHIFT);
	sys_write32(pccr, pwm_sun20i_d1_reg(cfg, pwm_sun20i_d1_pccr(channel)));

	pcr = sys_read32(pwm_sun20i_d1_reg(cfg, pwm_sun20i_d1_pcr(channel)));
	pcr &= ~(PCR_PRESCAL_MASK | PCR_ACT_STA | PCR_MODE | PCR_PUL_START);
	pcr |= (prescaler_k - 1U) & PCR_PRESCAL_MASK;
	if ((flags & PWM_POLARITY_INVERTED) == 0U) {
		pcr |= PCR_ACT_STA;
	}
	sys_write32(pcr, pwm_sun20i_d1_reg(cfg, pwm_sun20i_d1_pcr(channel)));

	ppr = ((period_pwm - 1U) << 16) | (pulse_pwm & 0xFFFFU);
	sys_write32(ppr, pwm_sun20i_d1_reg(cfg, pwm_sun20i_d1_ppr(channel)));

	pcgr = sys_read32(pwm_sun20i_d1_reg(cfg, PWM_PCGR));
	pcgr |= PCGR_GATE_BIT(channel);
	pcgr &= ~PCGR_BYPASS_BIT(channel);
	sys_write32(pcgr, pwm_sun20i_d1_reg(cfg, PWM_PCGR));

	per = sys_read32(pwm_sun20i_d1_reg(cfg, PWM_PER));
	per |= BIT(channel);
	sys_write32(per, pwm_sun20i_d1_reg(cfg, PWM_PER));

	return 0;
}

static int pwm_sun20i_d1_get_cycles_per_sec(const struct device *dev,
					       uint32_t channel, uint64_t *cycles)
{
	struct pwm_sun20i_d1_data *data = dev->data;

	if (channel >= PWM_CHANNELS) {
		return -EINVAL;
	}

	*cycles = data->input_clk;
	return 0;
}

static int pwm_sun20i_d1_init(const struct device *dev)
{
	const struct pwm_sun20i_d1_config *cfg = dev->config;
	struct pwm_sun20i_d1_data *data = dev->data;
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

	return 0;
}

static DEVICE_API(pwm, pwm_sun20i_d1_api) = {
	.set_cycles = pwm_sun20i_d1_set_cycles,
	.get_cycles_per_sec = pwm_sun20i_d1_get_cycles_per_sec,
};

#define PWM_SUN20I_D1_INIT(n)                                                      \
	PINCTRL_DT_INST_DEFINE(n);                                                    \
										  \
	static struct pwm_sun20i_d1_data pwm_sun20i_d1_data_##n;                 \
										  \
	static const struct pwm_sun20i_d1_config pwm_sun20i_d1_cfg_##n = {       \
		.base = DT_INST_REG_ADDR(n),                                             \
		.pcfg = PINCTRL_DT_INST_DEV_CONFIG_GET(n),                               \
		.clock_dev = COND_CODE_1(DT_INST_NODE_HAS_PROP(n, clocks),               \
				 (DEVICE_DT_GET(DT_INST_CLOCKS_CTLR(n))),                 \
				 (NULL)),                                              \
		.clock_id = (clock_control_subsys_t)COND_CODE_1(                         \
			DT_INST_NODE_HAS_PROP(n, clocks),                                 \
			(DT_INST_CLOCKS_CELL(n, clkid)),                                   \
			(0)),                                                               \
		.input_clk = 24000000U,                                                \
	};                                                                            \
										  \
	DEVICE_DT_INST_DEFINE(n, pwm_sun20i_d1_init, NULL,                       \
			      &pwm_sun20i_d1_data_##n,                          \
			      &pwm_sun20i_d1_cfg_##n,                           \
			      POST_KERNEL,                                         \
			      CONFIG_PWM_INIT_PRIORITY,                            \
			      &pwm_sun20i_d1_api);

DT_INST_FOREACH_STATUS_OKAY(PWM_SUN20I_D1_INIT)
