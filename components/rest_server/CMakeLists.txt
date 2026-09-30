idf_component_register(SRCS "rest_server.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES esp_http_server chip_health notification_manager wifi_bridge
                       EMBED_FILES "setup.html")
