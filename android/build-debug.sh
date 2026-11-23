#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

if ! command -v java >/dev/null 2>&1; then
  echo "ERROR: Java 17 is required." >&2
  exit 2
fi
if ! command -v gradle >/dev/null 2>&1; then
  echo "ERROR: Gradle 8.9+ is required. Install Gradle 8.10.2 and retry." >&2
  exit 2
fi
if [[ -z "${ANDROID_SDK_ROOT:-${ANDROID_HOME:-}}" ]]; then
  echo "ERROR: ANDROID_SDK_ROOT (or ANDROID_HOME) is not set." >&2
  exit 2
fi

cd "$SCRIPT_DIR"
gradle --no-daemon --stacktrace :app:assembleDebug
APK="$SCRIPT_DIR/app/build/outputs/apk/debug/app-debug.apk"
test -s "$APK"
printf '\nAPK created: %s\n' "$APK"
sha256sum "$APK"
