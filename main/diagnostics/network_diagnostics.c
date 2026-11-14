/* SPDX-License-Identifier: Apache-2.0
 * Bounded network snapshots and HTTPS failures; never log response bodies or query strings.
 */
#include "network_diagnostics.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "cellular_service.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "network_service.h"
#include "wifi_transfer.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"

static void http_target(esp_http_client_handle_t client, char target[160]) {
    target[0] = 0;
    if (client) (void)esp_http_client_get_url(client, target, 160);
    char *query = strpbrk(target, "?#");
    if (query) *query = 0;
}

static void http_peer(esp_http_client_handle_t client, char peer[48]) {
    snprintf(peer, 48, "unavailable");
    int fd = client ? esp_http_client_get_socket(client) : -1;
    struct sockaddr_storage address = {0};
    socklen_t size = sizeof(address);
    if (fd < 0 || getpeername(fd, (struct sockaddr *)&address, &size)) return;
    if (address.ss_family == AF_INET)
        (void)inet_ntop(AF_INET, &((struct sockaddr_in *)&address)->sin_addr, peer, 48);
    else if (address.ss_family == AF_INET6)
        (void)inet_ntop(AF_INET6, &((struct sockaddr_in6 *)&address)->sin6_addr, peer, 48);
}

static void dns_text(esp_netif_t *netif, esp_netif_dns_type_t type, char text[40]) {
    esp_netif_dns_info_t dns = {0};
    snprintf(text, 40, "unavailable");
    if (netif && esp_netif_get_dns_info(netif, type, &dns) == ESP_OK) {
        if (dns.ip.type == ESP_IPADDR_TYPE_V4)
            esp_ip4addr_ntoa(&dns.ip.u_addr.ip4, text, 40);
        else snprintf(text, 40, "IPv6");
    }
}

void network_diagnostics_snapshot(const char *owner, int bearer) {
#if defined(CONFIG_APP_NETWORK_CONNECT_TRACE) && CONFIG_APP_NETWORK_CONNECT_TRACE
    esp_log_level_set("esp-tls", ESP_LOG_DEBUG);
#endif
    network_status_t status = {0};
    network_service_get_status(&status);
    esp_netif_t *netif = bearer == NETWORK_BEARER_CELLULAR ? cellular_service_netif()
        : bearer == NETWORK_BEARER_WIFI ? wifi_transfer_netif() : NULL;
    esp_netif_ip_info_t ip = {0};
    if (netif) (void)esp_netif_get_ip_info(netif, &ip);
    char primary[40], backup[40];
    dns_text(netif, ESP_NETIF_DNS_MAIN, primary);
    dns_text(netif, ESP_NETIF_DNS_BACKUP, backup);
    const unsigned internal = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    const unsigned external = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
    ESP_LOGI(owner, "network bearer=%d route=%d active=%d default_match=%d ready=%d/%d "
             "ip=" IPSTR " gw=" IPSTR " dns=%s/%s clock=%lld internal=%u/%u psram=%u/%u",
             bearer, status.route, status.active_bearer,
             netif && esp_netif_get_default_netif() == netif,
             status.wifi_ready, status.cellular_ready, IP2STR(&ip.ip), IP2STR(&ip.gw),
             primary, backup, (long long)time(NULL),
             (unsigned)heap_caps_get_free_size(internal),
             (unsigned)heap_caps_get_largest_free_block(internal),
             (unsigned)heap_caps_get_free_size(external),
             (unsigned)heap_caps_get_largest_free_block(external));
}

void network_diagnostics_http_failure(const char *owner, esp_http_client_handle_t client,
                                      const char *phase, long result, int bearer) {
    // Preserve the caller's socket errno before any other diagnostic API runs.
    const int socket_errno = errno;
    int transport_errno = client ? esp_http_client_get_errno(client) : 0;
    int tls_code = 0, tls_flags = 0;
    esp_err_t tls_error = client
        ? esp_http_client_get_and_clear_last_tls_error(client, &tls_code, &tls_flags) : ESP_OK;
    char target[160], peer[48];
    http_target(client, target);
    http_peer(client, peer);
    ESP_LOGW(owner, "HTTPS failed phase=%s result=%ld errno=%d transport_errno=%d "
             "esp_tls=%s tls=0x%x verify=0x%x status=%d peer=%s target=%s",
             phase, result, socket_errno, transport_errno, esp_err_to_name(tls_error),
             (unsigned)tls_code, (unsigned)tls_flags,
             client ? esp_http_client_get_status_code(client) : 0, peer, target);
    network_diagnostics_snapshot(owner, bearer);
}

void network_diagnostics_http_response(const char *owner, esp_http_client_handle_t client, size_t received) {
    char target[160], peer[48];
    http_target(client, target);
    http_peer(client, peer);
    ESP_LOGI(owner, "HTTPS response status=%d bytes=%u complete=%d peer=%s target=%s",
             esp_http_client_get_status_code(client), (unsigned)received,
             esp_http_client_is_complete_data_received(client), peer, target);
}

void network_diagnostics_data_failure(const char *owner, const char *phase, const char *reason,
                                      size_t received, int64_t reference, int bearer) {
    ESP_LOGW(owner, "response rejected phase=%s reason=%s bytes=%u reference=%lld",
             phase, reason, (unsigned)received, (long long)reference);
    network_diagnostics_snapshot(owner, bearer);
}
