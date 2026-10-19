if(CONFIG_IDF_TARGET_ESP32S31)
    set(pmu_sources "read_pico_pmu_metalio.c")
else()
    set(pmu_sources "read_pico_pmu.c")
endif()

idf_component_register(
    SRCS ${pmu_sources}
    INCLUDE_DIRS "include"
    REQUIRES esp_driver_i2c fca9555
    PRIV_REQUIRES freertos esp_rom esp_timer nvs_flash
)
