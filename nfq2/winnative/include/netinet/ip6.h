// winnative: BSD style ipv6 headers
#pragma once
#include <netinet/in.h>
struct ip6_hdr
{
	union
	{
		struct ip6_hdrctl
		{
			uint32_t ip6_un1_flow;
			uint16_t ip6_un1_plen;
			uint8_t ip6_un1_nxt;
			uint8_t ip6_un1_hlim;
		} ip6_un1;
		uint8_t ip6_un2_vfc;
	} ip6_ctlun;
	struct in6_addr ip6_src;
	struct in6_addr ip6_dst;
} __attribute__((packed));
#define ip6_vfc ip6_ctlun.ip6_un2_vfc
#define ip6_flow ip6_ctlun.ip6_un1.ip6_un1_flow
#define ip6_plen ip6_ctlun.ip6_un1.ip6_un1_plen
#define ip6_nxt ip6_ctlun.ip6_un1.ip6_un1_nxt
#define ip6_hlim ip6_ctlun.ip6_un1.ip6_un1_hlim
#define ip6_hops ip6_ctlun.ip6_un1.ip6_un1_hlim
struct ip6_ext
{
	uint8_t ip6e_nxt;
	uint8_t ip6e_len;
} __attribute__((packed));
struct ip6_hbh
{
	uint8_t ip6h_nxt;
	uint8_t ip6h_len;
} __attribute__((packed));
struct ip6_dest
{
	uint8_t ip6d_nxt;
	uint8_t ip6d_len;
} __attribute__((packed));
struct ip6_rthdr
{
	uint8_t ip6r_nxt;
	uint8_t ip6r_len;
	uint8_t ip6r_type;
	uint8_t ip6r_segleft;
} __attribute__((packed));
struct ip6_frag
{
	uint8_t ip6f_nxt;
	uint8_t ip6f_reserved;
	uint16_t ip6f_offlg;
	uint32_t ip6f_ident;
} __attribute__((packed));
// network byte order masks, little endian host
#define IP6F_OFF_MASK 0xf8ff
#define IP6F_RESERVED_MASK 0x0600
#define IP6F_MORE_FRAG 0x0100
