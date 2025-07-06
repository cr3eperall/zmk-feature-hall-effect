
add_subdirectory(src/sensor/)
add_subdirectory(src/kscan/)
add_subdirectory(src/input_processors/)
add_subdirectory(src/input/)
add_subdirectory(src/behaviors/)
add_subdirectory(src/hid/) # copied and modified from https://github.com/badjeff/zmk-hid-io
target_sources(app PRIVATE src/adc.c)

zephyr_include_directories(include)
zephyr_include_directories(include/he)
zephyr_include_directories(include/he/hid) # copied and modified from https://github.com/badjeff/zmk-hid-io

zephyr_syscall_header(include/drivers/kscan_forwarder.h)
zephyr_syscall_header(include/drivers/pulse_set_forwarder.h)
