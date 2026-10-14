#!/usr/bin/env bash
# SPDX-FileCopyrightText: Copyright 2026 igawa6
# SPDX-License-Identifier: GPL-3.0-or-later
#
# List every path where Eden Duo differs from the Eden upstream commit recorded in
# .upstream/eden.json, split into Duo-only files and upstream-owned files that Duo edits.
#
#   duo-touched-ledger.sh                 print the TSV ledger for HEAD
#   duo-touched-ledger.sh --check LEDGER  fail (exit 1) when HEAD edits an upstream-owned file
#                                         that LEDGER does not list (new sync hotspot)
#
# Read-only: it runs git diff/show only and writes nothing to the repository.
set -euo pipefail

ref=${DUO_REF:-HEAD}
base=$(git show "$ref:.upstream/eden.json" | python3 -c 'import json,sys; print(json.load(sys.stdin)["source_commit"])')
git cat-file -e "$base^{commit}" 2>/dev/null || {
    echo "upstream commit $base is not in this repository; git fetch origin $base first" >&2
    exit 2
}

# Files Duo replaces wholesale (identity/branding). Kept out of the hook count.
branding_re='^(README\.md|CONTRIBUTING\.md|\.github/|docs/policies/AI\.md|src/android/app/src/main/res/(mipmap-[a-z]+|drawable)/(ic_launcher|ic_yuzu)[^/]*\.png|src/android/app/src/main/res/values/colors\.xml|src/android/app/src/main/res/layout(-w600dp)?/fragment_about\.xml)'

ledger() {
    git diff --no-renames --name-status "$base" "$ref" | while IFS=$'\t' read -r st path; do
        case $st in
            A) cat=duo-only ;;
            D) continue ;; # upstream files Duo deletes are re-dropped by the sync script
            *) if [[ $path =~ $branding_re ]]; then cat=branding; else cat=upstream-hook; fi ;;
        esac
        if [[ $st == M ]]; then
            hunks=$(git diff -U0 "$base" "$ref" -- "$path" | grep -c '^@@' || true)
            read -r add del _ < <(git diff --numstat "$base" "$ref" -- "$path")
        else
            hunks=-; read -r add del _ < <(git diff --numstat "$base" "$ref" -- "$path")
        fi
        printf '%s\t%s\t%s\t%s\t%s\t%s\n' "$cat" "$st" "$hunks" "$add" "$del" "$path"
    done
}

if [[ ${1:-} == --check ]]; then
    known=${2:?usage: --check LEDGER}
    new=$(ledger | awk -F'\t' '$1=="upstream-hook"||$1=="branding"{print $6}' |
          grep -Fvx -f <(awk -F'\t' '{print $NF}' "$known") || true)
    if [[ -n $new ]]; then
        echo "Upstream-owned files newly edited by Duo (add a hook file instead, or extend the ledger):"
        echo "$new"
        exit 1
    fi
    echo "ok: no new upstream-owned files edited (base $base)"
    exit 0
fi

printf 'category\tstatus\thunks\tadded\tdeleted\tpath\n'
ledger | sort -t$'\t' -k1,1 -k3,3nr
