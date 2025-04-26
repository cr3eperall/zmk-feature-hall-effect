// #include <nrfx_saadc.h>
#include <stdlib.h>
#include <zephyr/device.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/kscan.h>
#include <zephyr/input/input.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/pm/device.h>
#include <zephyr/settings/settings.h>

#include "drivers/kscan_forwarder.h"
#include "input-event-codes.h"
#include "adc.h"
// #include <zephyr/sys/util.h>

#ifndef CONFIG_HE_ADC_CALIBRATION_DELAY
#define CONFIG_HE_ADC_CALIBRATION_DELAY 2000000
#endif

LOG_MODULE_REGISTER(kscan_he_direct_pulsed,
                    CONFIG_LOG_MAX_LEVEL); // TODO change name

#define DT_DRV_COMPAT he_kscan_he_direct_pulsed

static int init_adc(const struct device *dev) {
    const struct kscan_he_config *conf = dev->config;
    struct kscan_he_data *data = dev->data;
    // volatile void *api = conf->adc_groups[0].keys[0].adc.dev->api;
    for (int i = 0; i < conf->group_count; i++) {
        data->adc_groups[i].as.channels = 0;
        for (int8_t j = 0; j < conf->he_groups[i].key_count; j++) {
            const struct adc_dt_spec *adc = &conf->he_groups[i].keys[j].adc;
            if (!device_is_ready(adc->dev)) {
                LOG_ERR("ADC is not ready: %s", adc->dev->name);
                return -ENODEV;
            }

            int err = adc_channel_setup_dt(adc);
            if (err) {
                LOG_ERR("Unable to configure ADC %u on %s for output",
                        adc->channel_id, adc->dev->name);
                return err;
            }
            data->adc_groups[i].as.channels |= BIT(adc->channel_id);
            LOG_INF("Configured ADC %u on %s for output", adc->channel_id,
                    adc->dev->name);
        }
    }

    return 0;
}

static int init_gpio(const struct device *dev) {
    const struct kscan_he_config *conf = dev->config;
    struct kscan_he_data *data = dev->data;
    if (!data->pulse_enabled)
        LOG_DBG("Pulse read disabled");
    for (int i = 0; i < conf->group_count; i++) {
        const struct gpio_dt_spec gpio = conf->he_groups[i].enable_gpio;
        if (gpio.port == NULL) {
            continue;
        }
        if (!device_is_ready(gpio.port)) {
            LOG_ERR("GPIO is not ready: %s", gpio.port->name);
            return -ENODEV;
        }
        int err;
        if (data->pulse_enabled) {
            err = gpio_pin_configure_dt(&gpio,
                                        GPIO_OUTPUT | GPIO_OUTPUT_INIT_LOW);
        } else {
            // if pulse read is disabled but there are enable gpios defined,
            // configure them as always on
            err = gpio_pin_configure_dt(&gpio,
                                        GPIO_OUTPUT | GPIO_OUTPUT_INIT_HIGH);
        }
        if (err) {
            LOG_ERR("Unable to configure pin %u on %s for output", gpio.pin,
                    gpio.port->name);
            return err;
        }
        LOG_DBG("Configured GPIO pin %u on %s for output", gpio.pin,
                gpio.port->name);
    }
    return 0;
}

static int set_all_gpio(const struct device *dev, int value) {
    const struct kscan_he_config *conf = dev->config;
    for (int i = 0; i < conf->group_count; i++) {
        const struct gpio_dt_spec gpio = conf->he_groups[i].enable_gpio;
        if (gpio.port == NULL) {
            continue;
        }
        int err = gpio_pin_set_dt(&gpio, value);
        if (err) {
            LOG_ERR("Failed to set output %i high: %i", gpio.pin, err);
            return err;
        }
    }
    return 0;
}

static int setup_pins(const struct device *dev) {
    int err = init_adc(dev);
    if (err) {
        LOG_ERR("Error during ADC init: %d", err);
        return err;
    }
    err = init_gpio(dev);
    if (err) {
        LOG_ERR("Error during GPIO init: %d", err);
        return err;
    }
    return 0;
}

static void kscan_he_read_continue(const struct device *dev) {
    const struct kscan_he_config *conf = dev->config;
    struct kscan_he_data *data = dev->data;

    data->scan_time += conf->wait_period_press;

    k_work_reschedule(&data->adc_read_work, K_TIMEOUT_ABS_MS(data->scan_time));
}

static void kscan_he_read_end(const struct device *dev) {
    struct kscan_he_data *data = dev->data;
    const struct kscan_he_config *conf = dev->config;

    data->scan_time += conf->wait_period_idle;

    // Return to polling slowly.
    k_work_reschedule(&data->adc_read_work, K_TIMEOUT_ABS_MS(data->scan_time));
}

static int kscan_he_read(const struct device *dev) {
    const struct kscan_he_config *conf = dev->config;
    struct kscan_he_data *data = dev->data;
    // k_msleep(3000);
    // LOG_INF("pin high");
    for (int i = 0; i < conf->group_count; i++) {
        const struct kscan_he_group_cfg group_cfg = conf->he_groups[i];
        int err;
        if (data->pulse_enabled) {
            int64_t before = k_uptime_ticks();
            err = gpio_pin_set_dt(&group_cfg.enable_gpio, 1);
            if (err) {
                LOG_ERR("Failed to set output %i high: %i",
                        group_cfg.enable_gpio.pin, err);
                return err;
            }
            // This has less accuracy than k_busy_wait but at least it allows other threads to execute
            int64_t elapsed=k_ticks_to_us_near64(k_uptime_ticks() - before);
            if(elapsed < (conf->read_turn_on_time/5)*4){ // if elapsed is less than 80% of the turn on time
                k_usleep(conf->read_turn_on_time-elapsed);
            }

            // k_busy_wait(conf->read_turn_on_time);
            // k_msleep(1000);
            // LOG_INF("adc read");
        }

        err = adc_read(group_cfg.keys[0].adc.dev, &data->adc_groups[i].as);

        //reset calibration
        if(data->adc_groups[i].as.calibrate){
            data->adc_groups[i].as.calibrate = false;
        }

        if (err) {
            LOG_ERR("ADC READ ERROR %d", err);
        }
        // k_msleep(1000);
        // LOG_INF("pin low");
        if (data->pulse_enabled) {
            err = gpio_pin_set_dt(&group_cfg.enable_gpio, 0);
            if (err) {
                LOG_ERR("Failed to set output %i low: %i",
                        group_cfg.enable_gpio.pin, err);
            }
        }
    }
    bool pressed = false;
    int64_t now = conf->calibrate ? k_uptime_get() : 0;
    int16_t buf[conf->group_count*conf->he_groups[0].key_count];
    for (int i = 0; i < conf->group_count; i++) {
        const struct kscan_he_group_cfg group_cfg = conf->he_groups[i];
        int8_t buffer_idx=0;
        for (int8_t channel = 0; channel < KSCAN_ADC_MAX_CHANNELS; channel++) {
            int8_t channel_mask = BIT(channel);
            if(!(data->adc_groups[i].as.channels & channel_mask)){
                continue;
            }
            const int8_t key_j = data->adc_groups[i].key_channels[channel];
            const int16_t max_height = conf->he_groups[i].switch_height;
            const int16_t raw_adc_value = data->adc_groups[i].adc_buffer[buffer_idx];
            
            if(!conf->calibrate){
                int16_t key_height =
                    kscan_adc_get_mapped_height(dev, i, key_j, raw_adc_value);
                const int16_t deadzone_top =
                    kscan_adc_cfg_deadzone_top(dev, i, key_j);
                const int16_t deadzone_bottom =
                    kscan_adc_cfg_deadzone_bottom(dev, i, key_j);
                if(key_height<0) key_height = 0;
                if(key_height > max_height) key_height = max_height;
                if(key_height <= max_height - deadzone_top && key_height > deadzone_bottom){
                    pressed = true;
                    // send sync only at the last key
                }
                input_report(dev, INPUT_EV_HE, INPUT_HE_RC(i, key_j), key_height, (i==conf->group_count-1 && key_j == group_cfg.key_count-1), K_FOREVER); //TODO check if timeout is needed
            }else{
                pressed = true;
                buf[i*conf->he_groups[0].key_count + key_j] = raw_adc_value;
            }

            // LOG_INF("adc, [%d,%d] raw: %hi, height: %hi", i, key_j,
            //         raw_adc_value, key_height);
            // if (key_height <= deadzone_top) {
            //     pressed = true;
            //     adc_key_state_update(&data->adc_groups[i].key_state_vec[key_j],
            //                          true, key_height);
            // } else if (key_height > deadzone_bottom) {
            //     adc_key_state_update(&data->adc_groups[i].key_state_vec[key_j],
            //                          false, key_height);
            // } else {
            //     // keep old state
            // }
            buffer_idx++;
        }
    }
    if (conf->calibrate) {
        char cbuf[conf->group_count*conf->he_groups[0].key_count*4+1];
        for (int i = 0; i < conf->group_count*conf->he_groups[0].key_count; i++) {
            sprintf(cbuf+i*4, "%04d", buf[i]);
        }
        cbuf[conf->group_count*conf->he_groups[0].key_count*4] = '\0';
        printk("~%lld:%s\n", now, cbuf);
    }
    // for (int i = 0; i < conf->group_count; i++) {
    //     struct kscan_adc_group_data group = data->adc_groups[i];
    //     for (int j = 0; j < conf->he_groups[i].key_count; j++) {
    //         if (adc_key_state_has_changed(&group.key_state_vec[j])) {
    //             const bool pressed =
    //                 adc_key_state_is_pressed(&group.key_state_vec[j]);

    //             LOG_DBG("Sending event at channel %i state %s", i,
    //                     pressed ? "on" : "off");
    //             data->callback(dev, i, j, pressed);
    //         }
    //     }
    // }

    // LOG_INF("pressed: %i", pressed);
    // return 0;
    if (pressed) {
        kscan_he_read_continue(dev);
    } else {
        kscan_he_read_end(dev);
    }

    return 0;
}

static void kscan_adc_read_work_handler(struct k_work *work) {
    struct k_work_delayable *dwork = k_work_delayable_from_work(work);
    struct kscan_he_data *data =
        CONTAINER_OF(dwork, struct kscan_he_data, adc_read_work);
    kscan_he_read(data->dev);
}

static void kscan_adc_calibrate_work_handler(struct k_work *work) {
    struct k_work_delayable *dwork = k_work_delayable_from_work(work);
    struct kscan_he_data *data =
        CONTAINER_OF(dwork, struct kscan_he_data, adc_calibration_work);

    const struct device *dev = data->dev;

    const struct kscan_he_config *conf = dev->config;
    for(int i = 0; i < conf->group_count; i++){
        data->adc_groups[i].as.calibrate = true;
    }
    k_work_reschedule(&data->adc_read_work, K_TIMEOUT_ABS_MS(CONFIG_HE_ADC_CALIBRATION_DELAY));
}

static void kscan_he_pulse_set(const struct device *dev, bool pulse_enable){
    // const struct kscan_he_config *conf = dev->config;
    struct kscan_he_data *data = dev->data;
    if(data->pulse_enabled==pulse_enable){
        return;
    }
    data->pulse_enabled=pulse_enable;
    if(pulse_enable){
        set_all_gpio(dev, 0);
    }else{
        set_all_gpio(dev, 1);
    }
}

// driver init function
static int kscan_he_init(const struct device *dev) {
    struct kscan_he_data *data = dev->data;
    const struct kscan_he_config *conf = dev->config;
    // TODO this shouldn't modify the config struct, it would be better to copy
    // input_adcs to data and modify that (it wouldn't really change anything
    // but it would look better)
    // kscan_adc_list_sort_by_channel(&conf->input_adcs);
    if(conf->calibrate){
        LOG_WRN("Calibration enabled, input will be sent to serial and will not generate input events");
    }
    data->dev = dev;
    data->scan_time = k_uptime_get();
    data->pulse_enabled=conf->pulse_read;
    for (int i = 0; i < conf->group_count; i++) {
        for (int8_t channel_ord = 0; channel_ord < conf->he_groups[i].key_count;
             channel_ord++) {
            int8_t channel_id =
                conf->he_groups[i].keys[channel_ord].adc.channel_id;
            data->adc_groups[i].key_channels[channel_id] = channel_ord;
        }
        // kscan_adc_sort_keys_by_channel(&conf->he_groups[i]); //FIXME conf is const so it shouldn't be modified
        data->adc_groups[i].as = (struct adc_sequence){
            .buffer = data->adc_groups[i].adc_buffer,
            .buffer_size = sizeof(int16_t) * (conf->he_groups[i].key_count),
            .calibrate = false,
            .channels = 0,
            .options = NULL,
            .oversampling = 0,
            .resolution = conf->resolution};
    }
    data->polyfit = malloc(conf->n_coeffs * sizeof(float));
    for (int i = 0; i < conf->n_coeffs; i++) {
        int32_t coeff_i = conf->polyfit_int32[i];
        float *coeff_f = (float *)&coeff_i;
        data->polyfit[i] = *coeff_f;
    }
    if(conf->kscan_forwarder){
        int err = kscan_forwarder_config_pulse_set(conf->kscan_forwarder, kscan_he_pulse_set);
        if (err) {
            LOG_ERR("Error during forwarder config_pulse_set: %d", err);
            return err;
        }
    }
    // init kwork
    k_work_init_delayable(&data->adc_read_work, kscan_adc_read_work_handler);
    k_work_init_delayable(&data->adc_calibration_work, kscan_adc_calibrate_work_handler);

#if IS_ENABLED(CONFIG_PM_DEVICE)
    pm_device_init_suspended(dev);

#if IS_ENABLED(CONFIG_PM_DEVICE_RUNTIME)
    pm_device_runtime_enable(dev);
#endif

#else
    LOG_INF("CONFIG_PM_DEVICE_RUNTIME disabled");
    int err = setup_pins(dev);
    if (err) {
        LOG_ERR("Error during pins setup: %d", err);
        return err;
    }
#endif

    return 0;
}

#if IS_ENABLED(CONFIG_PM_DEVICE)

// power management function
static int kscan_he_pm_action(const struct device *dev,
                              enum pm_device_action action) {
    switch (action) {
    case PM_DEVICE_ACTION_SUSPEND:
        return kscan_he_disable(dev);
    case PM_DEVICE_ACTION_RESUME:
        kscan_adc_setup_pins(dev) return kscan_he_enable(dev);
    default:
        return -ENOTSUP;
    }
}

#endif // IS_ENABLED(CONFIG_PM_DEVICE)

// config function
static int kscan_he_configure(const struct device *dev,
                              const kscan_callback_t callback) {
    struct kscan_he_data *data = dev->data;
    const struct kscan_he_config *conf = dev->config;
    if (!callback) {
        return -EINVAL;
    }
    data->callback = callback;
    if (conf->kscan_forwarder){
        int err = kscan_forwarder_config(conf->kscan_forwarder, callback, dev);
        if (err) {
            LOG_ERR("Error during forwarder config: %d", err);
            return err;
        }
    }

    return 0;
}

// enable function
static int kscan_he_enable(const struct device *dev) {
    LOG_INF("kscan adc enabled");
    struct kscan_he_data *data = dev->data;
    // const struct kscan_he_config *conf = dev->config;
    data->scan_time = k_uptime_get();
    if (!data->pulse_enabled) {
        set_all_gpio(dev, 1);
    }
    // calibrate once before use
    kscan_adc_calibrate_work_handler(&data->adc_calibration_work.work);

    return kscan_he_read(dev);
}

// disable function
static int kscan_he_disable(const struct device *dev) {
    LOG_INF("kscan adc disabled");
    struct kscan_he_data *data = dev->data;
    k_work_cancel_delayable(&data->adc_read_work);
    set_all_gpio(dev, 0);
    return 0;
}

// kscan api struct
static const struct kscan_driver_api kscan_he_api = {
    .config = kscan_he_configure,
    .enable_callback = kscan_he_enable,
    .disable_callback = kscan_he_disable,
};

// #define ZEROS(node_id) 0

#define CHILD_COUNT(node_id) 1

#define IMPLY(cond1, cond2) (!(cond1) || (cond2))

#define KSCAN_GROUP_DATA_INIT(node_id, inst_id)                                \
    (struct kscan_adc_group_data) {                                     \
        .adc_buffer = adc_buffer_##inst_id##_##node_id,                        \
        .key_channels = key_channels_##inst_id##_##node_id                     \
    }

#define ADC_DT_SPEC_STRUCT_1(ctlr, input)                                      \
    {.dev = DEVICE_DT_GET(ctlr),                                               \
     .channel_id = input,                                                      \
     ADC_CHANNEL_CFG_FROM_DT_NODE(DT_CHILD(ctlr, DT_CAT(channel_, input)))}    \
    /* stupid fucking workaround*/

#define ADC_DT_SPEC_GET_1(node_id)                                             \
    ADC_DT_SPEC_STRUCT_1(DT_IO_CHANNELS_CTLR(node_id),                         \
                         DT_IO_CHANNELS_INPUT(node_id))

#define KSCAN_KEY_INIT(node_id)                                                \
    (const struct kscan_he_key_cfg) {                                                \
        .adc = ADC_DT_SPEC_GET_1(node_id),                                     \
        .deadzone_top = DT_PROP(node_id, deadzone_top),                         \
        .deadzone_bottom = DT_PROP(node_id, deadzone_bottom),                    \
        .calibration_min = DT_PROP(node_id, calibration_min),                  \
        .calibration_max = DT_PROP(node_id, calibration_max),                  \
    }

#define KSCAN_GROUP_INIT(node_id, inst_id)                                     \
    (const struct kscan_he_group_cfg) {                                              \
        .enable_gpio = GPIO_DT_SPEC_GET_OR(                                    \
            node_id, enable_gpios, ({.port = NULL, .dt_flags = 0, .pin = 0})), \
        .switch_pressed_is_higher =                                            \
            DT_PROP(node_id, switch_pressed_is_higher),                        \
        .switch_height = DT_PROP(node_id, switch_height),                      \
        .key_count = DT_FOREACH_CHILD_SEP(node_id, CHILD_COUNT, (+)),          \
        .keys = keys_##inst_id##_##node_id                                     \
    }

#define GROUP_ALLOC(node_id, inst_id)                                          \
    BUILD_ASSERT(IMPLY(DT_INST_PROP(inst_id, pulse_read),                      \
                       DT_PROP_LEN_OR(node_id, enable_gpios, 0) > 0),          \
                 "enable-gpios needs to be defined if pulse-read is enabled"); \
    static int16_t adc_buffer_##inst_id##_##node_id[DT_FOREACH_CHILD_SEP(      \
        node_id, CHILD_COUNT, (+))] = {0};                                     \
    static int8_t key_channels_##inst_id##_##node_id[KSCAN_ADC_MAX_CHANNELS] = \
        {0};                                                                   \
    static struct kscan_he_key_cfg keys_##inst_id##_##node_id[] = {            \
        DT_FOREACH_CHILD_SEP(node_id, KSCAN_KEY_INIT, (, ))};

#define KSCAN_HE_INIT(n)                                                       \
    DT_INST_FOREACH_CHILD_VARGS(n, GROUP_ALLOC, n)                             \
    static const struct kscan_he_group_cfg kscan_he_group_cfg_##n[] = {        \
        DT_INST_FOREACH_CHILD_SEP_VARGS(n, KSCAN_GROUP_INIT, (, ), n)};        \
    static struct kscan_adc_group_data kscan_adc_group_data_##n[] = {          \
        DT_INST_FOREACH_CHILD_SEP_VARGS(n, KSCAN_GROUP_DATA_INIT, (, ), n)};   \
    static struct kscan_he_data kscan_he_data_##n = {                          \
        .adc_groups = kscan_adc_group_data_##n};                               \
    static const struct kscan_he_config kscan_he_config_##n = {                \
        .resolution = DT_INST_PROP(n, resolution),                             \
        .pulse_read = DT_INST_PROP(n, pulse_read),                             \
        .read_turn_on_time = DT_INST_PROP(n, read_turn_on_time),               \
        .wait_period_idle = DT_INST_PROP(n, wait_period_idle),                 \
        .wait_period_press = DT_INST_PROP(n, wait_period_press),               \
        .group_count = DT_INST_FOREACH_CHILD_SEP(n, CHILD_COUNT, (+)),         \
        .he_groups = kscan_he_group_cfg_##n,                                   \
        .kscan_forwarder = DEVICE_DT_GET_OR_NULL(DT_INST_PHANDLE(n, kscan_forwarder)), \
        .calibrate = DT_INST_PROP(n, calibrate),                              \
        .n_coeffs = DT_INST_PROP_LEN(n, polyfit),                  \
        .polyfit_int32 = {DT_INST_FOREACH_PROP_ELEM_SEP(                       \
            n, polyfit, DT_PROP_BY_IDX, (, ))},                                 \
    };                                                                         \
    PM_DEVICE_DT_INST_DEFINE(n, kscan_he_pm_action);                           \
    DEVICE_DT_INST_DEFINE(n, &kscan_he_init, PM_DEVICE_DT_INST_GET(n),         \
                          &kscan_he_data_##n, &kscan_he_config_##n,            \
                          POST_KERNEL, CONFIG_KSCAN_INIT_PRIORITY,             \
                          &kscan_he_api);

DT_INST_FOREACH_STATUS_OKAY(KSCAN_HE_INIT)