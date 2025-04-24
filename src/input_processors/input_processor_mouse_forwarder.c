#include <drivers/input_processor.h>
#include <stdlib.h>
#include <math.h>
#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <he/input-event-codes.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <dt-bindings/he/mouse_forwarder.h>

LOG_MODULE_REGISTER(input_processor_mouse_forwarder,
                    CONFIG_LOG_MAX_LEVEL);  // TODO change log level

#define DT_DRV_COMPAT he_input_processor_mouse_forwarder

struct mouse_forwarder_data {
    const struct device *dev;
    float scale_x;
    float scale_y;
    float scale_h_wheel;
    float scale_v_wheel;
    int64_t accumulation_start;
    float accumulator_x;
    int64_t acceleration_start_x;
    float accumulator_y;
    int64_t acceleration_start_y;
    float accumulator_h_wheel;
    int64_t acceleration_start_h_wheel;
    float accumulator_v_wheel;
    int64_t acceleration_start_v_wheel;
    int16_t last_values[MOUSE_CODE_COUNT];
    struct k_work_delayable work;
};
struct mouse_forwarder_config {
    int scale_x_int32;
    int scale_y_int32;
    int scale_h_wheel_int32;
    int scale_v_wheel_int32;
    int accumulation_time;
    int deadzone_bottom;
    int deadzone_top;
    int acceleration_exponent;
    int time_to_max_speed;
};

void update_acceleration_start(const struct device *dev){
    struct mouse_forwarder_data *data = dev->data;
    const struct mouse_forwarder_config *conf = dev->config;
    int limit = conf->deadzone_top;
    if(data->last_values[MOUSE_X_LEFT]<limit || data->last_values[MOUSE_X_RIGHT]<limit){
        if(data->acceleration_start_x==0){
            data->acceleration_start_x=k_uptime_get();
        }
    }else{
        data->acceleration_start_x=0;
    }
    if(data->last_values[MOUSE_Y_UP]<limit || data->last_values[MOUSE_Y_DOWN]<limit){
        if(data->acceleration_start_y==0){
            data->acceleration_start_y=k_uptime_get();
        }
    }else{
        data->acceleration_start_y=0;
    }
    if(data->last_values[MOUSE_H_WHEEL_LEFT]<limit || data->last_values[MOUSE_H_WHEEL_RIGHT]<limit){
        if(data->acceleration_start_h_wheel==0){
            data->acceleration_start_h_wheel=k_uptime_get();
        }
    }else{
        data->acceleration_start_h_wheel=0;
    }
    if(data->last_values[MOUSE_V_WHEEL_UP]<limit || data->last_values[MOUSE_V_WHEEL_DOWN]<limit){
        if(data->acceleration_start_v_wheel==0){
            data->acceleration_start_v_wheel=k_uptime_get();
        }
    }else{
        data->acceleration_start_v_wheel=0;
    }
}

float acceleration_mult(const struct device *dev, int64_t start_time){
    const struct mouse_forwarder_config *conf = dev->config;
    if(start_time==0){
        return 0.0;
    }
    int64_t now = k_uptime_get();
    int64_t move_duration = now - start_time;
    if(move_duration > conf->time_to_max_speed || conf->time_to_max_speed==0 || conf->acceleration_exponent==0){
        return 1.0;
    }
    
    float time_fraction = (float)move_duration / (float)conf->time_to_max_speed;
    return powf(time_fraction, conf->acceleration_exponent);
}

int accumulate(const struct device *dev, float value, int key_type){
    struct mouse_forwarder_data *data = dev->data;
    switch(key_type){
        case MOUSE_X_LEFT: // x left
            data->accumulator_x-=value*acceleration_mult(dev, data->acceleration_start_x);
            break;
        case MOUSE_Y_UP: // y up
            data->accumulator_y-=value*acceleration_mult(dev, data->acceleration_start_y);
            break;
        case MOUSE_X_RIGHT: // x right
            data->accumulator_x+=value*acceleration_mult(dev, data->acceleration_start_x);
            break;
        case MOUSE_Y_DOWN: // y down
            data->accumulator_y+=value*acceleration_mult(dev, data->acceleration_start_y);
            break;
        case MOUSE_H_WHEEL_LEFT: // h wheel left //TODO check if directions are correct
            data->accumulator_h_wheel-=value*acceleration_mult(dev, data->acceleration_start_h_wheel);
            break;
        case MOUSE_V_WHEEL_UP: // v wheel up
            data->accumulator_v_wheel-=value*acceleration_mult(dev, data->acceleration_start_v_wheel);
            break;
        case MOUSE_H_WHEEL_RIGHT: // h wheel right
            data->accumulator_h_wheel+=value*acceleration_mult(dev, data->acceleration_start_h_wheel);
            break;
        case MOUSE_V_WHEEL_DOWN: // v wheel down
            data->accumulator_v_wheel+=value*acceleration_mult(dev, data->acceleration_start_v_wheel);
            break;
        default:
            LOG_ERR("Unknown key type %d", key_type);
            return -EINVAL;
    }
    return 0;
}

bool must_sync(struct device *dev){
    struct mouse_forwarder_data *data = dev->data;
    const struct mouse_forwarder_config *conf = dev->config;
    int64_t now = k_uptime_get();
    if(data->accumulation_start == 0){
        data->accumulation_start = now;
    }
    if (now - data->accumulation_start >= conf->accumulation_time) {
        data->accumulation_start = 0;
        return data->accumulator_x!=0.0 || data->accumulator_y!=0.0 || data->accumulator_h_wheel!=0.0 || data->accumulator_v_wheel!=0.0;
    }
    return false;
}

void send_reports(struct device *dev){
    struct mouse_forwarder_data *data = dev->data;
    int32_t value;
    if(data->accumulator_h_wheel!=0.0){
        value= (int32_t)roundf(data->accumulator_h_wheel*data->scale_h_wheel);
        input_report(dev, INPUT_EV_REL, INPUT_REL_HWHEEL, value, 0, K_NO_WAIT);
    }
    if(data->accumulator_v_wheel!=0.0){
        value= (int32_t)roundf(data->accumulator_v_wheel*data->scale_v_wheel);
        input_report(dev, INPUT_EV_REL, INPUT_REL_WHEEL, value, 0, K_NO_WAIT);
    }
    value= (int32_t)roundf(data->accumulator_x*data->scale_x);
    input_report(dev, INPUT_EV_REL, INPUT_REL_X, value, 0, K_NO_WAIT);
    value= (int32_t)roundf(data->accumulator_y*data->scale_y);
    input_report(dev, INPUT_EV_REL, INPUT_REL_Y, value, 1, K_NO_WAIT);
    data->accumulator_x=0.0;
    data->accumulator_y=0.0;
    data->accumulator_h_wheel=0.0;
    data->accumulator_v_wheel=0.0;
}

float rescale_working_area(const struct device *dev,int32_t event_value){
    const struct mouse_forwarder_config *conf = dev->config;
    float scaled= 1.0-((float)(event_value-conf->deadzone_bottom))/((float)(conf->deadzone_top-conf->deadzone_bottom));
    scaled=CLAMP(scaled,0.0,1.0);
    return scaled;
}

static void send_report_work_handler(struct k_work *work) {
    struct k_work_delayable *dwork = k_work_delayable_from_work(work);
    struct mouse_forwarder_data *data = CONTAINER_OF(dwork, struct mouse_forwarder_data, work);
    bool active=false;
    update_acceleration_start(data->dev);
    for(int i=0; i<MOUSE_CODE_COUNT; i++){
        float scaled=rescale_working_area(data->dev,data->last_values[i]);
        if(scaled>0.0){
            active=true;
            accumulate(data->dev, scaled, i);
        }
    }
    active = active || data->accumulator_x!=0.0 || data->accumulator_y!=0.0 || data->accumulator_h_wheel!=0.0 || data->accumulator_v_wheel!=0.0;

    if(must_sync(data->dev)){
        send_reports(data->dev);
    }
    if(active){
        k_work_reschedule(&data->work, K_MSEC(1));
    }
}

static int mf_handle_event(const struct device *dev,
                               struct input_event *event, uint32_t key_type,
                               uint32_t param2,
                               struct zmk_input_processor_state *state) {
    struct mouse_forwarder_data *data = dev->data;
    const struct mouse_forwarder_config *conf = dev->config;
    if (event->type != INPUT_EV_HE) return ZMK_INPUT_PROC_CONTINUE;
    int output_event_code=0;
    float scaled=rescale_working_area(dev,event->value);
    if(key_type>= MOUSE_CODE_COUNT){
        LOG_ERR("Unknown key type %d", key_type);
        return -EINVAL;
    }
    data->last_values[key_type]=event->value;
    if(must_sync(dev)){
        send_reports(dev);
    }
    k_work_reschedule(&data->work, K_MSEC(1));
    return ZMK_INPUT_PROC_STOP;
}

static int mf_init(const struct device *dev) {
    struct mouse_forwarder_data *data = dev->data;
    const struct mouse_forwarder_config *conf = dev->config;
    data->dev=dev;
    
    float *scale_x = (float *)&conf->scale_x_int32;
    float *scale_y = (float *)&conf->scale_y_int32;
    float *scale_h_wheel = (float *)&conf->scale_h_wheel_int32;
    float *scale_v_wheel = (float *)&conf->scale_v_wheel_int32;
    data->scale_x=*scale_x;
    data->scale_y=*scale_y;
    data->scale_h_wheel=*scale_h_wheel;
    data->scale_v_wheel=*scale_v_wheel;
    data->accumulation_start=0;
    data->accumulator_x=0.0;
    data->acceleration_start_x=0;
    data->accumulator_y=0.0;
    data->acceleration_start_y=0;
    data->accumulator_h_wheel=0.0;
    data->acceleration_start_h_wheel=0;
    data->accumulator_v_wheel=0.0;
    data->acceleration_start_v_wheel=0;
    for(int i=0; i<MOUSE_CODE_COUNT; i++){
        data->last_values[i]=10000;
    }
    k_work_init_delayable(&data->work, &send_report_work_handler);
    return 0;
}

static struct zmk_input_processor_driver_api processor_api = {
    .handle_event = mf_handle_event,
};

#define MF_INIT(n)      \
    BUILD_ASSERT(DT_INST_PROP_LEN(n, working_area) == 2, \
                 "Working area must contain 2 values"); \
    BUILD_ASSERT(DT_INST_PROP_BY_IDX(n, working_area, 0) < DT_INST_PROP_BY_IDX(n, working_area, 1), \
                 "Working area bottom must be less than top"); \
    static const struct mouse_forwarder_config mf_config_##n = { \
        .scale_x_int32 = DT_INST_PROP(n, scale_x_float), \
        .scale_y_int32 = DT_INST_PROP(n, scale_y_float), \
        .scale_h_wheel_int32 = DT_INST_PROP(n, scale_h_wheel_float), \
        .scale_v_wheel_int32 = DT_INST_PROP(n, scale_v_wheel_float), \
        .accumulation_time = DT_INST_PROP(n, accumulation_time), \
        .deadzone_bottom = DT_INST_PROP_BY_IDX(n, working_area, 0), \
        .deadzone_top = DT_INST_PROP_BY_IDX(n, working_area, 1), \
        .acceleration_exponent= DT_INST_PROP(n, acceleration_exponent), \
        .time_to_max_speed= MAX(DT_INST_PROP(n, time_to_max_speed_ms), 0), \
    }; \
    static struct mouse_forwarder_data mf_data_##n; \
    DEVICE_DT_INST_DEFINE(n, &mf_init, NULL,                              \
                          &mf_data_##n,                     \
                          &mf_config_##n, POST_KERNEL,      \
                          CONFIG_INPUT_INIT_PRIORITY, &processor_api);

DT_INST_FOREACH_STATUS_OKAY(MF_INIT)