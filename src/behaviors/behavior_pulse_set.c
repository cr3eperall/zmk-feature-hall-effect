/*
 * Copyright (c) 2020 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT he_behavior_pulse_set

#include <drivers/behavior.h>
#include <drivers/pulse_set_forwarder.h>
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

struct pulse_set_config {
    const struct device *pulse_set_forwarder;
};

struct pulse_set_data {
    const struct device *dev;
    bool pulse_enabled;
};

static int
on_pulse_set_binding_pressed(struct zmk_behavior_binding *binding,
                             struct zmk_behavior_binding_event event) {
                                
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    const struct pulse_set_config *config = dev->config;
    struct pulse_set_data *data = dev->data;
    if (binding->param1 == 0 || binding->param1 == 3) {
        data->pulse_enabled = false;
        return pulse_set_forwarder_forward(config->pulse_set_forwarder, false);
    } else if (binding->param1 == 1 || binding->param1 == 4) {
        data->pulse_enabled = true;
        return pulse_set_forwarder_forward(config->pulse_set_forwarder, true);
    } else if (binding->param1 == 2) {
        data->pulse_enabled = !data->pulse_enabled;
        return pulse_set_forwarder_forward(config->pulse_set_forwarder,
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
on_pulse_set_binding_released(struct zmk_behavior_binding *binding,
                              struct zmk_behavior_binding_event event) {
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    const struct pulse_set_config *config = dev->config;
    struct pulse_set_data *data = dev->data;
    if(binding->param1==3){
        data->pulse_enabled = true;
        return pulse_set_forwarder_forward(config->pulse_set_forwarder, true);
    }else if(binding->param1==4){
        data->pulse_enabled = false;
        return pulse_set_forwarder_forward(config->pulse_set_forwarder, false);
    }else{
        return ZMK_BEHAVIOR_OPAQUE;
    }
    if (data->pulse_enabled) {
        LOG_DBG("Pulse enabled");
    } else {
        LOG_DBG("Pulse disabled");
    }
    return ZMK_BEHAVIOR_OPAQUE;
}

static int pulse_set_init(const struct device *dev) {
    struct pulse_set_data *data = dev->data;
    data->dev = dev;
    data->pulse_enabled = true;
    return 0;
}

static const struct behavior_driver_api behavior_key_press_driver_api = {
    .binding_pressed = on_pulse_set_binding_pressed,
    .binding_released = on_pulse_set_binding_released,
    .locality=BEHAVIOR_LOCALITY_EVENT_SOURCE,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    // .parameter_metadata = &metadata,
#endif // IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
};

#define HE_PULSE_SET_INST(n)                                                   \
    static const struct pulse_set_config pulse_set_config_##n = {              \
        .pulse_set_forwarder = DEVICE_DT_GET(DT_INST_PHANDLE(n, pulse_set_forwarder)), \
    };                                                                         \
    static struct pulse_set_data pulse_set_data_##n = {0};                     \
    BEHAVIOR_DT_INST_DEFINE(n, &pulse_set_init, NULL, &pulse_set_data_##n,     \
                            &pulse_set_config_##n, POST_KERNEL,                \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,               \
                            &behavior_key_press_driver_api);

DT_INST_FOREACH_STATUS_OKAY(HE_PULSE_SET_INST)
