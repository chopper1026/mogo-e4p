idf_component_register(
    SRCS "device_power_metalio.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_driver_i2c fca9555
    PRIV_REQUIRES freertos esp_rom esp_timer nvs_flash
)
