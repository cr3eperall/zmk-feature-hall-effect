#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <zephyr/device.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/dt-bindings/adc/adc.h>
#include <zephyr/sys/util.h>

struct kscan_adc{
    struct adc_dt_spec spec;
    /** The index of the ADC channel in the devicetree *-adcs array. */
    size_t index;
};

/** ADC_DT_SPEC_GET_BY_IDX(), but for a struct kscan_gpio. */
// #define KSCAN_ADC_GET_BY_IDX(node_id, idx)                                                  \
//     ((struct kscan_adc){.spec = ADC_DT_SPEC_GET_BY_IDX(node_id, idx), .index = idx})

// #define INST_ADC_LEN(n) DT_INST_PROP_LEN(n, io_channels)

struct kscan_adc_list{
    struct kscan_adc *adcs;
    size_t len;
};

/** Define a kscan_adc_list from a compile-time ADC array. */
// #define KSCAN_ADC_LIST(adc_array)                                                                \
//     ((struct kscan_adc_list){.adcs = adc_array, .len = ARRAY_SIZE(adc_array)})

static int compare_channels(const void *a, const void *b) {
    const struct kscan_adc *adc_a = a;
    const struct kscan_adc *adc_b = b;

    return adc_a->spec.channel_id - adc_b->spec.channel_id;
}

void kscan_adc_list_sort_by_channel(struct kscan_adc_list *list) {
    qsort(list->adcs, list->len, sizeof(list->adcs[0]), compare_channels);
}

struct kscan_gpio {
    struct gpio_dt_spec spec;
    /** The index of the GPIO in the devicetree *-gpios array. */
    size_t index;
};

/** GPIO_DT_SPEC_GET_BY_IDX(), but for a struct kscan_gpio. */
// #define KSCAN_GPIO_GET_BY_IDX(node_id, prop, idx)                                                  \
//     ((struct kscan_gpio){.spec = GPIO_DT_SPEC_GET_BY_IDX(node_id, prop, idx), .index = idx})

struct kscan_gpio_list {
    struct kscan_gpio *gpios;
    size_t len;
};

/** Define a kscan_gpio_list from a compile-time GPIO array. */
// #define KSCAN_GPIO_LIST(gpio_array)                                                                \
//     ((struct kscan_gpio_list){.gpios = gpio_array, .len = ARRAY_SIZE(gpio_array)})

struct adc_key_state{
    bool pressed : 1;
    bool changed : 1;
    int16_t last_value;
};

void adc_key_state_update(struct adc_key_state *state, bool pressed, int16_t value){
    if(state->pressed==pressed){
        state->changed = false;
    }else{
        state->pressed=pressed;
        state->changed=true;
    }
    state->last_value=value;
}

bool adc_key_state_is_pressed(struct adc_key_state *state){
    return state->pressed;
}

bool adc_key_state_has_changed(struct adc_key_state *state){
    return state->changed;
}

static int adc_ref_from_div(int32_t div){
    switch (div){
        case 1:
            return ADC_REF_VDD_1;
        case 2:
            return ADC_REF_VDD_1_2;
        case 3:
            return ADC_REF_VDD_1_3;
        case 4:
            return ADC_REF_VDD_1_4;
        default:
            LOG_ERR("Invalid ADC reference diveder: %d", div);
            return ADC_REF_VDD_1;
    }
}