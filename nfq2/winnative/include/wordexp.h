// winnative: shell-like word splitting used for @config files
#pragma once
typedef struct
{
	size_t we_wordc;
	char **we_wordv;
	size_t we_offs;
} wordexp_t;
#define WRDE_NOCMD 4
#define WRDE_NOSPACE 1
#define WRDE_BADCHAR 2
#define WRDE_BADVAL 3
#define WRDE_CMDSUB 4
#define WRDE_SYNTAX 5
int wordexp(const char *words, wordexp_t *pwordexp, int flags);
void wordfree(wordexp_t *pwordexp);
