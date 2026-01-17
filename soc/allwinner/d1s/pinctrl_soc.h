/*
 * Copyright (c) 2025
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_SOC_ALLWINNER_D1S_PINCTRL_SOC_H_
#define ZEPHYR_SOC_ALLWINNER_D1S_PINCTRL_SOC_H_

#include <zephyr/devicetree.h>
#include <zephyr/types.h>
#include <zephyr/dt-bindings/pinctrl/allwinner-d1-pinctrl.h>

/*
 * pinctrl_soc_pin_t bit layout:
 * - 0..12: pinmux encoding (port/pin/function)
 * - 13..14: pull configuration (packed 2-bit fields in PULL registers)
 * - 15..18: drive strength (packed 4-bit fields in DRV registers)
 */

typedef uint32_t pinctrl_soc_pin_t;

#define ALLWINNER_D1_PULL_SHIFT 13
#define ALLWINNER_D1_PULL_MASK  0x3

#define ALLWINNER_D1_DRV_SHIFT  15
#define ALLWINNER_D1_DRV_MASK   0xF

#define ALLWINNER_D1_PULL_NONE  0
#define ALLWINNER_D1_PULL_UP    1
#define ALLWINNER_D1_PULL_DOWN  2

#define ALLWINNER_D1_PIN_PORT(pin) \
	(((pin) >> ALLWINNER_D1_PINMUX_PORT_SHIFT) & ALLWINNER_D1_PINMUX_PORT_MASK)
#define ALLWINNER_D1_PIN_NUM(pin) \
	(((pin) >> ALLWINNER_D1_PINMUX_PIN_SHIFT) & ALLWINNER_D1_PINMUX_PIN_MASK)
#define ALLWINNER_D1_PIN_FUNC(pin) \
	(((pin) >> ALLWINNER_D1_PINMUX_FUNC_SHIFT) & ALLWINNER_D1_PINMUX_FUNC_MASK)
#define ALLWINNER_D1_PIN_PULL(pin) \
	(((pin) >> ALLWINNER_D1_PULL_SHIFT) & ALLWINNER_D1_PULL_MASK)
#define ALLWINNER_D1_PIN_DRV(pin) \
	(((pin) >> ALLWINNER_D1_DRV_SHIFT) & ALLWINNER_D1_DRV_MASK)

#define ALLWINNER_D1_PIN_INIT(node_id, prop, idx)                               \
	(DT_PROP(DT_PHANDLE_BY_IDX(node_id, prop, idx), pinmux) |              \
	 ((DT_PROP_OR(DT_PHANDLE_BY_IDX(node_id, prop, idx), bias_pull_up, 0)   \
	  ? ALLWINNER_D1_PULL_UP : 0) << ALLWINNER_D1_PULL_SHIFT) |            \
	 ((DT_PROP_OR(DT_PHANDLE_BY_IDX(node_id, prop, idx), bias_pull_down, 0) \
	  ? ALLWINNER_D1_PULL_DOWN : 0) << ALLWINNER_D1_PULL_SHIFT) |          \
	 (DT_PROP_OR(DT_PHANDLE_BY_IDX(node_id, prop, idx), drive_strength, 1)  \
	  << ALLWINNER_D1_DRV_SHIFT))

#define Z_PINCTRL_STATE_PIN_INIT(node_id, prop, idx) \
	ALLWINNER_D1_PIN_INIT(node_id, prop, idx),

#define Z_PINCTRL_STATE_PINS_INIT(node_id, prop) \
	{DT_FOREACH_PROP_ELEM(node_id, prop, Z_PINCTRL_STATE_PIN_INIT)}

#endif /* ZEPHYR_SOC_ALLWINNER_D1S_PINCTRL_SOC_H_ */
