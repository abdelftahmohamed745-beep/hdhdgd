# Nimbus Engine

Nimbus is an experimental C++20 game-engine codebase with an Android-oriented
architecture. It currently contains a small runtime, archetype ECS, scene
hierarchy, custom physics, CPU animation/particles/terrain/audio systems, an
asset pack and OBJ importer, a retained UI tree, a C++ editor facade, and an
MCP stdio server.

**Nimbus is not currently a production-complete game engine.** In particular,
the supplied source tree does not include an Android application project or
verified Vulkan/OpenGL ES runtime. See [the implementation status](docs/STATUS.md)
before relying on any subsystem.

## Repository layout

- `engine/core/`: math, fixed-step timing, logging, and job workers.
- `engine/ecs/`: header-only archetype ECS and component storage.
- `engine/scene/`: entity hierarchy, transform propagation, JSON scene files.
- `engine/rhi/`: rendering-device contract, optional Vulkan/GLES source backends,
  and a headless fallback.
- `engine/render/`: camera, mesh/material submission, and render statistics.
- `engine/physics/`, `anim/`, `particles/`, `terrain/`, `audio/`, `ai/`, `net/`,
  `ui/`, `asset/`, `script/`: focused engine subsystems.
- `editor/`: project/scene editor facade, visual-script graph, and MCP server.
- `android/`: experimental Android application host, JNI bridge, and native
  CMake target for the arm64 OpenGL ES runtime.
- `website/`: Nimbus download page source and Vercel project linkage.
- `tests/`: integration test executable; engine-level tests also live in
  `engine/engine/src/tests.cpp`.
- `docs/`: architecture, implementation status, and Android build instructions.

## Linux build and tests

Requirements: CMake 3.20+, a C++20 compiler, and Ninja (or another supported
CMake generator). Lua 5.4 is enabled when its runtime library is available.
Vulkan and GLES are optional and are omitted when their development headers and
libraries are not found.

```sh
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DNIMBUS_WARNINGS_AS_ERRORS=ON
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
```

Run the test executable directly for its per-system log:

```sh
./build/nimbus_tests
```

Build and run the MCP stdio server separately:

```sh
./build/nimbus_mcp_server
```

The MCP server reads JSON-RPC messages from stdin and writes protocol responses
to stdout; diagnostics are written to stderr. It does not run the test suite on
startup.

## Android runtime build

The `android/` directory now contains an experimental Gradle application for
`arm64-v8a` (minimum API 24). It hosts the existing C++ `nimbus::Engine` via JNI,
passes an Android native window to the engine, initializes the OpenGL ES 3 RHI,
and renders a small indexed demo mesh. Lua and Vulkan are disabled in this first
Android packaging target; native gameplay remains available without Lua.

This is a runtime smoke host, not a complete visual game editor. The Android
APK has **not yet been built or device-tested in this workspace** because the
Android SDK, NDK, Gradle, and Build Tools are unavailable here. See
[Android build instructions](docs/ANDROID_BUILD.md) for required tool versions,
manual build commands, and the GitHub Actions workflow. The workflow can also
deploy the APK and download page to the linked Vercel project when a
`VERCEL_TOKEN` repository secret is configured.

## Current verification

See [docs/STATUS.md](docs/STATUS.md) for what is implemented, what is partial,
and what could not be verified in the current environment. Do not infer support
from a module directory or API declaration alone.
