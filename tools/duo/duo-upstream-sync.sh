#!/usr/bin/env bash
# SPDX-FileCopyrightText: Copyright 2026 igawa6
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Merge a new Eden nightly source commit into Eden Duo.
#
#   duo-upstream-sync.sh --dry-run <upstream-commit>   predict conflicts; writes no branch/files
#   duo-upstream-sync.sh <upstream-commit>             create sync/eden-<sha> and merge into it
#
# Never pushes, never touches the current branch. Resolve listed conflicts by hand, build,
# test, then update .upstream/eden.json (release_tag/url) before committing the merge.
set -euo pipefail

dry=0
[[ ${1:-} == --dry-run ]] && { dry=1; shift; }
new=${1:?usage: duo-upstream-sync.sh [--dry-run] <upstream-commit>}
ref=${DUO_REF:-HEAD}

old=$(git show "$ref:.upstream/eden.json" | python3 -c 'import json,sys; print(json.load(sys.stdin)["source_commit"])')
for c in "$old" "$new"; do
    git cat-file -e "$c^{commit}" 2>/dev/null || git fetch --no-tags origin "$c"
done
new=$(git rev-parse "$new^{commit}")
git merge-base --is-ancestor "$old" "$new" || { echo "$new does not descend from $old" >&2; exit 2; }

echo "Upstream $old -> $new: $(git rev-list --count "$old..$new") commits"

# Files Duo replaces wholesale: always keep Duo's side, and re-drop upstream files Duo deleted.
# Android branding is an overlay in src/android/app/src/duo/res; the src/main/res copies are
# upstream's again and must take upstream's changes.
owned=(README.md CONTRIBUTING.md docs/policies/AI.md .github/PULL_REQUEST_TEMPLATE.md
       '.github/ISSUE_TEMPLATE/*')
mapfile -t dropped < <(git diff --no-renames --name-only --diff-filter=D "$old" "$ref")

echo "--- textual conflicts (explicit merge base $old)"
{ git merge-tree --write-tree --name-only --merge-base="$old" "$ref" "$new" || true; } | sed -n '2,/^$/p' |
    while read -r p; do
        tag=""
        for o in "${owned[@]}" "${dropped[@]}"; do [[ $p == $o ]] && tag="  (auto: Duo-owned)"; done
        if [[ -n $p ]]; then echo "  $p$tag"; fi
    done

echo "--- upstream changed files Duo also edits (check APIs even when they merge cleanly)"
comm -12 <(git diff --name-only "$old" "$new" | sort) \
         <(git diff --no-renames --name-only --diff-filter=M "$old" "$ref" | sort) | sed 's/^/  /'

if (( dry )); then exit 0; fi

[[ -z $(git status --porcelain --untracked-files=no) ]] || { echo "working tree not clean" >&2; exit 2; }
branch="sync/eden-${new:0:10}"
git config rerere.enabled true
git config rerere.autoupdate true
git config merge.ours.driver true
attrs="$(git rev-parse --git-path info/attributes)"
mkdir -p "$(dirname "$attrs")"
for o in "${owned[@]}"; do
    grep -qxF "$o merge=ours" "$attrs" 2>/dev/null || echo "$o merge=ours" >> "$attrs"
done

start=$(git rev-parse "$ref")
if ! git merge-base --is-ancestor "$old" "$start"; then
    # Ancestry-only bridge: same tree as Duo, second parent = the upstream commit it was synced to.
    start=$(git commit-tree "$start^{tree}" -p "$start" -p "$old" \
        -m "Record Eden upstream ${old:0:10} as merged (tree unchanged)")
    echo "bridge commit $start"
fi
git switch -c "$branch" "$start"
git merge --no-ff --no-commit "$new" || true
for d in "${dropped[@]}"; do git rm -q --ignore-unmatch -- "$d"; done
python3 - "$new" <<'EOF'
import json, sys
p = '.upstream/eden.json'; d = json.load(open(p))
d['source_commit'] = sys.argv[1]; d['release_tag'] = ''; d['release_url'] = ''
open(p, 'w').write(json.dumps(d, indent=2) + '\n')
EOF
git add .upstream/eden.json
echo "--- remaining conflicts on $branch"
git diff --name-only --diff-filter=U | sed 's/^/  /'
echo "Fill release_tag/release_url in .upstream/eden.json, build, test, then git commit."
