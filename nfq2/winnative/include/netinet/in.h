// winnative: unix style ip definitions
#pragma once
#include <sys/socket.h>
#define IPPROTO_IP 0
#define IPPROTO_HOPOPTS 0
#define IPPROTO_ICMP 1
#define IPPROTO_IGMP 2
#define IPPROTO_IPIP 4
#define IPPROTO_TCP 6
#define IPPROTO_UDP 17
#define IPPROTO_IPV6 41
#define IPPROTO_ROUTING 43
#define IPPROTO_FRAGMENT 44
#define IPPROTO_GRE 47
#define IPPROTO_ESP 50
#define IPPROTO_AH 51
#define IPPROTO_ICMPV6 58
#define IPPROTO_NONE 59
#define IPPROTO_DSTOPTS 60
#define IPPROTO_SCTP 132
#define IPPROTO_RAW 255
#define INADDR_ANY ((in_addr_t)0x00000000)
#define INADDR_LOOPBACK ((in_addr_t)0x7f000001)
#define INADDR_BROADCAST ((in_addr_t)0xffffffff)
#define INADDR_NONE ((in_addr_t)0xffffffff)
#define INET_ADDRSTRLEN 16
#define INET6_ADDRSTRLEN 46
// windows layout of in_addr/in6_addr. iphlpapi.h and friends need exactly these types
#include <inaddr.h>
#include <in6addr.h>
#ifndef s6_addr16
#define s6_addr16 u.Word
#endif
struct sockaddr_in
{
	sa_family_t sin_family;
	in_port_t sin_port;
	struct in_addr sin_addr;
	unsigned char sin_zero[8];
};
struct sockaddr_in6
{
	sa_family_t sin6_family;
	in_port_t sin6_port;
	uint32_t sin6_flowinfo;
	struct in6_addr sin6_addr;
	uint32_t sin6_scope_id;
};
static inline uint16_t wn_bswap16(uint16_t v) { return __builtin_bswap16(v); }
static inline uint32_t wn_bswap32(uint32_t v) { return __builtin_bswap32(v); }
#define htons(x) wn_bswap16(x)
#define ntohs(x) wn_bswap16(x)
#define htonl(x) wn_bswap32(x)
#define ntohl(x) wn_bswap32(x)
