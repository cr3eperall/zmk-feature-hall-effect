#include <drivers/input_processor.h>
#include <stdlib.h>
#include <zephyr/device.h>
#include <zephyr/logging/log.h>

#include <he/input-event-codes.h>

LOG_MODULE_DECLARE(feature_hall_effect, CONFIG_HE_LOG_LEVEL);

#define DT_DRV_COMPAT he_input_processor_blank_he

struct blank_he_config {
    bool log_enabled;
    bool block;
};

static int blank_he_handle_event(const struct device *dev,
                               struct input_event *event, uint32_t param1,
                               uint32_t param2,
                               struct zmk_input_processor_state *state) {
    if (event->type != INPUT_EV_HE)
        return ZMK_INPUT_PROC_CONTINUE;
    const struct blank_he_config *conf = dev->config;
    uint8_t row = INV_INPUT_HE_ROW(event->code);
    uint8_t col = INV_INPUT_HE_COL(event->code);
    if (conf->log_enabled) {
        LOG_INF("HE event received, pos: (%d,%d), value: %d", row, col, event->value);
    }

    return conf->block ? ZMK_INPUT_PROC_STOP : ZMK_INPUT_PROC_CONTINUE;
}

static struct zmk_input_processor_driver_api processor_api = {
    .handle_event = blank_he_handle_event,
};

#define BLANK_HE_INIT(n)                                                         \
    static const struct blank_he_config blank_he_config_##n = {                                  \
        .log_enabled = DT_INST_PROP(n, log),                           \
        .block = DT_INST_PROP(n, block),                                       \
    };                                                                         \
    DEVICE_DT_INST_DEFINE(n, NULL, NULL, NULL, &blank_he_config_##n,             \
                          POST_KERNEL, CONFIG_INPUT_INIT_PRIORITY,             \
                          &processor_api);

DT_INST_FOREACH_STATUS_OKAY(BLANK_HE_INIT)