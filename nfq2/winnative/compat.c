// Native Windows (mingw-w64) replacement of the cygwin runtime functions used by winws2.
// Compiled without the unix style headers from winnative/include. Real winsock is used here.

#define WN_COMPAT_IMPL
#include "compat.h"
#include "include/wordexp.h"
#include "include/sys/utsname.h"
#include "include/sys/cygwin.h"

#define WN_PATH_MAX 32768

// ---------------------------------------------------------------- strings and paths

static wchar_t *u8_to_w(const char *s)
{
	int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
	if (n <= 0) return NULL;
	wchar_t *w = (wchar_t*)malloc(n * sizeof(wchar_t));
	if (w && !MultiByteToWideChar(CP_UTF8, 0, s, -1, w, n))
	{
		free(w);
		w = NULL;
	}
	return w;
}
static char *w_to_u8(const wchar_t *w)
{
	int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
	if (n <= 0) return NULL;
	char *s = (char*)malloc(n);
	if (s && !WideCharToMultiByte(CP_UTF8, 0, w, -1, s, n, NULL, NULL))
	{
		free(s);
		s = NULL;
	}
	return s;
}
static bool is_sep(char c)
{
	return c == '/' || c == '\\';
}
// utf-8 path as it is used inside winws2 => wide windows path. understands /cygdrive/x/ prefix of old configs
static wchar_t *path_to_w(const char *path)
{
	wchar_t *w, *p;
	if (!strncmp(path, "/cygdrive/", 10) && isalpha((unsigned char)path[10]) && (is_sep(path[11]) || !path[11]))
	{
		size_t l = strlen(path);
		char *s = (char*)malloc(l + 2);
		if (!s) return NULL;
		s[0] = path[10];
		s[1] = ':';
		s[2] = '/';
		strcpy(s + 3, path[11] ? path + 12 : "");
		w = u8_to_w(s);
		free(s);
	}
	else
		w = u8_to_w(path);
	if (w)
		for (p = w; *p; p++) if (*p == L'/') *p = L'\\';
	return w;
}
// wide path => absolute wide path. result must be freed
static wchar_t *w_fullpath(const wchar_t *w)
{
	wchar_t *full = (wchar_t*)malloc(WN_PATH_MAX * sizeof(wchar_t));
	if (!full) return NULL;
	DWORD n = GetFullPathNameW(w, WN_PATH_MAX, full, NULL);
	if (!n || n >= WN_PATH_MAX)
	{
		free(full);
		return NULL;
	}
	return full;
}
// wide path => utf-8 with forward slashes, no trailing slash except drive root. result must be freed
static char *w_to_posix(const wchar_t *w)
{
	char *s = w_to_u8(w), *p;
	if (s)
	{
		for (p = s; *p; p++) if (*p == '\\') *p = '/';
		size_t l = strlen(s);
		while (l > 3 && s[l - 1] == '/') s[--l] = 0;
	}
	return s;
}

ssize_t cygwin_conv_path(cygwin_conv_path_t what, const void *from, void *to, size_t size)
{
	wchar_t *w = NULL, *full = NULL;
	char *s = NULL;
	ssize_t res = -1;
	size_t need;

	switch (what & CCP_CONVTYPE_MASK)
	{
	case CCP_WIN_A_TO_POSIX:
	case CCP_WIN_W_TO_POSIX:
		if ((what & CCP_CONVTYPE_MASK) == CCP_WIN_A_TO_POSIX)
		{
			if (!(w = path_to_w((const char*)from))) goto err;
			full = w_fullpath(w);
		}
		else
			full = w_fullpath((const wchar_t*)from);
		if (!full || !(s = w_to_posix(full))) goto err;
		need = strlen(s) + 1;
		if (!size)
			res = (ssize_t)need;
		else if (size >= need)
		{
			memcpy(to, s, need);
			res = 0;
		}
		else
			errno = ENOSPC;
		break;
	case CCP_POSIX_TO_WIN_W:
		if (!(w = path_to_w((const char*)from)) || !(full = w_fullpath(w))) goto err;
		need = (wcslen(full) + 1) * sizeof(wchar_t);
		if (!size)
			res = (ssize_t)need;
		else if (size >= need)
		{
			memcpy(to, full, need);
			res = 0;
		}
		else
			errno = ENOSPC;
		break;
	default:
		errno = EINVAL;
	}
ex:
	free(w);
	free(full);
	free(s);
	return res;
err:
	if (!errno) errno = EINVAL;
	goto ex;
}

char *wn_realpath(const char *path, char *resolved)
{
	wchar_t *w = NULL, *full = NULL;
	char *s = NULL, *res = NULL;

	if (!path || !*path)
	{
		errno = EINVAL;
		return NULL;
	}
	if (!(w = path_to_w(path)) || !(full = w_fullpath(w)))
	{
		errno = ENOENT;
		goto ex;
	}
	if (GetFileAttributesW(full) == INVALID_FILE_ATTRIBUTES)
	{
		errno = ENOENT;
		goto ex;
	}
	if (!(s = w_to_posix(full)))
	{
		errno = ENOMEM;
		goto ex;
	}
	if (resolved)
	{
		if (strlen(s) >= PATH_MAX)
		{
			errno = ENAMETOOLONG;
			goto ex;
		}
		strcpy(resolved, s);
		res = resolved;
	}
	else
	{
		res = s;
		s = NULL;
	}
ex:
	free(w);
	free(full);
	free(s);
	return res;
}

// posix dirname/basename. both separators are accepted. input string is modified
static size_t path_root_len(const char *path)
{
	return (isalpha((unsigned char)path[0]) && path[1] == ':') ? 2 : 0;
}
char *wn_dirname(char *path)
{
	static char dot[] = ".";
	if (!path || !*path) return dot;
	size_t root = path_root_len(path);
	size_t l = strlen(path);
	while (l > root + 1 && is_sep(path[l - 1])) l--;
	while (l > root && !is_sep(path[l - 1])) l--;
	if (l == root)
	{
		// no directory part
		if (!root) return dot;
		path[root] = 0;
		return path;
	}
	while (l > root + 1 && is_sep(path[l - 1])) l--;
	path[l] = 0;
	return path;
}
char *wn_basename(char *path)
{
	static char dot[] = ".";
	if (!path || !*path) return dot;
	size_t root = path_root_len(path);
	size_t l = strlen(path);
	while (l > root + 1 && is_sep(path[l - 1])) path[--l] = 0;
	char *p = path + l;
	while (p > path + root && !is_sep(p[-1])) p--;
	return *p ? p : path + root;
}

// ---------------------------------------------------------------- utf-8 file api

FILE *wn_fopen(const char *path, const char *mode)
{
	wchar_t wmode[16];
	size_t i;
	bool bt = false;
	for (i = 0; mode[i] && i < 12; i++)
	{
		wmode[i] = (unsigned char)mode[i];
		if (mode[i] == 'b' || mode[i] == 't') bt = true;
	}
	// cygwin does not translate line ends
	if (!bt) wmode[i++] = L'b';
	wmode[i] = 0;
	wchar_t *w = path_to_w(path);
	if (!w)
	{
		errno = ENOENT;
		return NULL;
	}
	FILE *F = _wfopen(w, wmode);
	free(w);
	return F;
}
int wn_open(const char *path, int flags, ...)
{
	int mode = 0;
	if (flags & _O_CREAT)
	{
		va_list ap;
		va_start(ap, flags);
		mode = va_arg(ap, int);
		va_end(ap);
	}
	wchar_t *w = path_to_w(path);
	if (!w)
	{
		errno = ENOENT;
		return -1;
	}
	if (!(flags & _O_TEXT)) flags |= _O_BINARY;
	int fd = _wopen(w, flags, mode ? (_S_IREAD | _S_IWRITE) : 0);
	free(w);
	return fd;
}
int wn_stat(const char *path, struct wn_stat *st)
{
	struct __stat64 s;
	wchar_t *w = path_to_w(path);
	if (!w)
	{
		errno = ENOENT;
		return -1;
	}
	// _wstat does not like trailing separators except drive root
	size_t l = wcslen(w);
	while (l > 3 && w[l - 1] == L'\\') w[--l] = 0;
	int r = _wstat64(w, &s);
	if (!r)
	{
		memset(st, 0, sizeof(*st));
		st->st_dev = s.st_dev;
		st->st_ino = s.st_ino;
		st->st_mode = s.st_mode;
		st->st_nlink = s.st_nlink;
		st->st_size = s.st_size;
		st->st_atim.tv_sec = s.st_atime;
		st->st_ctim.tv_sec = s.st_ctime;
		st->st_mtim.tv_sec = s.st_mtime;
		// sub-second part of modification time. reload check relies on seconds only
		WIN32_FILE_ATTRIBUTE_DATA fa;
		if (GetFileAttributesExW(w, GetFileExInfoStandard, &fa))
		{
			uint64_t ft = ((uint64_t)fa.ftLastWriteTime.dwHighDateTime << 32) | fa.ftLastWriteTime.dwLowDateTime;
			st->st_mtim.tv_nsec = (long)((ft % 10000000ULL) * 100);
		}
	}
	free(w);
	return r;
}
int wn_unlink(const char *path)
{
	wchar_t *w = path_to_w(path);
	if (!w)
	{
		errno = ENOENT;
		return -1;
	}
	int r = _wunlink(w);
	free(w);
	return r;
}
int wn_access(const char *path, int mode)
{
	wchar_t *w = path_to_w(path);
	if (!w)
	{
		errno = ENOENT;
		return -1;
	}
	// X_OK is not supported by msvcrt and causes invalid parameter
	int r = _waccess(w, mode & 6);
	free(w);
	return r;
}
int wn_rename(const char *from, const char *to)
{
	wchar_t *w1 = path_to_w(from), *w2 = path_to_w(to);
	int r = -1;
	if (w1 && w2)
	{
		// posix rename replaces existing file
		if (MoveFileExW(w1, w2, MOVEFILE_REPLACE_EXISTING))
			r = 0;
		else
			errno = GetLastError() == ERROR_FILE_NOT_FOUND || GetLastError() == ERROR_PATH_NOT_FOUND ? ENOENT : EACCES;
	}
	else
		errno = ENOENT;
	free(w1);
	free(w2);
	return r;
}
int wn_mkdir(const char *path)
{
	wchar_t *w = path_to_w(path);
	if (!w)
	{
		errno = ENOENT;
		return -1;
	}
	int r = _wmkdir(w);
	free(w);
	return r;
}
int wn_setvbuf(FILE *f, char *buf, int mode, size_t size)
{
	if (mode == _IOLBF) return setvbuf(f, NULL, _IOFBF, 4096);
	return setvbuf(f, buf, mode, size);
}
static inline void std_flush(FILE *f)
{
	if (f == stdout || f == stderr) fflush(f);
}
int wn_vfprintf(FILE *f, const char *fmt, va_list ap)
{
	int r = vfprintf(f, fmt, ap);
	std_flush(f);
	return r;
}
int wn_vprintf(const char *fmt, va_list ap)
{
	return wn_vfprintf(stdout, fmt, ap);
}
int wn_printf(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	int r = wn_vfprintf(stdout, fmt, ap);
	va_end(ap);
	return r;
}
int wn_fprintf(FILE *f, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	int r = wn_vfprintf(f, fmt, ap);
	va_end(ap);
	return r;
}
int wn_fputs(const char *s, FILE *f)
{
	int r = fputs(s, f);
	std_flush(f);
	return r;
}
int wn_puts(const char *s)
{
	int r = puts(s);
	fflush(stdout);
	return r;
}
int wn_fputc(int c, FILE *f)
{
	int r = fputc(c, f);
	if (c == '\n') std_flush(f);
	return r;
}
size_t wn_fwrite(const void *ptr, size_t size, size_t n, FILE *f)
{
	size_t r = fwrite(ptr, size, n, f);
	std_flush(f);
	return r;
}

// ---------------------------------------------------------------- libc

static uint64_t rnd_state = 0x853c49e6748fea9bULL;
void wn_srandom(unsigned int seed)
{
	rnd_state = ((uint64_t)seed << 32) ^ (uint64_t)GetCurrentProcessId() ^ ((uint64_t)GetTickCount() << 16) ^ 0x9e3779b97f4a7c15ULL;
}
long wn_random(void)
{
	// xorshift64*. result is 31 bit like posix random()
	uint64_t x = rnd_state;
	x ^= x >> 12;
	x ^= x << 25;
	x ^= x >> 27;
	rnd_state = x;
	return (long)(((x * 0x2545F4914F6CDD1DULL) >> 33) & 0x7FFFFFFF);
}

BOOLEAN NTAPI SystemFunction036(PVOID RandomBuffer, ULONG RandomBufferLength);
ssize_t getrandom(void *ptr, size_t len, unsigned int flags)
{
	(void)flags;
	if (len > 0x10000000) len = 0x10000000;
	if (!SystemFunction036(ptr, (ULONG)len))
	{
		errno = EIO;
		return -1;
	}
	return (ssize_t)len;
}

void *memmem(const void *haystack, size_t haystacklen, const void *needle, size_t needlelen)
{
	const uint8_t *h = (const uint8_t*)haystack, *n = (const uint8_t*)needle;
	if (!needlelen) return (void*)h;
	if (haystacklen < needlelen) return NULL;
	const uint8_t *end = h + haystacklen - needlelen;
	for (; h <= end; h++)
	{
		h = (const uint8_t*)memchr(h, *n, end - h + 1);
		if (!h) return NULL;
		if (!memcmp(h, n, needlelen)) return (void*)h;
	}
	return NULL;
}
char *strndup(const char *s, size_t n)
{
	size_t l = strnlen(s, n);
	char *d = (char*)malloc(l + 1);
	if (d)
	{
		memcpy(d, s, l);
		d[l] = 0;
	}
	return d;
}
int setenv(const char *name, const char *value, int overwrite)
{
	if (!name || !*name || strchr(name, '='))
	{
		errno = EINVAL;
		return -1;
	}
	if (!overwrite && getenv(name)) return 0;
	size_t l = strlen(name) + strlen(value) + 2;
	char *s = (char*)malloc(l);
	if (!s)
	{
		errno = ENOMEM;
		return -1;
	}
	snprintf(s, l, "%s=%s", name, value);
	// msvcrt copies the string
	int r = _putenv(s);
	free(s);
	return r;
}
int unsetenv(const char *name)
{
	return setenv(name, "", 1);
}
time_t timegm(struct tm *tm)
{
	return _mkgmtime(tm);
}

static char last_zone[16] = "GMT";
struct tm *wn_localtime_r(const time_t *t, struct tm *tm)
{
	struct tm *p = localtime(t);
	if (!p) return NULL;
	*tm = *p;
	struct tm g = *p;
	g.tm_isdst = 0;
	long long off = (long long)_mkgmtime(&g) - (long long)*t;
	long long a = off < 0 ? -off : off;
	if (a % 3600)
		snprintf(last_zone, sizeof(last_zone), "UTC%c%02d:%02d", off < 0 ? '-' : '+', (int)(a / 3600), (int)(a % 3600 / 60));
	else
		snprintf(last_zone, sizeof(last_zone), "UTC%c%02d", off < 0 ? '-' : '+', (int)(a / 3600));
	return tm;
}
struct tm *wn_gmtime_r(const time_t *t, struct tm *tm)
{
	struct tm *p = gmtime(t);
	if (!p) return NULL;
	*tm = *p;
	strcpy(last_zone, "GMT");
	return tm;
}
const char *wn_last_zone(void)
{
	return last_zone;
}

// no process model of unix. --daemon is not supported in native build
pid_t fork(void)
{
	errno = ENOSYS;
	return -1;
}
pid_t setsid(void)
{
	errno = ENOSYS;
	return -1;
}
pid_t getppid(void)
{
	return 0;
}
int chown(const char *path, uid_t owner, gid_t group)
{
	(void)path; (void)owner; (void)group;
	return 0;
}
uid_t getuid(void)
{
	return 0;
}

typedef LONG (WINAPI *t_RtlGetVersion)(OSVERSIONINFOW *);
int uname(struct utsname *u)
{
	OSVERSIONINFOW vi;
	DWORD sz;
	memset(u, 0, sizeof(*u));
	memset(&vi, 0, sizeof(vi));
	vi.dwOSVersionInfoSize = sizeof(vi);
	// GetVersionEx lies without manifest. ntdll tells the truth
	t_RtlGetVersion f = (t_RtlGetVersion)(void*)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion");
	if (f) f(&vi);
	// lua code detects windows by "CYGWIN" prefix of sysname. keep the format of cygwin
	snprintf(u->sysname, sizeof(u->sysname), "CYGWIN_NT-%u.%u-%u", (unsigned)vi.dwMajorVersion, (unsigned)vi.dwMinorVersion, (unsigned)vi.dwBuildNumber);
	snprintf(u->release, sizeof(u->release), "%u.%u.%u", (unsigned)vi.dwMajorVersion, (unsigned)vi.dwMinorVersion, (unsigned)vi.dwBuildNumber);
	snprintf(u->version, sizeof(u->version), "native");
	sz = sizeof(u->nodename);
	if (!GetComputerNameA(u->nodename, &sz)) *u->nodename = 0;
#ifdef _WIN64
	strcpy(u->machine, "x86_64");
#else
	strcpy(u->machine, "i686");
#endif
	return 0;
}

// ---------------------------------------------------------------- signals

#define WN_NSIG 32
static void (*sig_handlers[WN_NSIG])(int);
static bool ctrl_installed = false;

static BOOL WINAPI ctrl_handler(DWORD type)
{
	int sig = (type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT) ? SIGINT : SIGTERM;
	void (*h)(int) = sig_handlers[sig];
	if (!h) return FALSE;
	h(sig);
	// windows kills the process as soon as handler returns on close/logoff/shutdown. give main loop a chance to clean up
	if (type != CTRL_C_EVENT && type != CTRL_BREAK_EVENT) Sleep(3000);
	return TRUE;
}
int sigaction(int sig, const struct sigaction *act, struct sigaction *oact)
{
	if (sig <= 0 || sig >= WN_NSIG)
	{
		errno = EINVAL;
		return -1;
	}
	if (oact)
	{
		memset(oact, 0, sizeof(*oact));
		oact->sa_handler = sig_handlers[sig];
	}
	if (act)
	{
		sig_handlers[sig] = (act->sa_handler == SIG_DFL || act->sa_handler == SIG_IGN) ? NULL : act->sa_handler;
		if (!ctrl_installed && (sig == SIGINT || sig == SIGTERM))
		{
			SetConsoleCtrlHandler(ctrl_handler, TRUE);
			ctrl_installed = true;
		}
	}
	return 0;
}

// ---------------------------------------------------------------- sockets
// winws2 needs sockets only to ask the system which source address is used to reach a target :
// socket(DGRAM) + connect + getsockname + close. fake descriptors keep msvcrt close() away from winsock handles.

#define WN_SOCK_BASE 0x40000000
#define WN_SOCK_MAX 8
static struct
{
	bool used;
	int family;
	struct sockaddr_storage peer;
	int peerlen;
} wn_socks[WN_SOCK_MAX];

int wn_socket(int domain, int type, int protocol)
{
	(void)protocol;
	if (type != SOCK_DGRAM || (domain != AF_INET && domain != AF_INET6))
	{
		errno = EAFNOSUPPORT;
		return -1;
	}
	for (int i = 0; i < WN_SOCK_MAX; i++)
		if (!wn_socks[i].used)
		{
			memset(&wn_socks[i], 0, sizeof(wn_socks[i]));
			wn_socks[i].used = true;
			wn_socks[i].family = domain;
			return WN_SOCK_BASE + i;
		}
	errno = EMFILE;
	return -1;
}
static int sock_idx(int fd)
{
	int i = fd - WN_SOCK_BASE;
	return (i >= 0 && i < WN_SOCK_MAX && wn_socks[i].used) ? i : -1;
}
int wn_connect(int fd, const void *addr, int addrlen)
{
	int i = sock_idx(fd);
	if (i < 0 || addrlen <= 0 || addrlen > (int)sizeof(wn_socks[i].peer))
	{
		errno = EBADF;
		return -1;
	}
	memcpy(&wn_socks[i].peer, addr, addrlen);
	wn_socks[i].peerlen = addrlen;
	return 0;
}
int wn_getsockname(int fd, void *addr, int *addrlen)
{
	int i = sock_idx(fd), r = -1;
	if (i < 0 || !wn_socks[i].peerlen)
	{
		errno = EBADF;
		return -1;
	}
	SOCKET s = WSASocketW(wn_socks[i].family, SOCK_DGRAM, IPPROTO_UDP, NULL, 0, 0);
	if (s == INVALID_SOCKET)
	{
		errno = EAFNOSUPPORT;
		return -1;
	}
	if (!connect(s, (const struct sockaddr*)&wn_socks[i].peer, wn_socks[i].peerlen) && !getsockname(s, (struct sockaddr*)addr, addrlen))
		r = 0;
	else
		errno = ENETUNREACH;
	closesocket(s);
	return r;
}
int wn_getsockopt(int fd, int level, int optname, void *optval, int *optlen)
{
	(void)fd; (void)level; (void)optname; (void)optval; (void)optlen;
	errno = ENOSYS;
	return -1;
}
int wn_setsockopt(int fd, int level, int optname, const void *optval, int optlen)
{
	(void)fd; (void)level; (void)optname; (void)optval; (void)optlen;
	errno = ENOSYS;
	return -1;
}
int wn_close(int fd)
{
	int i = sock_idx(fd);
	if (i >= 0)
	{
		wn_socks[i].used = false;
		return 0;
	}
	return _close(fd);
}
const char *wn_inet_ntop(int af, const void *src, char *dst, size_t size)
{
	return inet_ntop(af, (PVOID)src, dst, size);
}
int wn_inet_pton(int af, const char *src, void *dst)
{
	return inet_pton(af, src, dst);
}

// ---------------------------------------------------------------- wordexp
// shell-like splitting of @config file. supported : whitespace separation, '...', "...", backslash escape,
// $VAR and ${VAR}, ~ at the beginning of a word. command substitution and unquoted shell operators are errors.
// pathname expansion (* ? [) is not performed. characters are taken literally.

struct we_buf
{
	char *p;
	size_t len, cap;
	bool have;
};
static bool we_putc(struct we_buf *b, char c)
{
	if (b->len + 1 >= b->cap)
	{
		size_t ncap = b->cap ? b->cap * 2 : 64;
		char *np = (char*)realloc(b->p, ncap);
		if (!np) return false;
		b->p = np;
		b->cap = ncap;
	}
	b->p[b->len++] = c;
	b->p[b->len] = 0;
	b->have = true;
	return true;
}
static bool we_puts(struct we_buf *b, const char *s)
{
	for (; *s; s++) if (!we_putc(b, *s)) return false;
	return true;
}
static bool we_finish(wordexp_t *we, struct we_buf *b)
{
	if (!b->have) return true;
	char **nv = (char**)realloc(we->we_wordv, (we->we_wordc + 2) * sizeof(char*));
	if (!nv) return false;
	we->we_wordv = nv;
	if (!(nv[we->we_wordc] = strdup(b->p ? b->p : ""))) return false;
	nv[++we->we_wordc] = NULL;
	b->len = 0;
	if (b->p) *b->p = 0;
	b->have = false;
	return true;
}
static bool we_name_start(char c)
{
	return isalpha((unsigned char)c) || c == '_';
}
static bool we_name_char(char c)
{
	return isalnum((unsigned char)c) || c == '_';
}
// *ps points to '$'. on success *ps points to the last consumed char and *val is the value or NULL
static int we_var(const char **ps, const char **val, char *name, size_t namesize)
{
	const char *s = *ps + 1;
	size_t n = 0;
	*val = NULL;
	if (*s == '(') return WRDE_CMDSUB;
	if (*s == '{')
	{
		for (s++; we_name_char(*s); s++)
			if (n + 1 < namesize) name[n++] = *s;
		if (*s != '}' || !n) return WRDE_SYNTAX;
	}
	else if (we_name_start(*s))
	{
		for (; we_name_char(*s); s++)
			if (n + 1 < namesize) name[n++] = *s;
		s--;
	}
	else
	{
		// lone '$' is literal
		*val = "$";
		return 0;
	}
	name[n] = 0;
	*val = getenv(name);
	if (!*val) *val = "";
	*ps = s;
	return 0;
}
void wordfree(wordexp_t *we)
{
	if (we && we->we_wordv)
	{
		for (size_t i = 0; i < we->we_wordc; i++) free(we->we_wordv[i]);
		free(we->we_wordv);
	}
	if (we)
	{
		we->we_wordv = NULL;
		we->we_wordc = 0;
	}
}
int wordexp(const char *s, wordexp_t *we, int flags)
{
	struct we_buf b = { NULL, 0, 0, false };
	char name[128];
	const char *val;
	int r = 0;

	(void)flags;
	we->we_wordc = 0;
	we->we_offs = 0;
	if (!(we->we_wordv = (char**)calloc(1, sizeof(char*)))) return WRDE_NOSPACE;

#define WE_FAIL(code) { r = code; goto err; }
#define WE_PUTC(c) { if (!we_putc(&b, c)) WE_FAIL(WRDE_NOSPACE) }

	for (; *s; s++)
	{
		switch (*s)
		{
		case ' ': case '\t': case '\n': case '\r':
			if (!we_finish(we, &b)) WE_FAIL(WRDE_NOSPACE)
			break;
		case '\'':
			b.have = true;
			for (s++; *s && *s != '\''; s++) WE_PUTC(*s)
			if (!*s) WE_FAIL(WRDE_SYNTAX)
			break;
		case '"':
			b.have = true;
			for (s++; *s && *s != '"'; s++)
			{
				if (*s == '\\' && (s[1] == '$' || s[1] == '`' || s[1] == '"' || s[1] == '\\'))
					WE_PUTC(*++s)
				else if (*s == '`')
					WE_FAIL(WRDE_CMDSUB)
				else if (*s == '$')
				{
					if ((r = we_var(&s, &val, name, sizeof(name)))) goto err;
					if (!we_puts(&b, val)) WE_FAIL(WRDE_NOSPACE)
				}
				else
					WE_PUTC(*s)
			}
			if (!*s) WE_FAIL(WRDE_SYNTAX)
			break;
		case '\\':
			if (s[1])
				WE_PUTC(*++s)
			else
				WE_PUTC('\\')
			break;
		case '$':
			if ((r = we_var(&s, &val, name, sizeof(name)))) goto err;
			// unquoted value is split to fields
			for (; *val; val++)
			{
				if (*val == ' ' || *val == '\t' || *val == '\n')
				{
					if (!we_finish(we, &b)) WE_FAIL(WRDE_NOSPACE)
				}
				else
					WE_PUTC(*val)
			}
			break;
		case '`':
			WE_FAIL(WRDE_CMDSUB)
		case '|': case '&': case ';': case '<': case '>': case '(': case ')': case '{': case '}':
			WE_FAIL(WRDE_BADCHAR)
		case '~':
			if (!b.have && (!s[1] || s[1] == '/' || s[1] == ' ' || s[1] == '\t'))
			{
				val = getenv("HOME");
				if (!val) val = getenv("USERPROFILE");
				if (val)
				{
					if (!we_puts(&b, val)) WE_FAIL(WRDE_NOSPACE)
					break;
				}
			}
			WE_PUTC('~')
			break;
		default:
			WE_PUTC(*s)
		}
	}
	if (!we_finish(we, &b)) WE_FAIL(WRDE_NOSPACE)
	free(b.p);
	return 0;
err:
	free(b.p);
	wordfree(we);
	return r;
}

// ---------------------------------------------------------------- command line
// cygwin1.dll does not use the windows argument splitter. it parses the raw command line itself and it also
// expands @file arguments (response files) before main() is called. that is how "winws2 @config" really works
// in the cygwin build : the engine never sees '@'. the same rules are reproduced here :
// - separators are space, tab, CR, LF
// - '...' and "..." group characters. inside "..." a backslash protects the next char and "" means one quote
// - inside quotes \\ and \<quote> are escapes unless the word looks like a dos path (X:... or \\...)
// - outside quotes a backslash protects the next char if the word has quotes or wildcard chars and is not a dos path
// - @file is replaced by the content of the file. nesting is allowed. unlike cygwin "@file" in quotes works too
// pathname expansion is not performed. wildcards, braces and ~ are taken literally.

#define WN_AT_FILE_LEVEL 10

struct arg_vec
{
	char **v;
	int c, cap;
};
static bool av_add(struct arg_vec *av, char *word)
{
	if (av->c + 2 > av->cap)
	{
		int ncap = av->cap ? av->cap * 2 : 64;
		char **nv = (char**)realloc(av->v, ncap * sizeof(char*));
		if (!nv) return false;
		av->v = nv;
		av->cap = ncap;
	}
	av->v[av->c++] = word;
	av->v[av->c] = NULL;
	return true;
}
static bool cl_issep(char c)
{
	return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}
static bool cl_isquote(char c)
{
	return c == '"' || c == '\'';
}
static bool cl_dos_path(const char *s)
{
	return (isalpha((unsigned char)s[0]) && s[1] == ':') || (s[0] == '\\' && s[1] == '\\');
}
// cmd points to opening quote. strip both quotes in place. returns position after the quoted text
static char *cl_quoted_strip(char *cmd)
{
	char quote = *cmd, *p;
	memmove(cmd, cmd + 1, strlen(cmd + 1) + 1);
	if ((p = strchr(cmd, quote)))
		memmove(p, p + 1, strlen(p + 1) + 1);
	else
		p = cmd + strlen(cmd);
	return p;
}
// cmd points to opening quote. quotes are kept for the later pass. returns position after closing quote
static char *cl_quoted_keep(char *cmd)
{
	char quote = *cmd, *p;
	const char *stop = quote == '\'' ? "'" : "\\\"";
	while (*cmd && *++cmd)
	{
		if (!(p = strpbrk(cmd, stop)))
			return cmd + strlen(cmd); // no closing quote
		if (*p == '\\')
			cmd = p + 1;
		else if (quote == '"' && p[1] == '"')
		{
			// doubled quote is a quoted quote
			*p = '\\';
			cmd = p + 1;
		}
		else
			return p + 1;
	}
	return cmd;
}
// remove quotes and escapes from a word. result must be freed
static char *cl_unquote(const char *word)
{
	if (*word != '~' && !strpbrk(word, "?*[\"'(){}")) return strdup(word);

	bool dos = cl_dos_path(word);
	if (!dos && cl_isquote(*word) && word[1] && word[2]) dos = cl_dos_path(word + 1);

	char *pat = (char*)malloc(strlen(word) * 2 + 2), *p = pat, *res, *d;
	const char *s;
	if (!pat) return NULL;
	// step 1 : every protected char gets a backslash
	for (s = word; *s; s++)
	{
		if (!cl_isquote(*s))
		{
			if (dos && *s == '\\') *p++ = '\\';
			*p++ = *s;
		}
		else
		{
			char quote = *s;
			while (*++s && *s != quote)
			{
				if (!dos && *s == '\\' && (s[1] == quote || s[1] == '\\')) s++;
				*p++ = '\\';
				*p++ = *s;
			}
			if (!*s) break;
		}
	}
	*p = 0;
	// step 2 : backslash protected chars become plain chars
	if ((res = (char*)malloc(strlen(pat) + 1)))
	{
		for (s = pat, d = res; *s; s++)
		{
			if (*s == '\\' && s[1]) s++;
			*d++ = *s;
		}
		*d = 0;
	}
	free(pat);
	return res;
}
static char *cl_read_file(const char *name, const char *tail)
{
	FILE *F = wn_fopen(name, "rb");
	if (!F) return NULL;
	char *buf = NULL;
	long size;
	if (!fseek(F, 0, SEEK_END) && (size = ftell(F)) >= 0 && !fseek(F, 0, SEEK_SET))
	{
		size_t tl = strlen(tail);
		if ((buf = (char*)malloc((size_t)size + tl + 2)))
		{
			size_t rd = fread(buf, 1, (size_t)size, F);
			// zero byte ends the text, as it would in cygwin
			rd = strnlen(buf, rd);
			buf[rd] = ' ';
			memcpy(buf + rd + 1, tail, tl + 1);
		}
	}
	fclose(F);
	return buf;
}
// cmd is modified. every word is added as a separate allocated string
static bool cl_parse(char *cmd, struct arg_vec *av, int level)
{
	while (*cmd)
	{
		if (cl_issep(*cmd))
		{
			cmd++;
			continue;
		}
		char *word = cmd, *sawquote = NULL;
		while (*cmd)
		{
			if (cl_isquote(*cmd))
			{
				sawquote = cmd;
				cmd = (av->c > 0 && *word != '@') ? cl_quoted_keep(cmd) : cl_quoted_strip(cmd);
			}
			else
				cmd++;
			if (cl_issep(*cmd)) break;
		}
		if (*cmd) *cmd++ = 0;

		char *arg = av->c ? cl_unquote(word) : strdup(word);
		if (!arg) return false;
		// cygwin expands only unquoted @file. "@file with spaces" is passed to the engine, which is limited to 16K
		// and uses another parser. here the quoted form is expanded too
		const char *atfile = (*word == '@' && sawquote != word) ? word + 1 : (cl_isquote(*word) && *arg == '@') ? arg + 1 : NULL;
		if (av->c && atfile && level < WN_AT_FILE_LEVEL)
		{
			char *content = cl_read_file(atfile, cmd);
			if (content)
			{
				free(arg);
				bool b = cl_parse(content, av, level + 1);
				free(content);
				return b;
			}
		}
		if (!av_add(av, arg)) return false;
	}
	return true;
}

// ---------------------------------------------------------------- entry point

int main(void)
{
	struct arg_vec av = { NULL, 0, 0 };
	char *cmdline = w_to_u8(GetCommandLineW());
	if (!cmdline || !cl_parse(cmdline, &av, 0) || av.c < 1) return 1;
	free(cmdline);
	int argc = av.c;
	char **argv = av.v;

	// cygwin always passes full path of the executable. EXEDIR depends on it
	wchar_t *wexe = (wchar_t*)malloc(WN_PATH_MAX * sizeof(wchar_t));
	if (wexe)
	{
		DWORD n = GetModuleFileNameW(NULL, wexe, WN_PATH_MAX);
		char *exe = (n && n < WN_PATH_MAX) ? w_to_posix(wexe) : NULL;
		if (exe) argv[0] = exe;
		free(wexe);
	}

	// no line end translation, like in cygwin
	_fmode = _O_BINARY;
	_setmode(_fileno(stdout), _O_BINARY);
	_setmode(_fileno(stderr), _O_BINARY);
	// all text inside is utf-8
	SetConsoleOutputCP(CP_UTF8);

	WSADATA wsa;
	WSAStartup(MAKEWORD(2, 2), &wsa);

	return wn_main(argc, argv);
}
