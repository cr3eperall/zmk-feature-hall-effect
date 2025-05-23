/*
 * Copyright (c) 2024 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT he_input_split

#include <drivers/input_processor.h>
#include <zephyr/device.h>
#include <zephyr/input/input.h>
#include <zephyr/kernel.h>

#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
#error "he,input-split only supports peripheral role"
struct zis_entry {
    uint8_t reg;
    const struct device *dev;
};

#define ZIS_ENTRY(n)                                                           \
    {.reg = DT_INST_REG_ADDR(n), .dev = DEVICE_DT_GET(DT_DRV_INST(n))},

static const struct zis_entry proxy_inputs[] = {
    DT_INST_FOREACH_STATUS_OKAY(ZIS_ENTRY)};

int zmk_input_split_report_peripheral_event(uint8_t reg, uint8_t type,
                                            uint16_t code, int32_t value,
                                            bool sync) {
    LOG_DBG("Got peripheral event for %d!", reg);
    for (size_t i = 0; i < ARRAY_SIZE(proxy_inputs); i++) {
        if (reg == proxy_inputs[i].reg) {
            return input_report(proxy_inputs[i].dev, type, code, value, sync,
                                K_NO_WAIT);
        }
    }

    return -ENODEV;
}

#define ZIS_INST(n)                                                            \
    DEVICE_DT_INST_DEFINE(n, NULL, NULL, NULL, NULL, POST_KERNEL,              \
                          CONFIG_ZMK_INPUT_SPLIT_INIT_PRIORITY, NULL);

#else

#include <zmk/split/peripheral.h>

#define ZIS_INST(n)                                                            \
    static const struct zmk_input_processor_entry he_processors_##n[] =           \
        COND_CODE_1(DT_INST_NODE_HAS_PROP(n, input_processors),                \
                    ({LISTIFY(DT_INST_PROP_LEN(n, input_processors),           \
                              ZMK_INPUT_PROCESSOR_ENTRY_AT_IDX, (, ),          \
                              DT_DRV_INST(n))}),                               \
                    ({}));                                                     \
    BUILD_ASSERT(DT_INST_NODE_HAS_PROP(n, device),                             \
                 "Peripheral input splits need a `device` property set");      \
    void he_split_input_handler_##n(struct input_event *evt) {                    \
        for (size_t i = 0; i < ARRAY_SIZE(he_processors_##n); i++) {              \
            int ret = zmk_input_processor_handle_event(                        \
                he_processors_##n[i].dev, evt, he_processors_##n[i].param1,          \
                he_processors_##n[i].param2, NULL);                               \
            if (ret < 0) {                                                     \
                LOG_ERR("Error %d from input processor %s", ret,               \
                    he_processors_##n[i].dev->name);                          \
                return;                                                        \
            } else if (ret == ZMK_INPUT_PROC_STOP) {                           \
                return;                                                        \
            }                                                                  \
        }                                                                      \
        struct zmk_split_transport_peripheral_event ev = {                     \
            .type = ZMK_SPLIT_TRANSPORT_PERIPHERAL_EVENT_TYPE_INPUT_EVENT,     \
            .data = {.input_event = {                                          \
                         .reg = DT_INST_REG_ADDR(n),                           \
                         .type = evt->type,                                    \
                         .code = evt->code,                                    \
                         .value = evt->value,                                  \
                         .sync = evt->sync,                                    \
                     }}};                                                      \
        int ret = zmk_split_peripheral_report_event(&ev);                      \
        if (ret < 0) {                                                         \
            LOG_ERR("Error %d from zmk_split_peripheral_report_event", ret);           \
            return;                                                            \
        }                                                                      \
    }                                                                          \
    INPUT_CALLBACK_DEFINE(DEVICE_DT_GET(DT_INST_PHANDLE(n, device)),           \
                          he_split_input_handler_##n);

#endif

DT_INST_FOREACH_STATUS_OKAY(ZIS_INST)