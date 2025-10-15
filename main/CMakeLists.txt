file(GLOB SRC_FILES
     "*.cpp"
     "*.h"
     "**/*.cpp"
     "**/*.h"
     "parser.c"
)

idf_component_register(
    SRCS ${SRC_FILES}
    INCLUDE_DIRS "." "compilation" "modules" "utils" "${CMAKE_BINARY_DIR}/generated"
    REQUIRES driver esp_wifi esp_timer efuse lwip esp_event esp_netif esp_adc esp32-zeug esp32-serial-flasher app_update esp_driver_uart esp_https_ota spi_flash bt nvs_flash
)
add_compile_definitions(OWL_TOKEN_RUN_LENGTH=256)
component_compile_options(-std=gnu++17 -Wsuggest-override)
set_source_files_properties(parser.c PROPERTIES COMPILE_FLAGS -Wno-missing-field-initializers)
