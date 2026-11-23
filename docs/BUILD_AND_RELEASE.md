# Nimbus Engine — Build and Release Guide

## 1. Linux Host Build & Test Verification (`Makefile`)
Tested on GCC 12.2 (`-std=c++20 -Wall -Wextra -Wpedantic -Werror`):

```bash
# Debug build + examples + 283-check test suite (uses libluajit-5.1.so.2 via dlopen)
make -j$(nproc) all test examples

# Release optimized build (-O2 -DNDEBUG) + tests + examples
make -j$(nproc) BUILD=release all test examples

# AddressSanitizer + UndefinedBehaviorSanitizer
make -j$(nproc) SANITIZE=asan all test

# ThreadSanitizer
make -j$(nproc) SANITIZE=tsan all test

# Individual simulation examples
./build/debug-none/nimbus_example_fps_arena
./build/debug-none/nimbus_example_ai_patrol
```

## 2. Cross-Platform CMake Build
For hosts with CMake >= 3.20:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DNIMBUS_WARNINGS_AS_ERRORS=ON \
  -DNIMBUS_ENABLE_LUA=ON \
  -DNIMBUS_BUILD_EDITOR=ON \
  -DNIMBUS_BUILD_TESTS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

## 3. Nimbus Studio Web / PWA & Express MCP Bridge (Port 3000)
Requires `nimbus_mcp_server` built via Section 1 (`make all`):

```bash
npm install
npm run lint     # TypeScript type-check (tsc --noEmit)
npm run build    # Production Vite SPA + asset bundle into dist/
npm run start    # Express server + MCP stdio bridge on http://0.0.0.0:3000
```

Workspace projects created via the UI or REST API are stored strictly under `./projects/` with exported `.nbpk` + Lua bundles under `./projects/_exports/`.

## 4. Android Dual-Flavor APK Builds (CI / SDK Host)
Requires Android SDK 35, NDK `27.2.12479018`, CMake `3.22.1`, JDK 17, Gradle `8.10.2` (not present in this offline container; configured in `.github/workflows/android-apk.yml`):

```bash
cd android
./gradlew :app:assembleRuntimeDebug :app:assembleEditorDebug
```

- `runtime` flavor (`com.nimbus.engine`): standalone engine runtime APK
- `editor` flavor (`com.nimbus.editor`): mobile editor APK (`IS_EDITOR=true`, links `nimbus_editor`)
