#include <drivers/input_processor.h>
#include <math.h>
#include <stdlib.h>
#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <dt-bindings/he/gamepad_forwarder.h>
#include <he/hid/endpoints.h>
#include <he/hid/hid_gamepad.h>
#include <he/input-event-codes.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>

LOG_MODULE_REGISTER(input_processor_gamepad_forwarder,
                    CONFIG_LOG_MAX_LEVEL); // TODO change log level

#define DT_DRV_COMPAT he_input_processor_gamepad_forwarder

#define GP_CODE_TO_SPEED_TYPE(code)                                            \
    GP_IS_LEFT(code)       ? GP_IS_JOYSTICK(code) ? 0 : 1                      \
    : GP_IS_JOYSTICK(code) ? 2                                                 \
                           : 3

struct gamepad_forwarder_data {
    const struct device *dev;
    float scale[GP_CODE_TO_IDX(GP_CODE_COUNT)];
    int64_t accumulation_start;
    // float accumulator[GP_CODE_TO_IDX(GP_CODE_COUNT)];
    int64_t acceleration_start[GP_CODE_TO_IDX(GP_CODE_COUNT)];
    int16_t last_values[GP_CODE_COUNT];
    int time_to_max_speed[4];
    struct k_work_delayable work;
};
struct gamepad_forwarder_config {
    int scale_x_l_int32;
    int scale_y_l_int32;
    int scale_t_l_int32;
    int scale_x_r_int32;
    int scale_y_r_int32;
    int scale_t_r_int32;
    int accumulation_time; // TODO rename
    int deadzone_bottom;
    int deadzone_top;
    int acceleration_exponent;
    int time_to_max_speed_tmp[4]; // joy left, trigger left, joy right, trigger
                                  // right
    int time_to_max_speed_len;
    bool normalize_circle;
};

void gp_update_acceleration_start(const struct device *dev) {
    struct gamepad_forwarder_data *data = dev->data;
    const struct gamepad_forwarder_config *conf = dev->config;
    int limit = conf->deadzone_top;
    for (int i = 0; i < GP_CODE_TO_IDX(GP_CODE_COUNT); i++) {
        if (data->last_values[2 * i] < limit ||
            data->last_values[2 * i + 1] < limit) {
            if (data->acceleration_start[i] == 0) {
                data->acceleration_start[i] = k_uptime_get();
            }
        } else {
            data->acceleration_start[i] = 0;
        }
    }
}

float gp_acceleration_mult(const struct device *dev, int code) {
    const struct gamepad_forwarder_config *conf = dev->config;
    struct gamepad_forwarder_data *data = dev->data;
    int64_t start_time = data->acceleration_start[GP_CODE_TO_IDX(code)];
    if (start_time == 0) {
        return 0.0;
    }
    int64_t now = k_uptime_get();
    int64_t move_duration = now - start_time;
    int time_to_max_speed =
        data->time_to_max_speed[GP_CODE_TO_SPEED_TYPE(code)];
    if (move_duration > time_to_max_speed || time_to_max_speed == 0 ||
        conf->acceleration_exponent == 0) {
        return 1.0;
    }

    float time_fraction = (float)move_duration / (float)time_to_max_speed;
    return powf(time_fraction, conf->acceleration_exponent);
}

// void gp_accumulate(const struct device *dev, float value, int key_type) {
//     struct gamepad_forwarder_data *data = dev->data;
//     if (key_type % 2 == 0) {
//         data->accumulator[GP_CODE_TO_IDX(key_type)] -= value;
//     } else {
//         data->accumulator[GP_CODE_TO_IDX(key_type)] += value;
//     }
//     return 0;
// }

float gp_rescale_working_area(const struct device *dev, int32_t event_value) {
    const struct gamepad_forwarder_config *conf = dev->config;
    float scaled =
        1.0 - ((float)(event_value - conf->deadzone_bottom)) /
                  ((float)(conf->deadzone_top - conf->deadzone_bottom));
    scaled = CLAMP(scaled, 0.0, 1.0);
    return scaled;
}

float gp_get_value(const struct device *dev, int code) {
    struct gamepad_forwarder_data *data = dev->data;
    int idx = GP_CODE_TO_IDX(code);
    float scaled =
        gp_rescale_working_area(dev, data->last_values[idx * 2 + 1]) -
        gp_rescale_working_area(dev, data->last_values[idx * 2]);
    scaled *= gp_acceleration_mult(data->dev, code);
    return scaled * data->scale[idx];
}

void normalize(float *x, float *y) {
    float radius = MIN(sqrtf((*x) * (*x) + (*y) * (*y)), 1.0);
    float angle = atan2f(*y, *x);
    *x=radius * cosf(angle);
    *y=radius * sinf(angle);
}

bool gp_must_sync(const struct device *dev) {
    struct gamepad_forwarder_data *data = dev->data;
    const struct gamepad_forwarder_config *conf = dev->config;
    int64_t now = k_uptime_get();
    if (data->accumulation_start == 0) {
        data->accumulation_start = now;
    }
    if (now - data->accumulation_start >= conf->accumulation_time) {
        data->accumulation_start = 0;
        return true;
    }
    return false;
}

void send_gp_report(const struct device *dev) {
    // struct gamepad_forwarder_data *data = dev->data;
    const struct gamepad_forwarder_config *conf = dev->config;
    float value_x, value_y;
    value_x = gp_get_value(dev, GP_L_LEFT);
    value_y = gp_get_value(dev, GP_L_UP);
    if(conf->normalize_circle){
        normalize(&value_x, &value_y);
    }
    zmk_hid_gamepad_joy_left_set((int16_t)value_x, (int16_t)value_y);
    value_x = gp_get_value(dev, GP_R_LEFT);
    value_y = gp_get_value(dev, GP_R_UP);
    if(conf->normalize_circle){
        normalize(&value_x, &value_y);
    }
    zmk_hid_gamepad_joy_right_set((int16_t)value_x, (int16_t)value_y);
    value_x = gp_get_value(dev, GP_L_TR);
    zmk_hid_gamepad_trigger_left_set((int16_t)value_x);
    value_x = gp_get_value(dev, GP_R_TR);
    zmk_hid_gamepad_trigger_right_set((int16_t)value_x);

    LOG_DBG("Sending gamepad report");
    zmk_endpoints_send_gamepad_report();
}

static void send_gp_report_work_handler(struct k_work *work) {
    struct k_work_delayable *dwork = k_work_delayable_from_work(work);
    struct gamepad_forwarder_data *data =
        CONTAINER_OF(dwork, struct gamepad_forwarder_data, work);
    bool active = false;
    gp_update_acceleration_start(data->dev);
    for (int i = 0; i < GP_CODE_COUNT; i++) {
        float scaled = gp_rescale_working_area(data->dev, data->last_values[i]);
        if (scaled > 0.0) {
            active = true;
            // gp_accumulate(data->dev, scaled, i);
        }
    }

    if (gp_must_sync(data->dev)) {
        send_gp_report(data->dev);
    }
    if (active) {
        k_work_schedule(&data->work, K_MSEC(1));
    }
}

static int gpf_handle_event(const struct device *dev, struct input_event *event,
                            uint32_t key_type, uint32_t param2,
                            struct zmk_input_processor_state *state) {
    struct gamepad_forwarder_data *data = dev->data;
    // const struct gamepad_forwarder_config *conf = dev->config;
    if (event->type != INPUT_EV_HE)
        return ZMK_INPUT_PROC_CONTINUE;
    // int output_event_code = 0;
    // float scaled = gp_rescale_working_area(dev, event->value);
    if (GP_INVALID(key_type)) {
        LOG_ERR("Unknown key type %d", key_type);
        return -EINVAL;
    }
    data->last_values[key_type] = event->value;
    // if(must_sync(dev)){
    //     send_reports(dev);
    // }
    k_work_reschedule(&data->work, K_NO_WAIT);
    return ZMK_INPUT_PROC_STOP;
}

static int gpf_init(const struct device *dev) {
    struct gamepad_forwarder_data *data = dev->data;
    const struct gamepad_forwarder_config *conf = dev->config;
    data->dev = dev;

    float *scale_x_l = (float *)&conf->scale_x_l_int32;
    float *scale_y_l = (float *)&conf->scale_y_l_int32;
    float *scale_t_l = (float *)&conf->scale_t_l_int32;
    float *scale_x_r = (float *)&conf->scale_x_r_int32;
    float *scale_y_r = (float *)&conf->scale_y_r_int32;
    float *scale_t_r = (float *)&conf->scale_t_r_int32;
    data->scale[GP_CODE_TO_IDX(GP_L_LEFT)] = *scale_x_l;
    data->scale[GP_CODE_TO_IDX(GP_L_UP)] = *scale_y_l;
    data->scale[GP_CODE_TO_IDX(GP_L_TR)] = *scale_t_l;
    data->scale[GP_CODE_TO_IDX(GP_R_LEFT)] = *scale_x_r;
    data->scale[GP_CODE_TO_IDX(GP_R_UP)] = *scale_y_r;
    data->scale[GP_CODE_TO_IDX(GP_R_TR)] = *scale_t_r;
    data->accumulation_start = 0;
    switch (conf->time_to_max_speed_len) {
    case 1:
        data->time_to_max_speed[0] = conf->time_to_max_speed_tmp[0];
        data->time_to_max_speed[1] = conf->time_to_max_speed_tmp[0];
        data->time_to_max_speed[2] = conf->time_to_max_speed_tmp[0];
        data->time_to_max_speed[3] = conf->time_to_max_speed_tmp[0];
        break;
    case 2:
        data->time_to_max_speed[0] = conf->time_to_max_speed_tmp[0];
        data->time_to_max_speed[1] = conf->time_to_max_speed_tmp[1];
        data->time_to_max_speed[2] = conf->time_to_max_speed_tmp[0];
        data->time_to_max_speed[3] = conf->time_to_max_speed_tmp[1];
        break;
    case 4:
        data->time_to_max_speed[0] = conf->time_to_max_speed_tmp[0];
        data->time_to_max_speed[1] = conf->time_to_max_speed_tmp[1];
        data->time_to_max_speed[2] = conf->time_to_max_speed_tmp[2];
        data->time_to_max_speed[3] = conf->time_to_max_speed_tmp[3];
        break;
    default:
        LOG_ERR("Invalid time_to_max_speed_len %d",
                conf->time_to_max_speed_len);
        return -EINVAL;
    }

    for (int i = 0; i < GP_CODE_TO_IDX(GP_CODE_COUNT); i++) {
        // data->accumulator[i] = 0.0;
        data->acceleration_start[i] = 0;

        data->last_values[2 * i] = 10000;
        data->last_values[2 * i + 1] = 10000;
    }
    k_work_init_delayable(&data->work, &send_gp_report_work_handler);
    return 0;
}

static struct zmk_input_processor_driver_api processor_api = {
    .handle_event = gpf_handle_event,
};

#define GPF_INIT(n)                                                            \
    BUILD_ASSERT(DT_INST_PROP_LEN(n, working_area) == 2,                       \
                 "Working area must contain 2 values");                        \
    BUILD_ASSERT(DT_INST_PROP_BY_IDX(n, working_area, 0) <                     \
                     DT_INST_PROP_BY_IDX(n, working_area, 1),                  \
                 "Working area bottom must be less than top");                 \
    BUILD_ASSERT(DT_INST_PROP_LEN(n, time_to_max_speed_ms) <= 2 ||             \
                     DT_INST_PROP_LEN(n, time_to_max_speed_ms) == 4,           \
                 "time_to_max_speed_ms must be 1, 2 or 4 values");             \
    static const struct gamepad_forwarder_config mf_config_##n = {             \
        .scale_x_l_int32 = DT_INST_PROP(n, scale_x_l_float),                   \
        .scale_y_l_int32 = DT_INST_PROP(n, scale_y_l_float),                   \
        .scale_t_l_int32 = DT_INST_PROP(n, scale_t_l_float),                   \
        .scale_x_r_int32 = DT_INST_PROP(n, scale_x_r_float),                   \
        .scale_y_r_int32 = DT_INST_PROP(n, scale_y_r_float),                   \
        .scale_t_r_int32 = DT_INST_PROP(n, scale_t_r_float),                   \
        .accumulation_time = DT_INST_PROP(n, accumulation_time),               \
        .deadzone_bottom = DT_INST_PROP_BY_IDX(n, working_area, 0),            \
        .deadzone_top = DT_INST_PROP_BY_IDX(n, working_area, 1),               \
        .acceleration_exponent = DT_INST_PROP(n, acceleration_exponent),       \
        .time_to_max_speed_len = DT_INST_PROP_LEN(n, time_to_max_speed_ms),    \
        .time_to_max_speed_tmp = {DT_INST_FOREACH_PROP_ELEM_SEP(               \
            n, time_to_max_speed_ms, DT_PROP_BY_IDX, (, ))},                   \
        .normalize_circle = DT_INST_PROP(n, normalize_circle),                 \
    };                                                                         \
    static struct gamepad_forwarder_data mf_data_##n;                          \
    DEVICE_DT_INST_DEFINE(n, &gpf_init, NULL, &mf_data_##n, &mf_config_##n,    \
                          POST_KERNEL, CONFIG_INPUT_INIT_PRIORITY,             \
                          &processor_api);

DT_INST_FOREACH_STATUS_OKAY(GPF_INIT)