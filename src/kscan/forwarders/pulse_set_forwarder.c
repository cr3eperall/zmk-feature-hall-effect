#include <drivers/pulse_set_forwarder.h>
#include <stdlib.h>
#include <zephyr/logging/log.h>
#include <zephyr/device.h>

LOG_MODULE_DECLARE(feature_hall_effect, CONFIG_HE_LOG_LEVEL);
#define DT_DRV_COMPAT he_pulse_set_forwarder

struct pulse_set_forwarder_data {
    const struct device *dev;
    const struct device *pulse_dev;
    pulse_set_forwarder_callback_t pulse_set_callback;
};

// driver init function
static int pulse_set_forwarder_init(const struct device *dev) {
    struct pulse_set_forwarder_data *data = dev->data;

    data->dev = dev;
    return 0;
}

static int forwarder_config(const struct device *dev,
                                          pulse_set_forwarder_callback_t callback,
                                          const struct device *pulse_dev) {
    struct pulse_set_forwarder_data *data = dev->data;

    if (!callback || !pulse_dev) {
        return -EINVAL;
    }
    data->pulse_set_callback = callback;
    data->pulse_dev = pulse_dev;
    return 0;
}

static int forwarder_forward(const struct device *dev, bool pulse_enable) {
    struct pulse_set_forwarder_data *data = dev->data;

    if (!data->pulse_set_callback) {
        return -ENOTSUP;
    }
    if(!data->pulse_dev){
        return -ENODEV;
    }
    data->pulse_set_callback(data->pulse_dev, pulse_enable);
    return 0;
}

// kscan api struct
static const struct pulse_set_forwarder_api forwarder_api = {
    .config= forwarder_config,
    .forward = forwarder_forward,
};

#define PULSE_SET_FORWARDER_INIT(n)                                                \
    static struct pulse_set_forwarder_data pulse_set_forwarder_data_##n;               \
    DEVICE_DT_INST_DEFINE(                                                     \
        n, &pulse_set_forwarder_init, NULL,                                        \
        &pulse_set_forwarder_data_##n, NULL, POST_KERNEL,                          \
        CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &forwarder_api);

DT_INST_FOREACH_STATUS_OKAY(PULSE_SET_FORWARDER_INIT)