#!/usr/bin/env python3
"""Tests of command line / config file parsing of the native winws2 build. Run by tests/run.sh."""
import json
import os
import random
import shlex
import subprocess
import sys
import tempfile

BIN = sys.argv[1]
HERE = os.path.dirname(os.path.abspath(__file__))
failed = 0


def fail(text):
    global failed
    failed += 1
    if failed <= 10:
        print("FAIL:", text)


def cl(cmdline):
    out = subprocess.run([BIN, "cl", cmdline], capture_output=True).stdout
    return [w.decode("utf-8", "replace") for w in out.split(b"\0")[:-1]]


with tempfile.TemporaryDirectory() as tmp:
    cfg = os.path.join(tmp, "c.txt")

    # 1. what ZapretGUI writes : one shlex.quote'd argument per line. no backslash here, see test 2
    random.seed(1)
    alpha = "ab Z9'\"$`*?[]{}()~|&;<>=:,./-_@#!%^+ я\t"
    for _ in range(2000):
        args = ["".join(random.choice(alpha) for _ in range(random.randint(1, 12))) for _ in range(random.randint(1, 6))]
        args = [a for a in args if a.strip()] or ["a"]
        with open(cfg, "w", encoding="utf-8") as f:
            f.write("\n".join(shlex.quote(a) for a in args) + "\n")
        got = cl("x.exe @" + cfg)
        if got != args:
            fail(f"shlex round trip: {args!r} -> {got!r}")

    # 2. quirks of cygwin recorded from the real cygwin build of winws2 (profile names it printed)
    golden = json.load(open(os.path.join(HERE, "cygwin_golden.json"), encoding="utf-8"))
    for case in golden:
        with open(cfg, "w", encoding="utf-8") as f:
            f.write("--a\n" + case["arg"] + "\n--lua-desync=pass\n--new\n--name=SECOND\n--lua-desync=pass\n")
        names = [w[len("--name="):] for w in cl("x.exe @" + cfg) if w.startswith("--name=")]
        if names != case["cygwin_names"]:
            fail(f"cygwin parity: {case['arg']!r}: cygwin {case['cygwin_names']!r}, ours {names!r}")

    # 3. file bigger than 16K is read completely, tail of the command line follows the file content
    big = [f"--arg{i}={'x' * 40}" for i in range(1000)]
    with open(cfg, "w", encoding="utf-8") as f:
        f.write("\n".join(big) + "\n")
    got = cl(f"x.exe @{cfg} --tail")
    if got != big + ["--tail"]:
        fail("big config file is not read completely")
    with open(os.path.join(tmp, "with space.txt"), "w") as f:
        f.write("--inside\n")
    if cl(f'x.exe "@{tmp}/with space.txt"') != ["--inside"]:
        fail("quoted @file with spaces is not expanded")
    if cl("x.exe @/nonexistent/file --x") != ["@/nonexistent/file", "--x"]:
        fail("missing @file must be passed through")

# 4. wordexp against libc on shell-like text without pathname expansion characters
os.environ["WN_T"] = "va lue"
alpha = "ab Z'\"\\$ {}()|&;<>=:,./-_\t"
random.seed(2)
samples = ["$WN_T", '"$WN_T"', "${WN_T}x", "a\\ b", "'a b' c", '"a\\"b"', "a'b", "$(x)", "`x`", "a;b", "$", "a$ b"]
samples += ["".join(random.choice(alpha) for _ in range(random.randint(1, 14))) for _ in range(3000)]
for text in samples:
    if "${" in text and "${WN_T}" not in text:
        continue  # only plain ${NAME} is supported
    if "$$" in text or "''" in text or '""' in text:
        continue  # $$ is not supported. glibc splits a word after empty quotes, posix shell does not
    proc = subprocess.run([BIN, "we", text], capture_output=True, text=True)
    if proc.returncode:
        fail(f"wordexp {text!r}: {proc.stdout.strip()}")

print("args tests:", "FAILED %d" % failed if failed else "ok")
sys.exit(1 if failed else 0)
