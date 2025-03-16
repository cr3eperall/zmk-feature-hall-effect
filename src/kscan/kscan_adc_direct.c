
#include "adc.h"

#include <stdlib.h>
#include <zephyr/device.h>
#include <zephyr/drivers/kscan.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/pm/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
// #include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(kscan_adc_direct, CONFIG_LOG_MAX_LEVEL); // TODO change name

#define DT_DRV_COMPAT zmk_kscan_adc_direct

#define KSCAN_ADC_GET_BY_IDX(node_id, idx)                                                  \
    ((struct kscan_adc){.spec = ADC_DT_SPEC_GET_BY_IDX(node_id, idx), .index = idx})

#define KSCAN_ADC_CFG_INIT(idx, inst_idx)                                                     \
    KSCAN_ADC_GET_BY_IDX(DT_DRV_INST(inst_idx), idx)

#define KSCAN_GPIO_CFG_INIT(idx, inst_idx)                                                     \
    KSCAN_GPIO_GET_BY_IDX(DT_DRV_INST(inst_idx), pwm_gpios, idx)


struct kscan_adc_data{
    const struct device *dev;
    struct adc_sequence as;
    int16_t *adc_buffer;
    struct adc_key_state *key_state_vec;
    kscan_callback_t callback;
    struct k_work_delayable work;
};

struct kscan_adc_config{
    struct kscan_adc_list input_adcs;
    bool pulse_read;
    const struct gpio_dt_spec enable_pin;
    int32_t resolution;
    int32_t sample_time;
    bool press_increases;
    int32_t read_turn_on_time;
    int32_t wait_period_idle;
    int32_t wait_period_press;
    int32_t on_treshold;
    int32_t off_treshold;
    int32_t calibration_min;
    int32_t calibration_max;
};

static int kscan_adc_init_adc(const struct device *dev){
    const struct kscan_adc_config *conf = dev->config;
    struct kscan_adc_data *data = dev->data;
    
    for(int i=0; i<conf->input_adcs.len; i++){
        const struct adc_dt_spec *adc = &conf->input_adcs.adcs[i].spec;
        if(!device_is_ready(adc->dev)){
            LOG_ERR("ADC is not ready: %s", adc->dev->name);
            return -ENODEV;
        }
        int err = adc_channel_setup_dt(adc);
        if (err) {
            LOG_ERR("Unable to configure ADC %u on %s for output", adc->channel_id, adc->dev->name);
            return err;
        }
        data->as.channels |= BIT(adc->channel_id);
        LOG_DBG("Configured ADC %u on %s for output", adc->channel_id, adc->dev->name);
    }

    return 0;
}

static int kscan_adc_init_gpio(const struct device *dev){
    const struct kscan_adc_config *conf = dev->config;

    const struct gpio_dt_spec *gpio = &conf->enable_pin;
    if(!device_is_ready(gpio->port)){
        LOG_ERR("PWM is not ready: %s", gpio->port->name);
        return -ENODEV;
    }
    int err = gpio_pin_configure_dt(gpio, GPIO_OUTPUT | GPIO_OUTPUT_INIT_LOW);
    if (err) {
        LOG_ERR("Unable to configure pin %u on %s for output", gpio->pin, gpio->port->name);
        return err;
    }
    LOG_DBG("Configured GPIO pin %u on %s for output", gpio->pin, gpio->port->name);
    
    return 0;
}

static int kscan_adc_setup_pins(const struct device *dev){
    int err = kscan_adc_init_adc(dev);
    if (err) {
        LOG_ERR("Error during ADC init: %d", err);
        return err;
    }
    err = kscan_adc_init_gpio(dev);
    if (err) {
        LOG_ERR("Error during GPIO init: %d", err);
        return err;
    }
    return 0;
}

static void kscan_adc_read_continue(const struct device *dev) {
    const struct kscan_adc_config *conf = dev->config;
    struct kscan_adc_data *data = dev->data;

    // data->scan_time += config->debounce_scan_period_ms;
    // TODO use constant scan time instead of wait time

    k_work_reschedule(&data->work, K_MSEC(conf->wait_period_press));
}

static void kscan_adc_read_end(const struct device *dev) {
    struct kscan_adc_data *data = dev->data;
    const struct kscan_adc_config *conf = dev->config;

    // data->scan_time += config->poll_period_ms;
    // TODO use constant scan time instead of wait time

    // Return to polling slowly.
    k_work_reschedule(&data->work, K_MSEC(conf->wait_period_idle));
}

static int kscan_adc_read(const struct device *dev){
    struct kscan_adc_config *conf= dev->config;
    struct kscan_adc_data *data= dev->data;
    // k_msleep(3000);
    // LOG_INF("pin high");
    int err = gpio_pin_set_dt(&conf->enable_pin, 1);
    if (err) {
        LOG_ERR("Failed to set output %i high: %i", conf->enable_pin.pin, err);
        return err;
    }
    // k_msleep(1000);
    // LOG_INF("busy wait");
    
    k_busy_wait(conf->read_turn_on_time);
    // k_msleep(1000);
    // LOG_INF("adc read");
    
    err = adc_read(conf->input_adcs.adcs[0].spec.dev, &data->as);
    if(err){
        LOG_ERR("ADC READ ERROR %d", err);
    }
    // k_msleep(1000);
    // LOG_INF("pin low");
    
    err = gpio_pin_set_dt(&conf->enable_pin, 0);
    if (err) {
        LOG_ERR("Failed to set output %i low: %i", conf->enable_pin.pin, err);
    }

    // TODO normalize adc results (invert if necessary)
    bool pressed=false;
    
    for(int i=0; i<conf->input_adcs.len; i++){
        LOG_INF("adc %d data: %hi", i, data->adc_buffer[i]);
        if(data->adc_buffer[i] >= conf->on_treshold){
            pressed=true;
            adc_key_state_update(&data->key_state_vec[i], true, data->adc_buffer[i]);
        }else if(data->adc_buffer[i] < conf->off_treshold){
            adc_key_state_update(&data->key_state_vec[i], false, data->adc_buffer[i]);
        }else{
            // keep old state
            pressed=true;
        }
    }

    for(int i=0; i<conf->input_adcs.len; i++){
        if(adc_key_state_has_changed(&data->key_state_vec[i])){
            const bool pressed = adc_key_state_is_pressed(&data->key_state_vec[i]);

            LOG_DBG("Sending event at channel %i state %s", i, pressed ? "on" : "off");
            data->callback(dev, 0, i, pressed);
        }
    }

    // LOG_INF("pressed: %i", pressed);
    // return 0;
    if(pressed){
        kscan_adc_read_continue(dev);
    }else{
        kscan_adc_read_end(dev);
    }

    return 0;
}

static void kscan_adc_work_handler(struct k_work *work) {
    struct k_work_delayable *dwork = k_work_delayable_from_work(work);
    struct kscan_adc_data *data = CONTAINER_OF(dwork, struct kscan_adc_data, work);
    kscan_adc_read(data->dev);
}

// driver init function
static int kscan_adc_direct_init(const struct device *dev){
    struct kscan_adc_data *data = dev->data;
    struct kscan_adc_config *conf = dev->config;
    // TODO this shouldn't modify the config struct, it would be better to copy input_adcs to data and modify that 
    // (it wouldn't really change anything but it would look better)
    kscan_adc_list_sort_by_channel(&conf->input_adcs);
    data->dev = dev;
    data->as=(struct adc_sequence){
        .buffer=data->adc_buffer,
        .buffer_size=sizeof(int16_t)*conf->input_adcs.len,
        .calibrate=false,
        .channels=0,
        .options=NULL,
        .oversampling=0,
        .resolution=conf->resolution
    };
    
    // init kwork
    k_work_init_delayable(&data->work, kscan_adc_work_handler);

#if IS_ENABLED(CONFIG_PM_DEVICE)
    pm_device_init_suspended(dev);

#if IS_ENABLED(CONFIG_PM_DEVICE_RUNTIME)
    pm_device_runtime_enable(dev);
#endif

#else
LOG_INF("CONFIG_PM_DEVICE_RUNTIME disabled");
    int err = kscan_adc_setup_pins(dev);
    if (err) {
        LOG_ERR("Error during pins setup: %d", err);
        return err;
    }
#endif

    return 0;
}

#if IS_ENABLED(CONFIG_PM_DEVICE)

//power management function
static int kscan_adc_direct_pm_action(const struct device *dev, enum pm_device_action action) {
    switch (action) {
    case PM_DEVICE_ACTION_SUSPEND:
        return kscan_adc_direct_disable(dev);
    case PM_DEVICE_ACTION_RESUME:
        return kscan_adc_direct_enable(dev);
    default:
        return -ENOTSUP;
    }
}

#endif // IS_ENABLED(CONFIG_PM_DEVICE)

// config function
static int kscan_adc_direct_configure(const struct device *dev, const kscan_callback_t callback) {
    struct kscan_adc_data *data = dev->data;

    if (!callback) {
        return -EINVAL;
    }

    data->callback = callback;
    return 0;
}

// enable function
static int kscan_adc_direct_enable(const struct device *dev) {
    LOG_INF("kscan adc enabled");
    return kscan_adc_read(dev);
}

// disable function
static int kscan_adc_direct_disable(const struct device *dev) {
    LOG_INF("kscan adc disabled");
    struct kscan_adc_data *data = dev->data;
    k_work_cancel_delayable(&data->work);
    return 0;
}

// kscan api struct
static const struct kscan_driver_api kscan_adc_direct_api = {
    .config = kscan_adc_direct_configure,
    .enable_callback = kscan_adc_direct_enable,
    .disable_callback = kscan_adc_direct_disable,
};

#define KSCAN_ADC_INIT(n)                                                                                                               \
BUILD_ASSERT(DT_INST_PROP(n, on_treshold) <=100 && DT_INST_PROP(n, on_treshold) >=0, "on-treshold is out of range (0-100)");            \
BUILD_ASSERT(DT_INST_PROP(n, off_treshold) <=100 && DT_INST_PROP(n, off_treshold) >=0, "off-treshold is out of range (0-100)");         \
    static struct kscan_adc kscan_adcs_##n[] = {                                                                                        \
        LISTIFY(DT_INST_PROP_LEN(n, io_channels), KSCAN_ADC_CFG_INIT, (, ), n)                                                          \
    };                                                                                                                                  \
    static int16_t adc_buffer_##n [DT_INST_PROP_LEN(n, io_channels)] = {0};                                                             \
    static struct adc_key_state key_state_vec_##n [DT_INST_PROP_LEN(n, io_channels)]= {0};                                              \
    static struct kscan_adc_data kscan_adc_data_##n={.adc_buffer = adc_buffer_##n, .key_state_vec = key_state_vec_##n};                 \
    static const struct kscan_adc_config kscan_adc_config_##n = {                                                                       \
        .input_adcs=KSCAN_ADC_LIST(kscan_adcs_##n),                                                                                     \
        .pulse_read=DT_INST_PROP(n, pulse_read),                                                                                        \
        .enable_pin=GPIO_DT_SPEC_GET_OR(DT_DRV_INST(n), enable_gpios, {}),                                                              \
        .resolution=DT_INST_PROP(n, resolution),                                                                                        \
        .sample_time=DT_INST_PROP(n, sample_time),                                                                                      \
        .press_increases=DT_INST_PROP(n, press_increases),                                                                              \
        .read_turn_on_time=DT_INST_PROP(n, read_turn_on_time),                                                                          \
        .wait_period_idle=DT_INST_PROP(n, wait_period_idle),                                                                            \
        .wait_period_press=DT_INST_PROP(n, wait_period_press),                                                                          \
        .on_treshold=DT_INST_PROP(n, on_treshold),                                                                                      \
        .off_treshold=DT_INST_PROP(n, off_treshold),                                                                                    \
        .calibration_min=DT_INST_PROP(n, calibration_min),                                                                              \
        .calibration_max=DT_INST_PROP(n, calibration_max),                                                                              \
    };                                                                                                                                  \
    PM_DEVICE_DT_INST_DEFINE(n, kscan_adc_direct_pm_action);                                                                            \
    DEVICE_DT_INST_DEFINE(n, &kscan_adc_direct_init, PM_DEVICE_DT_INST_GET(n), &kscan_adc_data_##n,                                     \
    &kscan_adc_config_##n, POST_KERNEL, CONFIG_KSCAN_INIT_PRIORITY,                                                                     \
    &kscan_adc_direct_api);
                                                                                                                                  
DT_INST_FOREACH_STATUS_OKAY(KSCAN_ADC_INIT);
