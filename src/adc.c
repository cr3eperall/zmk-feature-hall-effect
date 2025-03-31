#include "adc.h"
static int compare_key_channel(const void *a, const void *b) {
    const struct kscan_adc_key_cfg *adc_a = a;
    const struct kscan_adc_key_cfg *adc_b = b;

    return adc_a->adc.channel_id - adc_b->adc.channel_id;
}

void kscan_adc_sort_keys_by_channel(struct kscan_adc_group_cfg *group_cfg) {
    qsort(group_cfg->keys, group_cfg->key_count,
          sizeof(struct kscan_adc_key_cfg), compare_key_channel);
}

void adc_key_state_update(struct adc_key_state *state, bool pressed,
                          int16_t value) {
    if (state->pressed == pressed) {
        state->changed = false;
    } else {
        state->pressed = pressed;
        state->changed = true;
    }
    state->last_value = value;
}

bool adc_key_state_is_pressed(struct adc_key_state *state) {
    return state->pressed;
}

bool adc_key_state_has_changed(struct adc_key_state *state) {
    return state->changed;
}

int16_t kscan_adc_cfg_press_height(const struct device *dev, uint8_t group,
                                   uint8_t key) {
    const struct kscan_adc_config *config = dev->config;
    return config->adc_groups[group].keys[key].press_point;
}

int16_t kscan_adc_cfg_release_height(const struct device *dev, uint8_t group,
                                     uint8_t key) {
    const struct kscan_adc_config *config = dev->config;
    return config->adc_groups[group].keys[key].release_point;
}

int16_t kscan_adc_get_mapped_height(const struct device *dev, uint8_t group,
                                    uint8_t key) {
    struct kscan_adc_data *data = dev->data;
    const struct kscan_adc_config *config = dev->config;
    const struct kscan_adc_group_cfg group_cfg = config->adc_groups[group];
    const struct kscan_adc_key_cfg key_cfg = group_cfg.keys[key];

    int16_t raw_adc_value = data->adc_groups[group].adc_buffer[key];

    int16_t cal_min = key_cfg.calibration_min;
    int16_t cal_max = key_cfg.calibration_max;
    if (cal_min == cal_max) {
        cal_min = 0;
        cal_max = (2 << config->resolution) - 1;
    }

    float height_float = (float)(raw_adc_value - cal_min) / (float)(cal_max - cal_min);
    if (group_cfg.switch_pressed_is_higher) {
        height_float = 1.0 - height_float;
    }

    int16_t height = height_float * group_cfg.switch_height;
    return height;
}