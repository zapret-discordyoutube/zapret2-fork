#pragma once
#include <netinet/in.h>
const char *wn_inet_ntop(int af, const void *src, char *dst, size_t size);
int wn_inet_pton(int af, const char *src, void *dst);
#define inet_ntop wn_inet_ntop
#define inet_pton wn_inet_pton
