/*
 * Copyright (c) 2024 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include <zmk/endpoints.h>
#include <zmk/hid.h>
#include <zmk/usb_hid.h>
#include <zmk/hog.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(endpoints_gamepad, CONFIG_LOG_MAX_LEVEL);  // TODO change log level

#include <he/hid/endpoints.h>
#include <he/hid/hid.h>
#include <he/hid/usb_hid.h>
#include <he/hid/hog.h>

int zmk_endpoints_send_gamepad_report() {
    struct zmk_endpoint_instance current_instance = zmk_endpoints_selected();

    switch (current_instance.transport) {
#if IS_ENABLED(CONFIG_ZMK_USB)
    case ZMK_TRANSPORT_USB: {
        int err = zmk_usb_hid_send_gamepad_report();
        if (err) {
            LOG_ERR("FAILED TO SEND OVER USB: %d", err);
        }
        return err;
    }
#else
    case ZMK_TRANSPORT_USB: break;
#endif /* IS_ENABLED(CONFIG_ZMK_USB) */

#if IS_ENABLED(CONFIG_ZMK_BLE)
    case ZMK_TRANSPORT_BLE: {
        struct zmk_hid_gamepad_report *gamepad_report = zmk_hid_get_gamepad_report();
        int err = zmk_hog_send_gamepad_report(&gamepad_report->body);
        if (err) {
            LOG_ERR("FAILED TO SEND OVER HOG: %d", err);
        }
        return err;
    }
#else
    case ZMK_TRANSPORT_BLE: break;
#endif /* IS_ENABLED(CONFIG_ZMK_BLE) */
    }

    LOG_ERR("Unsupported endpoint transport %d", current_instance.transport);
    return -ENOTSUP;
}