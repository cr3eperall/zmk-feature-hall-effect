#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <zephyr/device.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/kscan.h>
#include <zephyr/dt-bindings/adc/adc.h>
#include <zephyr/sys/util.h>

#define KSCAN_ADC_MAX_CHANNELS 16

struct kscan_adc_group_data {
    struct adc_sequence as;
    struct adc_key_state *key_state_vec;
    int8_t *key_channels;
    int16_t *adc_buffer;
};

struct kscan_he_data {
    const struct device *dev;
    struct kscan_adc_group_data *adc_groups;
    kscan_callback_t callback;
    struct k_work_delayable work;
    int64_t scan_time;
};

struct kscan_he_key_cfg {
    struct adc_dt_spec adc;
    int16_t press_point;
    int16_t release_point;
    int16_t calibration_min;
    int16_t calibration_max;
};

struct kscan_he_group_cfg {
    const struct gpio_dt_spec enable_gpio;
    bool switch_pressed_is_higher;
    int16_t switch_height;
    int16_t key_count;
    struct kscan_he_key_cfg *keys;
};

struct kscan_he_config {
    bool pulse_read;
    int16_t resolution;
    int16_t read_turn_on_time;
    int16_t wait_period_idle;
    int16_t wait_period_press;
    int16_t group_count;
    struct kscan_he_group_cfg *he_groups;
};

static int compare_key_channel(const void *a, const void *b);

void kscan_adc_sort_keys_by_channel(struct kscan_he_group_cfg *group_cfg);

struct adc_key_state{
    bool pressed : 1;
    bool changed : 1;
    int16_t last_value;
};

void adc_key_state_update(struct adc_key_state *state, bool pressed, int16_t value);

bool adc_key_state_is_pressed(struct adc_key_state *state);

bool adc_key_state_has_changed(struct adc_key_state *state);

int16_t kscan_adc_cfg_press_height(const struct device *dev, uint8_t group, uint8_t key);
int16_t kscan_adc_cfg_release_height(const struct device *dev, uint8_t group, uint8_t key);

// Get the current height of a key from the adc buffer
int16_t kscan_adc_get_mapped_height(const struct device *dev, uint8_t group, uint8_t key);