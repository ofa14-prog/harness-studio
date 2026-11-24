#!/usr/bin/env bash
set -euo pipefail

repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$repo_dir"

remote="${PUSH_REMOTE:-origin}"
branch="$(git symbolic-ref --short HEAD)"

if [[ "$branch" != "main" ]]; then
  echo "Push yalnızca main dalından yapılabilir (mevcut: $branch)." >&2
  exit 1
fi
if ! git remote get-url "$remote" >/dev/null 2>&1; then
  echo "'$remote' remote'u bulunamadı." >&2
  exit 1
fi
if [[ -n "$(git status --porcelain)" ]]; then
  echo "Önce değişiklikleri commit edin; push yalnızca temiz çalışma ağacında yapılır." >&2
  exit 1
fi

echo "main dalı $remote remote'una gönderiliyor..."
git push "$remote" main
