/*
 * Copyright (c) 2025
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/init.h>
#include <zephyr/sys/util.h>

__weak int allwinner_d1s_dram_init(void)
{
	return 0;
}

static int allwinner_d1s_soc_init(void)
{
	if (IS_ENABLED(CONFIG_SOC_ALLWINNER_D1S_DDR_INIT)) {
		int ret = allwinner_d1s_dram_init();

		if (ret != 0) {
			return ret;
		}
	}

	return 0;
}

SYS_INIT(allwinner_d1s_soc_init, PRE_KERNEL_1, 0);
