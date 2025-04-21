
add_subdirectory_ifdef(CONFIG_SENSOR src/sensor/)
add_subdirectory_ifdef(CONFIG_KSCAN src/kscan/)
add_subdirectory_ifdef(CONFIG_INPUT src/input_processors/)
add_subdirectory_ifdef(CONFIG_INPUT src/input/)
target_sources(app PRIVATE src/adc.c)

zephyr_include_directories(include)

zephyr_syscall_header(include/drivers/kscan_forwarder.h)
