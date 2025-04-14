#include <drivers/kscan_forwarder.h>
#include <stdlib.h>
#include <zephyr/logging/log.h>
#include <zephyr/device.h>

LOG_MODULE_REGISTER(kscan_forwarder, CONFIG_LOG_MAX_LEVEL); // TODO change log level

#define DT_DRV_COMPAT zmk_kscan_forwarder

struct kscan_forwarder_data {
    const struct device *dev;
    const struct device *kscan_dev;
    kscan_forwarder_callback_t callback;
};

// driver init function
static int kscan_forwarder_init(const struct device *dev) {
    struct kscan_forwarder_data *data = dev->data;
    struct kscan_forwarder_config *conf = dev->config;

    data->dev = dev;
    return 0;
}

static int forwarder_config(const struct device *dev,
                                     kscan_forwarder_callback_t callback,
                                     const struct device *kscan_dev) {
    struct kscan_forwarder_data *data = dev->data;

    if (!callback || !kscan_dev) {
        return -EINVAL;
    }

    data->callback = callback;
    data->kscan_dev = kscan_dev;
    return 0;
}

static int forwarder_forward(const struct device *dev, uint32_t row,
                                   uint32_t column, bool pressed) {
    struct kscan_forwarder_data *data = dev->data;
    struct kscan_forwarder_config *conf = dev->config;

    if (!data->callback) {
        return -ENOTSUP;
    }
    data->callback(data->kscan_dev, row, column, pressed);
    return 0;
}

// kscan api struct
static const struct kscan_forwarder_api forwarder_api = {
    .config = forwarder_config,
    .forward = forwarder_forward,
};

#define KSCAN_FORWARDER_INIT(n)                                                \
    static struct kscan_forwarder_data kscan_forwarder_data_##n;               \
    DEVICE_DT_INST_DEFINE(                                                     \
        n, &kscan_forwarder_init, PM_DEVICE_DT_INST_GET(n),                    \
        &kscan_forwarder_data_##n, NULL, POST_KERNEL,                          \
        CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &forwarder_api);

DT_INST_FOREACH_STATUS_OKAY(KSCAN_FORWARDER_INIT)