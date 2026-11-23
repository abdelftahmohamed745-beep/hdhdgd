#!/data/data/com.termux/files/usr/bin/bash
set -Eeuo pipefail

REPO='https://github.com/abdelftahmohamed745-beep/nimbus-engine.-.git'
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT"

for cmd in git gh; do
  if ! command -v "$cmd" >/dev/null 2>&1; then
    printf 'Missing command: %s\n' "$cmd" >&2
    printf 'In Termux run: pkg install git gh -y\n' >&2
    exit 1
  fi
done

if ! gh auth status --hostname github.com >/dev/null 2>&1; then
  echo 'GitHub sign-in will open a one-time device authorization flow.'
  gh auth login --hostname github.com --git-protocol https --web
fi

gh auth setup-git

if [ ! -d .git ]; then
  git init
fi
git branch -M main

LOGIN="$(gh api user --jq .login)"
USER_ID="$(gh api user --jq .id)"
git config user.name "$LOGIN"
git config user.email "${USER_ID}+${LOGIN}@users.noreply.github.com"

if git remote get-url origin >/dev/null 2>&1; then
  git remote set-url origin "$REPO"
else
  git remote add origin "$REPO"
fi

# Never overwrite an existing remote branch or force-push the user's work.
if [ -n "$(git ls-remote --heads origin refs/heads/main)" ]; then
  echo 'STOP: origin/main already exists. No push was attempted.' >&2
  echo 'The target repository is no longer empty; review/merge it instead of overwriting it.' >&2
  exit 2
fi

git add -A
if git diff --cached --quiet; then
  echo 'No source changes to publish.'
  exit 0
fi

git commit -m 'Add Nimbus Engine source and Android APK CI'
git push --set-upstream origin main

cat <<'EOF'

Published Nimbus source to GitHub.
The Android workflow should start automatically on this first push.
Open: https://github.com/abdelftahmohamed745-beep/nimbus-engine.-/actions
When the run succeeds, download the APK from the workflow artifact or the Releases page.
EOF
