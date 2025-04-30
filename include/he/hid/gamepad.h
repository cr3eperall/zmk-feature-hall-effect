/*
 * Copyright (c) 2023 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once
#include <stdint.h>
#include <zephyr/sys/util.h>
#define BUTTONS_BYTES DIV_ROUND_UP(CONFIG_HE_GAMEPAD_HID_NUM_BUTTONS,8)
#if BUTTONS_BYTES==1
typedef uint8_t zmk_gamepad_button_flags_t;
#elif BUTTONS_BYTES==2
typedef uint16_t zmk_gamepad_button_flags_t;
#elif BUTTONS_BYTES==3
typedef uint32_t zmk_gamepad_button_flags_t;
#elif BUTTONS_BYTES==4
typedef uint32_t zmk_gamepad_button_flags_t;
#endif
typedef uint16_t zmk_gamepad_button_t;
