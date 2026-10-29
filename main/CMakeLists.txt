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
    REQUIRES
        app_update
        bno08x_driver
        bt
        driver
        efuse
        esp32-serial-flasher
        esp32-zeug
        esp_adc
        esp_driver_pcnt
        esp_driver_uart
        esp_event
        esp_https_ota
        esp_netif
        esp_timer
        esp_wifi
        lwip
        mbedtls
        nvs_flash
        spi_flash
    WHOLE_ARCHIVE
)
if(CONFIG_IDF_TARGET_ESP32)
    # The workaround for lost TX interrupts can count a frame twice and trip an assert in the TWAI ISR (see can.cpp)
    target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=twai_hal_get_events")
endif()
add_compile_definitions(OWL_TOKEN_RUN_LENGTH=256)
component_compile_options(-std=gnu++17 -Wsuggest-override)
set_source_files_properties(parser.c PROPERTIES COMPILE_FLAGS -Wno-missing-field-initializers)
