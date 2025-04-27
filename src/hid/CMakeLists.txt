target_sources_ifdef(CONFIG_HE_GAMEPAD_HID app PRIVATE endpoints.c)
target_sources_ifdef(CONFIG_HE_GAMEPAD_HID app PRIVATE hid_gamepad.c)
target_sources_ifdef(CONFIG_HE_GAMEPAD_HID app PRIVATE hog.c)
target_sources_ifdef(CONFIG_HE_GAMEPAD_HID app PRIVATE usb_hid.c)