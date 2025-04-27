
#include <drivers/input_processor.h>
#include <stdlib.h>
#include <zephyr/device.h>
#include <zephyr/logging/log.h>

#include <zmk/matrix_transform.h>
#include <zmk/physical_layouts.h>

#include <he/input-event-codes.h>

LOG_MODULE_DECLARE(feature_hall_effect, CONFIG_HE_LOG_LEVEL);

#define DT_DRV_COMPAT he_input_processor_keymap

struct he_layer_config {
    size_t processors_len;
    const struct zmk_input_processor_entry *processors;
};

struct he_keymap_config {
    size_t layers_len;
    zmk_matrix_transform_t matrix_transform;
    struct he_layer_config bindings[];
};

// static int keymap_init(const struct device *dev) { return 0; }

// static int physical_layout_rc_to_position(uint8_t row, uint8_t col) {
//     struct zmk_physical_layout const *const *phys_layouts;
//     zmk_physical_layouts_get_list(&phys_layouts);
//     int selected_idx = zmk_physical_layouts_get_selected();
//     if (selected_idx < 0) {
//         return selected_idx;
//     }
//     return zmk_matrix_transform_row_column_to_position(
//         phys_layouts[selected_idx]->matrix_transform, row, col);
// }

static int keymap_handle_event(const struct device *dev,
                               struct input_event *event, uint32_t layer_id,
                               uint32_t param2,
                               struct zmk_input_processor_state *state) {
    if (event->type != INPUT_EV_HE)
        return ZMK_INPUT_PROC_CONTINUE;
    const struct he_keymap_config *conf = dev->config;
    if (layer_id >= conf->layers_len) {
        LOG_ERR("Invalid layer id %d", layer_id);
        return ZMK_INPUT_PROC_CONTINUE;
    }
    int row = INV_INPUT_HE_ROW(event->code);
    int col = INV_INPUT_HE_COL(event->code);
    int position = zmk_matrix_transform_row_column_to_position(
        conf->matrix_transform, row, col);
    if (position < 0 || position >= conf->bindings[layer_id].processors_len) {
        LOG_ERR("Missing position (%d,%d) in layer %d",
            row, col,
                layer_id);
        return ZMK_INPUT_PROC_CONTINUE;
    }
    const struct zmk_input_processor_entry *proc_e =
        &conf->bindings[layer_id].processors[position];
    // LOG_DBG("Keymap event at (%d,%d), position: %d, value: %d, name: %s", row, col,
    // position, event->value, proc_e->dev->name);
    return zmk_input_processor_handle_event(proc_e->dev, event, proc_e->param1,
                                            proc_e->param2, state);
}

static struct zmk_input_processor_driver_api processor_api = {
    .handle_event = keymap_handle_event,
};

#define LAYER_CONFIG(node, n)                                                  \
    {                                                                          \
        .processors_len = DT_PROP_LEN_OR(node, input_processors, 0),           \
        .processors = _CONCAT(processor_##n, node),                            \
    }

#define PROCESSORS_CONFIG(node, n)                                             \
    static const struct zmk_input_processor_entry _CONCAT(                     \
        processor_##n, node)[DT_PROP_LEN_OR(node, input_processors, 0)] = {    \
        LISTIFY(DT_PROP_LEN(node, input_processors),                           \
                ZMK_INPUT_PROCESSOR_ENTRY_AT_IDX, (, ), node)};

#define KEYMAP_ONE(...) +1

#define KEYMAP_INIT(n)                                                         \
    DT_INST_FOREACH_CHILD_VARGS(n, PROCESSORS_CONFIG, n)                       \
    ZMK_MATRIX_TRANSFORM_EXTERN(DT_INST_PHANDLE(n, transform));                \
    static const struct he_keymap_config he_config_##n = {                     \
        .layers_len = (0 DT_INST_FOREACH_CHILD(n, KEYMAP_ONE)),                \
        .bindings = {DT_INST_FOREACH_CHILD_SEP_VARGS(n, LAYER_CONFIG, (, ),    \
                                                     n)},                      \
        .matrix_transform =                                                    \
            ZMK_MATRIX_TRANSFORM_T_FOR_NODE(DT_INST_PHANDLE(n, transform)),    \
    };                                                                         \
    DEVICE_DT_INST_DEFINE(n, NULL, NULL, NULL, &he_config_##n, POST_KERNEL,    \
                          CONFIG_INPUT_INIT_PRIORITY, &processor_api);

DT_INST_FOREACH_STATUS_OKAY(KEYMAP_INIT)