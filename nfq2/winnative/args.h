#pragma once
#include <stdbool.h>

struct wn_arg_vec
{
	char **v;
	int c, cap;
};
bool wn_av_add(struct wn_arg_vec *av, char *word);
// file content followed by a space and tail. NULL if the file cannot be read. result must be freed
char *wn_cl_read_file(const char *name, const char *tail);
// split command line the way cygwin does. cmd is modified. every word is added as a separate allocated string
bool wn_cl_parse(char *cmd, struct wn_arg_vec *av, int level);
