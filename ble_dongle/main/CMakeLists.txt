idf_component_register(SRCS "main.cpp"
                       INCLUDE_DIRS "."
                       REQUIRES bt esp_driver_uart nvs_flash)
