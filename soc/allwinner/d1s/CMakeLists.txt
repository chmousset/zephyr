# Copyright (c) 2025
# SPDX-License-Identifier: Apache-2.0

zephyr_library_sources(soc.c)
zephyr_include_directories(.)

set(SOC_LINKER_SCRIPT ${ZEPHYR_BASE}/include/zephyr/arch/riscv/common/linker.ld CACHE INTERNAL "")

set(ALLWINNER_COMMON_DIR ${CMAKE_CURRENT_LIST_DIR}/../common)
zephyr_library_sources_ifdef(CONFIG_SUNXI_EGON_HEADER ${ALLWINNER_COMMON_DIR}/egon_header.S)
zephyr_linker_sources_ifdef(CONFIG_SUNXI_EGON_HEADER ROM_START SORT_KEY 0x0sunxi_egon ${ALLWINNER_COMMON_DIR}/egon_header.ld)

if(CONFIG_SUNXI_EGON_HEADER)
  set(SUNXI_EGON_TOOL ${ZEPHYR_BASE}/../tools/sunxi/mksunxiboot.py)
  set_property(GLOBAL APPEND PROPERTY extra_post_build_commands COMMAND
    ${PYTHON_EXECUTABLE} ${SUNXI_EGON_TOOL}
    ${CMAKE_BINARY_DIR}/zephyr/zephyr.bin
    ${CMAKE_BINARY_DIR}/zephyr/zephyr.bin
  )
endif()
