idf_component_register(SRCS "rest_server.c" "auth.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES lwip esp_http_server esp_app_format esp_hw_support esp_timer
                                     chip_health heap_monitor notification_manager wifi_bridge
                       EMBED_FILES "setup.html")
