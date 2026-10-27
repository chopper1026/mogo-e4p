set(read_pico_requires epdiy e0470_epaper_waveform sc7a20h cst836u read_pico_pmu fatfs sy7636a fca9555 frontlight
                   esp_driver_gpio esp_driver_i2c esp_driver_ledc esp_driver_rmt esp_driver_sdmmc)

set(read_pico_sources
    "read_pico_init.c"
    "read_pico_epd_timing.c"
    "read_pico_sd.c"
    "read_pico_crashlog.c"
)

# S31 板没有蜂鸣器，用静默 stub；同时避免 RMT 与 EPD CKV 抢通道。
# The S31 board has no buzzer, so use a silent stub and avoid RMT contention with EPD CKV.
if(IDF_TARGET STREQUAL "esp32s31")
    list(APPEND read_pico_sources "read_pico_buzzer_stub.c")
else()
    list(APPEND read_pico_requires pwm_audio)
    list(APPEND read_pico_sources
        "read_pico_buzzer.c"
        "buzzer_score.c"
        "buzzer_duty_map.c"
        "buzzer_1bit.c"
    )
endif()

# S31 板（Metalio E-INK4 Plus）用 TPS65185/TCA9555；S3 原板保留 SY7636A/FCA9555。
# S31 board (Metalio E-INK4 Plus) uses TPS65185/TCA9555; keep the S3 original board.
if(CONFIG_IDF_TARGET_ESP32S31)
    list(APPEND read_pico_sources "board_metalio_e4p.c")
else()
    list(APPEND read_pico_sources "read_pico_board.c")
endif()

# Zbit ZB25VQ128 的 120 MHz HPM 覆盖只适用于原 S3 板；S31 板使用 80 MHz 默认时序。
# The Zbit ZB25VQ128 120 MHz HPM override targets the original S3 board; the S31 board
# runs the 80 MHz default timing.
if(CONFIG_IDF_TARGET_ESP32S3)
    list(APPEND read_pico_sources "read_pico_flash_hpm.c")
endif()

idf_component_register(
    SRCS ${read_pico_sources}
    INCLUDE_DIRS "include"
    REQUIRES ${read_pico_requires}
    PRIV_REQUIRES bootloader_support spi_flash freertos esp_timer esp_mm
    # flash_hpm_zb.c 只定义了一个覆盖 IDF weak 符号的表，没有任何
    # 代码引用它，不整包链接的话这个目标文件会被丢掉，覆盖就不生效。
    # flash_hpm_zb.c only defines a weak-symbol override table; keep the whole archive
    # so the object is not dropped by the linker.
    WHOLE_ARCHIVE
)
