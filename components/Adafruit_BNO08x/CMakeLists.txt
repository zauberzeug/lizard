idf_component_register(
    SRCS
        "Adafruit_BNO08x.cpp"
        "sh2.c"
        "sh2_util.c"
        "sh2_SensorValue.c"
        "shtp.c"
    INCLUDE_DIRS "."
    REQUIRES driver esp_timer freertos
)

