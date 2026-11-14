#pragma once
static inline void network_http_test_log(const char *tag, const char *format, ...) {
    (void)tag; (void)format;
}
#define ESP_LOGI(...) network_http_test_log(__VA_ARGS__)
