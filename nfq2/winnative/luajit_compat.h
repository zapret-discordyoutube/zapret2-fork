// force-included into LuaJIT sources. makes io.open, loadfile, os.remove, os.rename work with utf-8 paths
#pragma once
#include <stdio.h>
FILE *wn_fopen(const char *path, const char *mode);
int wn_unlink(const char *path);
int wn_rename(const char *from, const char *to);
#define fopen wn_fopen
#define remove wn_unlink
#define rename wn_rename
