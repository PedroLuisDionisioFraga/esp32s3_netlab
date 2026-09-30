idf_component_register(SRCS "wifi_bridge.c"
                            "wifi_bridge_nvs.c"
                            "dns_catch_all.c"
                       INCLUDE_DIRS "include"
                       REQUIRES freertos
                       PRIV_REQUIRES esp_wifi esp_netif esp_event esp_timer esp_hw_support esp_system
                                     nvs_flash lwip notification_manager)
