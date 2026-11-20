#!/usr/bin/env bash
# Delete ONLY remote branches whose tip is already contained in origin/main.
# Usage: bash tools/cleanup-merged-branches.sh             # inspect
#        bash tools/cleanup-merged-branches.sh --execute   # delete merged remote branches
set -euo pipefail
mode="${1:---dry-run}"
if [[ "$mode" != "--dry-run" && "$mode" != "--execute" ]]; then
  echo "Usage: $0 [--dry-run|--execute]" >&2
  exit 2
fi
git rev-parse --is-inside-work-tree >/dev/null
git remote get-url origin >/dev/null
git fetch --prune origin
git rev-parse --verify origin/main >/dev/null
echo "Main: $(git rev-parse --short origin/main)"
echo "Mode: $mode"
while IFS= read -r branch; do
  [[ -n "$branch" ]] || continue
  case "$branch" in
    main|HEAD|crosspod-renderer-linux-link|crosspoint-gfxrenderer-compile)
      echo "KEEP: $branch"
      continue
      ;;
  esac
  if git merge-base --is-ancestor "refs/remotes/origin/$branch" refs/remotes/origin/main; then
    if [[ "$mode" == "--execute" ]]; then
      echo "DELETE: $branch (fully merged)"
      git push origin --delete "$branch"
    else
      echo "WOULD DELETE: $branch (fully merged)"
    fi
  else
    echo "KEEP: $branch (contains commits not on main)"
  fi
done < <(git for-each-ref --format='%(refname:strip=3)' refs/remotes/origin | sort)
