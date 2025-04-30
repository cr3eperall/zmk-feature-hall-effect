/*
 * Copyright (c) 2024 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "zmk/keys.h"

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(feature_hall_effect, CONFIG_HE_LOG_LEVEL);

#include <zmk/hid.h>
#include <dt-bindings/zmk/modifiers.h>
#include <zmk/keymap.h>

#include <he/hid/hid.h>
#include <he/hid/hid_gamepad.h>

static struct zmk_hid_gamepad_report gamepad_report = {
    .report_id = HE_GAMEPAD_HID_REPORT_ID,
    .body = { .d_x = 0, .d_y = 0, .d_z = 0,  .d_rx = 0, .d_ry = 0, .d_rz = 0,
              .buttons = 0 }};

// Keep track of how often a button was pressed.
// Only release the button if the count is 0.
static int explicit_gamepad_btn_counts[CONFIG_HE_GAMEPAD_HID_NUM_BUTTONS] = {0};
static zmk_gamepad_button_flags_t explicit_gamepad_btns = {0};

#define SET_GAMEPAD_BUTTONS(btns)                                                                 \
    {                                                                                              \
        gamepad_report.body.buttons = btns;                                                   \
        LOG_DBG("GAMEPAD Buttons set to 0x%08X", gamepad_report.body.buttons);                        \
    }

int zmk_hid_gamepad_button_press(zmk_gamepad_button_t button) {
    if (button >= CONFIG_HE_GAMEPAD_HID_NUM_BUTTONS) {
        return -EINVAL;
    }

    explicit_gamepad_btn_counts[button]++;
    LOG_DBG("GAMEPAD Button %d count %d", button, explicit_gamepad_btn_counts[button]);
    WRITE_BIT(explicit_gamepad_btns, button, true);
    SET_GAMEPAD_BUTTONS(explicit_gamepad_btns);
    return 0;
}

int zmk_hid_gamepad_button_release(zmk_gamepad_button_t button) {
    if (button >= CONFIG_HE_GAMEPAD_HID_NUM_BUTTONS) {
        return -EINVAL;
    }

    if (explicit_gamepad_btn_counts[button] <= 0) {
        LOG_ERR("Tried to release GAMEPAD button %d too often", button);
        return -EINVAL;
    }
    explicit_gamepad_btn_counts[button]--;
    LOG_DBG("GAMEPAD Button %d count: %d", button, explicit_gamepad_btn_counts[button]);
    if (explicit_gamepad_btn_counts[button] == 0) {
        LOG_DBG("GAMEPAD Button %d released", button);
        WRITE_BIT(explicit_gamepad_btns, button, false);
    }
    SET_GAMEPAD_BUTTONS(explicit_gamepad_btns);
    return 0;
}

int zmk_hid_gamepad_buttons_press(zmk_gamepad_button_flags_t buttons) {
    for (zmk_gamepad_button_t i = 0; i < CONFIG_HE_GAMEPAD_HID_NUM_BUTTONS; i++) {
        if (buttons & BIT(i)) {
            zmk_hid_gamepad_button_press(i);
        }
    }
    return 0;
}

int zmk_hid_gamepad_buttons_release(zmk_gamepad_button_flags_t buttons) {
    for (zmk_gamepad_button_t i = 0; i < CONFIG_HE_GAMEPAD_HID_NUM_BUTTONS; i++) {
        if (buttons & BIT(i)) {
            zmk_hid_gamepad_button_release(i);
        }
    }
    return 0;
}

void zmk_hid_gamepad_joy_left_set(int16_t x, int16_t y) {
    gamepad_report.body.d_x = x;
    gamepad_report.body.d_y = y;
    LOG_DBG("joy left mov set to %d/%d", gamepad_report.body.d_x, gamepad_report.body.d_y);
}
void zmk_hid_gamepad_joy_left_update(int16_t x, int16_t y) {
    gamepad_report.body.d_x += x;
    gamepad_report.body.d_y += y;
    LOG_DBG("joy left mov updated to %d/%d", gamepad_report.body.d_x, gamepad_report.body.d_y);
}

void zmk_hid_gamepad_joy_right_set(int16_t x, int16_t y) {
    gamepad_report.body.d_z = x;
    gamepad_report.body.d_rz = y;
    LOG_DBG("joy right mov set to %d/%d", gamepad_report.body.d_z, gamepad_report.body.d_rz);
}
void zmk_hid_gamepad_joy_right_update(int16_t x, int16_t y) {
    gamepad_report.body.d_z += x;
    gamepad_report.body.d_rz += y;
    LOG_DBG("joy right mov updated to %d/%d", gamepad_report.body.d_z, gamepad_report.body.d_rz);
}

void zmk_hid_gamepad_trigger_left_set(int16_t d) {
    gamepad_report.body.d_rx = d;
    LOG_DBG("trigger left mov set to %d", gamepad_report.body.d_rx);
}
void zmk_hid_gamepad_trigger_left_update(int16_t d) {
    gamepad_report.body.d_rx+=d;
    LOG_DBG("trigger left mov updated to %d", gamepad_report.body.d_rx);
}

void zmk_hid_gamepad_trigger_right_set(int16_t d) {
    gamepad_report.body.d_ry = d;
    LOG_DBG("trigger right mov set to %d", gamepad_report.body.d_ry);
}
void zmk_hid_gamepad_trigger_right_update(int16_t d) {
    gamepad_report.body.d_ry+=d;
    LOG_DBG("trigger right mov updated to %d", gamepad_report.body.d_ry);
}

void zmk_hid_gamepad_clear(void) {
    LOG_DBG("joy report cleared");
    memset(&gamepad_report.body, 0, sizeof(gamepad_report.body));
}

struct zmk_hid_gamepad_report *zmk_hid_get_gamepad_report(void) {
    return &gamepad_report;
}

