# Nimbus Engine — Engineering Status

Last Updated: Milestone 5 Complete (Verification Audit Recorded)
Environment: Linux x86_64 (`g++ 12.2.0`, `make`, `python3`, `node v22.23.2`, `npm`, `libluajit-5.1.so.2`).
Environment Limits: `cmake`, Android SDK/NDK, `adb`, JDK/Gradle, and system EGL/GLES C dev headers are not installed in this offline container (`apt-get` unavailable). Accordingly, native Android APK compilation, OpenSL ES hardware audio playback, and Vulkan/GLES3 physical GPU execution are strictly classified as `NOT VERIFIED` locally and are guarded by GitHub Actions CI (`.github/workflows/android-apk.yml`).

## Verification Matrix (Measured Results)
| Suite / Target | Configuration | Result | Classification |
|---|---|---|---|
| `nimbus_tests` (`make all test`) | GCC 12.2 Debug (`-std=c++20 -Wall -Wextra -Wpedantic -Werror`, 0 warnings) | 283 / 283 checks PASS (`143` engine + `140` integration, live `libluajit-5.1.so.2`) | VERIFIED |
| `nimbus_tests` (`BUILD=release`) | GCC 12.2 `-O2 -DNDEBUG` (0 warnings) | 283 / 283 checks PASS | VERIFIED |
| `nimbus_tests` (`SANITIZE=asan`) | `-fsanitize=address,undefined` (0 warnings) | 283 / 283 checks PASS, 0 leaks/UB | VERIFIED |
| `nimbus_tests` (`SANITIZE=tsan`) | `-fsanitize=thread` (0 warnings) | 283 / 283 checks PASS, 0 data races | VERIFIED |
| `fps_arena_3d` & `ai_patrol_sim` | Debug & Release Linux headless simulation | Both exit 0 (`hits=39, rayHit=1`; `navTris=128, finalPos=(4.50,0,4.50)`) | VERIFIED |
| `nimbus_mcp_server` (`--workspace`) | Direct JSON-RPC 2.0 stdio | 39 schema tools listed, path traversal blocked | VERIFIED |
| Web/PWA Studio + `server.ts` | `tsc --noEmit` + Vite `npm run build` + `eslint`/`npm run lint` | 0 errors; live 16-endpoint HTTP/MCP API E2E PASS (`dist/` production SPA) | VERIFIED |
| Browser WebGL2 / Touch Gesture Rendering | Browserless container lacks Chromium/Playwright runtime | Source compiles cleanly; interactive WebGL2 canvas unrendered in headless container | PARTIALLY VERIFIED |
| Android Dual APKs (`runtime` + `editor`), GLES3 GPU, OpenSL ES | `android/` Gradle + CMake + JNI | Static code & CI workflow updated; Android SDK/NDK/Gradle absent locally | NOT VERIFIED |

## Subsystem Status Matrix
| Subsystem | Implemented Capabilities | Verification Status |
|---|---|---|
| Core (`jobs`, `time`, `log`, `math`, `events`) | FixedStep accumulator, `events::Dispatcher`, job deps/cancel, `Ray`/`Sphere`/`Capsule`/`OBB`/`Plane`/`Euler`, unified `FrameStats` | VERIFIED |
| Input (`engine/input`) | Keyboard, mouse, multi-touch, gamepad, tap/swipe/pinch gestures, action/axis binding | VERIFIED |
| ECS (`engine/ecs`) | Archetypes, generational handles, `Scheduler` DAG, `SerializerRegistry` reflection | VERIFIED |
| Scene (`engine/scene`) | Parent-child world matrices, v2 full component JSON (v1 compatible), Prefabs, multi-scene | VERIFIED |
| RHI (`engine/rhi`) | `HeadlessDevice` command validation, hardened `GlesDevice` (VAOs/FBOs/states/context-loss) | Headless VERIFIED; GLES3 NOT VERIFIED locally |
| Render (`engine/render`) | AABB frustum culling, material/light uniforms, textures, default Lit/Unlit/UI/Particle/Debug shaders, mesh factory | Headless VERIFIED; Hardware GPU NOT VERIFIED |
| Physics (`engine/physics`) | AABB/Sphere/Box/Capsule narrowphase, collision layers/masks, triggers, swept-sphere CCD, joints, terrain collision | VERIFIED |
| Anim, Particles, Terrain, Audio | Animator states/events/root-motion; Emitter shapes/bounce; Terrain sculpt/splat/scatter; Audio 3D pan/pause/WAV + OpenSL ES source | C++ Core VERIFIED; OpenSL ES hardware NOT VERIFIED |
| AI (`engine/ai`) | BT decorators (`Cooldown`, `Repeater`, `Condition`), Steering, Perception, `NavMesh::buildFromGrid` + A* + XZ Funnel, GOAP | VERIFIED |
| Net (`engine/net`) | POSIX UDP, Reliability, HMAC-SHA256 v2 auth, sliding replay window, payload stream encryption | Loopback VERIFIED |
| Asset (`engine/asset`) | NBPK v1 read/write w/ CRC32, async loader, glTF 2.0/GLB, OBJ, PPM/BMP/TGA RGBA8, GUID `AssetRegistry` + LRU cache | VERIFIED |
| Script (`engine/script`) | Dynamic `dlopen` LuaJIT 5.1 / Lua 5.4, hardened sandbox (`os`/`io`/`debug`/`ffi`/`jit`/loadlib), JIT-off + `LUA_MASKCOUNT` watchdog | VERIFIED |
| UI (`engine/ui`) | Retained widgets (`Container`, `Label`, `Button`, `Slider`, `ScrollView`, `ListView`, `TextField`), DPI scale, theme, Renderer batches | VERIFIED |
| Editor, Visual Script & MCP | Undo/Redo, templates, project bundle exporter, 18-node `VisualScript` w/ trace + Lua codegen, 39-tool MCP stdio server | VERIFIED |
| Live Studio Web/PWA (`src/`, `server.ts`) | Express MCP stdio bridge, mobile-first drawer + desktop 3-column workbench, WebGL2 3D viewport + gizmos, PWA manifest/SW | Build & HTTP API VERIFIED; Real browser GPU unrendered |
| Android Targets (`android/`) | Dual flavors (`runtime`, `editor`), JNI surface/resize/touch/audio/save/entity bridge, GitHub Actions CI | NOT VERIFIED locally (no SDK/NDK) |
