idf_component_register(SRCS "app_main.c"
                            "reset_button.c"
                       PRIV_REQUIRES nvs_flash esp_netif esp_event esp_driver_gpio freertos
                                     chip_health notification_manager wifi_bridge rest_server
                       INCLUDE_DIRS ".")

if(CONFIG_NETLAB_DEPLOY_WEB_PAGES)
    set(WEB_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../front/web")
    if(EXISTS ${WEB_SRC_DIR}/dist)
        littlefs_create_partition_image(www ${WEB_SRC_DIR}/dist FLASH_IN_PROJECT)
    else()
        message(FATAL_ERROR "'${WEB_SRC_DIR}/dist' doesn't exist. Please run 'pnpm install && pnpm build' under '${WEB_SRC_DIR}', or disable NETLAB_DEPLOY_WEB_PAGES in menuconfig.")
    endif()
endif()
