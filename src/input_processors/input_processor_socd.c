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
#include <he/socd.h>

LOG_MODULE_DECLARE(feature_hall_effect, CONFIG_HE_LOG_LEVEL);

#define DT_DRV_COMPAT he_input_processor_socd

#define CHECK_TRIGGER(trigger_pos, event_value, last_value, direction_down)    \
    direction_down ? (event_value < trigger_pos && last_value >= trigger_pos)  \
                   : (event_value > trigger_pos && last_value <= trigger_pos)

struct socd_config {
    int index;
    int sensitivity;
    int equal_sensitivity;
    bool kscan_passthrough;
    const struct device *kscan_forwarder;
    int actuation_point;
    int first_key_behavior_n;
    int bindings_len;
    const struct zmk_behavior_binding *bindings;
    enum socd_type type;
};

struct key_state {
    int16_t last_value;
    bool enabled;
    bool last_state;
    int16_t row;
    int16_t col;
    int64_t enable_time;
};

struct socd_data {
    const struct device *dev;
    struct key_state *key_states;
    // bool enabled;
};

int socd_trigger_key(const struct device *dev,
                     struct zmk_input_processor_state *state, bool pressed,
                     uint32_t key_idx) {
    const struct socd_config *conf = dev->config;
    const struct socd_data *data = dev->data;
    if (conf->kscan_passthrough) {
        return kscan_forwarder_forward(conf->kscan_forwarder,
                                       data->key_states[key_idx].row,
                                       data->key_states[key_idx].col, pressed);
    } else {
        int idx_begin = key_idx == 0 ? 0 : conf->first_key_behavior_n;
        int idx_end =
            key_idx == 0 ? conf->first_key_behavior_n : conf->bindings_len;
        for (int j = idx_begin; j < idx_end; j++) {
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
            int behavior_idx = pressed ? j : idx_end - j - 1;
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

int socd_set_key_state(const struct device *dev, struct input_event *event,
                       struct zmk_input_processor_state *state,
                       uint32_t key_idx, bool new_state) {
    struct socd_data *data = dev->data;
    struct key_state *key_state = &data->key_states[key_idx];
    if (key_state->row == INV_INPUT_HE_ROW(event->code) &&
        key_state->col == INV_INPUT_HE_COL(event->code)) {
        key_state->last_value = event->value;
    }
    if (key_state->last_state == new_state || key_state->col == -1 ||
        key_state->row == -1) {
        return 0;
    }
    int ret = socd_trigger_key(dev, state, new_state, key_idx);
    key_state->last_state = new_state;
    return ret;
}

static int socd_handle_event(const struct device *dev,
                             struct input_event *event, uint32_t key_idx,
                             uint32_t param2,
                             struct zmk_input_processor_state *state) {
    const struct socd_config *conf = dev->config;
    struct socd_data *data = dev->data;
    if (event->type != INPUT_EV_HE)
        return ZMK_INPUT_PROC_CONTINUE;
    int trigger_offset = conf->sensitivity / 2;
    int other_key_idx = 1 - key_idx;
    struct key_state *key_state = &data->key_states[key_idx];
    struct key_state *other_key_state = &data->key_states[other_key_idx];
    int row = INV_INPUT_HE_ROW(event->code);
    int col = INV_INPUT_HE_COL(event->code);
    if (key_state->col == -1 || key_state->row == -1) {
        key_state->row = row;
        key_state->col = col;
    } else if (key_state->col != col || key_state->row != row) {
        LOG_WRN("This slot(%d) is already bound to key (%d,%d)", key_idx, row,
                col);
    }
    if (key_state->enabled) {
        if (event->value >
            conf->actuation_point + trigger_offset) { // disable and release key
            int ret = socd_set_key_state(dev, event, state, key_idx, false);
            key_state->enabled = false;
            if (other_key_state->enabled) {
                socd_set_key_state(dev, event, state, other_key_idx, true);
                other_key_state->enable_time = k_uptime_get();
            }
            if (ret < 0) {
                return ret;
            }
        }
    } else if (event->value < conf->actuation_point - trigger_offset) { // enable
        key_state->enabled = true;
        key_state->enable_time = k_uptime_get();
    }
    if (!key_state->enabled) {
        return ZMK_INPUT_PROC_STOP;
    }
    switch (conf->type) {
    case SOCD_PRIORITY_0:
        if (key_idx == 0) {
            socd_set_key_state(dev, event, state, key_idx, true);
            socd_set_key_state(dev, event, state, other_key_idx, false);
        } else if (!other_key_state->enabled) {
            socd_set_key_state(dev, event, state, key_idx, true);
        }
        break;
    case SOCD_PRIORITY_1:
        if (key_idx == 1) {
            socd_set_key_state(dev, event, state, key_idx, true);
            socd_set_key_state(dev, event, state, other_key_idx, false);
        } else if (!other_key_state->enabled) {
            socd_set_key_state(dev, event, state, key_idx, true);
        }
        break;
    case SOCD_PRIORITY_FIRST:
        if (!other_key_state->enabled ||
            other_key_state->enable_time > key_state->enable_time) {
            socd_set_key_state(dev, event, state, key_idx, true);
            socd_set_key_state(dev, event, state, other_key_idx, false);
        }
        break;
    case SOCD_PRIORITY_LAST:
        if (!other_key_state->enabled ||
            other_key_state->enable_time < key_state->enable_time) {
            socd_set_key_state(dev, event, state, key_idx, true);
            socd_set_key_state(dev, event, state, other_key_idx, false);
        }
        break;
    case SOCD_PRIORITY_DEEPER:
        trigger_offset = MAX(trigger_offset, conf->equal_sensitivity);
        if (!other_key_state->enabled ||
            other_key_state->last_value - event->value >
                trigger_offset) { // other deactivated or this pressed further
            socd_set_key_state(dev, event, state, key_idx, true);
            socd_set_key_state(dev, event, state, other_key_idx, false);
            // LOG_DBG("other deactivated or this pressed further: %d %d
            // %d",other_key_state->enabled, other_key_state->last_value,
            // event->value);
        } else if (event->value - other_key_state->last_value >
                   trigger_offset) { // both activated and other pressed further
            socd_set_key_state(dev, event, state, key_idx, false);
            socd_set_key_state(dev, event, state, other_key_idx, true);
        } else { // both activated but pressed the same
            socd_set_key_state(dev, event, state, key_idx,
                               conf->equal_sensitivity != 0);
            socd_set_key_state(dev, event, state, other_key_idx,
                               conf->equal_sensitivity != 0);
        }
        break;
    case SOCD_PRIORITY_DIRECTION:
        if (!other_key_state->enabled ||
            event->value <
                key_state->last_value - trigger_offset) { // this going down
            socd_set_key_state(dev, event, state, key_idx, true);
            socd_set_key_state(dev, event, state, other_key_idx, false);
        } else if (event->value >
                   key_state->last_value +
                       trigger_offset) { // other active and this going up
            socd_set_key_state(dev, event, state, key_idx, false);
            socd_set_key_state(dev, event, state, other_key_idx, true);
        } // else: not enough changes, use the last state
        break;
    }

    return ZMK_INPUT_PROC_STOP;
}

static int socd_init(const struct device *dev) {
    struct socd_data *data = dev->data;
    // const struct socd_config *conf = dev->config;
    data->dev = dev;
    data->key_states = malloc(sizeof(struct key_state) * 2);
    for (int i = 0; i < 2; i++) {
        data->key_states[i].last_value =
            10000; // emulates the switch being at max height
        data->key_states[i].last_state = false;
        data->key_states[i].row = -1;
        data->key_states[i].col = -1;
        data->key_states[i].enabled = false;
        data->key_states[i].enable_time = 0;
    }
    // data->enabled = false;
    return 0;
}

static struct zmk_input_processor_driver_api processor_api = {
    .handle_event = socd_handle_event,
};

#define SOCD_INIT(n)                                                           \
    BUILD_ASSERT(!DT_INST_PROP(n, kscan_passthrough) ||                        \
                     DT_INST_NODE_HAS_PROP(n, kscan_forwarder),                \
                 "kscan_passthrough requires kscan_forwarder");                \
    static const struct zmk_behavior_binding socd_behaviors_bindings_##n[] =   \
        COND_CODE_1(                                                           \
            DT_INST_NODE_HAS_PROP(n, bindings),                                \
            ({LISTIFY(DT_INST_PROP_LEN(n, bindings),                           \
                      ZMK_KEYMAP_EXTRACT_BINDING, (, ), DT_DRV_INST(n))}),     \
            ({}));                                                             \
    static const struct socd_config socd_config_##n = {                        \
        .index = n,                                                            \
        .sensitivity = DT_INST_PROP(n, sensitivity),                           \
        .equal_sensitivity = DT_INST_PROP(n, equal_sensitivity),               \
        .kscan_passthrough = DT_INST_PROP(n, kscan_passthrough),               \
        .kscan_forwarder = DEVICE_DT_GET(DT_INST_PHANDLE(n, kscan_forwarder)), \
        .actuation_point = DT_INST_PROP(n, actuation_point),                   \
        .bindings_len = DT_INST_PROP_LEN_OR(n, bindings, 0),                   \
        .bindings = socd_behaviors_bindings_##n,                               \
        .first_key_behavior_n = DT_INST_PROP_OR(n, first_key_behavior_n, 0),   \
        .type = DT_INST_ENUM_IDX(n, type),                                     \
    };                                                                         \
    static struct socd_data socd_data_##n;                                     \
    DEVICE_DT_INST_DEFINE(n, &socd_init, NULL, &socd_data_##n,                 \
                          &socd_config_##n, POST_KERNEL,                       \
                          CONFIG_INPUT_INIT_PRIORITY, &processor_api);

DT_INST_FOREACH_STATUS_OKAY(SOCD_INIT)