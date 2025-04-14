# Copyright (c) 2020-2021 The ZMK Contributors
# SPDX-License-Identifier: MIT

# zephyr_include_directories(.)

target_sources(app PRIVATE battery_common.c)
target_sources_ifdef(CONFIG_ZMK_BATTERY_NRF_VDDH app PRIVATE battery_nrf_vddh.c)
# zephyr_library_sources_ifdef(CONFIG_ZMK_BATTERY_VOLTAGE_DIVIDER battery_voltage_divider.c)