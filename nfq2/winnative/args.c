// Argument parsing of the native winws2 build : cygwin compatible command line / @file splitting and wordexp.
// This file has no windows dependencies and is also compiled on the build host by tests/run.sh.

#ifdef _WIN32
#define WN_COMPAT_IMPL
#include "compat.h"
#include "include/wordexp.h"
#else
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>
#include <wordexp.h>
#define wn_fopen fopen
#undef wordexp
#undef wordfree
#define wordexp wn_wordexp
#define wordfree wn_wordfree
#endif
#include "args.h"

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
			if (!s[1]) WE_FAIL(WRDE_SYNTAX)
			WE_PUTC(*++s)
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

bool wn_av_add(struct wn_arg_vec *av, char *word)
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
char *wn_cl_read_file(const char *name, const char *tail)
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
bool wn_cl_parse(char *cmd, struct wn_arg_vec *av, int level)
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
			char *content = wn_cl_read_file(atfile, cmd);
			if (content)
			{
				free(arg);
				bool b = wn_cl_parse(content, av, level + 1);
				free(content);
				return b;
			}
		}
		if (!wn_av_add(av, arg)) return false;
	}
	return true;
}
