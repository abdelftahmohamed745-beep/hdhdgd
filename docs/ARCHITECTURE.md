# Nimbus Engine — System Architecture

This document reflects the verified architecture implemented across `engine/`, `editor/`, `examples/`, `android/`, `server.ts`, and `src/`.

## 1. Build Topology
- **Native Engine & Editor (`CMakeLists.txt` + `Makefile`)**:
  Modular C++20 static libraries for `core`, `input`, `ecs`, `scene`, `rhi`, `render`, `physics`, `anim`, `terrain`, `particles`, `audio`, `ai`, `net`, `ui`, `asset`, `script`, and `engine`, plus `editor` (`nimbus_editor`), the JSON-RPC stdio server (`nimbus_mcp_server`), `nimbus_tests`, and playable headless simulation binaries (`nimbus_example_fps_arena`, `nimbus_example_ai_patrol`).
- **Web/PWA Studio (`server.ts` + `src/`)**:
  Express backend on port 3000 spawning `nimbus_mcp_server --workspace ./projects` as a serialized JSON-RPC 2.0 child process, exposing `/api/*` REST endpoints confined strictly to `./projects`, and serving the React 19 + Tailwind v4 + WebGL2 PWA frontend.
- **Android Packaging (`android/`)**:
  Gradle `mode` flavor dimension targeting `com.nimbus.engine` (`runtime`, `IS_EDITOR=false`) and `com.nimbus.editor` (`editor`, `IS_EDITOR=true`), compiling `nimbus_engine` + `nimbus_editor` via NDK CMake + `native_engine.cpp` JNI glue.

## 2. Engine Runtime Pipeline
- **Lifecycle (`Engine`)**: Manages `Stopped`/`Playing`/`Paused` states, fixed-step accumulator (`1/60s` capped at 8 substeps), `events::Dispatcher`, `JobSystem`, and unified `FrameStats`.
- **ECS & Serialization (`ecs`, `scene`)**: Generational 64-bit entity handles over archetype pools, topological `ecs::Scheduler`, `SerializerRegistry` reflection, v2 component-complete JSON persistence (v1 compatible), reusable JSON prefabs, and active scene registry.
- **Rendering (`rhi`, `render`)**: Abstract `rhi::Device` backed by `GlesDevice` (GLES3 VAO/FBO/state cache + resize/context-loss rebuild hooks) or `HeadlessDevice` for CI/headless verification. `render::Renderer` executes AABB frustum culling, directional/point lighting uniforms, texture binding, material shaders, particle quads, UI 2D batches, and debug primitives.
- **Simulation (`physics`, `anim`, `particles`, `terrain`, `audio`, `ai`, `net`, `ui`)**:
  - Physics: Gravity/damping integration, AABB/Sphere/Box/Capsule narrowphase, layer masks, trigger/contact callbacks, swept-sphere CCD, distance/spring constraints, terrain heightfield queries.
  - AI: Grid NavMesh builder, A* triangle search, oriented XZ funnel smoothing, steering (`seek`/`flee`/`arrive`/`wander`/`obstacleAvoid`), Blackboard, Behavior Tree (`Sequence`/`Selector`/`Inverter`/`Cooldown`/`Repeater`/`Condition`), GOAP planner.
  - Net: Non-blocking POSIX UDP sockets, sequence/ack reliability, HMAC-SHA256 v2 authentication, 64-bit replay window, payload stream encryption, snapshot interpolation.
- **Assets & Scripting (`asset`, `script`)**:
  - `PackBuilder` & `Pack` handle CRC32-verified `.nbpk` bundles alongside glTF 2.0/GLB, Wavefront OBJ, PPM/BMP/TGA textures, WAV audio, and an LRU-bounded `AssetRegistry`.
  - `script::Runtime` dynamically resolves Lua 5.4 or LuaJIT 5.1 symbols via `dlopen`, blocks filesystem/process/FFI/JIT modules (`os`, `io`, `debug`, `dofile`, `loadfile`, `ffi`, `jit`, `package.loadlib`, `package.preload/loaded`), and enforces per-chunk bytecode instruction budgets via `luaJIT_setmode(OFF)` + `LUA_MASKCOUNT`.

## 3. Editor, MCP & Studio Architecture
- `Editor` wraps `Engine` with `CommandHistory` Undo/Redo, project scaffolding (`empty`, `3d_action`, `ai_sandbox`), `.nbpk` + Lua bundle export (`exportProject`), and an 18-node `VisualScript` graph (`execute()` w/ trace + `toLua()`).
- `nimbus_mcp_server` exposes 39 schema-described tools over stdio and enforces canonical workspace path confinement before touching disk.
- The Studio frontend (`src/App.tsx`, `Viewport3D.tsx`, `VisualScriptEditor.tsx`, `InspectorPanel.tsx`) provides desktop 3-column and mobile sticky-dock drawer workflows directly backed by native C++ state via `/api/*`.
