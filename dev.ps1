# SPDX-License-Identifier: GPL-3.0-or-later
#
# NOTRIX developer entry point (blueprint §29.2).
#
#   .\dev.ps1 build          configure + build the host target
#   .\dev.ps1 test           build and run the host test suite
#   .\dev.ps1 test Canvas    run only tests whose name contains "Canvas"
#   .\dev.ps1 golden         rebuild golden-image fixtures (review the PNGs!)
#   .\dev.ps1 preview        render frames to PNG and open them (no EMSDK needed)
#   .\dev.ps1 ci             what CI runs: warnings as errors, strict goldens
#   .\dev.ps1 emulator       build the browser emulator (needs EMSDK)
#   .\dev.ps1 verify         drive the built WASM module headlessly (needs node)
#   .\dev.ps1 device         cross-build for the TC002 and run it under ARM emulation
#   .\dev.ps1 panel <ip>     build the channel-order test and run it on the panel
#   .\dev.ps1 serve          build the emulator and serve it on localhost
#   .\dev.ps1 clean          remove build output
#   .\dev.ps1 doctor         report toolchain status
#
# The device-side verbs from the blueprint (deploy / logs / restore) arrive in
# Phase 7, once there is a TC002 to talk to.

[CmdletBinding()]
param(
    [Parameter(Position = 0)]
    [ValidateSet('build', 'test', 'ci', 'golden', 'preview', 'emulator', 'verify', 'device', 'panel', 'serve', 'clean', 'doctor')]
    [string]$Command = 'build',

    [Parameter(Position = 1, ValueFromRemainingArguments = $true)]
    [string[]]$Rest
)

$ErrorActionPreference = 'Stop'
$repoRoot = $PSScriptRoot

function Get-VsInstallPath {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path $vswhere)) { return $null }
    return & $vswhere -latest -products * -property installationPath
}

# Ninja invokes link.exe directly and inherits LIB/INCLUDE/PATH from the shell.
# A plain PowerShell prompt has no developer environment, and what CMake falls
# back to targets x86 while the compiler it picks is x64 — every CRT symbol then
# resolves against the wrong architecture and the link fails with LNK4272 plus a
# wall of unresolved externals. Entering the x64 dev shell first keeps the
# toolchain self-consistent.
function Initialize-MsvcEnvironment {
    if ($env:VSCMD_ARG_TGT_ARCH -eq 'x64') { return }

    $vsPath = Get-VsInstallPath
    if (-not $vsPath) { return }

    $devShell = Join-Path $vsPath 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll'
    if (-not (Test-Path $devShell)) { return }

    Import-Module $devShell -ErrorAction Stop
    # VsDevCmd.bat prints a harmless "vswhere.exe is not recognized" line from a
    # child cmd process, straight to the console handle — PowerShell redirection
    # cannot suppress it. Ignore the noise and check the outcome below instead.
    Enter-VsDevShell -VsInstallPath $vsPath -SkipAutomaticLocation `
        -DevCmdArguments '-arch=x64 -host_arch=x64' *>&1 | Out-Null

    if ($env:VSCMD_ARG_TGT_ARCH -ne 'x64') {
        throw "could not enter an x64 Visual Studio developer environment"
    }
}

function Find-CMake {
    $onPath = Get-Command cmake -ErrorAction SilentlyContinue
    if ($onPath) { return $onPath.Source }

    # Visual Studio ships CMake but does not put it on PATH.
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path $vswhere) {
        $vsPath = & $vswhere -latest -products * -property installationPath
        if ($vsPath) {
            $candidate = Join-Path $vsPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
            if (Test-Path $candidate) { return $candidate }
        }
    }

    throw "cmake not found. Install the 'Desktop development with C++' workload in Visual Studio, or run: winget install Kitware.CMake"
}

function Find-CTest {
    $cmake = Find-CMake
    $candidate = Join-Path (Split-Path $cmake -Parent) 'ctest.exe'
    if (Test-Path $candidate) { return $candidate }
    $onPath = Get-Command ctest -ErrorAction SilentlyContinue
    if ($onPath) { return $onPath.Source }
    throw "ctest not found next to cmake at $cmake"
}

function Invoke-Configure([string]$Preset) {
    Initialize-MsvcEnvironment
    $cmake = Find-CMake
    & $cmake --preset $Preset
    if ($LASTEXITCODE -ne 0) { throw "cmake configure failed for preset '$Preset'" }
}

function Invoke-Build([string]$Preset) {
    Initialize-MsvcEnvironment
    $cmake = Find-CMake
    if (-not (Test-Path (Join-Path $repoRoot "build\$Preset\CMakeCache.txt"))) {
        Invoke-Configure $Preset
    }
    & $cmake --build --preset $Preset
    if ($LASTEXITCODE -ne 0) { throw "build failed for preset '$Preset'" }
}

function Get-TestBinary([string]$Preset = 'host-debug') {
    # Single-config generators emit into the preset directory; multi-config ones
    # nest under the configuration name. Switching generators leaves the old
    # binary behind, and running a stale one silently reports stale results, so
    # pick the most recently written rather than the first path that exists.
    $testDir = Join-Path $repoRoot "build\$Preset\firmware\tests"
    if (-not (Test-Path $testDir)) {
        throw "no test output at $testDir — did the build succeed?"
    }

    $binary = Get-ChildItem $testDir -Recurse -Filter "notrix_tests.exe" -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime -Descending |
        Select-Object -First 1

    if (-not $binary) { throw "test binary not found under $testDir — did the build succeed?" }
    return $binary.FullName
}

switch ($Command) {
    'build' {
        Invoke-Build 'host-debug'
        Write-Host "`nBuild complete." -ForegroundColor Green
    }

    'test' {
        Invoke-Build 'host-debug'
        $binary = Get-TestBinary
        $filter = if ($Rest) { $Rest[0] } else { '' }
        & $binary $filter
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    }

    'ci' {
        # What CI actually runs: warnings as errors, and missing golden fixtures
        # treated as failures rather than quietly created. Worth having locally,
        # because the alternative is finding out from a red build.
        Invoke-Build 'ci'
        $binary = Get-TestBinary 'ci'
        $env:NOTRIX_STRICT_GOLDEN = '1'
        try { & $binary } finally { Remove-Item env:NOTRIX_STRICT_GOLDEN -ErrorAction SilentlyContinue }
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
        Write-Host "`nCI checks passed." -ForegroundColor Green
    }

    'verify' {
        # Drives the built WASM module headlessly. The C++ suite cannot catch a
        # missing Emscripten export or a stale EXPORTED_FUNCTIONS list; this can.
        $node = Get-Command node -ErrorAction SilentlyContinue
        if (-not $node) { throw "node not found on PATH; needed to verify the emulator build" }

        $harness = Join-Path $repoRoot 'simulator/web/tools/verify.mjs'
        & $node.Source $harness
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    }

    'device' {
        # Cross-compiles for the TC002 and runs the result under ARM emulation.
        # Needs no hardware, and answers the questions that are expensive to get
        # wrong on a device: does the core build for ARMv7, does it link, does it
        # execute, and what does the binary depend on.
        $engine = (Get-Command podman -ErrorAction SilentlyContinue) ??
                  (Get-Command docker -ErrorAction SilentlyContinue)
        if (-not $engine) {
            throw "podman or docker is needed to run the pinned cross-toolchain. See tooling/cross/."
        }

        $image = 'notrix-cross:bookworm'
        & $engine.Source build -t $image -f tooling/cross/Containerfile tooling/cross
        if ($LASTEXITCODE -ne 0) { throw "could not build the cross-toolchain image" }

        $script = @'
set -e
cmake --preset device-arm
cmake --build --preset device-arm
cd /src/build/device-arm/firmware
echo
echo "--- artifact ---"
file notrix_device_smoke
arm-linux-gnueabihf-strip -o /tmp/stripped notrix_device_smoke
echo "stripped: $(stat -c %s /tmp/stripped) bytes"
readelf -d notrix_device_smoke | grep NEEDED || echo "shared libraries: none (static)"
echo
echo "--- running on ARM ---"
qemu-arm-static notrix_device_smoke
'@
        # PowerShell here-strings carry CRLF line endings, and bash reads the
        # carriage return as part of each command, so every path ends in an
        # invisible character and nothing resolves.
        $script = $script -replace "`r", ""

        & $engine.Source run --rm -v "${repoRoot}:/src" $image bash -c $script
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
        Write-Host "`nDevice build runs on ARM." -ForegroundColor Green
    }

    'panel' {
        # Cross-builds the channel-order test and runs it on the real matrix.
        #
        # This is the one question the spidev capture could not answer: every
        # lit pixel the vendor app drew was white, and white is the same bytes
        # under RGB, GRB and BGR. The answer comes from a person looking at the
        # panel, so the job here is to get the binary in front of them with as
        # few steps as possible.
        $target = if ($Rest) { $Rest[0] } else { $null }

        $adb = Get-Command adb -ErrorAction SilentlyContinue
        if (-not $adb) { throw "adb not found on PATH. See docs/bring-up.md for how to get it." }

        $engine = (Get-Command podman -ErrorAction SilentlyContinue) ??
                  (Get-Command docker -ErrorAction SilentlyContinue)
        if (-not $engine) {
            throw "podman or docker is needed to run the pinned cross-toolchain. See tooling/cross/."
        }

        # Static, so the bookworm image is fine — the __libc_start_main problem
        # only bites dynamically linked executables.
        $image = 'notrix-cross:bookworm'
        & $engine.Source build -t $image -f tooling/cross/Containerfile tooling/cross
        if ($LASTEXITCODE -ne 0) { throw "could not build the cross-toolchain image" }

        $script = @'
set -e
cmake --preset device-arm
cmake --build --preset device-arm --target notrix_panel_test
file /src/build/device-arm/firmware/notrix_panel_test
'@
        $script = $script -replace "`r", ""

        & $engine.Source run --rm -v "${repoRoot}:/src" $image bash -c $script
        if ($LASTEXITCODE -ne 0) { throw "cross-build failed" }

        $binary = Join-Path $repoRoot 'build\device-arm\firmware\notrix_panel_test'
        if (-not (Test-Path $binary)) { throw "expected $binary after the build" }

        if ($target) {
            & $adb.Source connect $target | Out-Null
        }

        # /tmp is the volatile path — a power cycle wipes it, which is exactly
        # what we want from something that takes the panel away from zkgui.
        & $adb.Source push $binary /tmp/notrix_panel_test
        if ($LASTEXITCODE -ne 0) { throw "adb push failed — is the device connected?" }

        & $adb.Source shell chmod 700 /tmp/notrix_panel_test

        Write-Host "`nLook at the panel." -ForegroundColor Green
        Write-Host "  Three bands, one channel each, left to right." -ForegroundColor Gray
        Write-Host "  Leftmost colour is channel 0, middle is 1, right is 2." -ForegroundColor Gray
        Write-Host "  One white dot top-left, two white pixels top-right." -ForegroundColor Gray
        Write-Host ""

        & $adb.Source shell /tmp/notrix_panel_test
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    }

    'golden' {
        Invoke-Build 'host-debug'
        $binary = Get-TestBinary
        Write-Host "Regenerating golden fixtures. Review the PNGs in firmware\tests\testdata before committing." -ForegroundColor Yellow
        $env:NOTRIX_UPDATE_GOLDEN = '1'
        try { & $binary } finally { Remove-Item env:NOTRIX_UPDATE_GOLDEN -ErrorAction SilentlyContinue }
    }

    'preview' {
        # Renders frames to PNG through the simulator adapter. Not the emulator:
        # no input, nothing interactive — but it needs no Emscripten toolchain.
        Invoke-Build 'host-debug'

        $binary = Get-ChildItem (Join-Path $repoRoot "build\host-debug\firmware") -Recurse -Filter "notrix_preview.exe" -ErrorAction SilentlyContinue |
            Sort-Object LastWriteTime -Descending | Select-Object -First 1
        if (-not $binary) { throw "notrix_preview not found — did the build succeed?" }

        $outDir = Join-Path $repoRoot 'build\preview'
        New-Item -ItemType Directory -Force -Path $outDir | Out-Null

        & $binary.FullName $outDir
        if ($LASTEXITCODE -ne 0) { throw "preview render failed" }

        $page = Join-Path $outDir 'index.html'
        Write-Host "`nOpening $page" -ForegroundColor Green
        Start-Process $page
    }

    'emulator' {
        if (-not $env:EMSDK) {
            throw "EMSDK is not set. Install the Emscripten SDK, then run its emsdk_env.ps1 in this shell. See docs/development/toolchain.md"
        }
        Invoke-Build 'emulator'
        Write-Host "`nEmulator built into simulator\web\public." -ForegroundColor Green
    }

    'serve' {
        if ($env:EMSDK) { Invoke-Build 'emulator' }
        else { Write-Host "EMSDK not set — serving whatever is already in simulator\web\public." -ForegroundColor Yellow }

        $webRoot = Join-Path $repoRoot 'simulator\web\public'
        if (-not (Test-Path $webRoot)) { throw "no emulator build found at $webRoot" }

        # Resolve python.exe explicitly. A bare "python" on Windows often hits
        # the Microsoft Store app-execution alias, which is a stub that opens the
        # Store rather than running anything.
        $python = Get-Command python.exe -ErrorAction SilentlyContinue |
            Where-Object { $_.Source -notlike '*WindowsApps*' } |
            Select-Object -First 1
        if (-not $python) { throw "python.exe not found; needed for the static dev server" }

        Write-Host "Serving $webRoot at http://localhost:8080/ (Ctrl+C to stop)" -ForegroundColor Green
        & $python.Source -m http.server 8080 --directory $webRoot
    }

    'clean' {
        $buildDir = Join-Path $repoRoot 'build'
        if (Test-Path $buildDir) {
            Remove-Item $buildDir -Recurse -Force
            Write-Host "Removed $buildDir" -ForegroundColor Green
        } else {
            Write-Host "Nothing to clean."
        }
    }

    'doctor' {
        Write-Host "NOTRIX toolchain status`n" -ForegroundColor Cyan

        try { $cmake = Find-CMake; Write-Host ("  cmake       OK    " + $cmake) -ForegroundColor Green }
        catch { Write-Host "  cmake       MISSING" -ForegroundColor Red }

        $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
        if (Test-Path $vswhere) {
            $vsPath = & $vswhere -latest -products * -property installationPath
            $msvc = if ($vsPath) { Join-Path $vsPath 'VC\Tools\MSVC' } else { $null }
            if ($msvc -and (Test-Path $msvc)) {
                $toolset = (Get-ChildItem $msvc -Name | Select-Object -Last 1)
                Write-Host "  MSVC        OK    $toolset" -ForegroundColor Green
            } else {
                Write-Host "  MSVC        MISSING  (add 'Desktop development with C++' in the VS Installer)" -ForegroundColor Red
            }
        } else {
            Write-Host "  MSVC        UNKNOWN  (vswhere not found)" -ForegroundColor Yellow
        }

        if ($env:EMSDK) { Write-Host ("  emscripten  OK    " + $env:EMSDK) -ForegroundColor Green }
        else { Write-Host "  emscripten  MISSING  (needed only for 'dev.ps1 emulator')" -ForegroundColor Yellow }

        $python = Get-Command python.exe -ErrorAction SilentlyContinue |
            Where-Object { $_.Source -notlike '*WindowsApps*' } | Select-Object -First 1
        if ($python) { Write-Host ("  python      OK    " + $python.Source) -ForegroundColor Green }
        else { Write-Host "  python      MISSING  (needed only for 'dev.ps1 serve')" -ForegroundColor Yellow }

        Write-Host "`n  TC002       not required until Phase 7" -ForegroundColor DarkGray
    }
}
