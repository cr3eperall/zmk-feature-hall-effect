#include "adc.h"
#include <math.h>
#include <stdlib.h>
#include <zephyr/logging/log.h>

// static int compare_key_channel(const void *a, const void *b) {
//     const struct kscan_he_key_cfg *adc_a = a;
//     const struct kscan_he_key_cfg *adc_b = b;

//     return ((int)adc_a->adc.channel_id) - ((int)adc_b->adc.channel_id);
// }

// void kscan_adc_sort_keys_by_channel(struct kscan_he_group_cfg *group_cfg) {
//     qsort(group_cfg->keys, group_cfg->key_count,
//           sizeof(struct kscan_he_key_cfg), compare_key_channel);
// }

LOG_MODULE_REGISTER(feature_hall_effect,
    CONFIG_HE_LOG_LEVEL);

int16_t kscan_adc_cfg_deadzone_top(const struct device *dev, uint8_t group,
                                   uint8_t key) {
    const struct kscan_he_config *config = dev->config;
    return config->he_groups[group].keys[key].deadzone_top;
}

int16_t kscan_adc_cfg_deadzone_bottom(const struct device *dev, uint8_t group,
                                     uint8_t key) {
    const struct kscan_he_config *config = dev->config;
    return config->he_groups[group].keys[key].deadzone_bottom;
}

int16_t kscan_adc_get_mapped_height(const struct device *dev, uint8_t group,
                                    uint8_t key, int16_t raw_adc_value) {
    // struct kscan_he_data *data = dev->data;
    const struct kscan_he_config *conf = dev->config;
    const struct kscan_he_group_cfg group_cfg = conf->he_groups[group];
    const struct kscan_he_key_cfg key_cfg = group_cfg.keys[key];
    struct kscan_he_data *data = dev->data;

    int16_t cal_min = key_cfg.calibration_min;
    int16_t cal_max = key_cfg.calibration_max;
    if (cal_min == cal_max) {
        cal_min = 0;
        cal_max = (2 << conf->resolution) - 1;
    }

    float height_float = (float)(raw_adc_value - cal_min) / (float)(cal_max - cal_min);
    if (group_cfg.switch_pressed_is_higher) {
        height_float = 1.0 - height_float;
    }
    height_float = polyeval(data->polyfit, conf->n_coeffs, height_float);

    int16_t height = roundf(height_float * group_cfg.switch_height);
    return height;
}

float polyeval(float *coeffs, int16_t n_coeffs, float x){
    float tmp=0.0;
    for(int i=0; i<n_coeffs; i++){
        tmp=coeffs[i] + x*tmp;
    }
    return tmp;
}