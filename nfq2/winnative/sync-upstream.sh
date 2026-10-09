#!/bin/sh
# Keeps the fork up to date with bol-van/zapret2 : merge upstream, build, smoke test on windows, push.
# Runs from a systemd user timer. Conflicts and build breaks are handed to a headless Claude Code agent;
# if it cannot fix them the merge is rolled back and nothing is pushed.
#
# Environment :
#   WN_WIN_HOST  - ssh host of a windows machine for the smoke test (default : win10). empty = skip
#   WN_WIN_DIR   - directory on that machine with WinDivert.dll (default : C:/winws2-test/exe)
#   WN_AGENT     - command of the agent (default : claude). empty = no agent, just fail
#
# Exit codes tell the caller why nothing was synced (engine_autoupdate.py of ZapretGUI maps them to messages) :
#   0 synced or nothing to do      10 somebody works in the tree    11 no network (fetch or push)
#   12 merge conflict              13 build failed                  14 host tests failed
#   15 smoke test on windows failed

set -u

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
WN_WIN_HOST=${WN_WIN_HOST-win10}
WN_WIN_DIR=${WN_WIN_DIR-C:/winws2-test/exe}
WN_AGENT=${WN_AGENT-claude}
EXE=$ROOT/binaries/my/winnative-x86_64/winws2.exe

cd "$ROOT" || exit 1
log() { echo "$(date '+%F %T') $*"; }

agent() {
	[ -n "$WN_AGENT" ] || return 1
	log "asking agent: $1"
	# the prompt goes through stdin : --allowedTools takes a list and would swallow a positional prompt
	printf '%s\n' "You maintain a fork of bol-van/zapret2 in $ROOT. The fork adds nfq2/winnative/ : a native Windows (llvm-mingw) build of winws2 without cygwin, done by a force-included compat.h that emulates cygwin, compat.c that replaces the cygwin runtime, args.c and batch.c. The fork also carries a few small bug fixes in upstream files as separate commits (see git log). When upstream changes the same place, take the upstream version of the code and keep our fix only if it is still needed and still correct; otherwise adapt only files inside nfq2/winnative/. Never run git commit, git push, git merge or git reset. Task: $1 When finished, nfq2/winnative/build.sh must succeed." |
		$WN_AGENT -p --model sonnet --permission-mode acceptEdits \
			--allowedTools "Read,Edit,Write,Grep,Glob,Bash(git status:*),Bash(git diff:*),Bash(git log:*),Bash(git show:*),Bash(git add:*),Bash(nfq2/winnative/build.sh:*),Bash(bash nfq2/winnative/build.sh:*),Bash(sh nfq2/winnative/build.sh:*)"
}

build() { nfq2/winnative/build.sh >"$ROOT/binaries/my/build.log" 2>&1; }

smoke() {
	[ -n "$WN_WIN_HOST" ] || return 0
	scp -q "$EXE" "$WN_WIN_HOST:$WN_WIN_DIR/winws2_sync.exe" || return 1
	out=$(ssh "$WN_WIN_HOST" "cd /d $(echo "$WN_WIN_DIR" | tr / '\\') && winws2_sync.exe --dry-run --wf-dup-check=0 --wf-tcp-out=443 --lua-desync=pass" 2>&1)
	echo "$out" | grep -q "command line parameters verified"
}

if [ -n "$(git status --porcelain --untracked-files=no)" ]; then
	log "working tree is not clean. stop"
	exit 10
fi
git fetch -q upstream || { log "fetch failed"; exit 11; }
if git merge-base --is-ancestor upstream/master HEAD; then
	log "already up to date"
	# a push that failed last time must not be forgotten : the engine release refers to this commit
	if [ -n "$(git rev-list -n 1 origin/main..HEAD 2>/dev/null)" ]; then
		git push -q origin main || { log "push failed"; exit 11; }
		log "pushed commits left from the previous run"
	fi
	exit 0
fi
BEFORE=$(git rev-parse HEAD)
rollback() { git merge --abort 2>/dev/null; git reset -q --hard "$BEFORE"; log "rolled back: $2"; exit "$1"; }

log "merging $(git rev-parse --short upstream/master)"
mkdir -p "$ROOT/binaries/my"
if ! git merge -q --no-edit upstream/master; then
	agent "git merge of upstream/master stopped with conflicts. Resolve them (see git status), git add the resolved files." || rollback 12 "merge conflict"
	[ -z "$(git diff --name-only --diff-filter=U)" ] || rollback 12 "conflicts left"
	git commit -q --no-edit || rollback 12 "merge commit failed"
fi
if ! build; then
	agent "nfq2/winnative/build.sh fails after merging new upstream commits. The log is binaries/my/build.log. Fix files in nfq2/winnative/ so that it builds." || rollback 13 "build failed"
	build || rollback 13 "build still fails"
	git add nfq2/winnative
	git commit -q -m "winws2 native: adapt compat layer to upstream changes" || true
fi
nfq2/winnative/tests/run.sh >>"$ROOT/binaries/my/build.log" 2>&1 || rollback 14 "host tests failed"
smoke || rollback 15 "smoke test failed"
git push -q origin main || { log "push failed"; exit 11; }
log "synced to $(git describe --tags --always)"
