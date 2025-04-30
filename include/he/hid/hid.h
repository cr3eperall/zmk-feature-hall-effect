/*
 * Copyright (c) 2020 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <zephyr/usb/usb_device.h>
#include <zephyr/usb/class/usb_hid.h>

#include <zmk/keys.h>
#include <zmk/hid.h>
#include <zmk/endpoints_types.h>

#include <he/hid/gamepad.h>
#include <he/hid/hid_gamepad.h>

// #define ZMK_HID_GAMEPAD_NUM_BUTTONS 0x08
#define HE_GAMEPAD_HID_REPORT_ID 0x01
#if CONFIG_HE_GAMEPAD_HID_NUM_BUTTONS <= 16
#define BUTTONS_REMAINDER 8-(CONFIG_HE_GAMEPAD_HID_NUM_BUTTONS%8)
#else
#define BUTTONS_REMAINDER 32-CONFIG_HE_GAMEPAD_HID_NUM_BUTTONS
#endif
#include <dt-bindings/zmk/hid_usage.h>
#include <dt-bindings/zmk/hid_usage_pages.h>

// See https://www.usb.org/sites/default/files/hid1_11.pdf section 6.2.2.4 Main Items
//TODO add hat switches
static const uint8_t zmk_hid_gamepad_report_desc[] = {
    HID_USAGE_PAGE(HID_USAGE_GD),
    HID_USAGE(HID_USAGE_GD_GAMEPAD),
    HID_COLLECTION(HID_COLLECTION_APPLICATION),
    HID_REPORT_ID(HE_GAMEPAD_HID_REPORT_ID),
    HID_USAGE(HID_USAGE_GD_X),
    HID_USAGE(HID_USAGE_GD_Y),
#if CONFIG_HE_GAMEPAD_HID_SWAP_TRIGGER_R_AXIS
    HID_USAGE(HID_USAGE_GD_RX),
    HID_USAGE(HID_USAGE_GD_RY),
#else
    HID_USAGE(HID_USAGE_GD_Z),
    HID_USAGE(HID_USAGE_GD_RZ),
#endif
    HID_LOGICAL_MIN8(-0x7F),
    HID_LOGICAL_MAX8(0x7F),
    HID_REPORT_SIZE(0x08),
    HID_REPORT_COUNT(0x04),
    HID_INPUT(ZMK_HID_MAIN_VAL_DATA | ZMK_HID_MAIN_VAL_VAR | ZMK_HID_MAIN_VAL_ABS),
#if CONFIG_HE_GAMEPAD_HID_SWAP_TRIGGER_R_AXIS
    HID_USAGE(HID_USAGE_GD_Z),
    HID_USAGE(HID_USAGE_GD_RZ),
#else
    HID_USAGE(HID_USAGE_GD_RX),
    HID_USAGE(HID_USAGE_GD_RY),
#endif
    HID_LOGICAL_MIN8(0),
    HID_LOGICAL_MAX8(0xFF),
    HID_REPORT_SIZE(0x08),
    HID_REPORT_COUNT(0x02),
    HID_INPUT(ZMK_HID_MAIN_VAL_DATA | ZMK_HID_MAIN_VAL_VAR | ZMK_HID_MAIN_VAL_ABS),
    HID_USAGE_PAGE(HID_USAGE_BUTTON),
    HID_USAGE_MIN8(0x1),
    HID_USAGE_MAX8(CONFIG_HE_GAMEPAD_HID_NUM_BUTTONS),
    HID_LOGICAL_MIN8(0x00),
    HID_LOGICAL_MAX8(0x01),
    HID_REPORT_SIZE(0x01),
    HID_REPORT_COUNT(CONFIG_HE_GAMEPAD_HID_NUM_BUTTONS),
    HID_INPUT(ZMK_HID_MAIN_VAL_DATA | ZMK_HID_MAIN_VAL_VAR | ZMK_HID_MAIN_VAL_ABS),
#if BUTTONS_REMAINDER > 0
    HID_REPORT_SIZE(BUTTONS_REMAINDER),//TODO check if padding needs to be before or after the buttons
    HID_REPORT_COUNT(0x01),
    HID_INPUT(ZMK_HID_MAIN_VAL_CONST | ZMK_HID_MAIN_VAL_ARRAY | ZMK_HID_MAIN_VAL_ABS),
#endif
    HID_END_COLLECTION,
};
