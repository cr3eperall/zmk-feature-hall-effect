target_sources_ifdef(CONFIG_ZMK_DRIVER_KSCAN_ADC_DIRECT app PRIVATE src/kscan/kscan_adc_direct.c)
target_sources_ifdef(CONFIG_ZMK_DRIVER_KSCAN_ADC_DIRECT app PRIVATE src/kscan/kscan_he_direct_pulsed.c)
target_sources(app PRIVATE src/adc.c)

zephyr_syscall_header( include/drivers/kscan_forwarder.h)

zephyr_include_directories(include)

