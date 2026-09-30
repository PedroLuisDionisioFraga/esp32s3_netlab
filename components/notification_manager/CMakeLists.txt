idf_component_register(SRCS "notification_manager.c"
                       INCLUDE_DIRS "include"
                       REQUIRES freertos
                       PRIV_REQUIRES led_device)
