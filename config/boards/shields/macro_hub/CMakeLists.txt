if(CONFIG_SHIELD_MACRO_HUB)
    target_sources(app PRIVATE src/key_reporter.c)
    target_sources(app PRIVATE src/fingerprint.c)
endif()
