#!/bin/bash
# The fork's footprint in upstream's files (doc/modern-ux/UPSTREAM.md).
#
# Lists every file that exists at the merge base with upstream and that this tree changes, with the
# lines added and removed, and the totals. Our own files (added since the merge base) are not
# listed: they cannot conflict. Exits 1 if an upstream file was deleted (restore it and keep it out
# of the build in our own files instead), 2 on a usage or git error.
#
# Usage: scripts/mdmm-upstream-footprint.sh [--committed] [upstream-ref]
#   --committed   compare HEAD, not the working tree (uncommitted changes left out)
#   upstream-ref  default: upstream/release/md-mm-alpha

set -euo pipefail

target=()
upstream_ref="upstream/release/md-mm-alpha"
for arg in "$@"; do
  case "${arg}" in
    --committed) target=(HEAD) ;;
    -h|--help) sed -n '2,12p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    -*) echo "unknown option: ${arg}" >&2; exit 2 ;;
    *) upstream_ref="${arg}" ;;
  esac
done

cd "$(git rev-parse --show-toplevel)"
if ! base="$(git merge-base HEAD "${upstream_ref}")"; then
  echo "no merge base between HEAD and ${upstream_ref} (fetch the upstream remote first)" >&2
  exit 2
fi

echo "Upstream-owned files changed vs $(git rev-parse --short "${base}") (merge base with ${upstream_ref}), ${target[*]:-working tree}:"
echo

files=0
added=0
removed=0
deleted=0
while IFS=$'\t' read -r status path; do
  [[ -z "${path}" ]] && continue
  read -r a r _ < <(git diff --numstat --no-renames "${base}" ${target[@]+"${target[@]}"} -- "${path}")
  # Binary files have no line counts ("-").
  [[ "${a}" == "-" ]] && a=0
  [[ "${r}" == "-" ]] && r=0
  label="modified"
  if [[ "${status}" == D ]]; then
    label="DELETED"
    deleted=$((deleted + 1))
  fi
  printf '  %-9s %6s %6s  %s\n' "${label}" "+${a}" "-${r}" "${path}"
  added=$((added + a))
  removed=$((removed + r))
  files=$((files + 1))
done < <(git diff --name-status --no-renames --diff-filter=MDT "${base}" ${target[@]+"${target[@]}"})

echo
echo "Total: ${files} upstream files, +${added} -${removed} lines, ${deleted} deleted"
if [[ "${deleted}" -gt 0 ]]; then
  echo "Upstream files were deleted: restore them (git checkout ${base} -- <file>) and keep them out of our build in our own files." >&2
  exit 1
fi
