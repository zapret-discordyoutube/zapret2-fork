// winnative: BSD style udp header
#pragma once
#include <netinet/in.h>
struct udphdr
{
	uint16_t uh_sport;
	uint16_t uh_dport;
	uint16_t uh_ulen;
	uint16_t uh_sum;
} __attribute__((packed));
