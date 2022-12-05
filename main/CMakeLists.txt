set(srcs "main.c" "button.c" "ble_mesh_init.c" "ble_mesh_nvs.c")

idf_component_register(SRCS "${srcs}" INCLUDE_DIRS  ".")
target_compile_options(${COMPONENT_LIB} PRIVATE "-Wno-format")
