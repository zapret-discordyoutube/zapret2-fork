// host side test driver for args.c
//   test_args cl <command line>      : prints words separated by \0 (argv[0] excluded)
//   test_args we <text>              : compares wn_wordexp with the wordexp of the host libc
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wordexp.h>
#include "../args.h"

int wn_wordexp(const char *words, wordexp_t *we, int flags);
void wn_wordfree(wordexp_t *we);

int main(int argc, char **argv)
{
	if (argc != 3) return 2;
	if (!strcmp(argv[1], "cl"))
	{
		struct wn_arg_vec av = { NULL, 0, 0 };
		char *cmd = strdup(argv[2]);
		if (!wn_cl_parse(cmd, &av, 0)) return 3;
		for (int i = 1; i < av.c; i++)
		{
			fputs(av.v[i], stdout);
			fputc(0, stdout);
		}
		return 0;
	}
	wordexp_t a, b;
	int ra = wordexp(argv[2], &a, WRDE_NOCMD), rb = wn_wordexp(argv[2], &b, WRDE_NOCMD);
	if (!!ra != !!rb)
	{
		printf("status differs: libc=%d ours=%d\n", ra, rb);
		return 1;
	}
	if (ra) return 0;
	if (a.we_wordc != b.we_wordc)
	{
		printf("word count differs: libc=%zu ours=%zu\n", a.we_wordc, b.we_wordc);
		return 1;
	}
	for (size_t i = 0; i < a.we_wordc; i++)
		if (strcmp(a.we_wordv[i], b.we_wordv[i]))
		{
			printf("word %zu differs: libc='%s' ours='%s'\n", i, a.we_wordv[i], b.we_wordv[i]);
			return 1;
		}
	return 0;
}
