#pragma once
#include <stdint.h>
#include "lwip/netdb.h"
void bounded_dns_init();
int bounded_getaddrinfo(const char *node,const char *service,const struct addrinfo *hints,
                       struct addrinfo **result,int64_t deadline);
