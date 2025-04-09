#include <drivers/kscan_forwarder.h>
#include <stdlib.h>
#include <zephyr/device.h>

LOG_MODULE_REGISTER(kscan_forwarder,
                    CONFIG_LOG_MAX_LEVEL);  // TODO change log level

#define DT_DRV_COMPAT zmk_kscan_forwarder

struct kscan_forwarder_data {
    const struct device *dev;
    kscan_forwarder_callback_t callback;
};

struct kscan_forwarder_config {
    const struct device *kscan_dev;
};

// driver init function
static int kscan_forwarder_init(const struct device *dev) {
    struct kscan_forwarder_data *data = dev->data;
    struct kscan_forwarder_config *conf = dev->config;
    
    data->dev = dev;
    return 0;
}

static int kscan_forwarder_configure(const struct device *dev,
                                     kscan_forwarder_callback_t callback) {
    struct kscan_forwarder_data *data = dev->data;

    if (!callback) {
        return -EINVAL;
    }

    data->callback = callback;
    return 0;
}
static int kscan_forwarder_forward(const struct device *dev, uint32_t row,
                                   uint32_t column, bool pressed) {
    struct kscan_forwarder_data *data = dev->data;
    struct kscan_forwarder_config *conf = dev->config;
    
    if (!data->callback) {
        return -ENOTSUP;
    }
    data->callback(conf->kscan_dev, row, column, pressed);
    return 0;
}

// kscan api struct
static const struct kscan_forwarder_api forwarder_api = {
    .config = kscan_forwarder_configure,
    .forward = kscan_forwarder_forward,
};

#define KSCAN_FORWARDER_INIT(n)                                                \
    static struct kscan_forwarder_data kscan_forwarder_data_##n;               \
    static const struct kscan_forwarder_config kscan_forwarder_config_##n = {  \
        .kscan_dev = DEVICE_DT_GET(DT_INST_PHANDLE(n, kscan)),                 \
    };                                                                         \
    DEVICE_DT_INST_DEFINE(n, &kscan_forwarder_init, PM_DEVICE_DT_INST_GET(n), \
                          &kscan_forwarder_data_##n, &kscan_forwarder_config_##n,          \
                          POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,             \
                          &kscan_forwarder_api);

DT_INST_FOREACH_STATUS_OKAY(KSCAN_FORWARDER_INIT)