/*
 * Copyright (c) 2020 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <zmk/keys.h>
#include <zmk/hid.h>
#include <zmk/endpoints_types.h>

#include <he/hid/gamepad.h>

struct zmk_hid_gamepad_report_body{
    int8_t d_x;
    int8_t d_y;
    int8_t d_z;
    int8_t d_rz;
    int8_t d_rx;
    int8_t d_ry;
    zmk_gamepad_button_flags_t buttons;
} __packed;

struct zmk_hid_gamepad_report {
    uint8_t report_id;
    struct zmk_hid_gamepad_report_body body;
} __packed;
int zmk_hid_gamepad_button_press(zmk_gamepad_button_t button);
int zmk_hid_gamepad_button_release(zmk_gamepad_button_t button);

int zmk_hid_gamepad_buttons_press(zmk_gamepad_button_flags_t buttons);
int zmk_hid_gamepad_buttons_release(zmk_gamepad_button_flags_t buttons);

void zmk_hid_gamepad_joy_left_set(int16_t x, int16_t y);
void zmk_hid_gamepad_joy_left_update(int16_t x, int16_t y);

void zmk_hid_gamepad_joy_right_set(int16_t x, int16_t y);
void zmk_hid_gamepad_joy_right_update(int16_t x, int16_t y);

void zmk_hid_gamepad_trigger_left_set(int16_t d);
void zmk_hid_gamepad_trigger_left_update(int16_t d);

void zmk_hid_gamepad_trigger_right_set(int16_t d);
void zmk_hid_gamepad_trigger_right_update(int16_t d);
void zmk_hid_gamepad_clear(void);
struct zmk_hid_gamepad_report *zmk_hid_get_gamepad_report();
