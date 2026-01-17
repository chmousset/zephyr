/*
 * Copyright (c) 2025
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT jedec_spi_nand

#include <errno.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(spi_nand, CONFIG_FLASH_LOG_LEVEL);

#define SPI_NAND_CMD_RESET           0xFF
#define SPI_NAND_CMD_GET_FEATURE     0x0F
#define SPI_NAND_CMD_SET_FEATURE     0x1F
#define SPI_NAND_CMD_READ_ID         0x9F
#define SPI_NAND_CMD_PAGE_READ       0x13
#define SPI_NAND_CMD_READ_CACHE      0x03
#define SPI_NAND_CMD_PROGRAM_LOAD    0x02
#define SPI_NAND_CMD_PROGRAM_EXECUTE 0x10
#define SPI_NAND_CMD_WRITE_ENABLE    0x06
#define SPI_NAND_CMD_BLOCK_ERASE     0xD8

#define SPI_NAND_FEATURE_PROTECTION  0xA0
#define SPI_NAND_FEATURE_CONFIG      0xB0
#define SPI_NAND_FEATURE_STATUS      0xC0

#define SPI_NAND_STATUS_OIP           BIT(0)
#define SPI_NAND_STATUS_WEL           BIT(1)
#define SPI_NAND_STATUS_ERASE_FAIL    BIT(2)
#define SPI_NAND_STATUS_PROGRAM_FAIL  BIT(3)
#define SPI_NAND_STATUS_ECC_MASK      (0x3U << 4)
#define SPI_NAND_STATUS_ECC_UNCORR    (0x2U << 4)

#define SPI_NAND_CONFIG_ECC_EN        BIT(4)
#define SPI_NAND_CONFIG_BUF_MODE      BIT(3)

#define SPI_NAND_TIMEOUT_US 2000000U

struct spi_nand_config {
	struct spi_dt_spec spi;
	uint32_t page_size;
	uint32_t pages_per_block;
	uint32_t block_count;
	uint32_t block_size;
	uint32_t flash_size;
	struct flash_pages_layout layout;
	struct flash_parameters parameters;
};

struct spi_nand_data {
	struct k_sem lock;
	uint8_t *scratch;
};

static void spi_nand_acquire(const struct device *dev)
{
	struct spi_nand_data *data = dev->data;

	k_sem_take(&data->lock, K_FOREVER);
}

static void spi_nand_release(const struct device *dev)
{
	struct spi_nand_data *data = dev->data;

	k_sem_give(&data->lock);
}

static int spi_nand_get_feature(const struct device *dev, uint8_t addr, uint8_t *val)
{
	const struct spi_nand_config *cfg = dev->config;
	uint8_t cmd[2] = { SPI_NAND_CMD_GET_FEATURE, addr };

	struct spi_buf tx_bufs[] = {
		{ .buf = cmd, .len = sizeof(cmd) },
		{ .buf = NULL, .len = 1 },
	};
	struct spi_buf rx_bufs[] = {
		{ .buf = NULL, .len = sizeof(cmd) },
		{ .buf = val, .len = 1 },
	};

	const struct spi_buf_set tx_set = {
		.buffers = tx_bufs,
		.count = ARRAY_SIZE(tx_bufs),
	};
	const struct spi_buf_set rx_set = {
		.buffers = rx_bufs,
		.count = ARRAY_SIZE(rx_bufs),
	};

	return spi_transceive_dt(&cfg->spi, &tx_set, &rx_set);
}

static int spi_nand_set_feature(const struct device *dev, uint8_t addr, uint8_t val)
{
	const struct spi_nand_config *cfg = dev->config;
	uint8_t cmd[3] = { SPI_NAND_CMD_SET_FEATURE, addr, val };
	struct spi_buf tx_buf = { .buf = cmd, .len = sizeof(cmd) };
	const struct spi_buf_set tx_set = { .buffers = &tx_buf, .count = 1 };

	return spi_write_dt(&cfg->spi, &tx_set);
}

static int spi_nand_wait_ready(const struct device *dev, uint8_t *status)
{
	uint32_t timeout = SPI_NAND_TIMEOUT_US;
	uint8_t val = 0U;
	int ret;

	while (timeout-- != 0U) {
		ret = spi_nand_get_feature(dev, SPI_NAND_FEATURE_STATUS, &val);
		if (ret != 0) {
			return ret;
		}

		if ((val & SPI_NAND_STATUS_OIP) == 0U) {
			if (status != NULL) {
				*status = val;
			}
			return 0;
		}

		k_busy_wait(1);
	}

	return -ETIMEDOUT;
}

static int spi_nand_write_enable(const struct device *dev)
{
	const struct spi_nand_config *cfg = dev->config;
	uint8_t cmd = SPI_NAND_CMD_WRITE_ENABLE;
	struct spi_buf tx_buf = { .buf = &cmd, .len = 1 };
	const struct spi_buf_set tx_set = { .buffers = &tx_buf, .count = 1 };

	return spi_write_dt(&cfg->spi, &tx_set);
}

static int spi_nand_reset(const struct device *dev)
{
	const struct spi_nand_config *cfg = dev->config;
	uint8_t cmd = SPI_NAND_CMD_RESET;
	struct spi_buf tx_buf = { .buf = &cmd, .len = 1 };
	const struct spi_buf_set tx_set = { .buffers = &tx_buf, .count = 1 };
	int ret;

	ret = spi_write_dt(&cfg->spi, &tx_set);
	if (ret != 0) {
		return ret;
	}

	return spi_nand_wait_ready(dev, NULL);
}

static int spi_nand_page_read(const struct device *dev, uint32_t page, uint8_t *buf)
{
	const struct spi_nand_config *cfg = dev->config;
	uint8_t cmd[4];
	uint8_t status;
	int ret;

	cmd[0] = SPI_NAND_CMD_PAGE_READ;
	cmd[1] = (page >> 16) & 0xFF;
	cmd[2] = (page >> 8) & 0xFF;
	cmd[3] = page & 0xFF;

	struct spi_buf tx_cmd = { .buf = cmd, .len = sizeof(cmd) };
	const struct spi_buf_set tx_cmd_set = { .buffers = &tx_cmd, .count = 1 };

	ret = spi_write_dt(&cfg->spi, &tx_cmd_set);
	if (ret != 0) {
		return ret;
	}

	ret = spi_nand_wait_ready(dev, &status);
	if (ret != 0) {
		return ret;
	}

	if ((status & SPI_NAND_STATUS_ECC_MASK) == SPI_NAND_STATUS_ECC_UNCORR) {
		return -EBADMSG;
	}

	cmd[0] = SPI_NAND_CMD_READ_CACHE;
	cmd[1] = 0x00;
	cmd[2] = 0x00;
	cmd[3] = 0x00;

	struct spi_buf tx_bufs[] = {
		{ .buf = cmd, .len = sizeof(cmd) },
		{ .buf = NULL, .len = cfg->page_size },
	};
	struct spi_buf rx_bufs[] = {
		{ .buf = NULL, .len = sizeof(cmd) },
		{ .buf = buf, .len = cfg->page_size },
	};

	const struct spi_buf_set tx_set = {
		.buffers = tx_bufs,
		.count = ARRAY_SIZE(tx_bufs),
	};
	const struct spi_buf_set rx_set = {
		.buffers = rx_bufs,
		.count = ARRAY_SIZE(rx_bufs),
	};

	return spi_transceive_dt(&cfg->spi, &tx_set, &rx_set);
}

static int spi_nand_program_page(const struct device *dev, uint32_t page,
				 const uint8_t *buf)
{
	const struct spi_nand_config *cfg = dev->config;
	uint8_t cmd[4];
	uint8_t status;
	int ret;

	ret = spi_nand_write_enable(dev);
	if (ret != 0) {
		return ret;
	}

	cmd[0] = SPI_NAND_CMD_PROGRAM_LOAD;
	cmd[1] = 0x00;
	cmd[2] = 0x00;

	struct spi_buf tx_bufs[] = {
		{ .buf = cmd, .len = 3 },
		{ .buf = (void *)buf, .len = cfg->page_size },
	};
	const struct spi_buf_set tx_set = {
		.buffers = tx_bufs,
		.count = ARRAY_SIZE(tx_bufs),
	};

	ret = spi_write_dt(&cfg->spi, &tx_set);
	if (ret != 0) {
		return ret;
	}

	cmd[0] = SPI_NAND_CMD_PROGRAM_EXECUTE;
	cmd[1] = (page >> 16) & 0xFF;
	cmd[2] = (page >> 8) & 0xFF;
	cmd[3] = page & 0xFF;

	struct spi_buf tx_exec = { .buf = cmd, .len = sizeof(cmd) };
	const struct spi_buf_set tx_exec_set = { .buffers = &tx_exec, .count = 1 };

	ret = spi_write_dt(&cfg->spi, &tx_exec_set);
	if (ret != 0) {
		return ret;
	}

	ret = spi_nand_wait_ready(dev, &status);
	if (ret != 0) {
		return ret;
	}

	if ((status & SPI_NAND_STATUS_PROGRAM_FAIL) != 0U) {
		return -EIO;
	}

	return 0;
}

static int spi_nand_erase_block(const struct device *dev, uint32_t block)
{
	const struct spi_nand_config *cfg = dev->config;
	uint32_t page = block * cfg->pages_per_block;
	uint8_t cmd[4];
	uint8_t status;
	int ret;

	ret = spi_nand_write_enable(dev);
	if (ret != 0) {
		return ret;
	}

	cmd[0] = SPI_NAND_CMD_BLOCK_ERASE;
	cmd[1] = (page >> 16) & 0xFF;
	cmd[2] = (page >> 8) & 0xFF;
	cmd[3] = page & 0xFF;

	struct spi_buf tx_buf = { .buf = cmd, .len = sizeof(cmd) };
	const struct spi_buf_set tx_set = { .buffers = &tx_buf, .count = 1 };

	ret = spi_write_dt(&cfg->spi, &tx_set);
	if (ret != 0) {
		return ret;
	}

	ret = spi_nand_wait_ready(dev, &status);
	if (ret != 0) {
		return ret;
	}

	if ((status & SPI_NAND_STATUS_ERASE_FAIL) != 0U) {
		return -EIO;
	}

	return 0;
}

static int spi_nand_read(const struct device *dev, off_t offset, void *data,
			 size_t len)
{
	const struct spi_nand_config *cfg = dev->config;
	struct spi_nand_data *drv_data = dev->data;
	uint8_t *dst = data;
	int ret = 0;

	if ((offset < 0) || ((uint64_t)offset + len > cfg->flash_size)) {
		return -EINVAL;
	}

	if (len == 0U) {
		return 0;
	}

	spi_nand_acquire(dev);

	while (len > 0U) {
		uint32_t page = offset / cfg->page_size;
		uint32_t page_off = offset % cfg->page_size;
		size_t copy_len = MIN(len, cfg->page_size - page_off);

		ret = spi_nand_page_read(dev, page, drv_data->scratch);
		if (ret != 0) {
			break;
		}

		memcpy(dst, drv_data->scratch + page_off, copy_len);

		dst += copy_len;
		offset += copy_len;
		len -= copy_len;
	}

	spi_nand_release(dev);
	return ret;
}

static int spi_nand_write(const struct device *dev, off_t offset,
			  const void *data, size_t len)
{
	const struct spi_nand_config *cfg = dev->config;
	const uint8_t *src = data;
	int ret = 0;

	if ((offset < 0) || ((uint64_t)offset + len > cfg->flash_size)) {
		return -EINVAL;
	}

	if (len == 0U) {
		return 0;
	}

	if ((offset % cfg->page_size) != 0U || (len % cfg->page_size) != 0U) {
		return -EINVAL;
	}

	spi_nand_acquire(dev);

	while (len > 0U) {
		uint32_t page = offset / cfg->page_size;

		ret = spi_nand_program_page(dev, page, src);
		if (ret != 0) {
			break;
		}

		src += cfg->page_size;
		offset += cfg->page_size;
		len -= cfg->page_size;
	}

	spi_nand_release(dev);
	return ret;
}

static int spi_nand_erase(const struct device *dev, off_t offset, size_t size)
{
	const struct spi_nand_config *cfg = dev->config;
	int ret = 0;

	if ((offset < 0) || ((uint64_t)offset + size > cfg->flash_size)) {
		return -EINVAL;
	}

	if (size == 0U) {
		return 0;
	}

	if ((offset % cfg->block_size) != 0U || (size % cfg->block_size) != 0U) {
		return -EINVAL;
	}

	spi_nand_acquire(dev);

	while (size > 0U) {
		uint32_t block = offset / cfg->block_size;

		ret = spi_nand_erase_block(dev, block);
		if (ret != 0) {
			break;
		}

		offset += cfg->block_size;
		size -= cfg->block_size;
	}

	spi_nand_release(dev);
	return ret;
}

static const struct flash_parameters *spi_nand_get_parameters(const struct device *dev)
{
	const struct spi_nand_config *cfg = dev->config;

	return &cfg->parameters;
}

static size_t spi_nand_get_size(const struct device *dev)
{
	const struct spi_nand_config *cfg = dev->config;

	return cfg->flash_size;
}

#if defined(CONFIG_FLASH_PAGE_LAYOUT)
static void spi_nand_page_layout(const struct device *dev,
				 const struct flash_pages_layout **layout,
				 size_t *layout_size)
{
	const struct spi_nand_config *cfg = dev->config;

	*layout = &cfg->layout;
	*layout_size = 1U;
}
#endif

#if defined(CONFIG_FLASH_JESD216_API)
static int spi_nand_read_jedec_id(const struct device *dev, uint8_t *id)
{
	const struct spi_nand_config *cfg = dev->config;
	uint8_t cmd[2] = { SPI_NAND_CMD_READ_ID, 0x00 };

	struct spi_buf tx_bufs[] = {
		{ .buf = cmd, .len = sizeof(cmd) },
		{ .buf = NULL, .len = 3 },
	};
	struct spi_buf rx_bufs[] = {
		{ .buf = NULL, .len = sizeof(cmd) },
		{ .buf = id, .len = 3 },
	};

	const struct spi_buf_set tx_set = {
		.buffers = tx_bufs,
		.count = ARRAY_SIZE(tx_bufs),
	};
	const struct spi_buf_set rx_set = {
		.buffers = rx_bufs,
		.count = ARRAY_SIZE(rx_bufs),
	};

	return spi_transceive_dt(&cfg->spi, &tx_set, &rx_set);
}
#endif

static int spi_nand_init(const struct device *dev)
{
	const struct spi_nand_config *cfg = dev->config;
	struct spi_nand_data *data = dev->data;
	uint8_t cfg_reg;
	int ret;

	k_sem_init(&data->lock, 1, 1);

	if (!spi_is_ready_dt(&cfg->spi)) {
		return -ENODEV;
	}

	ret = spi_nand_reset(dev);
	if (ret != 0) {
		return ret;
	}

	ret = spi_nand_set_feature(dev, SPI_NAND_FEATURE_PROTECTION, 0x00);
	if (ret != 0) {
		return ret;
	}

	ret = spi_nand_get_feature(dev, SPI_NAND_FEATURE_CONFIG, &cfg_reg);
	if (ret != 0) {
		return ret;
	}

	cfg_reg |= SPI_NAND_CONFIG_ECC_EN | SPI_NAND_CONFIG_BUF_MODE;
	return spi_nand_set_feature(dev, SPI_NAND_FEATURE_CONFIG, cfg_reg);
}

static DEVICE_API(flash, spi_nand_api) = {
	.read = spi_nand_read,
	.write = spi_nand_write,
	.erase = spi_nand_erase,
	.get_parameters = spi_nand_get_parameters,
	.get_size = spi_nand_get_size,
#if defined(CONFIG_FLASH_PAGE_LAYOUT)
	.page_layout = spi_nand_page_layout,
#endif
#if defined(CONFIG_FLASH_JESD216_API)
	.read_jedec_id = spi_nand_read_jedec_id,
#endif
};

#define SPI_NAND_TOTAL_SIZE(n) \
	(DT_INST_PROP(n, page_size) * DT_INST_PROP(n, pages_per_block) * \
	 DT_INST_PROP(n, block_count))

#define SPI_NAND_BLOCK_SIZE(n) \
	(DT_INST_PROP(n, page_size) * DT_INST_PROP(n, pages_per_block))

#define SPI_NAND_INIT(n)                                                        \
	static uint8_t spi_nand_scratch_##n[DT_INST_PROP(n, page_size)];          \
	static struct spi_nand_data spi_nand_data_##n = {                          \
		.scratch = spi_nand_scratch_##n,                                     \
	};                                                                          \
	static const struct spi_nand_config spi_nand_config_##n = {                \
		.spi = SPI_DT_SPEC_INST_GET(n, SPI_WORD_SET(8)),                    \
		.page_size = DT_INST_PROP(n, page_size),                            \
		.pages_per_block = DT_INST_PROP(n, pages_per_block),                \
		.block_count = DT_INST_PROP(n, block_count),                        \
		.block_size = SPI_NAND_BLOCK_SIZE(n),                               \
		.flash_size = SPI_NAND_TOTAL_SIZE(n),                               \
		.layout = {                                                         \
			.pages_count = DT_INST_PROP(n, block_count),                   \
			.pages_size = SPI_NAND_BLOCK_SIZE(n),                         \
		},                                                                  \
		.parameters = {                                                     \
			.write_block_size = DT_INST_PROP(n, page_size),                \
			.erase_value = 0xFF,                                         \
		},                                                                  \
	};                                                                          \
	DEVICE_DT_INST_DEFINE(n, spi_nand_init, NULL,                               \
			      &spi_nand_data_##n,                                   \
			      &spi_nand_config_##n,                                 \
			      POST_KERNEL,                                           \
			      CONFIG_FLASH_INIT_PRIORITY,                            \
			      &spi_nand_api);

DT_INST_FOREACH_STATUS_OKAY(SPI_NAND_INIT)
