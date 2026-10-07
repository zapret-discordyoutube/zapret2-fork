// Native Windows (mingw-w64) replacement of the cygwin runtime functions used by winws2.
// Compiled without the unix style headers from winnative/include. Real winsock is used here.

#define WN_COMPAT_IMPL
#include "compat.h"
#include "include/wordexp.h"
#include "include/sys/utsname.h"
#include "include/sys/cygwin.h"
#include "args.h"

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

// ---- append mode cache
// upstream writes every log line as fopen("at") + fprintf + fclose. on windows that is several syscalls,
// path conversion and antivirus scan per line. here the file stays open, fclose() only flushes.
// the file is opened with full sharing. if somebody deletes it, it is reopened (checked once per second).
#define WN_APPEND_MAX 4
static struct
{
	char *path;
	FILE *F;
	DWORD checked;
} wn_append[WN_APPEND_MAX];

static FILE *append_open(const wchar_t *w, bool binary_plus)
{
	HANDLE h = CreateFileW(w, binary_plus ? (GENERIC_READ | FILE_APPEND_DATA) : FILE_APPEND_DATA,
		FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (h == INVALID_HANDLE_VALUE)
	{
		errno = GetLastError() == ERROR_ACCESS_DENIED ? EACCES : ENOENT;
		return NULL;
	}
	int fd = _open_osfhandle((intptr_t)h, _O_APPEND | _O_BINARY);
	if (fd < 0)
	{
		CloseHandle(h);
		return NULL;
	}
	FILE *F = _fdopen(fd, "ab");
	if (!F) _close(fd);
	return F;
}
static bool append_stale(FILE *F)
{
	// deleted file with an open handle stays in "delete pending" state
	FILE_STANDARD_INFO si;
	HANDLE h = (HANDLE)_get_osfhandle(_fileno(F));
	return h == INVALID_HANDLE_VALUE || !GetFileInformationByHandleEx(h, FileStandardInfo, &si, sizeof(si)) || si.DeletePending || !si.NumberOfLinks;
}
static FILE *append_get(const char *path, const char *mode)
{
	int i, free_slot = -1;
	for (i = 0; i < WN_APPEND_MAX; i++)
	{
		if (!wn_append[i].path)
		{
			if (free_slot < 0) free_slot = i;
		}
		else if (!strcmp(wn_append[i].path, path))
			return wn_append[i].F;
	}
	wchar_t *w = path_to_w(path);
	if (!w)
	{
		errno = ENOENT;
		return NULL;
	}
	FILE *F = append_open(w, strchr(mode, '+') != NULL);
	free(w);
	if (F && free_slot >= 0 && (wn_append[free_slot].path = strdup(path)))
	{
		wn_append[free_slot].F = F;
		wn_append[free_slot].checked = GetTickCount();
	}
	// no free slot : the caller gets an ordinary stream and fclose() really closes it
	return F;
}
int wn_fclose(FILE *f)
{
	for (int i = 0; i < WN_APPEND_MAX; i++)
		if (wn_append[i].path && wn_append[i].F == f)
		{
			int r = fflush(f);
			DWORD now = GetTickCount();
			if (now - wn_append[i].checked >= 1000)
			{
				wn_append[i].checked = now;
				if (append_stale(f))
				{
					fclose(f);
					free(wn_append[i].path);
					wn_append[i].path = NULL;
					wn_append[i].F = NULL;
				}
			}
			return r;
		}
	return fclose(f);
}

FILE *wn_fopen(const char *path, const char *mode)
{
	if (mode[0] == 'a') return append_get(path, mode);

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
static void ctl_init(void);
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
		ctl_init();
		if (!ctrl_installed && (sig == SIGINT || sig == SIGTERM))
		{
			SetConsoleCtrlHandler(ctrl_handler, TRUE);
			ctrl_installed = true;
		}
	}
	return 0;
}

// ---------------------------------------------------------------- control
// there is no kill -HUP on windows. every unix signal of winws2 gets a named event :
//   Global\winws2_sig_<pid>_hup   reload lists
//   Global\winws2_sig_<pid>_usr1  dump conntrack
//   Global\winws2_sig_<pid>_usr2  dump autohostlist counters and ip cache
//   Global\winws2_sig_<pid>_term  graceful stop
// events are polled from the main thread (at most every 50 ms), so handlers run in the main thread.
// "winws2 --wn-signal=<hup|usr1|usr2|term>[:<pid>]" sets the event. without pid - for every running winws2.

static const struct
{
	const char *name;
	int sig;
} wn_ctl_sigs[] = { { "hup", SIGHUP }, { "usr1", SIGUSR1 }, { "usr2", SIGUSR2 }, { "term", SIGTERM } };
#define WN_CTL_COUNT (sizeof(wn_ctl_sigs) / sizeof(*wn_ctl_sigs))
static HANDLE wn_ctl_events[WN_CTL_COUNT];
static bool wn_ctl_ready = false;
static DWORD wn_ctl_tick = 0;

static void ctl_event_name(char *buf, size_t size, DWORD pid, const char *name)
{
	snprintf(buf, size, "Global\\winws2_sig_%u_%s", (unsigned)pid, name);
}
static void ctl_init(void)
{
	char name[64];
	if (wn_ctl_ready) return;
	wn_ctl_ready = true;
	for (size_t i = 0; i < WN_CTL_COUNT; i++)
	{
		ctl_event_name(name, sizeof(name), GetCurrentProcessId(), wn_ctl_sigs[i].name);
		wn_ctl_events[i] = CreateEventA(NULL, FALSE, FALSE, name);
	}
}
void wn_control_poll(void)
{
	if (!wn_ctl_ready) return;
	DWORD now = GetTickCount();
	if (now - wn_ctl_tick < 50) return;
	wn_ctl_tick = now;
	for (size_t i = 0; i < WN_CTL_COUNT; i++)
		if (wn_ctl_events[i] && WaitForSingleObject(wn_ctl_events[i], 0) == WAIT_OBJECT_0)
		{
			void (*h)(int) = sig_handlers[wn_ctl_sigs[i].sig];
			if (h) h(wn_ctl_sigs[i].sig);
		}
}
DWORD wn_WaitForSingleObject(HANDLE h, DWORD ms)
{
	DWORD r = WaitForSingleObject(h, ms);
	wn_control_poll();
	return r;
}
// returns process exit code
static int ctl_send(const char *spec)
{
	char sig[16], name[64];
	const char *colon = strchr(spec, ':');
	size_t l = colon ? (size_t)(colon - spec) : strlen(spec), i;
	DWORD pid = colon ? (DWORD)strtoul(colon + 1, NULL, 10) : 0;
	int sent = 0;

	if (l >= sizeof(sig)) l = sizeof(sig) - 1;
	memcpy(sig, spec, l);
	sig[l] = 0;
	for (i = 0; i < WN_CTL_COUNT && strcmp(sig, wn_ctl_sigs[i].name); i++);
	if (i == WN_CTL_COUNT || (colon && !pid))
	{
		fprintf(stderr, "usage: --wn-signal=<hup|usr1|usr2|term>[:<pid>]\n");
		return 2;
	}
	// event names contain pid. without pid try every process
	DWORD pids[4096], cb = 0, n = 1;
	if (pid)
		pids[0] = pid;
	else
	{
		BOOL WINAPI K32EnumProcesses(DWORD *lpidProcess, DWORD cb, DWORD *lpcbNeeded);
		if (!K32EnumProcesses(pids, sizeof(pids), &cb)) cb = 0;
		n = cb / sizeof(DWORD);
	}
	for (DWORD k = 0; k < n; k++)
	{
		if (pids[k] == GetCurrentProcessId()) continue;
		ctl_event_name(name, sizeof(name), pids[k], sig);
		HANDLE h = OpenEventA(EVENT_MODIFY_STATE, FALSE, name);
		if (h)
		{
			if (SetEvent(h))
			{
				printf("%s sent to pid %u\n", sig, (unsigned)pids[k]);
				sent++;
			}
			CloseHandle(h);
		}
	}
	if (!sent) fprintf(stderr, "no running winws2 accepted the signal\n");
	return sent ? 0 : 1;
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

// ---------------------------------------------------------------- entry point

int main(void)
{
	struct wn_arg_vec av = { NULL, 0, 0 };
	char *cmdline = w_to_u8(GetCommandLineW());
	if (!cmdline || !wn_cl_parse(cmdline, &av, 0) || av.c < 1) return 1;
	free(cmdline);
	int argc = av.c;
	char **argv = av.v;

	if (argc >= 2 && !strncmp(argv[1], "--wn-signal=", 12)) return ctl_send(argv[1] + 12);

	// "$file", and "@file" that was not expanded above, are read by upstream code into a 16K buffer.
	// longer file is silently cut there. read the whole file here, with the same word splitting rules.
	if (argc >= 2 && (argv[1][0] == '@' || argv[1][0] == '$') && argv[1][1])
	{
		char *text = wn_cl_read_file(argv[1] + 1, "");
		if (text)
		{
			wordexp_t we;
			for (char *p = text; *p; p++) if (*p == '\n' || *p == '\r' || *p == '\t') *p = ' ';
			if (wordexp(text, &we, WRDE_NOCMD))
			{
				fprintf(stderr, "failed to split command line options from file '%s'\n", argv[1] + 1);
				return 1;
			}
			free(text);
			struct wn_arg_vec nv = { NULL, 0, 0 };
			if (!wn_av_add(&nv, argv[0])) return 1;
			for (size_t i = 0; i < we.we_wordc; i++)
				if (!wn_av_add(&nv, we.we_wordv[i])) return 1;
			argc = nv.c;
			argv = nv.v;
		}
	}

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
