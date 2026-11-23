# Nimbus Engine — Master Engineering Roadmap

## Mission
Transform the baseline Nimbus C++20 engine repository into an integrated game engine runtime, mobile-first visual editor & PWA environment, MCP tooling suite, dual-flavor Android Gradle/NDK target configuration, and verified simulation examples.

## Completed Milestones Summary
- **Milestone 1 (Core, Input, ECS, Scene, Lifecycle)**:
  - Implemented `events::Dispatcher`, job dependencies & cancellation tokens, extended math (`Ray`, `Sphere`, `Plane`, `Capsule`, `OBB`, Euler/quaternion helpers), `input::InputSystem` (touch, multi-touch, pointer, keys, gamepad, tap/swipe/pinch gestures, action/axis rebinding), `ecs::Scheduler` topological execution & `SerializerRegistry`, `scene::Scene` v2 JSON component serialization (v1 backward-compatible), prefabs, multi-scene switching, and unified `Engine::Stats` reporting.
- **Milestone 2 (RHI, Renderer, Physics, Anim, Particles, Terrain, Audio)**:
  - Hardened `rhi::GlesDevice` (VAOs, blend/depth pipeline state, FBO render targets, viewport resize & context-loss recovery) + `rhi::HeadlessDevice` command/draw/pixel-hash verification; upgraded `render::Renderer` (AABB frustum culling, material/light uniforms, textures, default Lit/Unlit/UI/Particle/Wireframe shaders, procedural mesh factory).
  - Upgraded `physics::World` (capsule narrowphase, collision layers/masks, contact & trigger callbacks, swept-sphere CCD, distance/spring joints, heightfield terrain sampling).
  - Upgraded `anim::Animator` (state machine transitions, animation events, root motion), `particles::System` (emitter shapes & ground bounce/damping), `terrain::Terrain` (sculpt brushes, normalized splatmaps, deterministic vegetation scatter), and `audio::Mixer` (3D stereo equal-power panning, per-voice/global pause/resume, WAV parser).
- **Milestone 3 (Asset Pipeline, Dynamic LuaJIT/Lua 5.4 Sandbox, AI, Net, UI)**:
  - Upgraded `asset` (`PackBuilder` CRC32 NBPK writer, glTF 2.0 / GLB & OBJ mesh import, PPM/BMP/TGA RGBA8 texture decode, GUID `AssetRegistry` with LRU byte-budget eviction).
  - Upgraded `script::Runtime` with dynamic `dlopen` ABI binding supporting both LuaJIT 5.1 (`libluajit-5.1.so.2`) and Lua 5.4, strict sandbox blocking `os`/`io`/`debug`/`dofile`/`loadfile`/`ffi`/`jit` + `package.loaded`/`package.preload`, and `luaJIT_setmode` + `LUA_MASKCOUNT` instruction watchdog terminating infinite loops.
  - Upgraded `ai` (`NavMesh::buildFromGrid` + A* + corrected XZ Funnel string-pulling, steering behaviors, `Cooldown`/`Repeater`/`Condition` BT decorators), `net` (authenticated HMAC-SHA256 v2 handshake, sliding anti-replay window, stream cipher encryption), and `ui` (`Slider`, `ScrollView`, `ListView`, `TextField`, `Theme`, DPI scaling, renderer batch emission).
- **Milestone 4 (Editor Core, 39-Tool MCP Server, Live Web/PWA Studio)**:
  - Implemented `Editor` `CommandHistory` Undo/Redo, project templates (`empty`, `3d_action`, `ai_sandbox`), `exportProject` self-contained `assets.nbpk` + Lua bundler, and 18-node `VisualScript` engine with live trace execution & Lua codegen.
  - Expanded `nimbus_mcp_server` to 39 schema-defined JSON-RPC 2.0 stdio tools with strict workspace path confinement.
  - Built live mobile-first Nimbus Studio (`server.ts` Express + MCP stdio bridge on port 3000 + React 19/Tailwind v4 PWA in `src/`) featuring a WebGL2 lit 3D viewport (touch orbit/pinch/pan, ray picking, XYZ Move/Rotate/Scale gizmos, `webglcontextlost` recovery), SVG Bezier visual script graph editor + Lua sandbox runner, inspector tabs, OBJ/NBPK asset manager, and live C++ test/log panel.
- **Milestone 5 (Android Dual-Flavor Packaging, Examples, Hardening Verification)**:
  - Configured `android/` dual product flavors (`runtime` `com.nimbus.engine` and `editor` `com.nimbus.editor`), upgraded `native_engine.cpp` & `MainActivity.java` (lit 3D cube via engine shader/mesh factory, touch orbit forwarding, play/pause/tone/+entity/save JNI bridge), and updated `.github/workflows/android-apk.yml`.
  - Built and verified `fps_arena_3d` and `ai_patrol_sim` end-to-end simulations and expanded `tests/main.cpp` to 283 total checks passing across Debug, Release, ASan+UBSan, and TSan.
