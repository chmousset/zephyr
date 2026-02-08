/*
 * Copyright (c) 2025
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT allwinner_sun20i_d1_spi

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/sys_io.h>

LOG_MODULE_REGISTER(spi_sun6i, CONFIG_SPI_LOG_LEVEL);

#include "spi_context.h"

#define SPI_GCR 0x04
#define SPI_TCR 0x08
#define SPI_IER 0x10
#define SPI_ISR 0x14
#define SPI_FCR 0x18
#define SPI_FSR 0x1C
#define SPI_WCR 0x20
#define SPI_CCR 0x24
#define SPI_MBC 0x30
#define SPI_MTC 0x34
#define SPI_BCC 0x38
#define SPI_TXD 0x200
#define SPI_RXD 0x300

#define SPI_GCR_EN    BIT(0)
#define SPI_GCR_MODE  BIT(1)
#define SPI_GCR_TP_EN BIT(7)
#define SPI_GCR_SRST  BIT(31)

#define SPI_TCR_CPHA      BIT(0)
#define SPI_TCR_CPOL      BIT(1)
#define SPI_TCR_SPOL      BIT(2)
#define SPI_TCR_SSCTL     BIT(3)
#define SPI_TCR_SS_SEL_SHIFT 4
#define SPI_TCR_SS_OWNER  BIT(6)
#define SPI_TCR_SS_LEVEL  BIT(7)
#define SPI_TCR_FBS       BIT(12)
#define SPI_TCR_XCH       BIT(31)

#define SPI_FCR_RX_RST BIT(15)
#define SPI_FCR_TX_RST BIT(31)

#define SPI_FSR_RX_CNT_MASK 0xFF

#define SPI_CCR_CDR2_MASK  0xFF
#define SPI_CCR_CDR1_SHIFT 8
#define SPI_CCR_CDR1_MASK  0xF
#define SPI_CCR_DRS        BIT(12)

#define SPI_FIFO_DEPTH  64U
#define SPI_XCH_TIMEOUT 100000U
#define SPI_RX_TIMEOUT  100000U

struct spi_sun6i_config {
	uintptr_t base;
	const struct pinctrl_dev_config *pcfg;
	const struct device *clock_dev;
	clock_control_subsys_t clock_ids[2];
	size_t num_clocks;
};

struct spi_sun6i_data {
	struct spi_context ctx;
	uint32_t mod_clk_rate;
};

static inline uint32_t spi_sun6i_read(const struct spi_sun6i_config *cfg,
					     uint32_t reg)
{
	return sys_read32(cfg->base + reg);
}

static inline void spi_sun6i_write(const struct spi_sun6i_config *cfg,
					  uint32_t reg, uint32_t val)
{
	sys_write32(val, cfg->base + reg);
}

static void spi_sun6i_dump_regs(const struct device *dev)
{
	const struct spi_sun6i_config *cfg = dev->config;

	LOG_ERR("%s: GCR=0x%08x TCR=0x%08x FCR=0x%08x FSR=0x%08x ISR=0x%08x CCR=0x%08x",
		dev->name,
		spi_sun6i_read(cfg, SPI_GCR),
		spi_sun6i_read(cfg, SPI_TCR),
		spi_sun6i_read(cfg, SPI_FCR),
		spi_sun6i_read(cfg, SPI_FSR),
		spi_sun6i_read(cfg, SPI_ISR),
		spi_sun6i_read(cfg, SPI_CCR));
	LOG_ERR("%s: MBC=0x%08x MTC=0x%08x BCC=0x%08x",
		dev->name,
		spi_sun6i_read(cfg, SPI_MBC),
		spi_sun6i_read(cfg, SPI_MTC),
		spi_sun6i_read(cfg, SPI_BCC));
}

static void spi_sun6i_reset_fifos(const struct spi_sun6i_config *cfg)
{
	uint32_t val = spi_sun6i_read(cfg, SPI_FCR);

	val |= SPI_FCR_TX_RST | SPI_FCR_RX_RST;
	spi_sun6i_write(cfg, SPI_FCR, val);
}

static void spi_sun6i_set_cs_level(const struct device *dev, bool active)
{
	struct spi_sun6i_data *data = dev->data;
	const struct spi_sun6i_config *cfg = dev->config;
	const struct spi_config *config = data->ctx.config;
	bool active_low = (config->operation & SPI_CS_ACTIVE_HIGH) == 0U;
	uint32_t tcr = spi_sun6i_read(cfg, SPI_TCR);
	bool level = active_low ? !active : active;

	if (level) {
		tcr |= SPI_TCR_SS_LEVEL;
	} else {
		tcr &= ~SPI_TCR_SS_LEVEL;
	}

	spi_sun6i_write(cfg, SPI_TCR, tcr);

	if (config->cs.delay != 0U) {
		k_busy_wait(config->cs.delay);
	}
}

static int spi_sun6i_calc_clk(uint32_t input_clk, uint32_t freq,
				     uint32_t *ccr_out)
{
	uint32_t best_rate = 0U;
	uint32_t best_cdr = 0U;
	bool best_cdr2 = true;

	if (freq == 0U) {
		return -EINVAL;
	}

	for (uint32_t cdr2 = 0U; cdr2 <= SPI_CCR_CDR2_MASK; cdr2++) {
		uint32_t rate = input_clk / (2U * (cdr2 + 1U));

		if ((rate <= freq) && (rate > best_rate)) {
			best_rate = rate;
			best_cdr = cdr2;
			best_cdr2 = true;
		}
	}

	for (uint32_t cdr1 = 0U; cdr1 <= SPI_CCR_CDR1_MASK; cdr1++) {
		uint32_t rate = input_clk / (1U << cdr1);

		if ((rate <= freq) && (rate > best_rate)) {
			best_rate = rate;
			best_cdr = cdr1;
			best_cdr2 = false;
		}
	}

	if (best_rate == 0U) {
		uint32_t cdr2_rate = input_clk / (2U * (SPI_CCR_CDR2_MASK + 1U));
		uint32_t cdr1_rate = input_clk / (1U << SPI_CCR_CDR1_MASK);

		if (cdr2_rate <= cdr1_rate) {
			best_cdr2 = true;
			best_cdr = SPI_CCR_CDR2_MASK;
		} else {
			best_cdr2 = false;
			best_cdr = SPI_CCR_CDR1_MASK;
		}
	}

	if (best_cdr2) {
		*ccr_out = SPI_CCR_DRS | (best_cdr & SPI_CCR_CDR2_MASK);
	} else {
		*ccr_out = (best_cdr & SPI_CCR_CDR1_MASK) << SPI_CCR_CDR1_SHIFT;
	}

	return 0;
}

static int spi_sun6i_configure(const struct device *dev,
				      const struct spi_config *config)
{
	struct spi_sun6i_data *data = dev->data;
	const struct spi_sun6i_config *cfg = dev->config;
	uint32_t tcr;
	uint32_t ccr;

	LOG_DBG("%s: configure freq=%uHz slave=%u op=0x%08x",
		dev->name, config->frequency, config->slave, config->operation);

	if ((config->operation & SPI_OP_MODE_SLAVE) != 0U) {
		LOG_ERR("%s: slave mode not supported", dev->name);
		return -ENOTSUP;
	}

	if (SPI_WORD_SIZE_GET(config->operation) != 8U) {
		LOG_ERR("%s: unsupported word size %u",
			dev->name, SPI_WORD_SIZE_GET(config->operation));
		return -ENOTSUP;
	}

	if (config->slave > 3U) {
		LOG_ERR("%s: invalid slave %u", dev->name, config->slave);
		return -EINVAL;
	}

	if (spi_context_configured(&data->ctx, config)) {
		return 0;
	}

	data->ctx.config = config;

	if (spi_sun6i_calc_clk(data->mod_clk_rate, config->frequency, &ccr) == 0) {
		LOG_DBG("%s: mod_clk=%uHz ccr=0x%08x", dev->name, data->mod_clk_rate, ccr);
		spi_sun6i_write(cfg, SPI_CCR, ccr);
	}

	tcr = spi_sun6i_read(cfg, SPI_TCR);
	tcr &= ~(SPI_TCR_CPHA | SPI_TCR_CPOL | SPI_TCR_SPOL | SPI_TCR_FBS |
		 (0x3U << SPI_TCR_SS_SEL_SHIFT));

	if ((config->operation & SPI_MODE_CPHA) != 0U) {
		tcr |= SPI_TCR_CPHA;
	}
	if ((config->operation & SPI_MODE_CPOL) != 0U) {
		tcr |= SPI_TCR_CPOL;
	}
	if ((config->operation & SPI_TRANSFER_LSB) != 0U) {
		tcr |= SPI_TCR_FBS;
	}
	if ((config->operation & SPI_CS_ACTIVE_HIGH) == 0U) {
		tcr |= SPI_TCR_SPOL;
	}

	tcr |= SPI_TCR_SS_OWNER;
	tcr |= (config->slave << SPI_TCR_SS_SEL_SHIFT);
	tcr |= SPI_TCR_SS_LEVEL;

	spi_sun6i_write(cfg, SPI_TCR, tcr);

	return 0;
}

static int spi_sun6i_transfer(const struct device *dev)
{
	struct spi_sun6i_data *data = dev->data;
	const struct spi_sun6i_config *cfg = dev->config;
	struct spi_context *ctx = &data->ctx;
	int ret = 0;

	LOG_DBG("%s: transfer tx=%zu rx=%zu",
		dev->name,
		spi_context_total_tx_len(ctx),
		spi_context_total_rx_len(ctx));

	while ((spi_context_tx_len_left(ctx, 1U) != 0U) ||
	       (spi_context_rx_len_left(ctx, 1U) != 0U)) {
		size_t tx_left = spi_context_tx_len_left(ctx, 1U);
		size_t rx_left = spi_context_rx_len_left(ctx, 1U);
		size_t len = MAX(tx_left, rx_left);
		size_t chunk = MIN(len, SPI_FIFO_DEPTH);

		spi_sun6i_reset_fifos(cfg);
		spi_sun6i_write(cfg, SPI_MBC, chunk);
		spi_sun6i_write(cfg, SPI_MTC, chunk);
		spi_sun6i_write(cfg, SPI_BCC, chunk);

		for (size_t i = 0U; i < chunk; i++) {
			uint8_t val = 0xFF;

			if (spi_context_tx_buf_on(ctx)) {
				val = *ctx->tx_buf;
			}
			spi_context_update_tx(ctx, 1U, 1U);

			sys_write8(val, cfg->base + SPI_TXD);
		}

		spi_sun6i_write(cfg, SPI_TCR,
				       spi_sun6i_read(cfg, SPI_TCR) | SPI_TCR_XCH);

		for (uint32_t wait = 0U; wait < SPI_XCH_TIMEOUT; wait++) {
			if ((spi_sun6i_read(cfg, SPI_TCR) & SPI_TCR_XCH) == 0U) {
				break;
			}
			if (wait == (SPI_XCH_TIMEOUT - 1U)) {
				LOG_ERR("%s: XCH timeout", dev->name);
				spi_sun6i_dump_regs(dev);
				spi_context_complete(ctx, dev, -EIO);
				return -EIO;
			}
		}

		for (uint32_t wait = 0U; wait < SPI_RX_TIMEOUT; wait++) {
			if (((spi_sun6i_read(cfg, SPI_FSR)) & SPI_FSR_RX_CNT_MASK) >=
			    chunk) {
				break;
			}
			if (wait == (SPI_RX_TIMEOUT - 1U)) {
				LOG_ERR("%s: RX timeout FSR=0x%08x", dev->name,
					spi_sun6i_read(cfg, SPI_FSR));
				spi_sun6i_dump_regs(dev);
				spi_context_complete(ctx, dev, -EIO);
				return -EIO;
			}
		}

		for (size_t i = 0U; i < chunk; i++) {
			uint8_t val = sys_read8(cfg->base + SPI_RXD);

			if (spi_context_rx_buf_on(ctx)) {
				*ctx->rx_buf = val;
			}
			spi_context_update_rx(ctx, 1U, 1U);
		}
	}

	spi_context_complete(ctx, dev, ret);
	return ret;
}

static int spi_sun6i_transceive(const struct device *dev,
				       const struct spi_config *config,
				       const struct spi_buf_set *tx_bufs,
				       const struct spi_buf_set *rx_bufs)
{
	struct spi_sun6i_data *data = dev->data;
	int ret;

	spi_context_lock(&data->ctx, false, NULL, NULL, config);

	ret = spi_sun6i_configure(dev, config);
	if (ret != 0) {
		spi_context_release(&data->ctx, ret);
		return ret;
	}

	spi_context_buffers_setup(&data->ctx, tx_bufs, rx_bufs, 1);

	if (spi_cs_is_gpio(config)) {
		spi_context_cs_control(&data->ctx, true);
	} else {
		spi_sun6i_set_cs_level(dev, true);
	}

	ret = spi_sun6i_transfer(dev);

	if (spi_cs_is_gpio(config)) {
		spi_context_cs_control(&data->ctx, false);
	} else {
		if ((config->operation & SPI_HOLD_ON_CS) == 0U) {
			spi_sun6i_set_cs_level(dev, false);
		}
	}

	ret = spi_context_wait_for_completion(&data->ctx);
	spi_context_release(&data->ctx, ret);

	return ret;
}

#ifdef CONFIG_SPI_ASYNC
static int spi_sun6i_transceive_async(const struct device *dev,
					     const struct spi_config *config,
					     const struct spi_buf_set *tx_bufs,
					     const struct spi_buf_set *rx_bufs,
					     spi_callback_t cb,
					     void *userdata)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(config);
	ARG_UNUSED(tx_bufs);
	ARG_UNUSED(rx_bufs);
	ARG_UNUSED(cb);
	ARG_UNUSED(userdata);

	return -ENOTSUP;
}
#endif

static int spi_sun6i_release(const struct device *dev,
				    const struct spi_config *config)
{
	ARG_UNUSED(config);
	struct spi_sun6i_data *data = dev->data;

	spi_context_unlock_unconditionally(&data->ctx);
	return 0;
}

static int spi_sun6i_init(const struct device *dev)
{
	const struct spi_sun6i_config *cfg = dev->config;
	struct spi_sun6i_data *data = dev->data;
	uint32_t val;
	int ret;
	uint32_t timeout;

	LOG_INF("%s: init", dev->name);

	if (!device_is_ready(cfg->clock_dev)) {
		LOG_ERR("%s: clock device not ready", dev->name);
		return -ENODEV;
	}

	/* Enable bus gate clock (index 0) — also deasserts reset in the CCU. */
	ret = clock_control_on(cfg->clock_dev, cfg->clock_ids[0]);
	if (ret != 0) {
		LOG_ERR("%s: failed to enable bus clock (%d)", dev->name, ret);
		return ret;
	}

	/* Enable and configure module clock (index 1), if present. */
	if (cfg->num_clocks > 1) {
		ret = clock_control_on(cfg->clock_dev, cfg->clock_ids[1]);
		if (ret != 0) {
			LOG_ERR("%s: failed to enable module clock (%d)", dev->name, ret);
			return ret;
		}

		/* Set module clock to maximum (HOSC rate). The SPI-local CCR
		 * divider handles per-transfer frequency selection.
		 */
		uint32_t req_rate = 24000000U;

		ret = clock_control_set_rate(cfg->clock_dev, cfg->clock_ids[1],
					     &req_rate);
		if ((ret != 0) && (ret != -ENOSYS)) {
			LOG_WRN("%s: set_rate failed (%d)", dev->name, ret);
		}

		k_busy_wait(10);

		ret = clock_control_get_rate(cfg->clock_dev, cfg->clock_ids[1],
					     &data->mod_clk_rate);
		if (ret != 0) {
			LOG_WRN("%s: get_rate failed (%d), assuming 24MHz", dev->name, ret);
			data->mod_clk_rate = 24000000U;
		}
	} else {
		data->mod_clk_rate = 24000000U;
	}

	LOG_INF("%s: module clock %uHz", dev->name, data->mod_clk_rate);

	ret = pinctrl_apply_state(cfg->pcfg, PINCTRL_STATE_DEFAULT);
	if (ret < 0) {
		LOG_ERR("%s: pinctrl_apply_state failed (%d)", dev->name, ret);
		return ret;
	}

	/* Soft-reset the SPI controller. */
	spi_sun6i_write(cfg, SPI_GCR, 0U);
	k_busy_wait(1);
	val = SPI_GCR_EN | SPI_GCR_MODE | SPI_GCR_TP_EN;
	spi_sun6i_write(cfg, SPI_GCR, val);
	for (timeout = 0U; timeout < 100000U; timeout++) {
		if ((spi_sun6i_read(cfg, SPI_GCR) & SPI_GCR_SRST) == 0U) {
			break;
		}
	}
	if (timeout == 100000U) {
		LOG_ERR("%s: soft reset timeout", dev->name);
	}

	/* Manual chip-select mode, CS deasserted. */
	spi_sun6i_write(cfg, SPI_TCR, SPI_TCR_SS_OWNER | SPI_TCR_SS_LEVEL);

	spi_sun6i_reset_fifos(cfg);
	spi_sun6i_write(cfg, SPI_IER, 0U);

	ret = spi_context_cs_configure_all(&data->ctx);
	if (ret != 0) {
		return ret;
	}

	spi_context_unlock_unconditionally(&data->ctx);

	LOG_INF("%s: init complete", dev->name);
	return 0;
}

static DEVICE_API(spi, spi_sun6i_api) = {
	.transceive = spi_sun6i_transceive,
#ifdef CONFIG_SPI_ASYNC
	.transceive_async = spi_sun6i_transceive_async,
#endif
#ifdef CONFIG_SPI_RTIO
	.iodev_submit = spi_rtio_iodev_default_submit,
#endif
	.release = spi_sun6i_release,
};

#define SPI_SUN6I_INIT(n)							\
	PINCTRL_DT_INST_DEFINE(n);						\
										\
	static struct spi_sun6i_data spi_sun6i_data_##n = {	\
		SPI_CONTEXT_INIT_LOCK(spi_sun6i_data_##n, ctx),		\
		SPI_CONTEXT_INIT_SYNC(spi_sun6i_data_##n, ctx),		\
		SPI_CONTEXT_CS_GPIOS_INITIALIZE(DT_DRV_INST(n), ctx)		\
	};									\
										\
	static const struct spi_sun6i_config spi_sun6i_cfg_##n = { \
		.base = DT_INST_REG_ADDR(n),					\
		.pcfg = PINCTRL_DT_INST_DEV_CONFIG_GET(n),			\
		.clock_dev = DEVICE_DT_GET(DT_INST_CLOCKS_CTLR(n)),		\
		.clock_ids = {							\
			(clock_control_subsys_t)				\
				DT_INST_CLOCKS_CELL_BY_IDX(n, 0, clkid),	\
			COND_CODE_1(DT_INST_CLOCKS_HAS_IDX(n, 1),		\
				((clock_control_subsys_t)			\
				 DT_INST_CLOCKS_CELL_BY_IDX(n, 1, clkid)),	\
				((clock_control_subsys_t)0)),			\
		},								\
		.num_clocks = DT_INST_NUM_CLOCKS(n),				\
	};									\
										\
	SPI_DEVICE_DT_INST_DEFINE(n, spi_sun6i_init, NULL,		\
				  &spi_sun6i_data_##n,			\
				  &spi_sun6i_cfg_##n,			\
				  POST_KERNEL,					\
				  CONFIG_SPI_INIT_PRIORITY,			\
				  &spi_sun6i_api);

DT_INST_FOREACH_STATUS_OKAY(SPI_SUN6I_INIT)
