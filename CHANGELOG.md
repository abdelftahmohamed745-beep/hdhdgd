# Changelog

## [0.2.0] — Milestones 1–5 Productization & Hardening

### Added
- **Input (`engine/input`)**: Touch, multi-touch, mouse pointer, keyboard, gamepad axes/buttons, tap/swipe/pinch gesture detection, and remappable action/axis bindings.
- **Core & ECS (`engine/core`, `engine/ecs`, `engine/engine`)**: Event `Dispatcher`, job dependency DAG + cancellation tokens, `Ray`/`Sphere`/`Capsule`/`OBB`/`Plane`/Euler math primitives, topological `ecs::Scheduler`, `SerializerRegistry` reflection, and unified `Engine::Stats`.
- **Scene (`engine/scene`)**: v2 full component JSON serialization (v1 backward-compatible), reusable JSON prefabs, and named multi-scene registry.
- **RHI & Renderer (`engine/rhi`, `engine/render`)**: `HeadlessDevice` command/pixel-hash verification, hardened `GlesDevice` (VAOs, blend/depth state cache, FBOs, resize & context-loss rebuild hooks), AABB frustum culling, lighting/material/texture uniforms, Lit/Unlit/UI/Particle/Debug shaders, and mesh factory (`createCube`, `createPlane`, `createSphere`).
- **Physics, Anim, Particles, Terrain, Audio**: Capsule narrowphase, collision layers/masks, contact & trigger callbacks, swept-sphere CCD, distance/spring joints, heightfield terrain sampling; Animator state transitions & animation events; Particle emitter shapes & ground bounce; Terrain raise/lower/smooth/flatten sculpt brushes, normalized splatmaps, deterministic vegetation scatter; Audio 3D stereo panning, per-voice/global pause/resume, WAV parser.
- **Asset & LuaJIT Sandbox (`engine/asset`, `engine/script`)**: CRC32 `.nbpk` `PackBuilder`, glTF 2.0/GLB + OBJ + PPM/BMP/TGA RGBA8 importers, GUID `AssetRegistry` w/ LRU eviction; dynamic `dlopen` LuaJIT 5.1 / Lua 5.4 ABI loader, sandbox blocking `os`/`io`/`debug`/`dofile`/`loadfile`/`ffi`/`jit`/`package.loadlib` + `package.loaded`/`preload`, and JIT-disabled `LUA_MASKCOUNT` infinite-loop watchdog.
- **AI, Net, UI**: `NavMesh::buildFromGrid` + A* + oriented XZ funnel string-pulling, steering (`seek`/`flee`/`arrive`/`wander`/`obstacleAvoid`), `Cooldown`/`Repeater`/`Condition` BT decorators; authenticated HMAC-SHA256 v2 UDP handshake, 64-bit replay window, payload stream encryption; `Slider`, `ScrollView`, `ListView`, `TextField`, `Theme`, DPI scaling, and Renderer batch emission.
- **Editor, MCP & Live Web/PWA Studio (`editor/`, `server.ts`, `src/`)**: `CommandHistory` Undo/Redo, project templates (`empty`, `3d_action`, `ai_sandbox`), `.nbpk` + Lua bundle exporter, 18-node `VisualScript` graph w/ live execution & Lua codegen, 39-tool schema-complete workspace-confined `nimbus_mcp_server`, and mobile-first React 19 / Tailwind v4 / WebGL2 PWA Studio on port 3000.
- **Android Dual-Flavor Packaging & Simulation Examples (`android/`, `examples/`)**: `runtime` (`com.nimbus.engine`) + `editor` (`com.nimbus.editor`) flavors, JNI touch/playback/audio/entity/save bridge, CI verification workflow, `fps_arena_3d` and `ai_patrol_sim` examples.

### Fixed
- Fixed `Engine::renderFrame()` overwriting CPU `Stats` accumulated by `Engine::tick()`.
- Fixed `NavMesh::findPath()` left/right portal orientation in the funnel string-puller so unobstructed paths simplify cleanly instead of zig-zagging across every shared edge.
- Fixed LuaJIT `LUA_MASKCOUNT` instruction watchdog bypass caused by trace compilation (`jit_setmode(OFF)` when arming watchdog).
- Fixed test harness Mixer buffer initialization and VisualScript export ordering.
