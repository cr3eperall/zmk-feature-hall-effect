#include <drivers/input_processor.h>
#include <stdlib.h>
#include <zephyr/device.h>
#include <zephyr/logging/log.h>

#include "input-event-codes.h"

LOG_MODULE_REGISTER(input_processor_matrix_offset,
                    CONFIG_LOG_MAX_LEVEL);  // TODO change log level

#define DT_DRV_COMPAT he_input_processor_matrix_offset

static int matrix_offset_handle_event(const struct device *dev,
                               struct input_event *event, uint32_t x,
                               uint32_t y,
                               struct zmk_input_processor_state *state) {
    if (event->type != INPUT_EV_HE) return ZMK_INPUT_PROC_CONTINUE;
    uint8_t row=INV_INPUT_HE_ROW(event->code);
    uint8_t col=INV_INPUT_HE_COL(event->code);
    event->code=INPUT_HE_RC(row+y,col+x);
    return ZMK_INPUT_PROC_CONTINUE;
}

static struct zmk_input_processor_driver_api processor_api = {
    .handle_event = matrix_offset_handle_event,
};

#define MAT_OFFSET_INIT(n)      \
    DEVICE_DT_INST_DEFINE(n, NULL, NULL,                              \
                          NULL,                     \
                          NULL, POST_KERNEL,      \
                          CONFIG_INPUT_INIT_PRIORITY, &processor_api);

DT_INST_FOREACH_STATUS_OKAY(MAT_OFFSET_INIT)