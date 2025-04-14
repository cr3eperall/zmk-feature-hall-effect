#pragma once

#include <stddef.h>
#include <zephyr/device.h>
#include <zephyr/types.h>
#include <errno.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @cond INTERNAL_HIDDEN
 *
 * Kscan forwarder driver API definition and system call entry points.
 */

typedef void (*kscan_forwarder_callback_t)(const struct device *dev, uint32_t row,
    uint32_t column,
    bool pressed);


typedef int (*kscan_forwarder_config_t)(const struct device *dev,
    kscan_forwarder_callback_t callback, const struct device *kscan_dev);
typedef int (*kscan_forwarder_forward_t)(const struct device *dev, uint32_t row,
                               uint32_t column, bool pressed);

__subsystem struct kscan_forwarder_api {
    kscan_forwarder_config_t config;
    kscan_forwarder_forward_t forward;
};
/**
 * @endcond
 */

/**
 * @brief Gives the kscan callback to the kscan forwarder device.
 * @param dev Pointer to the device structure for the driver instance.
 * @param callback to call when forwarding a key event.
 *
 * @retval 0 If successful.
 * @retval Negative errno code if failure.
 */
__syscall int kscan_forwarder_config(const struct device *dev,
    kscan_forwarder_callback_t callback, const struct device *kscan_dev);

static inline int z_impl_kscan_forwarder_config(const struct device *dev,
    kscan_forwarder_callback_t callback, const struct device *kscan_dev) {
    const struct kscan_forwarder_api *api =
        (const struct kscan_forwarder_api *)dev->api;

    if (api->config == NULL ) {
        return -ENOTSUP;
    }

    return api->config(dev, callback, kscan_dev);
}

/**
 * @brief Forward a key event to the kscan device using the callback function.
 * @param dev Pointer to the device structure for the driver instance.
 *
 * @retval 0 If successful.
 * @retval Negative errno code if failure.
 */
__syscall int kscan_forwarder_forward(const struct device *dev, uint32_t row,
                                      uint32_t column, bool pressed);

static inline int z_impl_kscan_forwarder_forward(const struct device *dev,
                                                 uint32_t row, uint32_t col,
                                                 bool pressed) {
    const struct kscan_forwarder_api *api = (const struct kscan_forwarder_api *)dev->api;

    if (api->forward == NULL) {
        return -ENOTSUP;
    }

    return api->forward(dev, row, col, pressed);
}

#ifdef __cplusplus
}
#endif

/**
 * @}
 */

#include <syscalls/kscan_forwarder.h>
