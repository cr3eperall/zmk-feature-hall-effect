target_sources_ifdef(CONFIG_ZMK_DRIVER_KSCAN_ADC_DIRECT app PRIVATE src/kscan/kscan_adc_direct.c)

zephyr_include_directories(include)

