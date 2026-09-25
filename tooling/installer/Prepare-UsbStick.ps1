# SPDX-License-Identifier: GPL-3.0-or-later
#
# Prepares a USB stick the TC002 will flash from.
#
# The procedure is three fiddly details in a trench coat, and getting any one
# of them wrong looks identical from the outside - the clock simply ignores
# the stick:
#
#   * a plain flash drive, not a card reader. Readers present an empty-slot
#     state and sometimes several LUNs, and the device's vold would not mount
#     one.
#   * FAT32 with 4 KB clusters. Windows defaults a 32 GB volume to 16 KB,
#     which is what a failed attempt used.
#   * a file called `zkautoupgrade` containing one byte, 0x30. Without it the
#     loader ignores external media entirely - an attempt with update.img,
#     extupdate.img, full_update.zk *and* zkimg/update.img but no sentinel did
#     nothing at all. Nobody could guess this from the binaries; it came from
#     Ulanzi support.
#
# **This erases the drive.** Every guard below exists because the cost of
# picking the wrong letter is somebody's photographs.

[CmdletBinding()]
param(
    # Drive letter to prepare, e.g. D: - omit to be shown a list.
    [string] $Drive,

    # The image to write. Defaults to what 'dev.ps1 image' produces.
    [string] $Image,

    # Skip the typed confirmation. For scripting only; you are on your own.
    [switch] $Force
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)

function Fail($message) {
    Write-Host "`n  $message`n" -ForegroundColor Red
    exit 1
}

# --- the image ---------------------------------------------------------------

if (-not $Image) {
    $Image = Join-Path $repoRoot 'restore\stipple-update.img'
}

if (-not (Test-Path $Image)) {
    Fail @"
No image at $Image

Build one from your own device first - there is nothing to download, because
an image contains Ulanzi's firmware:

    .\dev.ps1 capture <device-ip>:5555
    .\dev.ps1 image
"@
}

$imageInfo = Get-Item $Image
Write-Host ""
Write-Host "  image   $($imageInfo.Name)  ($([math]::Round($imageInfo.Length / 1MB, 2)) MB)" -ForegroundColor Cyan

# Checked before anything is erased. An image the device will refuse is worth
# discovering now rather than while standing over the clock wondering why the
# stick did nothing.
$python = Get-Command python.exe -ErrorAction SilentlyContinue
if ($python) {
    $imgtool = Join-Path $repoRoot 'tooling\imgtool\imgtool.py'
    if (Test-Path $imgtool) {
        $report = & $python.Source $imgtool info $Image 2>&1
        if ($LASTEXITCODE -ne 0) {
            Fail "That file is not an update image the device would accept:`n$report"
        }
        $report | Where-Object { $_ -match 'partition|payload|CRC32|MD5' } |
            ForEach-Object { Write-Host "          $_" -ForegroundColor DarkGray }
    }
}

# --- choosing the drive ------------------------------------------------------

function Get-Candidates {
    # DriveType 2 is removable. Fixed disks are never offered, so an external
    # backup drive cannot be picked by accident.
    Get-CimInstance Win32_LogicalDisk -Filter 'DriveType = 2' | ForEach-Object {
        [pscustomobject]@{
            Letter = $_.DeviceID
            Label  = $_.VolumeName
            FS     = $_.FileSystem
            SizeGB = [math]::Round($_.Size / 1GB, 1)
        }
    }
}

$candidates = @(Get-Candidates)

if (-not $candidates) {
    Fail "No removable drive found. Insert a plain USB flash drive - not a card reader."
}

if (-not $Drive) {
    Write-Host "`n  Removable drives:" -ForegroundColor Cyan
    foreach ($c in $candidates) {
        $label = if ($c.Label) { $c.Label } else { '(no label)' }
        Write-Host ("    {0}  {1,-16} {2,-6} {3} GB" -f $c.Letter, $label, $c.FS, $c.SizeGB)
    }
    Write-Host ""
    $Drive = Read-Host "  Which drive should be erased and prepared"
}

$Drive = $Drive.Trim().TrimEnd('\')
if ($Drive -notmatch '^[A-Za-z]:?$') { Fail "'$Drive' is not a drive letter." }
if ($Drive.Length -eq 1) { $Drive = "$Drive`:" }
$Drive = $Drive.ToUpper()

$target = $candidates | Where-Object { $_.Letter -eq $Drive }
if (-not $target) {
    Fail @"
$Drive is not a removable drive.

Only removable drives are offered, on purpose: this formats the target, and a
fixed disk in that list would be one keystroke from a very bad afternoon.
"@
}

# A 1 TB "removable" volume is an external disk somebody uses for backups, not
# a stick for a pixel clock.
if ($target.SizeGB -gt 128) {
    Fail "$Drive is $($target.SizeGB) GB. That is too large to be the flash drive you meant; refusing."
}

# --- confirmation ------------------------------------------------------------

Write-Host ""
Write-Host "  About to ERASE $Drive" -ForegroundColor Yellow
Write-Host "    label   $(if ($target.Label) { $target.Label } else { '(none)' })" -ForegroundColor Yellow
Write-Host "    size    $($target.SizeGB) GB" -ForegroundColor Yellow
Write-Host "    format  $($target.FS)" -ForegroundColor Yellow

$existing = @(Get-ChildItem "$Drive\" -Force -ErrorAction SilentlyContinue |
              Where-Object { $_.Name -notlike 'System Volume*' } |
              Select-Object -First 8)
if ($existing) {
    Write-Host "`n    It currently contains:" -ForegroundColor Yellow
    $existing | ForEach-Object { Write-Host "      $($_.Name)" -ForegroundColor Yellow }
}

if (-not $Force) {
    Write-Host ""
    # Typing the letter rather than 'y'. A yes/no prompt is answered by reflex;
    # this one cannot be, and the letter is the thing being got wrong.
    $typed = Read-Host "  Type $Drive to confirm, anything else to cancel"
    if ($typed.Trim().ToUpper().TrimEnd('\') -ne $Drive) {
        Write-Host "`n  Cancelled. Nothing was changed.`n" -ForegroundColor Green
        exit 0
    }
}

# --- do it -------------------------------------------------------------------

Write-Host "`n  Formatting FAT32 with 4 KB clusters..." -ForegroundColor Cyan

# AllocationUnitSize is the detail Windows gets wrong on its own: a 32 GB
# volume defaults to 16 KB, and a stick formatted that way was ignored by the
# device.
try {
    Format-Volume -DriveLetter $Drive.TrimEnd(':') -FileSystem FAT32 `
        -AllocationUnitSize 4096 -NewFileSystemLabel 'STIPPLE' `
        -Confirm:$false -Force | Out-Null
} catch {
    Fail "Could not format $Drive - $($_.Exception.Message)"
}

Write-Host "  Copying the image..." -ForegroundColor Cyan
Copy-Item $Image "$Drive\update.img" -Force

Write-Host "  Writing the zkautoupgrade sentinel..." -ForegroundColor Cyan
# Exactly one byte, 0x30, ASCII '0'. No newline, no BOM, no extension.
[System.IO.File]::WriteAllBytes("$Drive\zkautoupgrade", [byte[]](0x30))

# --- verify ------------------------------------------------------------------

Write-Host "  Verifying..." -ForegroundColor Cyan

$sourceHash = (Get-FileHash $Image -Algorithm SHA256).Hash
$copiedHash = (Get-FileHash "$Drive\update.img" -Algorithm SHA256).Hash
if ($sourceHash -ne $copiedHash) {
    Fail "The copy on $Drive does not match the source. Try a different stick."
}

$sentinel = [System.IO.File]::ReadAllBytes("$Drive\zkautoupgrade")
if ($sentinel.Length -ne 1 -or $sentinel[0] -ne 0x30) {
    Fail "The sentinel did not write correctly."
}

$volume = Get-Volume -DriveLetter $Drive.TrimEnd(':')
if ($volume.AllocationUnitSize -and $volume.AllocationUnitSize -ne 4096) {
    Write-Host "  warning: cluster size is $($volume.AllocationUnitSize), not 4096" -ForegroundColor Yellow
}

Write-Host ""
Write-Host "  Ready." -ForegroundColor Green
Write-Host "    $Drive\update.img        $([math]::Round((Get-Item "$Drive\update.img").Length / 1MB, 2)) MB, verified" -ForegroundColor Green
Write-Host "    $Drive\zkautoupgrade     1 byte" -ForegroundColor Green
Write-Host ""
Write-Host "  Now, in this order:" -ForegroundColor Cyan
Write-Host "    1. Power the clock from the base pins, not USB - the port must stay free."
Write-Host "    2. Switch it on."
Write-Host "    3. Insert the stick."
Write-Host "    4. Wait. It restarts and reflashes on its own."
Write-Host "    5. Remove the stick when the Ulanzi logo appears."
Write-Host ""
Write-Host "  Remove it because whatever sits at /mnt/storage/update.img is what" -ForegroundColor Yellow
Write-Host "  the device installs on its next recovery - and one kind of recovery" -ForegroundColor Yellow
Write-Host "  happens unattended. See docs/install.md." -ForegroundColor Yellow
Write-Host ""
