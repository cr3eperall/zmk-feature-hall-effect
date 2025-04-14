# target_sources_ifdef(CONFIG_ZMK_DRIVER_KSCAN_HE_DIRECT_PULSED app PRIVATE kscan_he_direct_pulsed.c)
# target_sources_ifdef(CONFIG_ZMK_DRIVER_KSCAN_FORWARDER app PRIVATE kscan_forwarder.c)
target_sources(app PRIVATE kscan_he_direct_pulsed.c)
target_sources(app PRIVATE kscan_forwarder.c)