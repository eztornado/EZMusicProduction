---
id: module-release-artifacts
type: module-design
title: Artefactos de release — empaquetado runtime del engine
status: active
---

# Release artifacts — runtime packaging of the engine (release.yml)

## Responsibility

The GitHub Actions release workflow (`.github/workflows/release.yml`) produces
self-contained per-platform bundles: after extraction, the app must run
out-of-the-box — no environment variables (e.g. `LD_LIBRARY_PATH`,
`CADENCE_ENGINE_DYLIB`), no package installation.

## Decisions

### Linux: $ORIGIN RUNPATH + filtered dependency copy

The Dart FFI side opens `libcadence_engine.so` next to the executable
(`ui/lib/engine/cadence_bindings.dart`, `resolveEngineDylib`). The dynamic
loader resolves the .so's own dependencies (libopenblas, libfluidsynth, …) via
RPATH → `LD_LIBRARY_PATH` → ldconfig cache — **never** the loading library's
directory. Therefore:

- Every `.so` shipped in the bundle must carry `RUNPATH=$ORIGIN` (set via
  `patchelf --set-rpath '$ORIGIN'` in the packaging step; the engine target
  also sets BUILD/INSTALL_RPATH `$ORIGIN` in `engine/CMakeLists.txt`).
- `DT_RUNPATH` is **not transitive**: each copied dependency (not only the
  engine .so) needs its own `$ORIGIN`, or second-level deps such as
  `libopenblas → libgfortran` will fail on systems lacking them.
- Dependencies are discovered with `ldd` on the engine .so, but base system
  libraries are **excluded**: glibc core (`libc`, `libm`, `ld-linux`,
  `libpthread`, `librt`, …), `libstdc++`, `libgcc_s`, and the X11 family
  (`libX11`, `libxcb`, …). They exist on any desktop Linux; shadowing glibc
  from a different distro crashes with `GLIBC_PRIVATE` errors, and an older
  bundled libstdc++ can starve other in-process libs of symbols.

### Windows: fluidsynth.dll copied next to the exe

`cadence_engine.dll` links the FetchContent-built FluidSynth. Its DLL (and
sibling runtime DLLs from `build/_deps/fluidsynth-build`) are copied into the
release directory; `LoadLibrary` search already includes the executable's
directory, so no path configuration is needed. CBLAS is optional on Windows —
without it, AI generation (MusicVAE) is disabled by build definition
(`CADENCE_NO_CBLAS`).

### macOS

The dylib is copied into `Contents/MacOS/` and ad-hoc re-signed; macOS was not
part of the runtime-deps fix (system frameworks via Accelerate).

## Versioning (YY.MM.DD.N)

The app version follows the local `build_app.sh` scheme: `YY.MM.DD.N` (N =
build counter). The workflow generates it, it never commits it back:

- Tag `vYY.MM.DD.N` → version parsed from the tag (releases are reproducible;
  the tag is the source of truth).
- `workflow_dispatch` or a non-conforming ref → UTC date + `github.run_number`.
- A "Generate app version" step in each job (macos/linux/windows) writes
  `ui/lib/generated/version.dart` (`kAppVersion`, consumed by the About dialog)
  and exports `APP_VERSION` to `GITHUB_ENV`. On macOS the same value is stamped
  into `CFBundleShortVersionString`/`CFBundleVersion` before codesign.
- The committed `ui/lib/generated/version.dart` stays as the dev default for
  `flutter run`; `build_app.sh` keeps generating it locally with the same
  format, so both paths agree.

## Known limitation

End-to-end artifact validation happens when a `v*` tag triggers the workflow;
local verification covered the loader mechanics (RUNPATH `$ORIGIN`, non-
transitivity, patchelf'd deps) on a synthetic bundle.
