// winnative: minimal unix style socket definitions. no real sockets are used by winws2
#pragma once
#define AF_UNSPEC 0
#define AF_INET 2
#define AF_INET6 23
#define PF_INET AF_INET
#define PF_INET6 AF_INET6
#define SOCK_STREAM 1
#define SOCK_DGRAM 2
#define SOCK_RAW 3
struct sockaddr
{
	sa_family_t sa_family;
	char sa_data[14];
};
struct sockaddr_storage
{
	sa_family_t ss_family;
	char _ss_pad1[6];
	int64_t _ss_align;
	char _ss_pad2[112];
};
#define SOL_SOCKET 0xffff
#define SO_SNDBUF 0x1001
#define SO_RCVBUF 0x1002
int wn_socket(int domain, int type, int protocol);
#define socket wn_socket
int wn_connect(int fd, const void *addr, int addrlen);
int wn_getsockname(int fd, void *addr, int *addrlen);
int wn_getsockopt(int fd, int level, int optname, void *optval, int *optlen);
int wn_setsockopt(int fd, int level, int optname, const void *optval, int optlen);
#define connect wn_connect
#define getsockname wn_getsockname
#define getsockopt wn_getsockopt
#define setsockopt wn_setsockopt
