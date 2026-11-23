# Nimbus Engine — Android APK delivery

This package contains the existing Nimbus C++20 engine source plus a small native Android host. It does not replace the engine with a WebView or another engine.

## What will be built

- Android package: `com.nimbus.engine`
- ABI: `arm64-v8a`
- Minimum OS: Android 7.0 / API 24 (compatible with Android 12 / API 31)
- Renderer: OpenGL ES 3
- Toolchain: JDK 17, Gradle 8.10.2, Android SDK 35, NDK 27.2.12479018, CMake 3.22.1
- Output: debug-signed APK for direct installation/testing

The Android host currently starts the real Nimbus `nimbus::Engine` and renders a small indexed test mesh. It is a runtime/build smoke app, not yet a full mobile game editor or complete content-authoring interface. Vulkan presentation is not used; GLES is the Android path.

## Publish from Termux (one-time GitHub authorization)

1. Extract this ZIP into a folder in Termux, for example `~/nimbus-engine`.
2. Install the two CLI tools if they are missing:

   ```sh
   pkg install git gh -y
   ```

3. Run the included publisher:

   ```sh
   cd ~/nimbus-engine
   bash ./publish-to-github.sh
   ```

4. On first use, GitHub CLI opens a device login. Complete the approval in your browser. Do not paste a GitHub token into this chat.

The script creates an initial `main` commit and pushes the source. It refuses to overwrite an existing `main` branch and never force-pushes.

## APK build and download

The GitHub Actions workflow first builds and runs the Nimbus host regression suite, then provisions Android SDK/NDK/Gradle and builds the APK. It verifies the package name, ABI, zip alignment and APK signature before uploading.

- Actions: `https://github.com/abdelftahmohamed745-beep/nimbus-engine.-/actions`
- Releases: `https://github.com/abdelftahmohamed745-beep/nimbus-engine.-/releases`

A successful workflow publishes `NimbusEngine-Android-arm64-v8a-debug.apk` and `SHA256SUMS.txt`. Debug signing is for testing/installing directly; it is not a Play Store release signature.

## Verification status

The extracted source has previously passed the Linux CMake build with warnings treated as errors and 237 direct engine/integration checks. No Android APK has yet been built or installed on a physical device in this environment. The GitHub Actions workflow is designed to perform the first real Android/NDK packaging and validation.
