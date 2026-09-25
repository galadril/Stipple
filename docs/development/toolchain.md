# Toolchain setup

Stipple core is portable C++17 with no external dependencies, so the host build
needs only a compiler and CMake. The browser emulator additionally needs the
Emscripten SDK. Nothing here requires a TC002 — device tooling arrives in
Phase 7.

Check what you already have:

```powershell
.\dev.ps1 doctor
```

## Windows

### 1. C++ compiler and CMake

If Visual Studio is installed, add the C++ workload — it brings MSVC, CMake and
Ninja together:

```powershell
# Run from an elevated prompt. Adjust the install path if yours differs.
& "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vs_installer.exe" modify `
    --installPath "C:\Program Files\Microsoft Visual Studio\18\Enterprise" `
    --add Microsoft.VisualStudio.Workload.NativeDesktop `
    --includeRecommended --passive --norestart
```

Alternatively, without Visual Studio:

```powershell
winget install Kitware.CMake
winget install Microsoft.VisualStudio.2022.BuildTools --override "--quiet --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
```

CMake installed by Visual Studio is not added to `PATH`. `dev.ps1` finds it via
`vswhere`, so this only matters if you invoke `cmake` directly.

`dev.ps1` also enters an **x64** developer shell before configuring. That is not
cosmetic: the presets pin the Ninja generator, Ninja invokes `link.exe` directly
and inherits `LIB` from the shell, and a plain prompt leaves CMake pairing the
x64 compiler with x86 CRT libraries — which fails as a wall of unresolved C
runtime symbols rather than anything that mentions architecture. Invoking
`cmake` yourself means entering that shell yourself.

Running `dev.ps1` prints a harmless `'vswhere.exe' is not recognized` line. It
comes from a child `cmd` inside `VsDevCmd.bat` writing straight to the console,
where PowerShell redirection cannot reach it. It is noise, not a failure.

### 2. Emscripten (emulator only)

Needs no administrator rights. Install somewhere writable — the user profile
rather than the drive root, which normally needs elevation:

```powershell
git clone --depth 1 https://github.com/emscripten-core/emsdk.git $env:USERPROFILE\emsdk
cd $env:USERPROFILE\emsdk
.\emsdk.bat install 3.1.64
.\emsdk.bat activate 3.1.64
. .\emsdk_env.ps1        # sets EMSDK for the current shell only
```

Pin the same version CI uses (see `.github/workflows/ci.yml`) so emulator builds
are reproducible — blueprint §32.

`emsdk_env.ps1` is per-shell and must be dot-sourced. Re-run it in each new
terminal, or add it to your PowerShell profile.

Building the emulator needs **both** environments in one shell, in this order:
the Visual Studio x64 developer shell (which provides Ninja and CMake), then
`emsdk_env.ps1` (which provides `emcc`). `dev.ps1 emulator` does the first for
you; activate Emscripten yourself beforehand.

## Linux / macOS

```bash
sudo apt install build-essential cmake ninja-build    # Debian/Ubuntu
brew install cmake ninja                              # macOS
```

Emscripten installs the same way as above, using `./emsdk_env.sh`.

## Building

```powershell
.\dev.ps1 build          # configure + build core and tests
.\dev.ps1 test           # build and run the full suite
.\dev.ps1 test Canvas    # only tests whose name contains "Canvas"
.\dev.ps1 emulator       # build the WASM emulator
.\dev.ps1 serve          # build it and serve on http://localhost:8080/
```

Or drive CMake directly:

```bash
cmake --preset host-debug
cmake --build --preset host-debug
ctest --preset host-debug
```

## Golden-image fixtures

Rendering tests compare against raw RGB fixtures in
`firmware/tests/testdata/`. On a normal run a *missing* fixture is created
automatically, with a notice and a magnified PNG to review. A *mismatch* is
always a failure.

```powershell
.\dev.ps1 golden         # rewrite fixtures after an intentional change
```

Always look at the PNG before committing. A fixture regenerated without review
records the bug instead of catching it.

When a test fails, actual and expected frames are written to
`firmware/tests/testdata/_failed/` as 8× PNGs. CI uploads that directory as a
build artefact.

In CI, `STIPPLE_STRICT_GOLDEN=1` turns a missing fixture into a failure, since
there it means the file was never committed.

## Warnings

`-Wall -Wextra -Wpedantic` (or `/W4`) everywhere; CI builds with
`STIPPLE_WARNINGS_AS_ERRORS=ON`. To reproduce a CI warning failure locally:

```powershell
cmake --preset ci
cmake --build --preset ci
```

Sanitizers are host-only — the device toolchain has no support for them:

```bash
cmake --preset sanitize && cmake --build --preset sanitize
```
