# Repository Guidelines

## Project Structure

This is an Android port of Crash Team Racing with a C/C++ native game layer. `android/` contains the Gradle app, Java UI and resources; `game/`, `include/`, `platform/` and `main.c` contain the native game and platform integration. `externals/` holds vendored SDL3 and other dependencies. `tests/` contains host-side C/Java checks and Android interaction scripts; `docs/` has architecture and maintenance guides. `tools/` contains asset utilities, and `screenshots/` contains documentation images. Game disc images are user supplied and must not be committed.

## Project Contracts

Before a non-trivial change, consult the project [change contract](docs/CHANGE_CONTRACT.md) and the relevant product, architecture, or design document in `docs/`. Keep detailed subsystem guidance in its existing guide. Project skills are in `.agents/skills/`; use `ctr-mobile-ui` for Android UI/touch work, `ctr-mobile-native` for native engine/platform work, and `ctr-mobile-android-validation` to choose and report validation. The Gradle Android flow is the supported build; the root CMake project rejects desktop builds.

## Build and Development

Build the debug APK from the Android project:

```powershell
cd android
.\gradlew.bat :app:assembleDebug
```

The APK is written to `android/app/build/outputs/apk/debug/app-debug.apk`. The Android build requires SDK, NDK `27.2.12479018`, and CMake `3.22.1`. The root CMake project is invoked by Gradle and intentionally rejects desktop builds. Install on a connected device with `adb install -r app/build/outputs/apk/debug/app-debug.apk` from `android/`.

## Code Style

Follow the existing language and neighboring-file conventions. Native C formatting is defined by `.clang-format`: LLVM-derived style, four-space indentation, tabs for indentation, right-aligned pointers, and a 160-column limit. Keep Android Java changes within the app's existing package and resource structure. Avoid unrelated edits to vendored code in `externals/`.

## Tests and Validation

Run focused host checks with `python tests/run_android_touch_tests.py` (requires GCC with pthread support and Java/Javac on PATH) or `python tests/run_adventure_caption_tests.py`. Other `tests/run_android_*.py` scripts cover specific Android interactions and may require a device/emulator and ADB. For native or UI changes, build the APK and validate the affected flow on a device when available; compilation alone does not verify runtime behavior.

## Changes and Reviews

This checkout does not include Git metadata or a contribution policy, so no commit-message convention can be confirmed. Use a short imperative commit subject when committing (for example, `Fix touch input release`). Pull requests should explain the user-visible change, list validation commands and results, link related issues, and include screenshots for UI changes. Call out device/model coverage and any remaining runtime checks.

## Configuration and Assets

Do not add copyrighted game images or user disc files. Keep SDK/NDK configuration aligned with the versions above and avoid committing local build output, device data, or secrets.
