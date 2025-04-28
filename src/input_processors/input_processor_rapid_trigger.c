#include <drivers/behavior.h>
#include <drivers/input_processor.h>
#include <drivers/kscan_forwarder.h>
#include <stdlib.h>
#include <zephyr/device.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util_macro.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/keymap.h>
#include <zmk/virtual_key_position.h>

#include <he/input-event-codes.h>

LOG_MODULE_DECLARE(feature_hall_effect, CONFIG_HE_LOG_LEVEL);

#define DT_DRV_COMPAT he_input_processor_rapid_trigger

#define CHECK_TRIGGER(trigger_pos, event_value, last_value, direction_down)    \
    direction_down ? (event_value < trigger_pos && last_value >= trigger_pos)  \
                   : (event_value > trigger_pos && last_value <= trigger_pos)

struct rt_config {
    int index;
    int sensitivity;
    bool kscan_passthrough;
    const struct device *kscan_forwarder;
    int keymap_size;
    int actuation_point;
    int bindings_len;
    const struct zmk_behavior_binding *bindings;
};

struct key_state {
    int16_t last_value;
    bool last_state;
    bool enabled;
};

struct rt_data {
    const struct device *dev;
    struct key_state *key_states;
};

int rt_trigger_key(const struct device *dev, struct input_event *event,
                   struct zmk_input_processor_state *state, bool pressed) {
    const struct rt_config *conf = dev->config;
    // const struct rt_data *data = dev->data;
    if (conf->kscan_passthrough) {
        return kscan_forwarder_forward(conf->kscan_forwarder,
                                       INV_INPUT_HE_ROW(event->code),
                                       INV_INPUT_HE_COL(event->code), pressed);
    } else {
        for (int j = 0; j < conf->bindings_len; j++) {
            struct zmk_behavior_binding_event behavior_event = {
                .position = ZMK_VIRTUAL_KEY_POSITION_BEHAVIOR_INPUT_PROCESSOR(
                    state->input_device_index,
                    conf->index), // I could use the real position of the key
                                  // but it would require a pointer to the
                                  // transform matrix
                .timestamp = k_uptime_get(),
#if IS_ENABLED(CONFIG_ZMK_SPLIT)
                .source = ZMK_POSITION_STATE_CHANGE_SOURCE_LOCAL,
#endif
            };
            // reverse the order of the bindings when returning up
            int behavior_idx = pressed ? j : conf->bindings_len - j - 1;
            int ret = zmk_behavior_invoke_binding(&conf->bindings[behavior_idx],
                                                  behavior_event, pressed);
            if (ret < 0) {
                LOG_ERR("Error invoking behavior binding[%d]: %d", behavior_idx,
                        ret);
                return ret;
            }
        }
    }
    return 0;
}

int rt_set_key_state(const struct device *dev, struct input_event *event,
                     struct zmk_input_processor_state *state, uint32_t key_idx,
                     bool pressed) {
    struct rt_data *data = dev->data;
    struct key_state *key_state = &data->key_states[key_idx];
    key_state->last_value = event->value;
    if (key_state->last_state == pressed) {
        return 0;
    }
    int ret = rt_trigger_key(dev, event, state, pressed);
    key_state->last_state = pressed;
    return ret;
}

static int rt_handle_event(const struct device *dev, struct input_event *event,
                           uint32_t key_idx, uint32_t param2,
                           struct zmk_input_processor_state *state) {
    const struct rt_config *conf = dev->config;
    struct rt_data *data = dev->data;
    if (event->type != INPUT_EV_HE)
        return ZMK_INPUT_PROC_CONTINUE;
    int trigger_offset = conf->sensitivity / 2;
    struct key_state *key_state = &data->key_states[key_idx];
    if (key_state->enabled) {
        if (event->value >
            conf->actuation_point + trigger_offset) { // disable and release key
            int ret = rt_set_key_state(dev, event, state, key_idx, false);
            key_state->enabled = false;
            if (ret < 0) {
                return ret;
            }
        }
    } else if (event->value < conf->actuation_point - trigger_offset) { // enable
        int ret = rt_set_key_state(dev, event, state, key_idx, true);
        key_state->enabled = true;
        if (ret < 0) {
            return ret;
        }
    }
    if (!key_state->enabled) {
        return ZMK_INPUT_PROC_STOP;
    }
    int ret = 0;
    // TODO currently this works by setting the trigger point in steps of
    // `sensitivity/2`
    //  it could be more precise by saving the minimum or maximum value reached
    //  and triggering based on that but it works well enough for now
    if (event->value < key_state->last_value - trigger_offset) { // going down
        ret = rt_set_key_state(dev, event, state, key_idx, true);
    } else if (event->value >
               key_state->last_value + trigger_offset) { // going up
        ret = rt_set_key_state(dev, event, state, key_idx, false);
    }
    if (ret < 0) {
        return ret;
    }
    return ZMK_INPUT_PROC_STOP;
}

static int rt_init(const struct device *dev) {
    struct rt_data *data = dev->data;
    const struct rt_config *conf = dev->config;
    data->dev = dev;
    data->key_states = malloc(sizeof(struct key_state) * conf->keymap_size);
    for (int i = 0; i < conf->keymap_size; i++) {
        data->key_states[i].last_value =
            10000; // emulates the switch being at max height
        data->key_states[i].last_state = false;
    }
    return 0;
}

static struct zmk_input_processor_driver_api processor_api = {
    .handle_event = rt_handle_event,
};

#define RT_INIT(n)                                                             \
    BUILD_ASSERT(!DT_INST_PROP(n, kscan_passthrough) ||                        \
                     DT_INST_NODE_HAS_PROP(n, kscan_forwarder),                \
                 "kscan_passthrough requires kscan_forwarder");                \
    static const struct zmk_behavior_binding rt_behaviors_bindings_##n[] =     \
        COND_CODE_1(                                                           \
            DT_INST_NODE_HAS_PROP(n, bindings),                                \
            ({LISTIFY(DT_INST_PROP_LEN(n, bindings),                           \
                      ZMK_KEYMAP_EXTRACT_BINDING, (, ), DT_DRV_INST(n))}),     \
            ({}));                                                             \
    static const struct rt_config rt_config_##n = {                            \
        .index = n,                                                            \
        .sensitivity = DT_INST_PROP(n, sensitivity),                           \
        .kscan_passthrough = DT_INST_PROP(n, kscan_passthrough),               \
        .kscan_forwarder = DEVICE_DT_GET(DT_INST_PHANDLE(n, kscan_forwarder)), \
        .keymap_size = DT_INST_PROP(n, keymap_size),                           \
        .actuation_point = DT_INST_PROP(n, actuation_point),                   \
        .bindings_len = DT_INST_PROP_LEN_OR(n, bindings, 0),                   \
        .bindings = rt_behaviors_bindings_##n,                                 \
    };                                                                         \
    static struct rt_data rt_data_##n;                                         \
    DEVICE_DT_INST_DEFINE(n, &rt_init, NULL, &rt_data_##n, &rt_config_##n,     \
                          POST_KERNEL, CONFIG_INPUT_INIT_PRIORITY,             \
                          &processor_api);

DT_INST_FOREACH_STATUS_OKAY(RT_INIT)