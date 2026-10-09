#!/usr/bin/env python3
# Windows side stress test of the graceful stop. Run as administrator on a windows machine :
#
#   python stop_race.py <dir with winws2.exe and WinDivert.dll> <@config file> <working dir> [cycles]
#
# The config must capture outgoing udp to 203.0.113.0/24 (any --wf-udp-out range above 40000 does).
#
# What it catches. The engine waits for a packet with an overlapped read and wakes up every 50 ms
# to look at the stop request. If it quits while the read is still pending, the receive buffer is freed
# but the driver still owns it : a packet that comes a moment later is written into freed heap memory.
# The process then dies in ntdll heap code with an access violation at 0xFFFFFFFFFFFFFFFF (or 0xC0000374).
#
# Every cycle : start the engine, keep silence, request the stop, then send a packet exactly on the system
# clock tick at which the 50 ms wait of the engine times out. Unfixed engine crashes in 5-10 cycles of 100.
# Exit code of the script is the number of cycles that did not end with exit code 0.

import ctypes
import os
import socket
import subprocess
import sys
import tempfile
import time

exe_dir, config, work_dir = sys.argv[1], sys.argv[2], sys.argv[3]
cycles = int(sys.argv[4]) if len(sys.argv) > 4 else 100

k32 = ctypes.WinDLL("kernel32", use_last_error=True)
k32.OpenEventW.restype = ctypes.c_void_p
k32.SetEvent.argtypes = [ctypes.c_void_p]
k32.CloseHandle.argtypes = [ctypes.c_void_p]
k32.GetTickCount64.restype = ctypes.c_uint64

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.setsockopt(socket.IPPROTO_IP, 14, 1)  # IP_DONTFRAGMENT : header bytes that do not look like a pointer
payload = b"\xff" * 1200
out_path = os.path.join(tempfile.gettempdir(), "winws2_stop_race.log")


def send(n):
    for j in range(n):
        try:
            sock.sendto(payload, ("203.0.113.%d" % (1 + j % 200), 40000 + j))
        except OSError:
            pass


results = {}
for it in range(cycles):
    with open(out_path, "wb") as f:
        p = subprocess.Popen([os.path.join(exe_dir, "winws2.exe"), "@" + config], cwd=work_dir, stdout=f, stderr=f,
                             stdin=subprocess.DEVNULL, creationflags=0x08000000)
    started = time.time()
    ready = False
    while time.time() - started < 10 and p.poll() is None and not ready:
        with open(out_path, "rb") as f:
            ready = b"capture is started" in f.read()
        time.sleep(0.02)
    if not ready:
        if p.poll() is None:
            p.kill()
        p.wait()
        key = "not started"
    else:
        time.sleep(0.3)
        send(1)  # the engine starts its 50 ms wait from this packet
        t_sync = time.perf_counter()
        while time.perf_counter() - t_sync < 0.030:
            pass
        h = k32.OpenEventW(2, False, "Global\\winws2_sig_%d_term" % p.pid)
        k32.SetEvent(h)
        k32.CloseHandle(h)
        # 0.6 ms : the engine starts its wait a bit later than the packet was sent. without it the tick
        # can be caught before the engine times out when the system timer resolution is 1 ms
        while time.perf_counter() - t_sync < 0.0506:
            pass
        tick = k32.GetTickCount64()
        while k32.GetTickCount64() == tick:
            pass
        send(3)
        try:
            key = "0x%X" % (p.wait(timeout=5) & 0xFFFFFFFF)
        except subprocess.TimeoutExpired:
            p.kill()
            p.wait()
            key = "hang"
    results[key] = results.get(key, 0) + 1
    if key != "0x0":
        print("cycle", it, key, flush=True)

print("cycles", cycles, results)
sys.exit(cycles - results.get("0x0", 0))
