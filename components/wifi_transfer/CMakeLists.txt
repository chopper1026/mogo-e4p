# SPDX-FileCopyrightText: 2026 mindreset
# SPDX-License-Identifier: Apache-2.0
# 独立热点传书组件与嵌入页面。/ Standalone hotspot transfer component and embedded page.
idf_build_get_property(web_python PYTHON)
set(transfer_page "${CMAKE_CURRENT_BINARY_DIR}/web_assets/upload.html.gz")
set(transfer_packer "${CMAKE_CURRENT_LIST_DIR}/../../tools/compress_transfer_page.py")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_LIST_DIR}/upload.html" "${transfer_packer}")
execute_process(COMMAND "${web_python}" "${transfer_packer}"
    --source "${CMAKE_CURRENT_LIST_DIR}/upload.html" --output "${transfer_page}"
    RESULT_VARIABLE transfer_page_result)
if(NOT transfer_page_result EQUAL 0)
    message(FATAL_ERROR "Transfer page compression failed")
endif()
idf_component_register(SRCS "wifi_transfer.c"
    INCLUDE_DIRS "include"
    REQUIRES ble_page_turner book_search esp_wifi esp_netif esp_event esp_http_server nvs_flash fatfs esp_timer espressif__cjson
    EMBED_FILES "${transfer_page}")
