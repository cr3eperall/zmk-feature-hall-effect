#include <drivers/behavior.h>
#include <drivers/input_processor.h>
#include <drivers/kscan_forwarder.h>
#include <stdlib.h>
#include <zephyr/device.h>
#include <zephyr/logging/log.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/keymap.h>
#include <zmk/virtual_key_position.h>

#include <he/input-event-codes.h>

LOG_MODULE_DECLARE(feature_hall_effect, CONFIG_HE_LOG_LEVEL);

#define DT_DRV_COMPAT he_input_processor_adjustable_actuation

enum direction_e {
    DIR_UP,
    DIR_DOWN,
    DIR_BOTH,
};

#define CHECK_TRIGGER(trigger_pos, event_value, last_value, direction_down)    \
    direction_down ? (event_value < trigger_pos && last_value >= trigger_pos)  \
                   : (event_value > trigger_pos && last_value <= trigger_pos)

struct adj_act_behavior_config {
    int position;
    enum direction_e direction;
    int bindings_len;
    const struct zmk_behavior_binding *bindings;
};

struct adj_act_config {
    int index;
    int sensitivity;
    bool kscan_passthrough;
    int kscan_passthrough_position;
    int keymap_size;
    const struct device *kscan_forwarder;
    int positions_len;
    struct adj_act_behavior_config positions[]; // TODO could use an hashmap
};

struct key_state {
    int16_t last_value;
    bool last_state;
};

struct adj_act_data {
    const struct device *dev;
    struct key_state *key_states;
};

int adj_act_trigger_key(const struct device *dev, struct input_event *event,
                        struct zmk_input_processor_state *state, int pos_idx,
                        bool pressed) {
    const struct adj_act_config *conf = dev->config;
    // const struct adj_act_data *data = dev->data;
    if (conf->kscan_passthrough) {
        return kscan_forwarder_forward(conf->kscan_forwarder,
                                       INV_INPUT_HE_ROW(event->code),
                                       INV_INPUT_HE_COL(event->code), pressed);
    } else {
        for (int j = 0; j < conf->positions[pos_idx].bindings_len; j++) {
            struct zmk_behavior_binding_event behavior_event = {
                .position = ZMK_VIRTUAL_KEY_POSITION_BEHAVIOR_INPUT_PROCESSOR(
                    state->input_device_index,
                    conf->index), // I could use the real position of the key
                                  // but it would require a pointer to the
                                  // transform matrix
                .timestamp = k_uptime_get(),
            // TODO: find where the event was generated and set the source
#if IS_ENABLED(CONFIG_ZMK_SPLIT)
                .source = ZMK_POSITION_STATE_CHANGE_SOURCE_LOCAL,
#endif
            };
            // reverse the order of the bindings when returning up
            int behavior_idx =
                pressed ? j : conf->positions[pos_idx].bindings_len - j - 1;
            int ret = zmk_behavior_invoke_binding(
                &conf->positions[pos_idx].bindings[behavior_idx],
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

int adj_act_set_key_state(const struct device *dev, struct input_event *event,
                          struct zmk_input_processor_state *state, int pos_idx,
                          uint32_t key_idx, bool pressed) {
    struct adj_act_data *data = dev->data;
    struct key_state *key_state = &data->key_states[key_idx];
    if (key_state->last_state == pressed) {
        return 0;
    }
    int ret = adj_act_trigger_key(dev, event, state, pos_idx, pressed);
    key_state->last_state = pressed;
    return ret;
}

static int adj_act_handle_event(const struct device *dev,
                                struct input_event *event, uint32_t key_idx,
                                uint32_t param2,
                                struct zmk_input_processor_state *state) {
    const struct adj_act_config *conf = dev->config;
    struct adj_act_data *data = dev->data;
    if (event->type != INPUT_EV_HE)
        return ZMK_INPUT_PROC_CONTINUE;
    enum direction_e excluded_dir;
    int trigger_offset;
    bool pressed;
    struct key_state *key_state = &data->key_states[key_idx];
    if (event->value < key_state->last_value) {
        excluded_dir = DIR_UP;
        trigger_offset = -conf->sensitivity / 2;
        pressed = true;
    } else {
        excluded_dir = DIR_DOWN;
        trigger_offset = conf->sensitivity / 2;
        pressed = false;
    }
    // LOG_INF("adj_act_handle_event, key_idx: %d, event: %d, last_value: %d,
    // last_state: %d",
    //         key_idx, event->value, key_state->last_value,
    //         key_state->last_state);
    for (int i = -1; i < conf->positions_len; i++) {
        if (i == -1 && conf->kscan_passthrough) {
            int trigger_pos = conf->kscan_passthrough_position + trigger_offset;
            if (CHECK_TRIGGER(trigger_pos, event->value, key_state->last_value,
                              pressed)) {
                int ret = adj_act_set_key_state(dev, event, state, i, key_idx,
                                                pressed);
                if (ret < 0) {
                    LOG_ERR("Error invoking kscan forwarder: %d", ret);
                    key_state->last_value = event->value;
                    return ret;
                }
            }
            break;
        } else {
            continue;
        }

        if (conf->positions[i].direction == excluded_dir)
            continue;
        int trigger_pos = conf->positions[i].position + trigger_offset;
        if (CHECK_TRIGGER(trigger_pos, event->value, key_state->last_value,
                          pressed)) {
            int ret =
                adj_act_set_key_state(dev, event, state, i, key_idx, pressed);
            if (ret < 0) {
                LOG_ERR("Error invoking behavior binding: %d", ret);
                key_state->last_value = event->value;
                return ret;
            }
        }
    }
    key_state->last_value = event->value;
    return ZMK_INPUT_PROC_STOP;
}

static int adj_act_init(const struct device *dev) {
    struct adj_act_data *data = dev->data;
    const struct adj_act_config *conf = dev->config;
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
    .handle_event = adj_act_handle_event,
};

#define ADJ_ACT_POS_ENTRY_INIT(node, n)                                        \
    static const struct zmk_behavior_binding                                   \
        adj_act_behaviors_bindings_##n##_##node[] = {                          \
            LISTIFY(DT_PROP_LEN(node, bindings), ZMK_KEYMAP_EXTRACT_BINDING,   \
                    (, ), node)};                                              \
    static const struct adj_act_behavior_config                                \
        adj_act_behaviors_conf_##n##_##node = {                                \
            .position = DT_PROP(node, position),                               \
            .direction = DT_ENUM_IDX(node, direction),                         \
            .bindings = adj_act_behaviors_bindings_##n##_##node,               \
            .bindings_len =                                                    \
                ARRAY_SIZE(adj_act_behaviors_bindings_##n##_##node),           \
    };

#define ADJ_ACT_POS_ENTRY(node, n) adj_act_behaviors_conf_##n##_##node

#define ADJ_ACT_ONE(...) +1

#define ADJ_ACT_INIT(n)                                                        \
    BUILD_ASSERT(!DT_INST_PROP(n, kscan_passthrough) ||                        \
                     DT_INST_NODE_HAS_PROP(n, kscan_forwarder),                \
                 "kscan_passthrough requires kscan_forwarder");                \
    DT_INST_FOREACH_CHILD_SEP_VARGS(n, ADJ_ACT_POS_ENTRY_INIT, (, ), n)        \
    static const struct adj_act_config adj_act_config_##n = {                  \
        .index = n,                                                            \
        .sensitivity = DT_INST_PROP(n, sensitivity),                           \
        .kscan_passthrough = DT_INST_PROP(n, kscan_passthrough),               \
        .kscan_passthrough_position =                                          \
            DT_INST_PROP(n, kscan_passthrough_position),                       \
        .kscan_forwarder = DEVICE_DT_GET(DT_INST_PHANDLE(n, kscan_forwarder)), \
        .keymap_size = DT_INST_PROP(n, keymap_size),                           \
        .positions_len = 0 DT_INST_FOREACH_CHILD(n, ADJ_ACT_ONE),              \
        .positions = {DT_INST_FOREACH_CHILD_SEP_VARGS(n, ADJ_ACT_POS_ENTRY,    \
                                                      (, ), n)},               \
    };                                                                         \
    static struct adj_act_data adj_act_data_##n;                               \
    DEVICE_DT_INST_DEFINE(n, &adj_act_init, NULL, &adj_act_data_##n,           \
                          &adj_act_config_##n, POST_KERNEL,                    \
                          CONFIG_INPUT_INIT_PRIORITY, &processor_api);

DT_INST_FOREACH_STATUS_OKAY(ADJ_ACT_INIT)