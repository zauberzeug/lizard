set(SH2_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../Adafruit_BNO08x/src")

idf_component_register(
    SRCS
        "bno08x.cpp"
        "${SH2_DIR}/sh2.c"
        "${SH2_DIR}/sh2_util.c"
        "${SH2_DIR}/sh2_SensorValue.c"
        "${SH2_DIR}/shtp.c"
    INCLUDE_DIRS "." "${SH2_DIR}"
    REQUIRES driver esp_timer
)
