target_sources_ifdef(CONFIG_HE_INPUT_LISTENER app PRIVATE input_listener.c)
target_sources_ifdef(CONFIG_HE_INPUT_SPLIT app PRIVATE input_split.c)