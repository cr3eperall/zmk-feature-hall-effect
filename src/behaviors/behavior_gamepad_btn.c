/*
 * Copyright (c) 2020 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_behavior_gamepad_btn

#include <drivers/behavior.h>
#include <drivers/kscan_forwarder.h>
#include <zephyr/device.h>
#include <zephyr/logging/log.h>

#include <zmk/behavior.h>
#include <zmk/event_manager.h>
#include <zmk/events/keycode_state_changed.h>
#include <he/hid/endpoints.h>
#include <he/hid/hid_gamepad.h>
#include <dt-bindings/he/gamepad_forwarder.h>

LOG_MODULE_REGISTER(behavior_gamepad_btn,
                    CONFIG_LOG_MAX_LEVEL); // TODO change log level

#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)

//  static const struct behavior_parameter_value_metadata param_values[] = {
//      {
//          .display_name = "Key",
//          .type = BEHAVIOR_PARAMETER_VALUE_TYPE_HID_USAGE,
//      },
//  };

//  static const struct behavior_parameter_metadata_set param_metadata_set[] =
//  {{
//      .param1_values = param_values,
//      .param1_values_len = ARRAY_SIZE(param_values),
//  }};

//  static const struct behavior_parameter_metadata metadata = {
//      .sets_len = ARRAY_SIZE(param_metadata_set),
//      .sets = param_metadata_set,
//  };

#endif

static int on_gp_btn_binding_pressed(struct zmk_behavior_binding *binding,
                                     struct zmk_behavior_binding_event event) {
    int btn=binding->param1;
    if(btn>=GP_BTN_COUNT){
        LOG_ERR("Invalid button %d", btn);
        return -EINVAL;
    }
    zmk_hid_gamepad_button_press(btn);
    zmk_endpoints_send_gamepad_report();

    return ZMK_BEHAVIOR_OPAQUE;
}

static int on_gp_btn_binding_released(struct zmk_behavior_binding *binding,
                                      struct zmk_behavior_binding_event event) {
    int btn=binding->param1;
    if(btn>=GP_BTN_COUNT){
        LOG_ERR("Invalid button %d", btn);
        return -EINVAL;
    }
    zmk_hid_gamepad_button_release(btn);
    zmk_endpoints_send_gamepad_report();

    return ZMK_BEHAVIOR_OPAQUE;
}

static int gp_btn_binding_init(const struct device *dev) {
    return 0;
}

static const struct behavior_driver_api behavior_key_press_driver_api = {
    .binding_pressed = on_gp_btn_binding_pressed,
    .binding_released = on_gp_btn_binding_released,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    .parameter_metadata = &metadata,
#endif // IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
};

#define GP_BTN_INST(n)                                                         \
    BEHAVIOR_DT_INST_DEFINE(n, &gp_btn_binding_init, NULL, NULL, NULL, POST_KERNEL,            \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,               \
                            &behavior_key_press_driver_api);

DT_INST_FOREACH_STATUS_OKAY(GP_BTN_INST)
