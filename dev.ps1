# SPDX-License-Identifier: GPL-3.0-or-later
#
# NOTRIX developer entry point (blueprint §29.2).
#
#   .\dev.ps1 build          configure + build the host target
#   .\dev.ps1 test           build and run the host test suite
#   .\dev.ps1 test Canvas    run only tests whose name contains "Canvas"
#   .\dev.ps1 golden         rebuild golden-image fixtures (review the PNGs!)
#   .\dev.ps1 preview        render frames to PNG and open them (no EMSDK needed)
#   .\dev.ps1 emulator       build the browser emulator (needs EMSDK)
#   .\dev.ps1 serve          build the emulator and serve it on localhost
#   .\dev.ps1 clean          remove build output
#   .\dev.ps1 doctor         report toolchain status
#
# The device-side verbs from the blueprint (deploy / logs / restore) arrive in
# Phase 7, once there is a TC002 to talk to.

[CmdletBinding()]
param(
    [Parameter(Position = 0)]
    [ValidateSet('build', 'test', 'golden', 'preview', 'emulator', 'serve', 'clean', 'doctor')]
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

function Get-TestBinary {
    # Single-config generators emit into the preset directory; multi-config ones
    # nest under the configuration name. Switching generators leaves the old
    # binary behind, and running a stale one silently reports stale results, so
    # pick the most recently written rather than the first path that exists.
    $testDir = Join-Path $repoRoot "build\host-debug\firmware\tests"
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

        $python = Get-Command python -ErrorAction SilentlyContinue
        if (-not $python) { throw "python not found; needed for the static dev server" }

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

        $python = Get-Command python -ErrorAction SilentlyContinue
        if ($python) { Write-Host ("  python      OK    " + $python.Source) -ForegroundColor Green }
        else { Write-Host "  python      MISSING  (needed only for 'dev.ps1 serve')" -ForegroundColor Yellow }

        Write-Host "`n  TC002       not required until Phase 7" -ForegroundColor DarkGray
    }
}
