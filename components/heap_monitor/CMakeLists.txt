idf_component_register(SRCS "heap_monitor.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES freertos heap)

# The text renderers behind heaptop's `ht` command, its failure log and its alert limits are not
# public API: they live in heaptop's src/. Hence the exact version pin in idf_component.yml.
idf_component_get_property(heaptop_dir pedroluisdionisiofraga__heaptop COMPONENT_DIR)
target_include_directories(${COMPONENT_LIB} PRIVATE "${heaptop_dir}/src")
