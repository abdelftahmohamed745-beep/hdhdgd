# Nimbus Engine Android Runtime APK

This folder contains a native Android host for the existing Nimbus C++20 runtime.
It targets **arm64-v8a**, uses an Android `SurfaceView`, initializes the engine
on the render thread, creates an EGL/OpenGL ES 3 context, and draws a small
indexed triangle through Nimbus' existing renderer. The APK is a runtime smoke
host, not a graphical game editor and not a general-purpose game launcher.

## Toolchain

- JDK 17
- Gradle 8.9 or newer (CI pins 8.10.2)
- Android SDK Platform 35 and Build Tools 35.0.0
- Android NDK 27.2.12479018
- CMake 3.22.1
- Android API 24 minimum; arm64-v8a only

Lua is disabled for this Android APK. The engine treats the missing optional
Lua 5.4 runtime as a disabled scripting subsystem and continues with native
runtime functionality. Android OpenSL ES audio initialization is attempted;
if the device/backend does not provide it, the engine continues without audio.

## Build locally

Configure the Android SDK environment (`ANDROID_SDK_ROOT` and `JAVA_HOME`),
install the exact SDK/NDK/CMake packages above, then run:

```bash
cd android
gradle --no-daemon :app:assembleDebug
```

Or from the repository root:

```bash
./android/build-debug.sh
```

The installable debug APK is written to:

```text
android/app/build/outputs/apk/debug/app-debug.apk
```

It is signed with the Android debug key, which is suitable for local testing.
It is not a Play Store release signature.

## Build and publish with GitHub Actions

The workflow at `.github/workflows/android-apk.yml` installs the Android toolchain,
builds the arm64 debug APK, verifies package metadata, copies it to
`website/downloads/NimbusEngine.apk`, records its SHA-256, and uploads the APK
as the `NimbusEngine-Android-arm64-debug` workflow artifact for 30 days.

To also publish the APK and the download page to the existing Vercel project,
add a GitHub Actions repository secret named `VERCEL_TOKEN` containing a Vercel
token with deployment permission for the Nimbus download project. The Vercel
project linkage is stored in `website/.vercel/project.json`; the token itself
must never be committed. Once the repository is pushed to GitHub, run the
workflow manually from Actions or push a change to `main`/`master`. If the secret
is absent, the build artifact is still uploaded and the Vercel deployment step
is skipped without failing the build.

The CI workflow is prepared in the source tree, but it has not been executed
from this environment. This session has no Android SDK/NDK/Gradle, and the
connected GitHub account currently exposes no repository to run the workflow.

## Runtime behavior and limits

- A native C++ `nimbus::Engine` instance is initialized against the Activity's
  Android native window from a dedicated render thread.
- OpenGL ES is selected explicitly; Vulkan is intentionally disabled for this
  first Android packaging target because its current backend is not ready for
  device presentation verification.
- Surface destruction and Activity pause stop and join the render thread before
  releasing the native window. Resume creates a new engine/device instance.
- The demo triangle exercises shader compilation, vertex/index upload, scene
  lookup, camera uniform submission, draw calls, and EGL presentation.
- This host does not claim to provide a scene editor, asset browser, input API,
  scripting runtime, or complete game-authoring workflow.

A successful CI APK build is a compile/package verification only. Actual
installation, GPU compatibility, lifecycle stability, frame pacing, thermal
behavior, and audio must still be checked on physical Android hardware.

## Verification performed in this workspace

- Android Manifest XML parsed successfully.
- The Gradle/CMake source paths were checked against the repository layout.
- The native engine was rebuilt on the host with Lua, Vulkan, and GLES disabled
  and warnings treated as errors.
- A dedicated no-Lua runtime smoke test initialized the engine, entered play,
  advanced fixed-step simulation, paused, stepped explicitly, and shut down.
- The Android APK itself was **not** built here: Android SDK, NDK, Gradle,
  `android.jar`, and Android Build Tools are unavailable in this workspace, and
  Android SDK downloads could not be resolved from this network environment.
