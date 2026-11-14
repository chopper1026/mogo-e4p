/* SPDX-License-Identifier: Apache-2.0
 * Deterministic DNS answers; never query a real resolver from a host regression.
 */
#include "network_http_test_dns.h"
#include <arpa/inet.h>
#include <assert.h>
#include <stdlib.h>

const char *network_http_test_addresses[4] = {NETWORK_HTTP_TEST_FIRST, NETWORK_HTTP_TEST_SECOND};
unsigned network_http_test_address_count = 2, network_http_test_dns_calls, network_http_test_dns_live;
int network_http_test_dns_error;

int network_http_test_getaddrinfo(const char *host, const char *service,
        const struct addrinfo *hints, struct addrinfo **result) {
    assert(host && *host && !service && hints->ai_family == AF_INET && hints->ai_socktype == SOCK_STREAM);
    ++network_http_test_dns_calls;
    *result = NULL;
    if (network_http_test_dns_error) return network_http_test_dns_error;
    struct addrinfo **next = result;
    for (unsigned i = 0; i < network_http_test_address_count; ++i) {
        struct entry {struct addrinfo info; struct sockaddr_in address;} *entry = calloc(1, sizeof(*entry));
        assert(entry);
        entry->info.ai_family = AF_INET;
        entry->info.ai_addrlen = sizeof(entry->address);
        entry->info.ai_addr = (struct sockaddr *)&entry->address;
        assert(inet_pton(AF_INET, network_http_test_addresses[i], &entry->address.sin_addr) == 1);
        *next = &entry->info;
        next = &entry->info.ai_next;
        ++network_http_test_dns_live;
    }
    return 0;
}

void network_http_test_freeaddrinfo(struct addrinfo *answer) {
    while (answer) {
        struct addrinfo *next = answer->ai_next;
        assert(network_http_test_dns_live);
        --network_http_test_dns_live;
        free(answer);
        answer = next;
    }
}

void network_http_test_dns_reset(void) {
    assert(!network_http_test_dns_live);
    network_http_test_dns_calls = 0;
    network_http_test_dns_error = 0;
    network_http_test_address_count = 2;
    network_http_test_addresses[0] = NETWORK_HTTP_TEST_FIRST;
    network_http_test_addresses[1] = NETWORK_HTTP_TEST_SECOND;
}
