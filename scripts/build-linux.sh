#!/usr/bin/env bash
set -euo pipefail

MODE="${1:-debug}"
JOBS="${JOBS:-$(nproc 2>/dev/null || echo 2)}"

case "$MODE" in
  debug)
    make -j"$JOBS" BUILD=debug SANITIZE=none all test
    ;;
  release)
    make -j"$JOBS" BUILD=release SANITIZE=none all test
    ;;
  asan)
    make -j"$JOBS" BUILD=debug SANITIZE=asan all test
    ;;
  tsan)
    make -j"$JOBS" BUILD=debug SANITIZE=tsan all test
    ;;
  mcp)
    make -j"$JOBS" BUILD=debug SANITIZE=none mcp
    ;;
  *)
    echo "Usage: $0 [debug|release|asan|tsan|mcp]" >&2
    exit 2
    ;;
esac
