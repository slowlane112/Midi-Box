file(GLOB_RECURSE SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.c")

idf_component_register(
    SRCS ${SOURCES}
    INCLUDE_DIRS "." "board" "drivers" "midi" "ui" "ui/generated" "screens" "app" "viz" "ble" "studio" "learn"
    REQUIRES esp_driver_gpio esp_driver_uart esp_driver_spi esp_driver_ledc
             esp_lcd esp_timer fatfs sdmmc esp_driver_sdspi nvs_flash bt
)

# The v1 visualisers and v1 NimBLE code are kept as they were; don't let a newer compiler warning stop the build.
file(GLOB V1_VIZ_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/viz/display_*.c"
                         "${CMAKE_CURRENT_SOURCE_DIR}/ble/ble.c"
                         "${CMAKE_CURRENT_SOURCE_DIR}/ble/ble_scan.c"
                         "${CMAKE_CURRENT_SOURCE_DIR}/ble/ble_midi.c")
set_source_files_properties(${V1_VIZ_SOURCES} PROPERTIES COMPILE_OPTIONS "-Wno-error")
