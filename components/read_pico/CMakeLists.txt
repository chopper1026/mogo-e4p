set(read_pico_requires epdiy e0470_epaper_waveform sc7a20h cst836u read_pico_pmu fatfs sy7636a fca9555 frontlight
                   esp_driver_gpio esp_driver_i2c esp_driver_ledc esp_driver_rmt esp_driver_sdmmc)

set(read_pico_sources
    "read_pico_init.c"
    "read_pico_epd_timing.c"
    "read_pico_sd.c"
    "read_pico_crashlog.c"
    "read_pico_buzzer_stub.c"
    "board_metalio_e4p.c"
)

idf_component_register(
    SRCS ${read_pico_sources}
    INCLUDE_DIRS "include"
    REQUIRES ${read_pico_requires}
    PRIV_REQUIRES bootloader_support spi_flash freertos esp_timer esp_mm
)
