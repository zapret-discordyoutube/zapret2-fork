// winnative: path conversion replacement. "posix" paths are utf-8 windows paths with forward slashes
#pragma once
typedef unsigned int cygwin_conv_path_t;
#define CCP_POSIX_TO_WIN_A 0
#define CCP_POSIX_TO_WIN_W 1
#define CCP_WIN_A_TO_POSIX 2
#define CCP_WIN_W_TO_POSIX 3
#define CCP_CONVTYPE_MASK 3
#define CCP_ABSOLUTE 0
#define CCP_RELATIVE 0x100
ssize_t cygwin_conv_path(cygwin_conv_path_t what, const void *from, void *to, size_t size);
