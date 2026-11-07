set(cst836u_sources "cst836u.c")
set(cst836u_firmware "")
if(CONFIG_IDF_TARGET_ESP32S31 AND CONFIG_TOUCH_FT6336U_UPGRADE_ON_BOOT)
    list(APPEND cst836u_sources "ft6336u_upgrade.cpp")
    list(APPEND cst836u_firmware "firmware/ft6336u_v04_app.bin")
endif()

idf_component_register(
    SRCS ${cst836u_sources}
    INCLUDE_DIRS "include"
    REQUIRES board_i2c esp_driver_gpio esp_driver_i2c
    PRIV_REQUIRES freertos
    EMBED_FILES ${cst836u_firmware}
)
