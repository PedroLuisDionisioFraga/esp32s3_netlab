idf_component_register(SRCS "rest_server.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES lwip esp_http_server esp_timer chip_health heap_monitor notification_manager
                                     wifi_bridge
                       EMBED_FILES "setup.html")
