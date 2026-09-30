idf_component_register(SRCS "chip_health.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES esp_driver_tsens esp_hw_support esp_system esp_timer)
