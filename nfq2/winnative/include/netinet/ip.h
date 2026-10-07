// winnative: BSD style ipv4 header
#pragma once
#include <netinet/in.h>
struct ip
{
	uint8_t ip_hl:4, ip_v:4;
	uint8_t ip_tos;
	uint16_t ip_len;
	uint16_t ip_id;
	uint16_t ip_off;
#define IP_RF 0x8000
#define IP_DF 0x4000
#define IP_MF 0x2000
#define IP_OFFMASK 0x1fff
	uint8_t ip_ttl;
	uint8_t ip_p;
	uint16_t ip_sum;
	struct in_addr ip_src, ip_dst;
} __attribute__((packed));
#define IPVERSION 4
#define MAXTTL 255
#define IPDEFTTL 64
#define IPTOS_ECN_MASK 0x03
#define IPTOS_ECN_NOT_ECT 0x00
#define IPTOS_ECN_ECT1 0x01
#define IPTOS_ECN_ECT0 0x02
#define IPTOS_ECN_CE 0x03
