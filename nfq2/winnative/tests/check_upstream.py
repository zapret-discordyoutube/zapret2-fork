#!/usr/bin/env python3
# The native layer relies on details of upstream code that a merge can silently change. Check them.
#
# batch.c holds unmodified packets until the next receive. On quit and when the logical network disappears
# windivert_recv() returns before receiving, and the caller frees the receive buffer and closes the handle.
# The held packets are sent by usleep : windivert_recv_exit() has to call it before it looks at the exit conditions.
import re
import sys

nfq2 = sys.argv[1]
src = open(nfq2 + "/darkmagic.c", encoding="utf-8", errors="replace").read()
m = re.search(r"static bool windivert_recv_exit\(void\)\s*\{(.*?)\n\}", src, re.S)
if not m:
    sys.exit("upstream check: windivert_recv_exit() not found in darkmagic.c")
body = m.group(1)
u, q, n = body.find("usleep("), body.find("bQuit"), body.find("logical_net_filter_match")
if u < 0 or q < 0 or n < 0 or not u < q or not u < n:
    sys.exit("upstream check: windivert_recv_exit() must call usleep() before checking bQuit and the logical network. "
             "batch.c flushes held packets in usleep, see the comment there")
m = re.search(r"static bool windivert_recv_filter\(.*?\)\s*\{(.*?)\n\}", src, re.S)
if not m or not 0 <= m.group(1).find("windivert_recv_exit()") < m.group(1).find("WinDivertRecvEx("):
    sys.exit("upstream check: windivert_recv_filter() must call windivert_recv_exit() before WinDivertRecvEx()")
print("upstream checks: ok")
