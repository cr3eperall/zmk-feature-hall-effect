/*
 * Copyright (c) 2020 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT he_behavior_set_pulse

#include <drivers/behavior.h>
#include <drivers/kscan_forwarder.h>
#include <zephyr/device.h>
#include <zephyr/logging/log.h>

#include <zmk/behavior.h>
#include <zmk/event_manager.h>
#include <zmk/events/keycode_state_changed.h>

LOG_MODULE_DECLARE(feature_hall_effect, CONFIG_HE_LOG_LEVEL);

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

struct set_pulse_config {
    const struct device *kscan_forwarder;
};

struct set_pulse_data {
    const struct device *dev;
    bool pulse_enabled;
};

static int
on_set_pulse_binding_pressed(struct zmk_behavior_binding *binding,
                             struct zmk_behavior_binding_event event) {
                                
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    const struct set_pulse_config *config = dev->config;
    struct set_pulse_data *data = dev->data;
    if (binding->param1 == 0) {
        data->pulse_enabled = false;
        return kscan_forwarder_pulse_set(config->kscan_forwarder, false);
    } else if (binding->param1 == 1) {
        data->pulse_enabled = true;
        return kscan_forwarder_pulse_set(config->kscan_forwarder, true);
    } else if (binding->param1 == 2) {
        data->pulse_enabled = !data->pulse_enabled;
        return kscan_forwarder_pulse_set(config->kscan_forwarder,
                                         data->pulse_enabled);
    }
    if (data->pulse_enabled) {
        LOG_DBG("Pulse enabled");
    } else {
        LOG_DBG("Pulse disabled");
    }
    return ZMK_BEHAVIOR_OPAQUE;
}

static int
on_set_pulse_binding_released(struct zmk_behavior_binding *binding,
                              struct zmk_behavior_binding_event event) {

    return ZMK_BEHAVIOR_OPAQUE;
}

static int set_pulse_init(const struct device *dev) {
    struct set_pulse_data *data = dev->data;
    data->dev = dev;
    data->pulse_enabled = true;
    return 0;
}

static const struct behavior_driver_api behavior_key_press_driver_api = {
    .binding_pressed = on_set_pulse_binding_pressed,
    .binding_released = on_set_pulse_binding_released,
    .locality=BEHAVIOR_LOCALITY_EVENT_SOURCE,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    .parameter_metadata = &metadata,
#endif // IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
};

#define HE_SET_PULSE_INST(n)                                                   \
    static const struct set_pulse_config set_pulse_config_##n = {              \
        .kscan_forwarder = DEVICE_DT_GET(DT_INST_PHANDLE(n, kscan_forwarder)), \
    };                                                                         \
    static struct set_pulse_data set_pulse_data_##n = {0};                     \
    BEHAVIOR_DT_INST_DEFINE(n, &set_pulse_init, NULL, &set_pulse_data_##n,     \
                            &set_pulse_config_##n, POST_KERNEL,                \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,               \
                            &behavior_key_press_driver_api);

DT_INST_FOREACH_STATUS_OKAY(HE_SET_PULSE_INST)
