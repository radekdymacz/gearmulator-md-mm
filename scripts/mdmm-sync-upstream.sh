#!/bin/sh
# Brings upstream improvements into our fork on a review branch.
#
#   scripts/mdmm-sync-upstream.sh             merge joelanders' release/md-mm-alpha
#   scripts/mdmm-sync-upstream.sh --check     only report how far behind we are
#   scripts/mdmm-sync-upstream.sh <ref>       merge another ref, e.g. gearmulator/main
#
# The merge lands on a new branch sync/upstream-<date> cut from main, never on main itself.
# Nothing is pushed: build, test, then push the branch and open a PR into main.
# Remotes: origin = radekdymacz (ours), upstream = joelanders (the MD/MM line, which itself
# merges dsp56300/gearmulator), gearmulator = dsp56300 (optional, added on demand).
set -eu
cd "$(git rev-parse --show-toplevel)"

BASE=main
check_only=0
SRC=upstream/release/md-mm-alpha
for a in "$@"; do
	case "$a" in
		--check) check_only=1 ;;
		*) SRC="$a" ;;
	esac
done

# A shallow clone has no merge base: fetch the whole history once.
if [ "$(git rev-parse --is-shallow-repository)" = true ]; then
	echo "Shallow clone: fetching the full history (one time)."
	git fetch --unshallow origin
fi

git remote get-url upstream >/dev/null 2>&1 || git remote add upstream https://github.com/joelanders/gearmulator-md-mm.git
case "$SRC" in
	gearmulator/*) git remote get-url gearmulator >/dev/null 2>&1 || git remote add gearmulator https://github.com/dsp56300/gearmulator.git ;;
esac
# Remember conflict resolutions, so the same conflict is solved once.
git config rerere.enabled true
git config rerere.autoupdate true

git fetch --quiet origin
git fetch --quiet "${SRC%%/*}"

# What joelanders is doing next: his open PRs, and which of them would collide with our main.
if [ "${SRC%%/*}" = upstream ] && command -v gh >/dev/null 2>&1; then
	git fetch --quiet upstream '+refs/pull/*/head:refs/remotes/upstream-pr/*' || true
	echo "Open upstream PRs (conflicting files if merged into $BASE):"
	gh pr list -R joelanders/gearmulator-md-mm --state open --json number,title --jq '.[] | "\(.number)\t\(.title)"' |
	while IFS="$(printf '\t')" read -r n title; do
		files=$(git merge-tree --write-tree --name-only --no-messages "$BASE" "upstream-pr/$n" 2>/dev/null | tail -n +2 | tr '\n' ' ')
		echo "  #$n $title: ${files:-no conflict}"
	done
fi

behind=$(git rev-list --count "$BASE..$SRC")
ahead=$(git rev-list --count "$SRC..$BASE")
echo "$BASE is $behind commits behind $SRC and $ahead ahead."
if [ "$behind" -eq 0 ]; then
	echo "Up to date. Nothing to merge."
	exit 0
fi
echo "New upstream commits:"
git log --oneline --no-merges "$BASE..$SRC" | head -40
[ "$check_only" -eq 1 ] && exit 0

if [ -n "$(git status --porcelain --untracked-files=no)" ]; then
	echo "The working tree has changes. Commit or stash them first." >&2
	exit 1
fi

branch="sync/upstream-$(date +%Y-%m-%d)"
if git show-ref --verify --quiet "refs/heads/$branch"; then
	echo "Branch $branch exists already. Delete it or finish it first." >&2
	exit 1
fi
git switch --quiet -c "$branch" "$BASE"

if git merge --no-ff --no-edit -m "Merge $SRC into $BASE ($behind upstream commits)" "$SRC"; then
	echo
	echo "Merged on $branch. Next:"
	echo "  1. Build and run the tests (MD/MM editors, ctest)."
	echo "  2. git push -u origin $branch"
	echo "  3. Open a PR into $BASE."
	exit 0
fi

echo
echo "Merge stopped on conflicts in:"
git diff --name-only --diff-filter=U | sed 's/^/  /'
echo
echo "Fix each file, then: git add <file> && git commit --no-edit"
echo "To give up: git merge --abort && git switch $BASE && git branch -D $branch"
exit 2
