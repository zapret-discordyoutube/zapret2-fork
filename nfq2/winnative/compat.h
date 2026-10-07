// Native Windows (mingw-w64) build of winws2 without cygwin1.dll.
// This header is force-included (-include) before every source file.
// It pulls CRT headers in native mode first, then pretends to be cygwin so that
// the upstream "#ifdef __CYGWIN__" code paths are used without source changes.
#pragma once
#ifndef WINNATIVE_COMPAT_H
#define WINNATIVE_COMPAT_H

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#ifdef WN_COMPAT_IMPL
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#define WIN32_LEAN_AND_MEAN
// keep winsock out. unix style network headers are provided in winnative/include
#define _WINSOCKAPI_
#define _WINSOCK2API_
#endif
#define __USE_MINGW_ANSI_STDIO 1
#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE

#include <stddef.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdbool.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <time.h>
#include <wchar.h>
#include <signal.h>
#include <fcntl.h>
#include <unistd.h>
#include <getopt.h>
#include <libgen.h>
#include <io.h>
#include <direct.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <windows.h>

#define WINNATIVE 1
#ifndef WN_COMPAT_IMPL
#define __CYGWIN__ 1
#endif

// cygwin has 4096. engine buffers are sized by this
#undef PATH_MAX
#define PATH_MAX 4096

#define __BEGIN_DECLS
#define __END_DECLS

// ---- errno values missing in msvcrt
#ifndef ENOBUFS
#define ENOBUFS 105
#endif
#ifndef ESHUTDOWN
#define ESHUTDOWN 110
#endif
#ifndef EAFNOSUPPORT
#define EAFNOSUPPORT 106
#endif
#ifndef ENODEV
#define ENODEV 19
#endif

// ---- types
typedef unsigned int uid_t;
typedef unsigned int gid_t;
#ifndef WN_COMPAT_IMPL
typedef unsigned short sa_family_t;
typedef unsigned short in_port_t;
typedef uint32_t in_addr_t;
typedef int socklen_t;
typedef unsigned char u_char;
typedef unsigned short u_short;
typedef unsigned int u_int;
#endif
typedef uint8_t u_int8_t;
typedef uint16_t u_int16_t;
typedef uint32_t u_int32_t;
typedef uint64_t u_int64_t;

// ---- signals. there are no unix signals on native windows. handlers are installed but never fire
// except Ctrl-C / Ctrl-Break / console close which are mapped to SIGINT/SIGTERM in compat.c
#ifndef SIGHUP
#define SIGHUP 1
#endif
#ifndef SIGUSR1
#define SIGUSR1 30
#endif
#ifndef SIGUSR2
#define SIGUSR2 31
#endif
#ifndef SIGPIPE
#define SIGPIPE 13
#endif
typedef int sigset_t_wn;
#define sigset_t sigset_t_wn
struct siginfo_wn;
typedef struct siginfo_wn siginfo_t;
struct sigaction
{
	void (*sa_handler)(int);
	void (*sa_sigaction)(int, siginfo_t *, void *);
	sigset_t sa_mask;
	int sa_flags;
};
#define SA_RESTART 0
#define SA_SIGINFO 0
int sigaction(int sig, const struct sigaction *act, struct sigaction *oact);
// zapret1 uses signal(). same handler table as sigaction
void (*wn_signal(int sig, void (*handler)(int)))(int);
#define sigemptyset(s) (*(s)=0)
#define sigfillset(s) (*(s)=~0)
#define sigaddset(s,n) (*(s)|=1<<((n)&31))

// ---- libc functions missing in mingw
long wn_random(void);
void wn_srandom(unsigned int seed);
void *memmem(const void *haystack, size_t haystacklen, const void *needle, size_t needlelen);
char *wn_realpath(const char *path, char *resolved);
char *wn_dirname(char *path);
char *wn_basename(char *path);
int chown(const char *path, uid_t owner, gid_t group);
uid_t getuid(void);
int wn_close(int fd);
char *get_current_dir_name(void);
int wn_chdir(const char *path);
int setenv(const char *name, const char *value, int overwrite);
int unsetenv(const char *name);
time_t timegm(struct tm *tm);
char *strndup(const char *s, size_t n);
pid_t fork(void);
pid_t setsid(void);
pid_t getppid(void);
int wn_mkdir(const char *path);
#ifndef O_NONBLOCK
#define O_NONBLOCK 0
#endif
#ifndef O_NOCTTY
#define O_NOCTTY 0
#endif
#ifndef S_IRGRP
#define S_IRGRP 0
#define S_IROTH 0
#define S_IWGRP 0
#define S_IWOTH 0
#endif

// ---- utf-8 aware file api. msvcrt narrow functions use ANSI code page. all strings inside winws2 are utf-8
FILE *wn_fopen(const char *path, const char *mode);
int wn_open(const char *path, int flags, ...);
struct wn_stat
{
	unsigned int st_dev;
	uint64_t st_ino;
	unsigned short st_mode;
	short st_nlink;
	uid_t st_uid;
	gid_t st_gid;
	int64_t st_size;
	struct timespec st_atim, st_mtim, st_ctim;
};
int wn_stat(const char *path, struct wn_stat *st);
int wn_unlink(const char *path);
int wn_access(const char *path, int mode);
int wn_rename(const char *from, const char *to);
#ifndef WN_COMPAT_IMPL
#define random wn_random
#define srandom wn_srandom
#define realpath wn_realpath
#define dirname wn_dirname
#define basename wn_basename
#define close wn_close
#undef signal
#define signal wn_signal
#define chdir wn_chdir
#define mkdir(path,mode) wn_mkdir(path)
// cygwin malloc returns zeroed memory for big blocks and some code relies on it (windivert_filter emptiness check).
// native heap returns garbage. zero everything to stay on the safe side
#define malloc(size) calloc(1,(size))
#define fopen wn_fopen
#define open wn_open
#define unlink wn_unlink
#define access wn_access
#define rename wn_rename
// struct tag and function live in different namespaces. one macro renames both
#define stat wn_stat
#undef st_atime
#undef st_mtime
#undef st_ctime
#define st_atime st_atim.tv_sec
#define st_mtime st_mtim.tv_sec
#define st_ctime st_ctim.tv_sec
#ifndef S_IFLNK
#define S_IFLNK 0xA000
#endif
#ifndef S_IFSOCK
#define S_IFSOCK 0xC000
#endif

// msvcrt has no line buffering : _IOLBF means full buffering and unbuffered stream costs one syscall per character.
// stdout/stderr are kept fully buffered and flushed after every output call
int wn_printf(const char *fmt, ...) __attribute__((format(gnu_printf, 1, 2)));
int wn_fprintf(FILE *f, const char *fmt, ...) __attribute__((format(gnu_printf, 2, 3)));
int wn_vprintf(const char *fmt, va_list ap);
int wn_vfprintf(FILE *f, const char *fmt, va_list ap);
int wn_fputs(const char *s, FILE *f);
int wn_puts(const char *s);
int wn_fputc(int c, FILE *f);
size_t wn_fwrite(const void *ptr, size_t size, size_t n, FILE *f);
#undef printf
#undef fprintf
#undef vprintf
#undef vfprintf
#undef putc
#undef putchar
#define printf wn_printf
#define fprintf wn_fprintf
#define vprintf wn_vprintf
#define vfprintf wn_vfprintf
#define fputs wn_fputs
#define puts wn_puts
#define fputc wn_fputc
#define putc wn_fputc
#define putchar(c) wn_fputc(c, stdout)
#define fwrite wn_fwrite
#define setvbuf wn_setvbuf

// struct tm has no tm_zone in msvcrt. zone name of the last localtime_r/gmtime_r call is kept in compat.c
#undef localtime_r
#undef gmtime_r
#define localtime_r wn_localtime_r
#define gmtime_r wn_gmtime_r
#define tm_zone tm_sec<0 ? "" : wn_last_zone()

// append mode files (debug log, autohostlist) are opened once and kept. upstream opens and closes the file for every line
int wn_fclose(FILE *f);
#define fclose wn_fclose

// unix signals replacement. see "control" in compat.c
DWORD wn_WaitForSingleObject(HANDLE h, DWORD ms);
void wn_control_poll(void);
#define WaitForSingleObject wn_WaitForSingleObject

// batched reinjection of unmodified packets. see batch.c
#include "windivert/windivert.h"
BOOL wn_WinDivertRecvEx(HANDLE handle, VOID *pPacket, UINT packetLen, UINT *pRecvLen, UINT64 flags, WINDIVERT_ADDRESS *pAddr, UINT *pAddrLen, LPOVERLAPPED lpOverlapped);
BOOL wn_WinDivertSend(HANDLE handle, const VOID *pPacket, UINT packetLen, UINT *pSendLen, const WINDIVERT_ADDRESS *pAddr);
#define WinDivertRecvEx wn_WinDivertRecvEx
#define WinDivertSend wn_WinDivertSend

// real entry point is in compat.c. it converts utf-16 command line to utf-8 argv
#define main wn_main
#endif // !WN_COMPAT_IMPL
int wn_main(int argc, char **argv);
int wn_setvbuf(FILE *f, char *buf, int mode, size_t size);
struct tm *wn_localtime_r(const time_t *t, struct tm *tm);
struct tm *wn_gmtime_r(const time_t *t, struct tm *tm);
const char *wn_last_zone(void);

#endif
