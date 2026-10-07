#!/bin/sh
# Keeps the fork up to date with bol-van/zapret2 : merge upstream, build, smoke test on windows, push.
# Runs from a systemd user timer. Conflicts and build breaks are handed to a headless Claude Code agent;
# if it cannot fix them the merge is rolled back and nothing is pushed.
#
# Environment :
#   WN_WIN_HOST  - ssh host of a windows machine for the smoke test (default : win10). empty = skip
#   WN_WIN_DIR   - directory on that machine with WinDivert.dll (default : C:/winws2-test/exe)
#   WN_AGENT     - command of the agent (default : claude). empty = no agent, just fail

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
	$WN_AGENT -p --model sonnet --permission-mode acceptEdits \
		--allowedTools "Read,Edit,Write,Grep,Glob,Bash(git status:*),Bash(git diff:*),Bash(git log:*),Bash(git show:*),Bash(git add:*),Bash(nfq2/winnative/build.sh:*)" \
		"You maintain a fork of bol-van/zapret2 in $ROOT. The fork adds nfq2/winnative/ : a native Windows (llvm-mingw) build of winws2 without cygwin, done by a force-included compat.h that emulates cygwin, compat.c that replaces the cygwin runtime and batch.c. Upstream files must stay as upstream wrote them : always take the upstream version of upstream files and adapt only files inside nfq2/winnative/. Never run git commit, git push, git merge or git reset. Task: $1 When finished, nfq2/winnative/build.sh must succeed." </dev/null
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
	exit 1
fi
git fetch -q upstream || { log "fetch failed"; exit 1; }
if git merge-base --is-ancestor upstream/master HEAD; then
	log "already up to date"
	exit 0
fi
BEFORE=$(git rev-parse HEAD)
rollback() { git merge --abort 2>/dev/null; git reset -q --hard "$BEFORE"; log "rolled back: $1"; exit 1; }

log "merging $(git rev-parse --short upstream/master)"
mkdir -p "$ROOT/binaries/my"
if ! git merge -q --no-edit upstream/master; then
	agent "git merge of upstream/master stopped with conflicts. Resolve them (see git status), git add the resolved files." || rollback "merge conflict"
	[ -z "$(git diff --name-only --diff-filter=U)" ] || rollback "conflicts left"
	git commit -q --no-edit || rollback "merge commit failed"
fi
if ! build; then
	agent "nfq2/winnative/build.sh fails after merging new upstream commits. The log is binaries/my/build.log. Fix files in nfq2/winnative/ so that it builds." || rollback "build failed"
	build || rollback "build still fails"
	git add nfq2/winnative
	git commit -q -m "winws2 native: adapt compat layer to upstream changes" || true
fi
nfq2/winnative/tests/run.sh >>"$ROOT/binaries/my/build.log" 2>&1 || rollback "host tests failed"
smoke || rollback "smoke test failed"
git push -q origin main || { log "push failed"; exit 1; }
log "synced to $(git describe --tags --always)"
