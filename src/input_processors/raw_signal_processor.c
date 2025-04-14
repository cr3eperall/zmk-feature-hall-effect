
#include "input-event-codes.h"
#include <drivers/input_processor.h>
#include <stdlib.h>
#include <zephyr/device.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(raw_signal_processor,
                    CONFIG_LOG_MAX_LEVEL); // TODO change log level

#define DT_DRV_COMPAT he_raw_signal_processor

typedef struct {
    double z1, z2;
} SOS_State;

struct raw_signal_processor_data {
    const struct device *dev;
    const int sections;
    SOS_State sos_state;
    int16_t last_value;
    int64_t last_value_time;
    double *coeffs;
};

struct raw_signal_processor_config {
    int16_t n_coeffs;
    int32_t *filter_coeffs_int32;
    int32_t variable_sample_compensation;
    int32_t wait_period_press;
};

double apply_sos_filter(const struct device *dev, double input) {
    double output = input;
    struct raw_signal_processor_data *data = dev->data;
    const struct raw_signal_processor_config *conf = dev->config;
    double *sos = data->coeffs;
    for (int i = 0; i < data->sections; i++) {
        const double b0= sos[i*6];
        const double b1= sos[i*6+1];
        const double b2= sos[i*6+2];
        const double a0= sos[i*6+3]; // should be normalized to 1
        const double a1= sos[i*6+4];
        const double a2= sos[i*6+5];
        double y = b0 * output + data->sos_state.z1;
    
        // Update internal states:
        // The new z1 is computed as b1*x[n] - a1*y[n] + previous z2.
        // The new z2 becomes b2*x[n] - a2*y[n].
        data->sos_state.z1 = b1 * output - a1 * y + data->sos_state.z2;
        data->sos_state.z2 = b2 * output - a2 * y;

        output=y;
    }

    return output;
}

static int raw_sp_init(const struct device *dev) {
    struct raw_signal_processor_data *data = dev->data;
    const struct raw_signal_processor_config *config = dev->config;

    data->dev = dev;
    data->last_value=-1;
    data->sos_state=(SOS_State){.z1 = 0.0, .z2 = 0.0};
    data->coeffs=malloc(config->n_coeffs/2 * sizeof(double));
    for (int i = 0; i < config->n_coeffs/2; i++) {
        int64_t coeff_i=config->filter_coeffs_int32[2*i]<<32 + config->filter_coeffs_int32[2*i+1];
        double *coeff_d=(double *)&coeff_i;
        data->coeffs[i] = *coeff_d;
    }
    LOG_INF("raw signal processor init");

    return 0;
}

static int raw_sp_handle_event(const struct device *dev,
                               struct input_event *event, uint32_t param1,
                               uint32_t param2,
                               struct zmk_input_processor_state *state) {
    // struct raw_signal_processor_data *data = dev->data;
    const struct raw_signal_processor_config *config = dev->config;
    struct raw_signal_processor_data *data = dev->data;

    int64_t now = k_uptime_get();
    if(now-data->last_value_time> 2*config->wait_period_press){
        // add compensation
        
    }

    return 0;
}

static struct zmk_input_processor_driver_api signal_processor_api = {
    .handle_event = raw_sp_handle_event,
};

#define VALUE(n, prop, idx) DT_PROP_BY_IDX(n, prop, idx),

#define RAW_SP_INIT(n)                                                         \
    BUILD_ASSERT(DT_INST_PROP_LEN(n, filter_coefficients)%12==0,            \
                 "Filter coefficients must be a multiple of 12 (2 int32 for each double coefficient)");              \
    static struct raw_signal_processor_data raw_signal_processor_data_##n;     \
    static const struct raw_signal_processor_config                            \
        raw_signal_processor_config_##n = {                                    \
            .n_coeffs = DT_INST_PROP_LEN(n, filter_coefficients),                \
            .filter_coeffs_int32 = (int32_t *){DT_INST_FOREACH_PROP_ELEM_SEP(        \
                n, filter_coefficients, DT_PROP_BY_IDX, (, ))},                  \
            .variable_sample_compensation = DT_INST_PROP(n, variable_sample_compensation), \
            .wait_period_press = DT_INST_PROP(n, wait_period_press),            \
    };                                                                         \
    DEVICE_DT_INST_DEFINE(n, &raw_sp_init, NULL,                               \
                          &raw_signal_processor_data_##n,                      \
                          &raw_signal_processor_config_##n, POST_KERNEL,       \
                          CONFIG_INPUT_INIT_PRIORITY, &signal_processor_api);

DT_INST_FOREACH_STATUS_OKAY(RAW_SP_INIT)