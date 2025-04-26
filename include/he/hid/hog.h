/*
 * Copyright (c) 2020 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <zmk/keys.h>
#include <zmk/hid.h>

#include <he/hid/hid.h>


int zmk_hog_send_gamepad_report(struct zmk_hid_gamepad_report_body *body);
