
add_subdirectory_ifdef(CONFIG_SENSOR src/sensor/)
add_subdirectory_ifdef(CONFIG_KSCAN src/kscan/)
add_subdirectory_ifdef(CONFIG_INPUT src/input_processors/)
add_subdirectory_ifdef(CONFIG_INPUT src/input/)
add_subdirectory_ifdef(CONFIG_INPUT src/behaviors/)
add_subdirectory_ifdef(CONFIG_INPUT src/hid/) # copied and modified from https://github.com/badjeff/zmk-hid-io
target_sources(app PRIVATE src/adc.c)

zephyr_include_directories(include)
zephyr_include_directories(include/he)
zephyr_include_directories(include/he/hid) # copied and modified from https://github.com/badjeff/zmk-hid-io

zephyr_syscall_header(include/drivers/kscan_forwarder.h)
