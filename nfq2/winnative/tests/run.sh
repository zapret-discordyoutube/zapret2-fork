#!/bin/sh
# Host side tests of the native winws2 build. No windows needed.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
${HOSTCC:-cc} -O1 -Wall -o "$TMP/test_args" "$HERE/test_args.c" "$HERE/../args.c"
python3 "$HERE/test_args.py" "$TMP/test_args"
