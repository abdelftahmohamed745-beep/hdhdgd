# Nimbus Engine — implementation and verification status

**Snapshot:** 2026-11-22. This report describes the supplied source tree after the changes in this delivery. It is not a production-readiness or completion declaration.

## Verified in the available Linux environment

| Check | Result |
|---|---|
| GCC C++20 build with `-Wall -Wextra -Wpedantic -Werror` | Passed |
| Clang C++20 build with warnings treated as errors | Passed |
| CTest | 1/1 test target passed on each configured build |
| Engine-level checks | 143 passed, 0 failed |
| Additional integration/regression checks | 94 passed, 0 failed |
| Total direct test-runner checks | **237 passed, 0 failed** |
| AddressSanitizer + UndefinedBehaviorSanitizer | Passed with leak detection enabled on exercised paths |
| ThreadSanitizer | Passed on exercised test paths; no race report |
| MCP stdio smoke test | `initialize`, `tools/list` (22 tools), and `engine_status` tool invocation passed; clean EOF shutdown |
| Clean GCC build from the extracted delivery archive with warnings as errors | Passed; CTest and all 237 checks passed |
| Vulkan/GLES backend compilation | Not performed: development headers/libraries unavailable |
| Android NDK build or APK packaging | Not performed: SDK/NDK and Android application project unavailable |
| Physical Android device runtime | Not performed |
| GPU presentation or physical audio output | Not verified |

The test suite exercises deterministic CPU-side behavior and test doubles. A successful Linux build does not prove Android compatibility, GPU rendering, audio-device output, or production gameplay behavior.

## Subsystem assessment

| Subsystem | Implemented and exercised | Remaining material gaps |
|---|---|---|
| Core/runtime | Engine ownership and cleanup, fixed-step `tick`, bounded catch-up, pause/resume/stop, paused single-step, basic frame/CPU timing stats, bounded worker count (max 4), task futures and shutdown | No platform application loop, lifecycle dispatcher, render-thread ownership model, or broad job-system integration across all systems. GPU timing and platform performance budgets are absent. |
| Mathematics | Vector/quaternion/matrix and bounds/frustum helpers; deterministic unit checks | More property-based, SIMD, precision, and platform-specific numerical validation is needed. |
| ECS | Generation handles, archetypes, aligned component storage, queries, move-only component support, construction rollback, stale-handle/lifetime tests | No complete system scheduler, dependency graph, parallel query execution, reflection, or general ECS serialization. Component relocation is deliberately constrained to safe types. |
| Scene | Hierarchy, local/world transforms, transform propagation, versioned JSON save/load, malformed-load preservation | No prefab/override system, general component serializer, async scene streaming, or editor-grade recovery pipeline. |
| RHI | Shared resource/command interface and headless device absence handling | Vulkan is not a complete presentation backend: no real surface/swapchain/present path and render-pass command path is incomplete. GLES source has not been compiled or hardware-tested. No confirmed functional GPU backend in this delivery. |
| Renderer | Mesh/material submission, point frustum culling, indexed/non-indexed draws, correct triangle counts, mesh validation and rollback, explicit resource release | No verified full GPU frame path, render graph, PBR/lighting/shadows, texture/material binding pipeline, shader compiler pipeline, GPU profiling, or device-loss recovery. |
| Physics | Fixed-step integration, primitive AABB/sphere contacts, iterative velocity/position response, friction, ray queries | Limited shape/constraint set, no robust continuous collision detection, no full production broadphase, and no deterministic cross-platform guarantee. |
| Animation | Keyframe interpolation, quaternion interpolation, crossfade blending, hierarchy/skin matrices, two-bone IK checks; Animator updated by engine ticks | No importer, complete animation state-machine/event workflow, root motion, or verified GPU skinning. |
| Particles | CPU emission/simulation, fractional emission accumulation, per-emitter limits, per-particle gravity/colour/size evolution | No GPU particle simulation/render backend or editor tooling. |
| Terrain | Heightfield/noise/chunk mesh and distance LOD primitives | No complete terrain editor, streaming/vegetation system, or verified renderer/physics integration at scale. |
| Audio | Fractional pitch cursor, linear resampling, stereo mix, validation/clamping; Android OpenSL ES source now creates an engine/output mix/player and double-buffer queue, selected by Mixer on Android | OpenSL ES code was not compiled or run here; no physical-device output verification. No desktop audio backend or mixer/voice profiler. |
| AI | Behavior-tree primitives, state/perception/planning helpers, ECS `Agent` update in engine fixed ticks | No navmesh baking, complete navigation/steering stack, multithreaded scheduling, or debug visualization. |
| Networking | UDP sockets, packet bounds/serialization, sequence/ack reliability helper, snapshots, prediction helpers, UDP client heartbeat; Engine owns/ticks Server and Client | No authenticated/encrypted protocol, mature congestion control, complete authoritative ECS replication/prediction/reconciliation pipeline, or hostile-network certification. |
| UI | Retained widget tree, basic layout, pointer/input routing, draw-list generation | No font shaping, text rendering, accessibility/input-method integration, full touch gesture system, or RHI presentation integration. |
| Assets | Validated NBPK pack reader with CRC/bounds checks, OBJ importer validation, worker-thread async reads with callbacks delivered from update | No glTF/GLB, texture/audio/font import pipeline, platform conversion/compression, full asset registry/dependency graph, or cache budgeting. `Pack` must outlive its `Loader` and must not be closed/reopened while requests are pending. |
| Lua | Lua 5.4 runtime when available, execution/call/attachment and a small native binding surface | No comprehensive Entity/Component/Input/Physics/UI API, hardened sandbox quotas, or full hot-reload lifecycle. |
| Editor | Project/scene file operations, entity inspection/position operations, play/pause/stop/step hooks, console/log and MCP-facing operations | No complete desktop/mobile GUI, interactive viewport/gizmos, full inspector, asset browser/import UI, undo/redo, or isolated play-mode world. `loadProject` currently loads the optional `assets.nbpk`; it is not a full project/scene/script loader. |
| Visual scripting | Typed nodes/pins/edges, validation, limited event-driven evaluator, print and set-position callbacks, graph export including edges | Small supported node set only; no full node library, general data/flow semantics, graph canvas interaction, debugger, or robust save/versioning workflow. |
| MCP | JSON-RPC stdio `initialize`, `tools/list`, tool invocation and 22 registered tools; startup does not run tests | Limited protocol surface: no cancellation/progress subscriptions, authorization model, or extended conformance suite. Tools must be treated as trusted local operations. |
| Android | Native-window parameter seam, NDK-oriented CMake options, Android OpenSL ES backend source | No Gradle app, manifest, Activity, JNI/native lifecycle bridge, APK target, SDK/NDK build, install, touch verification, pause/resume/rotation handling, or device testing. |

## Important backend findings

The Vulkan source must not be described as functioning Vulkan rendering. Static inspection found that initialization does not create a platform surface or swapchain, the provided native window is not used to establish presentation, the render-pass path does not issue the required begin-render-pass command, and `present` does not present a swapchain image.

The GLES source contains EGL/GLES operations but was excluded by CMake because the required development headers and libraries are absent in this environment. It therefore has not been compiler-validated or hardware-validated here. A source file is not proof of a functioning backend.

The OpenSL ES backend is platform-guarded and could not be compiled without the Android NDK headers/libraries. Its implementation is an integration attempt, not a device-verification claim.

## Engineering changes in this delivery

- Corrected renderer triangle statistics to use submitted geometry counts; validated vertex/index layouts and index ranges before GPU allocation; rolled back partial mesh uploads; added explicit mesh/material/uniform release checks.
- Hardened engine initialization/shutdown, rejected zero dimensions, cleaned up Lua initialization failure, initialized terrain, and forwarded the native window into the RHI factory.
- Added an engine fixed-step `tick`, capped catch-up at eight steps, populated basic frame/CPU update stats, allowed explicit paused single-step, and reset time/state across stop/shutdown/reinitialization.
- Made pre-initialization calls to `play`, `tick`, `step`, `createEntity`, and `destroyEntity` safe; added lifecycle regression tests.
- Made asset loading asynchronous on a worker thread, bounded pending requests, delivered callbacks from `update`, and completed failed reads rather than leaving requests pending.
- Fixed ECS component-slot alignment and hardend component insertion against throwing constructors and non-assignable move-constructible components.
- Added animation interpolation/blending/hierarchy evaluation, integrated Animator and AI Agent updates into fixed engine ticks, and connected owned networking endpoints to the engine tick.
- Improved particle simulation with per-emitter limits and per-particle property evolution.
- Implemented physics solver iteration settings and friction response rather than leaving solver iteration settings unused.
- Improved software audio pitch/resampling/stereo mixing and added an OpenSL ES queue backend implementation (Android build/device verification unavailable).
- Added limited executable visual-script paths and edge export; this is not a complete visual scripting editor.
- Improved MCP JSON value representation for strict Clang compatibility and verified `initialize`/`tools/list` over the actual stdio process.
- Reduced default JobSystem worker count to a maximum of four and made partial thread creation failure clean up already-started workers safely.
- Added regression coverage for the above, bringing the current suite to 143 engine checks plus 94 integration checks. The UDP loopback test now advances beyond the server idle timeout and verifies heartbeat keepalive.

## Reproducible Linux build

```sh
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DNIMBUS_WARNINGS_AS_ERRORS=ON
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
./build/nimbus_tests
```

For Clang, configure with `-DCMAKE_CXX_COMPILER=clang++` in a separate build directory. For AddressSanitizer/UndefinedBehaviorSanitizer or ThreadSanitizer, use compiler and linker flags supported by the selected host toolchain; ThreadSanitizer availability varies by environment.

## Repository provenance

The supplied source was a ZIP archive without a `.git` directory. Git history and the original working-tree state could not be inspected. Changes were applied to the extracted supplied files without replacing the project with a new implementation.

## Android packaging update (2026-11-22)

An experimental Android Runtime host has since been added under `android/`.
It targets arm64-v8a/API 24+, hosts `nimbus::Engine` through JNI, and uses the
existing OpenGL ES backend against an Android `SurfaceView`. It currently draws
a small indexed triangle as a runtime smoke scene; it is not an in-app editor.

The APK has not been built or verified on an Android device in the current
workspace because Android SDK/NDK/Gradle/Build Tools are unavailable and the
SDK download host could not be resolved. A GitHub Actions workflow is included
to build and verify the APK, upload it as a workflow artifact, and optionally
publish it together with `website/` to the linked Vercel project when the
repository has a `VERCEL_TOKEN` secret configured. See `docs/ANDROID_BUILD.md`
and `ANDROID_DELIVERY_STATUS.txt` for exact validation evidence and limits.
