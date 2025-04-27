
#include <drivers/input_processor.h>
#include <stdlib.h>
#include <zephyr/device.h>
#include <zephyr/logging/log.h>

#include <he/input-event-codes.h>

LOG_MODULE_DECLARE(feature_hall_effect, CONFIG_HE_LOG_LEVEL);

#define DT_DRV_COMPAT he_raw_signal_processor

typedef struct {
    float z1, z2;
} SOS_State;

struct key_data_t {
    SOS_State sos_state;
    int16_t last_value;
    uint64_t last_value_time;
    uint64_t last_report_time;
};

struct raw_signal_processor_data {
    const struct device *dev;
    const int sections;
    struct key_data_t *key_data;
    float *coeffs;
};

struct raw_signal_processor_config {
    int16_t n_coeffs;
    uint8_t rows;
    uint8_t cols;
    int8_t offset_x;
    int8_t offset_y;
    int16_t deadzone_bottom;
    int16_t deadzone_top;
    int32_t variable_sample_compensation;
    int32_t wait_period_press;
    int32_t sample_div;
    int32_t filter_coeffs_int32[];
};

float apply_sos_filter(const struct device *dev, int row, int col,
                       float input) {
    float output = input;
    struct raw_signal_processor_data *data = dev->data;
    const struct raw_signal_processor_config *conf = dev->config;
    float *sos = data->coeffs;
    SOS_State *state = &data->key_data[row * conf->cols + col].sos_state;
    for (int i = 0; i < data->sections; i++) {
        const float b0 = sos[i * 6];
        const float b1 = sos[i * 6 + 1];
        const float b2 = sos[i * 6 + 2];
        // const float a0 = sos[i * 6 + 3];  // should be normalized to 1
        const float a1 = sos[i * 6 + 4];
        const float a2 = sos[i * 6 + 5];
        float y = b0 * output + state->z1;

        // Update internal states:
        // The new z1 is computed as b1*x[n] - a1*y[n] + previous z2.
        // The new z2 becomes b2*x[n] - a2*y[n].
        state->z1 = b1 * output - a1 * y + state->z2;
        state->z2 = b2 * output - a2 * y;

        output = y;
    }

    return output;
}

static int raw_sp_init(const struct device *dev) {
    struct raw_signal_processor_data *data = dev->data;
    const struct raw_signal_processor_config *conf = dev->config;

    data->dev = dev;
    data->coeffs = malloc(conf->n_coeffs * sizeof(float));
    data->key_data = calloc(conf->rows * conf->cols, sizeof(struct key_data_t));
    for (int i = 0; i < conf->rows * conf->cols; i++) {
        data->key_data[i].last_value = -1;
    }
    for (int i = 0; i < conf->n_coeffs; i++) {
        int32_t coeff_i = conf->filter_coeffs_int32[i];
        float *coeff_f = (float *)&coeff_i;
        data->coeffs[i] = *coeff_f;
    }
    LOG_INF("raw signal processor init");

    return 0;
}

static int raw_sp_handle_event(const struct device *dev,
                               struct input_event *event, uint32_t param1,
                               uint32_t param2,
                               struct zmk_input_processor_state *state) {
    if (event->type != INPUT_EV_HE) return ZMK_INPUT_PROC_CONTINUE;
    const struct raw_signal_processor_config *conf = dev->config;
    struct raw_signal_processor_data *data = dev->data;
    uint8_t row = INV_INPUT_HE_ROW(event->code);
    uint8_t col = INV_INPUT_HE_COL(event->code);
    if (row < conf->offset_y ||
        col < conf->offset_x ||
        row > conf->offset_y + conf->rows ||
        col > conf->offset_x + conf->cols) {
        // LOG_INF("CONT oob");
        return ZMK_INPUT_PROC_CONTINUE;
    }

    row = row - conf->offset_x;
    int64_t now = k_uptime_get();
    int32_t key_data_idx = row * conf->cols + col;
    float filtered_value;
    if (unlikely(data->key_data[key_data_idx].last_value == -1)) {
        // prime the filter state
        for (int i = 0; i < 50; i++) {
            filtered_value=apply_sos_filter(dev, row, col, (float)event->value);
        }
        data->key_data[key_data_idx].last_value = (int16_t)event->value;
        data->key_data[key_data_idx].last_value_time = now;
        if(filtered_value < conf->deadzone_bottom || filtered_value > conf->deadzone_top){
            // LOG_INF("STOP first, val: %d", (int32_t)filtered_value);
            return ZMK_INPUT_PROC_STOP;
        } else {
            // LOG_INF("CONT first");
            data->key_data[key_data_idx].last_report_time = now;
            return ZMK_INPUT_PROC_CONTINUE;
        }
    }
    float float_in = (float)event->value;
    if (now - data->key_data[key_data_idx].last_value_time >
        2 * conf->wait_period_press) {
        // add compensation
        float tmp = float_in;
        for (int i = 0; i < conf->variable_sample_compensation; i++) {
            tmp = apply_sos_filter(dev, row, col,
                                   (tmp + (float)event->value) / 2.0f);
        }
    }
    data->key_data[key_data_idx].last_value_time = now;
    filtered_value = apply_sos_filter(dev, row, col, float_in);
    if (now - data->key_data[key_data_idx].last_report_time < conf->wait_period_press*conf->sample_div){
        // LOG_INF("STOP wait");
        return ZMK_INPUT_PROC_STOP;
    }
    event->value = (int32_t)filtered_value;
    if(filtered_value < conf->deadzone_bottom || filtered_value > conf->deadzone_top){
        // LOG_INF("STOP dzone val:%d, filt:%d", data->key_data[key_data_idx].last_value, (int32_t)filtered_value);
        if(data->key_data[key_data_idx].last_value <= conf->deadzone_top && data->key_data[key_data_idx].last_value >= conf->deadzone_bottom){
            data->key_data[key_data_idx].last_value = (int16_t)event->value;
            data->key_data[key_data_idx].last_report_time = now;
            return ZMK_INPUT_PROC_CONTINUE;
        }
        data->key_data[key_data_idx].last_value = (int16_t)event->value;
        return ZMK_INPUT_PROC_STOP;
    } else {
        // LOG_INF("CONT dzone");
        // LOG_INF("RC(%d,%d), val: %d", row, col, (int32_t)event->value);
        data->key_data[key_data_idx].last_report_time = now;
        data->key_data[key_data_idx].last_value = (int16_t)event->value;
        return ZMK_INPUT_PROC_CONTINUE;
    }
}

static struct zmk_input_processor_driver_api signal_processor_api = {
    .handle_event = raw_sp_handle_event,
};

#define RAW_SP_INIT(n)                                                        \
    BUILD_ASSERT(DT_INST_PROP_LEN(n, filter_coefficients) % 6 == 0,           \
                 "Filter coefficients must be a multiple of 6");              \
    BUILD_ASSERT(DT_INST_PROP_LEN(n, matrix_size) == 2,                       \
                 "Matrix size must contain 2 values");                        \
    BUILD_ASSERT(DT_INST_PROP_LEN(n, working_area) == 2,                      \
                 "Working area must contain 2 values");                       \
    BUILD_ASSERT(DT_INST_PROP_BY_IDX(n, working_area, 0) <                    \
                DT_INST_PROP_BY_IDX( n, working_area, 1),                     \
                 "Working area bottom must be less than top");                \
    static struct raw_signal_processor_data raw_signal_processor_data_##n = { \
        .sections = DT_INST_PROP_LEN(n, filter_coefficients) / 6,             \
    };                                                                        \
    static const struct raw_signal_processor_config                           \
        raw_signal_processor_config_##n = {                                   \
            .n_coeffs = DT_INST_PROP_LEN(n, filter_coefficients),             \
            .rows = DT_INST_PROP_BY_IDX(n, matrix_size, 0),                   \
            .cols = DT_INST_PROP_BY_IDX(n, matrix_size, 1),                   \
            .offset_x = DT_INST_PROP_BY_IDX(n, offset, 0),                    \
            .offset_y = DT_INST_PROP_BY_IDX(n, offset, 1),                    \
            .deadzone_bottom = DT_INST_PROP_BY_IDX(n, working_area, 0),       \
            .deadzone_top = DT_INST_PROP_BY_IDX(n, working_area, 1),          \
            .filter_coeffs_int32 = {DT_INST_FOREACH_PROP_ELEM_SEP( \
                n, filter_coefficients, DT_PROP_BY_IDX, (, ))},               \
            .variable_sample_compensation =                                   \
                DT_INST_PROP(n, variable_sample_compensation),                \
            .wait_period_press = DT_INST_PROP(n, wait_period_press),          \
            .sample_div = DT_INST_PROP(n, sample_div),                        \
    };                                                                        \
    DEVICE_DT_INST_DEFINE(n, &raw_sp_init, NULL,                              \
                          &raw_signal_processor_data_##n,                     \
                          &raw_signal_processor_config_##n, POST_KERNEL,      \
                          CONFIG_INPUT_INIT_PRIORITY, &signal_processor_api);

DT_INST_FOREACH_STATUS_OKAY(RAW_SP_INIT)