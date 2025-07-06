#pragma once

#include <stddef.h>
#include <zephyr/device.h>
#include <zephyr/types.h>
#include <errno.h>

#ifdef __cplusplus
extern "C" {
#endif


typedef void (*pulse_set_forwarder_callback_t)(const struct device *dev, bool pulse_enable);


typedef int (*pulse_set_forwarder_config_t)(const struct device *dev,
    pulse_set_forwarder_callback_t callback,const struct device *pulse_dev);
typedef int (*pulse_set_forwarder_forward_t)(const struct device *dev, bool pulse_enable);    


__subsystem struct pulse_set_forwarder_api {
    pulse_set_forwarder_config_t config;
    pulse_set_forwarder_forward_t forward;
};
/**
 * @endcond
 */


/**
 * @brief Gives the pulse_set callback to the kscan forwarder device.
 * @param dev Pointer to the device structure for the driver instance.
 * @param callback to call when forwarding a pulse_set.
 *
 * @retval 0 If successful.
 * @retval Negative errno code if failure.
 */
__syscall int pulse_set_forwarder_config(const struct device *dev,
    pulse_set_forwarder_callback_t callback, const struct device *pulse_dev);

static inline int z_impl_pulse_set_forwarder_config(const struct device *dev,
    pulse_set_forwarder_callback_t callback,const struct device *pulse_dev) {
    const struct pulse_set_forwarder_api *api =
        (const struct pulse_set_forwarder_api *)dev->api;

    if (api->config == NULL ) {
        return -ENOTSUP;
    }

    return api->config(dev, callback, pulse_dev);
}

/**
 * @brief Forward a key event to the kscan device using the callback function.
 * @param dev Pointer to the device structure for the driver instance.
 *
 * @retval 0 If successful.
 * @retval Negative errno code if failure.
 */
__syscall int pulse_set_forwarder_forward(const struct device *dev, bool pulse_enable);

static inline int z_impl_pulse_set_forwarder_forward(const struct device *dev, bool pulse_enable) {
    const struct pulse_set_forwarder_api *api = (const struct pulse_set_forwarder_api *)dev->api;

    if (api->forward == NULL) {
        return -ENOTSUP;
    }

    return api->forward(dev, pulse_enable);
}

#ifdef __cplusplus
}
#endif

/**
 * @}
 */

#include <syscalls/pulse_set_forwarder.h>
