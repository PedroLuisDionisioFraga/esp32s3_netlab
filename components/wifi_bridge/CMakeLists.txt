set(srcs "wifi_bridge.c"
         "wifi_bridge_nvs.c"
         "dns_catch_all.c")
if(CONFIG_WIFI_BRIDGE_ROUTER_MODE)
    # The lwIP hook that feeds traffic_observer.c is added in the project CMakeLists.txt.
    list(APPEND srcs "traffic_observer.c" "traffic_parse.c")
endif()

idf_component_register(SRCS ${srcs}
                       INCLUDE_DIRS "include"
                       REQUIRES freertos
                       PRIV_REQUIRES esp_wifi esp_netif esp_event esp_timer esp_hw_support esp_system
                                     nvs_flash lwip notification_manager)
