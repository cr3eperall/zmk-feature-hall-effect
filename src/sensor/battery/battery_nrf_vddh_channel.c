/*
 * Copyright (c) 2021 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 *
 * This is a simplified version of battery_voltage_divider.c which always reads
 * the VDDHDIV5 channel of the &adc node and multiplies it by 5.
 */

#define DT_DRV_COMPAT zmk_battery_nrf_vddh_channel

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/logging/log.h>

// #include "battery_common.h"

LOG_MODULE_DECLARE(feature_hall_effect, CONFIG_ZMK_LOG_LEVEL);
// FIXME: if a battery driver is enabled on an adc channel not used by the
// kscan, everything breaks(all kscans stop producing outputs, bluetooth keeps
// working for a while) at the second battery_channel_get_ch when the adc is
// read
#define VDDHDIV (5)

struct battery_value_ch {
    uint16_t adc_raw;
    uint16_t millivolts;
    uint8_t state_of_charge;
};

int battery_channel_get_ch(const struct battery_value_ch *value,
                           enum sensor_channel chan,
                           struct sensor_value *val_out) {
    switch (chan) {
    case SENSOR_CHAN_GAUGE_VOLTAGE:
        val_out->val1 = value->millivolts / 1000;
        val_out->val2 = (value->millivolts % 1000) * 1000U;
        break;

    case SENSOR_CHAN_GAUGE_STATE_OF_CHARGE:
        val_out->val1 = value->state_of_charge;
        val_out->val2 = 0;
        break;

    default:
        return -ENOTSUP;
    }

    return 0;
}

uint8_t lithium_ion_mv_to_pct_ch(int16_t bat_mv) {
    // Simple linear approximation of a battery based off adafruit's discharge
    // graph: https://learn.adafruit.com/li-ion-and-lipoly-batteries/voltages

    if (bat_mv >= 4200) {
        return 100;
    } else if (bat_mv <= 3450) {
        return 0;
    }

    return bat_mv * 2 / 15 - 459;
}
struct vddh_data_ch {
    // struct adc_channel_cfg acc;
    struct adc_sequence as;
    struct battery_value_ch value;
};

struct vddh_config_ch {
    // uint8_t channel_id;
    const struct adc_dt_spec channel;
};

static int vddh_sample_fetch_ch(const struct device *dev,
                                enum sensor_channel chan) {
    // Make sure selected channel is supported
    if (chan != SENSOR_CHAN_GAUGE_VOLTAGE &&
        chan != SENSOR_CHAN_GAUGE_STATE_OF_CHARGE && chan != SENSOR_CHAN_ALL) {
        LOG_DBG("Selected channel is not supported: %d.", chan);
        return -ENOTSUP;
    }

    struct vddh_data_ch *drv_data = dev->data;
    const struct vddh_config_ch *conf = dev->config;
    struct adc_sequence *as = &drv_data->as;
    // wait for other adc tasks to finish
    k_busy_wait(50);
    int rc = adc_read(conf->channel.dev, as);
    as->calibrate = false;

    if (rc != 0) {
        LOG_ERR("Failed to read ADC: %d", rc);
        return rc;
    }

    int32_t val = drv_data->value.adc_raw;
    rc = adc_raw_to_millivolts(adc_ref_internal(conf->channel.dev),
                               conf->channel.channel_cfg.gain, as->resolution,
                               &val);
    if (rc != 0) {
        LOG_ERR("Failed to convert raw ADC to mV: %d", rc);
        return rc;
    }

    drv_data->value.millivolts = val * VDDHDIV;
    drv_data->value.state_of_charge =
        lithium_ion_mv_to_pct_ch(drv_data->value.millivolts);

    LOG_DBG("ADC raw %d ~ %d mV => %d%%", drv_data->value.adc_raw,
            drv_data->value.millivolts, drv_data->value.state_of_charge);

    return rc;
}

static int vddh_channel_get_ch(const struct device *dev,
                               enum sensor_channel chan,
                               struct sensor_value *val) {
    struct vddh_data_ch const *drv_data = dev->data;
    return battery_channel_get_ch(&drv_data->value, chan, val);
}

static const struct sensor_driver_api vddh_ch_api = {
    .sample_fetch = vddh_sample_fetch_ch,
    .channel_get = vddh_channel_get_ch,
};

static int vddh_init_ch(const struct device *dev) {
    struct vddh_data_ch *drv_data = dev->data;
    const struct vddh_config_ch *conf = dev->config;

    if (!device_is_ready(conf->channel.dev)) {
        LOG_ERR("ADC device is not ready %s", conf->channel.dev->name);
        return -ENODEV;
    }

    drv_data->as = (struct adc_sequence){
        .channels = BIT(conf->channel.channel_id),
        .buffer = &drv_data->value.adc_raw,
        .buffer_size = sizeof(drv_data->value.adc_raw),
        .oversampling = 0, // oversampling limited to 0 because of anomaly 212: SAADC: Events are not generated when
        // switching from scan mode to no-scan mode with burst enabled
        // https://docs.nordicsemi.com/bundle/errata_nRF52840_Rev2/page/ERR/nRF52840/Rev2/latest/err_840.html
        // there is a workaround but its propably not worth it
        .calibrate = true,
        .options = NULL,
        .resolution = 12,
    };

    // #ifdef CONFIG_ADC_NRFX_SAADC
    //     drv_data->acc = (struct adc_channel_cfg){
    //         .gain = ADC_GAIN_1_2,
    //         .reference = ADC_REF_INTERNAL,
    //         .acquisition_time = ADC_ACQ_TIME(ADC_ACQ_TIME_MICROSECONDS, 40),
    //         .input_positive = SAADC_CH_PSELN_PSELN_VDDHDIV5,
    //         .channel_id = conf->channel_id,
    //     };

    //     drv_data->as.resolution = 12;
    // #else
    // #error Unsupported ADC
    // #endif
    const int rc = adc_channel_setup_dt(&conf->channel);
    //     const int rc = adc_channel_setup(adc, &drv_data->acc);
    LOG_DBG("VDDHDIV5 setup returned %d", rc);

    return rc;
}

#define ADC_DT_SPEC_STRUCT_2(ctlr, input)                                      \
    {.dev = DEVICE_DT_GET(ctlr),                                               \
     .channel_id = input,                                                      \
     ADC_CHANNEL_CFG_FROM_DT_NODE(DT_CHILD(ctlr, DT_CAT(channel_, input)))}    \
    /* stupid fucking workaround*/

#define ADC_DT_SPEC_GET_2(node_id)                                             \
    ADC_DT_SPEC_STRUCT_2(DT_IO_CHANNELS_CTLR(node_id),                         \
                         DT_IO_CHANNELS_INPUT(node_id))

#if DT_NODE_EXISTS(DT_DRV_INST(0))

static struct vddh_data_ch vddh_data_ch;

static const struct vddh_config_ch vddh_config_ch = {
    .channel = ADC_DT_SPEC_GET_2(DT_DRV_INST(0)),
};

DEVICE_DT_INST_DEFINE(0, &vddh_init_ch, NULL, &vddh_data_ch, &vddh_config_ch,
                      POST_KERNEL, CONFIG_SENSOR_INIT_PRIORITY, &vddh_ch_api);
#endif
