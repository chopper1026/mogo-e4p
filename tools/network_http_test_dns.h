/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <netdb.h>
#define NETWORK_HTTP_TEST_FIRST "104.21.8.63"
#define NETWORK_HTTP_TEST_SECOND "172.67.138.158"
extern const char *network_http_test_addresses[4];
extern unsigned network_http_test_address_count, network_http_test_dns_calls, network_http_test_dns_live;
extern int network_http_test_dns_error;
int network_http_test_getaddrinfo(const char *, const char *, const struct addrinfo *, struct addrinfo **);
void network_http_test_freeaddrinfo(struct addrinfo *);
void network_http_test_dns_reset(void);
