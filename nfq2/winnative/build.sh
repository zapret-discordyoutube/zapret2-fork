#!/bin/sh
# Native Windows build of winws2.exe without cygwin1.dll. Cross compiled on Linux by llvm-mingw.
#
#   nfq2/winnative/build.sh [x86_64|i686]
#
# Environment :
#   WN_TOOLCHAIN - directory of llvm-mingw (msvcrt flavour). default : ~/tools/llvm-mingw
#   WN_DEPS      - where luajit and zlib are downloaded and built. default : ~/tools/winws2-deps
#   WN_OPTIMIZE  - optimization flags. default : -O2
# Result : binaries/my/winnative-<arch>/winws2.exe

set -e

ARCH=${1:-x86_64}
case "$ARCH" in
	x86_64) BITS=64; LJ_CPU= ;;
	i686) BITS=32; LJ_CPU="-msse2 -mfpmath=sse" ;;
	*) echo "unknown arch $ARCH" >&2; exit 1 ;;
esac

HERE=$(cd "$(dirname "$0")" && pwd)
NFQ2=$(dirname "$HERE")
ROOT=$(dirname "$NFQ2")
WN_TOOLCHAIN=${WN_TOOLCHAIN:-$HOME/tools/llvm-mingw}
WN_DEPS=${WN_DEPS:-$HOME/tools/winws2-deps}
WN_OPTIMIZE=${WN_OPTIMIZE:--O2}
LUAJIT_RELEASE=2.1-20250826
ZLIB_RELEASE=1.3.2
TRIPLE=$ARCH-w64-mingw32
DEPS=$WN_DEPS/$ARCH
OUT=$ROOT/binaries/my/winnative-$ARCH
OBJ=$DEPS/obj

PATH=$WN_TOOLCHAIN/bin:$PATH
export PATH
command -v $TRIPLE-clang >/dev/null || { echo "llvm-mingw not found in $WN_TOOLCHAIN" >&2; exit 1; }

mkdir -p "$DEPS/src" "$DEPS/lib" "$DEPS/include" "$OUT" "$OBJ"

# luajit is rebuilt when the compat header changes
LJ_STAMP=$DEPS/lib/luajit.stamp
LJ_KEY="$LUAJIT_RELEASE $WN_OPTIMIZE $(cksum < "$HERE/luajit_compat.h")"
if [ ! -f "$DEPS/lib/libluajit-5.1.a" ] || [ "$(cat "$LJ_STAMP" 2>/dev/null)" != "$LJ_KEY" ]; then
	LJ=$DEPS/src/luajit2-$LUAJIT_RELEASE
	[ -d "$LJ" ] || curl -fsSL https://github.com/openresty/luajit2/archive/refs/tags/v$LUAJIT_RELEASE.tar.gz | tar -xz -C "$DEPS/src"
	make -C "$LJ/src" clean >/dev/null 2>&1 || true
	[ $BITS = 32 ] && HOSTCC="gcc -m32" || HOSTCC=gcc
	make -C "$LJ/src" -j"$(nproc)" HOST_CC="$HOSTCC" CROSS=$TRIPLE- TARGET_SYS=Windows BUILDMODE=static \
		XCFLAGS="-DLUAJIT_DISABLE_FFI" CCOPT="$WN_OPTIMIZE -fomit-frame-pointer" \
		TARGET_CFLAGS="$LJ_CPU -include $HERE/luajit_compat.h" libluajit.a >/dev/null
	mkdir -p "$DEPS/include/luajit-2.1"
	cp "$LJ/src/lua.h" "$LJ/src/lualib.h" "$LJ/src/lauxlib.h" "$LJ/src/luaconf.h" "$LJ/src/luajit.h" "$DEPS/include/luajit-2.1/"
	cp "$LJ/src/libluajit.a" "$DEPS/lib/libluajit-5.1.a"
	echo "$LJ_KEY" > "$LJ_STAMP"
fi

if [ ! -f "$DEPS/lib/libz.a" ]; then
	Z=$DEPS/src/zlib-$ZLIB_RELEASE
	[ -d "$Z" ] || curl -fsSL https://github.com/madler/zlib/archive/refs/tags/v$ZLIB_RELEASE.tar.gz | tar -xz -C "$DEPS/src"
	make -C "$Z" -f win32/Makefile.gcc clean >/dev/null 2>&1 || true
	make -C "$Z" -f win32/Makefile.gcc -j"$(nproc)" PREFIX=$TRIPLE- CFLAGS="$WN_OPTIMIZE" libz.a >/dev/null
	cp "$Z/libz.a" "$DEPS/lib/"
	cp "$Z/zlib.h" "$Z/zconf.h" "$DEPS/include/"
fi

# the gui recognizes the banner "github version <ver> (<hash>)"
GH_VER=$(git -C "$ROOT" describe --tags --always 2>/dev/null || echo unknown)
GH_HASH=$(git -C "$ROOT" rev-parse HEAD 2>/dev/null || echo unknown)

CFLAGS="-std=gnu99 $WN_OPTIMIZE -flto -ffunction-sections -fdata-sections $LJ_CPU \
	-Wno-address-of-packed-member -Wno-parentheses -Wno-pointer-sign -Wno-logical-op-parentheses -Wno-dangling-else \
	-I$DEPS/include -I$DEPS/include/luajit-2.1 -DLUAJIT -DZAPRET_GH_VER=$GH_VER-native -DZAPRET_GH_HASH=$GH_HASH"

cd "$NFQ2"
rm -f "$OBJ"/*.o
for f in *.c crypto/*.c; do
	$TRIPLE-clang $CFLAGS -include winnative/compat.h -Iwinnative/include -Iwindows -c "$f" -o "$OBJ/$(echo "$f" | tr / _).o"
done
$TRIPLE-clang $CFLAGS -c winnative/compat.c -o "$OBJ/winnative_compat.o"
$TRIPLE-clang $CFLAGS -include winnative/compat.h -Iwinnative/include -Iwindows -c winnative/batch.c -o "$OBJ/winnative_batch.o"

$TRIPLE-clang $CFLAGS -static -Wl,--gc-sections -Wl,--nxcompat -Wl,--dynamicbase -s \
	-o "$OUT/winws2.exe" "$OBJ"/*.o windows/res/winws_res$BITS.o \
	-L"$DEPS/lib" -lluajit-5.1 -lz -Lwindows/windivert -lwindivert$BITS \
	-lole32 -loleaut32 -liphlpapi -lntdll -lws2_32 -ladvapi32 -luuid -lwinpthread

ls -l "$OUT/winws2.exe"
