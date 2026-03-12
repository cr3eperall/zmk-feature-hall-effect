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
#include "adc_multiplexer.h"
// #include <zephyr/sys/util.h>

#ifndef CONFIG_HE_ADC_CALIBRATION_DELAY
#define CONFIG_HE_ADC_CALIBRATION_DELAY 2000000
#endif

#ifndef HE_LOG_REGISTERED
#define HE_LOG_REGISTERED
    LOG_MODULE_REGISTER(feature_hall_effect, CONFIG_HE_LOG_LEVEL);
#else
    LOG_MODULE_DECLARE(feature_hall_effect, CONFIG_HE_LOG_LEVEL);
#endif

#define DT_DRV_COMPAT he_kscan_multiplexer

static int init_adc(const struct device *dev) {
    const struct kscan_he_mux_config *conf = dev->config;
    struct kscan_he_mux_data *data = dev->data;
    // volatile void *api = conf->adc_groups[0].keys[0].adc.dev->api;
    data->as.channels = 0;
    for (int i = 0; i < conf->group_count; i++) {
        const struct adc_dt_spec *adc = &conf->he_groups[i].adc_pin;
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
        data->as.channels |= BIT(adc->channel_id);
        LOG_INF("Configured ADC %u on %s for output", adc->channel_id,
                adc->dev->name);
    }

    return 0;
}

static int init_gpio(const struct device *dev) {
    const struct kscan_he_mux_config *conf = dev->config;
    struct kscan_he_mux_data *data = dev->data;
    for (int i = 0; i < conf->gpio_count; i++) {
        const struct gpio_dt_spec gpio = conf->address_gpios[i];
        if (gpio.port == NULL) {
            continue;
        }
        if (!device_is_ready(gpio.port)) {
            LOG_ERR("GPIO is not ready: %s", gpio.port->name);
            return -ENODEV;
        }
        int err;
        err = gpio_pin_configure_dt(&gpio, GPIO_OUTPUT | GPIO_OUTPUT_INIT_LOW);
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

static int set_address(const struct device *dev, int16_t value) {
    const struct kscan_he_mux_config *conf = dev->config;
    if(value > (1 << conf->gpio_count)-1 || value < 0){
        LOG_ERR("Address %i out of range (max %i)", value, (1 << conf->gpio_count)-1);
        return -EINVAL;
    }
    for (int i = 0; i < conf->gpio_count; i++) {
        const struct gpio_dt_spec gpio = conf->address_gpios[i];
        if (gpio.port == NULL) {
            continue;
        }
        int err = gpio_pin_set_dt(&gpio, value & 1);
        if (err) {
            LOG_ERR("Failed to set output %i to %i: %i", gpio.pin, value & 1, err);
            return err;
        }
        value >>= 1;
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
    const struct kscan_he_mux_config *conf = dev->config;
    struct kscan_he_mux_data *data = dev->data;

    data->scan_time += conf->wait_period_press;

    k_work_reschedule(&data->adc_read_work, K_TIMEOUT_ABS_MS(data->scan_time));
}

static void kscan_he_read_end(const struct device *dev) {
    struct kscan_he_mux_data *data = dev->data;
    const struct kscan_he_mux_config *conf = dev->config;

    data->scan_time += conf->wait_period_idle;

    // Return to polling slowly.
    k_work_reschedule(&data->adc_read_work, K_TIMEOUT_ABS_MS(data->scan_time));
}

static bool parse_adc_buffer(const struct device *dev, int16_t address){
    const struct kscan_he_mux_config *conf = dev->config;
    struct kscan_he_mux_data *data = dev->data;
    bool pressed=false;
    int64_t now = conf->calibrate ? k_uptime_get() : 0;
    int16_t buf[conf->group_count];
    memset(buf, 0, sizeof(buf));
    int8_t buffer_idx=0;
    for (int8_t channel = 0; channel < KSCAN_ADC_MAX_CHANNELS; channel++) {
        int8_t channel_mask = BIT(channel);
        if(!(data->as.channels & channel_mask)){
            continue;
        }
        const int8_t key_i = data->key_channels[channel];
        int16_t key_j=address- conf->he_groups[key_i].address_range_min;
        if(key_j<0 || conf->he_groups[key_i].address_range_max - key_j<0){
            continue;
        }
        const int16_t max_height = conf->he_groups[key_i].switch_height;
        const int16_t raw_adc_value = data->adc_buffer[buffer_idx];
        
        if(!conf->calibrate){
            int16_t key_height =
                kscan_mux_adc_get_mapped_height(dev, key_i, address, raw_adc_value);
            const int16_t deadzone_top =
                kscan_mux_adc_cfg_deadzone_top(dev, key_i, address);
            const int16_t deadzone_bottom =
                kscan_mux_adc_cfg_deadzone_bottom(dev, key_i, address);
            if(key_height<0) key_height = 0;
            if(key_height > max_height) key_height = max_height;
            if(key_height <= max_height - deadzone_top && key_height > deadzone_bottom){
                pressed = true;
            }
            // send sync only at the last key
            // FIXME: this sends the sync early if the adc pins are not ordered
            input_report(dev, INPUT_EV_HE, INPUT_HE_RC(key_i, address), key_height, (key_i==conf->group_count-1 && address == conf->he_groups[key_i].address_range_max), K_FOREVER); //TODO check if timeout is needed
        }else{
            pressed = true;
            buf[key_i] = raw_adc_value;
        }
        buffer_idx++;
    }
    if (conf->calibrate) {
        char cbuf[conf->group_count * 4 + 1];
        for (int i = 0; i < conf->group_count; i++) {
            sprintf(cbuf + i * 4, "%04d", buf[i]);
        }
        cbuf[conf->group_count * 4] = '\0';
        printk("~%lld,%d:%s\n", now, address, cbuf);
    }
    return pressed;
}

static int kscan_he_read(const struct device *dev) {
    const struct kscan_he_mux_config *conf = dev->config;
    struct kscan_he_mux_data *data = dev->data;
    // k_msleep(3000);
    // LOG_INF("pin high");
    bool pressed = false;
    for (int i = data->global_min_address; i<=data->global_max_address; i++) {

        int err = set_address(dev, i);
        if (err) {
            LOG_ERR("Failed to set address %i: %i", i, err);
            return err;
        }

        k_usleep(conf->address_to_read_delay);

        err = adc_read(conf->he_groups[0].adc_pin.dev, &data->as);

        if(data->as.calibrate){
            data->as.calibrate = false;
        }

        if (err) {
            LOG_ERR("ADC READ ERROR %d", err);
        }

        pressed=parse_adc_buffer(dev, i) || pressed;
    }
    int err = set_address(dev, 0);
    if (err) {
        LOG_ERR("Failed to reset address: %i", err);
        return err;
    }
    
    if (pressed) {
        kscan_he_read_continue(dev);
    } else {
        kscan_he_read_end(dev);
    }

    return 0;
}

static void kscan_adc_read_work_handler(struct k_work *work) {
    struct k_work_delayable *dwork = k_work_delayable_from_work(work);
    struct kscan_he_mux_data *data =
        CONTAINER_OF(dwork, struct kscan_he_mux_data, adc_read_work);
    kscan_he_read(data->dev);
}

static void kscan_adc_calibrate_work_handler(struct k_work *work) {
    struct k_work_delayable *dwork = k_work_delayable_from_work(work);
    struct kscan_he_mux_data *data =
        CONTAINER_OF(dwork, struct kscan_he_mux_data, adc_calibration_work);

    const struct device *dev = data->dev;

    const struct kscan_he_mux_config *conf = dev->config;
    data->as.calibrate = true;
    k_work_reschedule(&data->adc_read_work, K_TIMEOUT_ABS_MS(CONFIG_HE_ADC_CALIBRATION_DELAY));
}

// driver init function
static int kscan_he_init(const struct device *dev) {
    struct kscan_he_mux_data *data = dev->data;
    const struct kscan_he_mux_config *conf = dev->config;
    if(conf->calibrate){
        LOG_WRN("Calibration enabled, input will be sent to serial and will not generate input events");
    }
    data->dev = dev;
    data->scan_time = k_uptime_get();
    for (int8_t channel_ord = 0; channel_ord < conf->group_count; channel_ord++) {
        int8_t channel_id = conf->he_groups[channel_ord].adc_pin.channel_id;
        data->key_channels[channel_id] = channel_ord;
    }
    //print key channels
    for (int i = 0; i < KSCAN_ADC_MAX_CHANNELS; i++) {
        if (data->key_channels[i] >= 0) {
            LOG_DBG("Key channel %d -> key %d", i, data->key_channels[i]);
        }
    }
    for (int i = 0; i < conf->group_count; i++) {
        data->as = (struct adc_sequence){
            .buffer = data->adc_buffer,
            .buffer_size = sizeof(int16_t) * (conf->group_count),
            .calibrate = false,
            .channels = 0,
            .options = NULL,
            .oversampling = 0,
            .resolution = conf->resolution};
    }
    data->polyfit = malloc(conf->n_coeffs * sizeof(float));
    for (int i = 0; i < conf->n_coeffs; i++) { //TODO could use a lookup table to save some processing time
        int32_t coeff_i = conf->polyfit_int32[i];
        float *coeff_f = (float *)&coeff_i;
        data->polyfit[i] = *coeff_f;
    }
    data->global_max_address=0;
    data->global_min_address=(1 << conf->group_count) -1;
    data->group_data = malloc(conf->group_count * sizeof(struct kscan_he_mux_group_data));
    for (int i = 0; i < conf->group_count; i++) {
        int16_t addr_min = conf->he_groups[i].address_range_min;
        int16_t addr_max = conf->he_groups[i].address_range_max;
        data->global_min_address=MIN(data->global_min_address, addr_min);
        data->global_max_address=MAX(data->global_max_address, addr_max);
        int16_t key_count = addr_max - addr_min + 1;
        data->group_data[i].keys= malloc(key_count * sizeof(struct kscan_he_mux_key_cfg));
        for(int j =0; j<key_count; j++){
            if(j<conf->he_groups[i].key_count){
                data->group_data[i].keys[j]=conf->he_groups[i].key_cfg[j];
            }else{
                data->group_data[i].keys[j].deadzone_top=conf->default_deadzone_top;
                data->group_data[i].keys[j].deadzone_bottom= conf->default_deadzone_bottom;
                data->group_data[i].keys[j].calibration_max=conf->default_calibration_max;
                data->group_data[i].keys[j].calibration_min= conf->default_calibration_min;
            }
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

// config function
static int kscan_he_configure(const struct device *dev,
                              const kscan_callback_t callback) {
    struct kscan_he_mux_data *data = dev->data;
    const struct kscan_he_mux_config *conf = dev->config;
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
    struct kscan_he_mux_data *data = dev->data;
    // const struct kscan_he_config *conf = dev->config;
    data->scan_time = k_uptime_get();

    // calibrate once before use
    kscan_adc_calibrate_work_handler(&data->adc_calibration_work.work);

    return kscan_he_read(dev);
}

// disable function
static int kscan_he_disable(const struct device *dev) {
    LOG_INF("kscan adc disabled");
    struct kscan_he_mux_data *data = dev->data;
    k_work_cancel_delayable(&data->adc_read_work);
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
        setup_pins(dev);
        return kscan_he_enable(dev);
    default:
        return -ENOTSUP;
    }
}

#endif // IS_ENABLED(CONFIG_PM_DEVICE)

// kscan api struct
static const struct kscan_driver_api kscan_he_api = {
    .config = kscan_he_configure,
    .enable_callback = kscan_he_enable,
    .disable_callback = kscan_he_disable,
};

// #define ZEROS(node_id) 0

#define CHILD_COUNT(node_id) 1

#define IMPLY(cond1, cond2) (!(cond1) || (cond2))

// #define KSCAN_GROUP_DATA_INIT(node_id, inst_id)                                \
//     (struct kscan_he_group_data) {                                     \
//         .adc_buffer = adc_buffer_##inst_id##_##node_id,                        \
//         .key_channels = key_channels_##inst_id##_##node_id                     \
//     }

#define ADC_DT_SPEC_STRUCT_1(ctlr, input)                                      \
    {.dev = DEVICE_DT_GET(ctlr),                                               \
     .channel_id = input,                                                      \
     ADC_CHANNEL_CFG_FROM_DT_NODE(DT_CHILD(ctlr, DT_CAT(channel_, input)))}    \
    /* stupid fucking workaround*/

#define ADC_DT_SPEC_GET_1(node_id)                                             \
    ADC_DT_SPEC_STRUCT_1(DT_IO_CHANNELS_CTLR(node_id),                         \
                         DT_IO_CHANNELS_INPUT(node_id))

#define KSCAN_KEY_INIT(node_id)                                                \
    (const struct kscan_he_mux_key_cfg) {                                                \
        .deadzone_top = DT_PROP(node_id, deadzone_top),                         \
        .deadzone_bottom = DT_PROP(node_id, deadzone_bottom),                    \
        .calibration_min = DT_PROP(node_id, calibration_min),                  \
        .calibration_max = DT_PROP(node_id, calibration_max)                  \
    }

#define KSCAN_GROUP_INIT(node_id, inst_id)                                                                                    \
    (const struct kscan_he_mux_group_cfg) {                                              \
        .key_cfg = keys_##inst_id##_##node_id,                                   \
        .key_count = ARRAY_SIZE(keys_##inst_id##_##node_id),               \
        .adc_pin = ADC_DT_SPEC_GET_1(node_id), \
        .switch_pressed_is_higher =                                            \
            DT_PROP(node_id, switch_pressed_is_higher),                        \
        .address_range_min = DT_PROP(node_id, address_range_min),              \
        .address_range_max = DT_PROP(node_id, address_range_max),              \
        .switch_height = DT_PROP(node_id, switch_height)                      \
    }

#define GROUP_ALLOC(node_id, inst_id)                                          \
        BUILD_ASSERT(DT_PROP(node_id, address_range_max) >=                             \
                     DT_PROP(node_id, address_range_min),                      \
                 "address_range_max must be >= address_range_min");            \
    BUILD_ASSERT(DT_PROP(node_id, address_range_min)>=0, "address_range_min must be >= 0"); \
    BUILD_ASSERT(DT_PROP(node_id, address_range_max) < (1 << DT_INST_PROP_LEN(inst_id, address_gpios)), "address_range_max must be < 2^count(address_gpios)"); \
    static struct kscan_he_mux_key_cfg keys_##inst_id##_##node_id[] = {            \
        DT_FOREACH_CHILD_SEP_VARGS(node_id, KSCAN_KEY_INIT, (, ))}; 

#define KSCAN_HE_INIT(n)                                                       \
    DT_INST_FOREACH_CHILD_VARGS(n, GROUP_ALLOC, n)                             \
    static const struct kscan_he_mux_group_cfg kscan_he_group_cfg_##n[] = {        \
        DT_INST_FOREACH_CHILD_SEP_VARGS(n, KSCAN_GROUP_INIT, (, ), n)};        \
    static int16_t adc_buffer_##inst_id[DT_INST_FOREACH_CHILD_SEP(      \
        n, CHILD_COUNT, (+))] = {0};                                     \
    static int8_t key_channels_##inst_id[KSCAN_ADC_MAX_CHANNELS] = \
        {0};                                                       \
    static struct kscan_he_mux_data kscan_he_data_##n = {                          \
        .key_channels = key_channels_##inst_id,                                  \
        .adc_buffer = adc_buffer_##inst_id,                                      \
    };                                                                         \
    static struct gpio_dt_spec address_gpios_##n[] = {DT_INST_FOREACH_PROP_ELEM_SEP(    \
        n, address_gpios, GPIO_DT_SPEC_GET_BY_IDX, (, )) \
    }; \
    static const struct kscan_he_mux_config kscan_he_config_##n = {                \
        .resolution = DT_INST_PROP(n, resolution),                             \
        .gpio_count = DT_INST_PROP_LEN(n, address_gpios),                     \
        .address_gpios = address_gpios_##n,                                  \
        .address_to_read_delay = DT_INST_PROP(n, address_to_read_delay),       \
        .wait_period_idle = DT_INST_PROP(n, wait_period_idle),                 \
        .wait_period_press = DT_INST_PROP(n, wait_period_press),               \
        .group_count = DT_INST_FOREACH_CHILD_SEP(n, CHILD_COUNT, (+)),         \
        .he_groups = kscan_he_group_cfg_##n,                                   \
        .kscan_forwarder = DEVICE_DT_GET_OR_NULL(DT_INST_PHANDLE(n, kscan_forwarder)), \
        .default_calibration_min = DT_INST_PROP(n, default_calibration_min), \
        .default_calibration_max = DT_INST_PROP(n, default_calibration_max),   \
        .default_deadzone_top = DT_INST_PROP(n, default_deadzone_top),       \
        .default_deadzone_bottom = DT_INST_PROP(n, default_deadzone_bottom), \
        .calibrate = DT_INST_PROP(n, calibrate),                              \
        .n_coeffs = DT_INST_PROP_LEN(n, polyfit),                              \
        .polyfit_int32 = {DT_INST_FOREACH_PROP_ELEM_SEP(                       \
            n, polyfit, DT_PROP_BY_IDX, (, ))}                                 \
    };                                                                         \
    PM_DEVICE_DT_INST_DEFINE(n, kscan_he_pm_action);                           \
    DEVICE_DT_INST_DEFINE(n, &kscan_he_init, PM_DEVICE_DT_INST_GET(n),         \
                          &kscan_he_data_##n, &kscan_he_config_##n,            \
                          POST_KERNEL, CONFIG_KSCAN_INIT_PRIORITY,             \
                          &kscan_he_api);

DT_INST_FOREACH_STATUS_OKAY(KSCAN_HE_INIT)