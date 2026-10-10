cmake_minimum_required(VERSION 3.22)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# "Trim" the build. Include the minimal set of components, main, and anything it depends on.
idf_build_set_property(MINIMAL_BUILD ON)
project(esp32s3-netlab VERSION "0.1.0")

# Router lab mode: lwIP shows every received IPv4 packet to components/wifi_bridge/traffic_observer.c
# (ESP-IDF's documented way to add a custom lwIP hook).
if(CONFIG_WIFI_BRIDGE_ROUTER_MODE)
    idf_component_get_property(lwip lwip COMPONENT_LIB)
    target_compile_options(${lwip} PRIVATE "-I${PROJECT_DIR}/components/wifi_bridge/lwip_hook")
    target_compile_definitions(${lwip} PRIVATE "-DESP_IDF_LWIP_HOOK_FILENAME=\"traffic_hook.h\"")
endif()
