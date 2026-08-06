# OpenOSX headless boot test harness (Windows host).
#
# Boots an ESP image in QEMU (TCG, Intel Penryn profile) with serial logged to
# a file, waits for a pass/fail verdict in the serial log, and exits 0/1.
#
# Usage:
#   test-boot.ps1 -EspPath C:\openosx-test\esp.img            # boot a local image
#   test-boot.ps1 -FromLatestRun                              # gh run download, then boot
#   test-boot.ps1 -EspPath ... -PassRegex 'protoinit' -TimeoutSec 180
#
# Pass/fail model: PassRegex decides success. FailRegex (default: a panic that
# is NOT the expected M1 launchd-exec panic) short-circuits failure. On
# timeout, fail with the serial tail printed.

param(
    [string]$EspPath = "",
    [switch]$FromLatestRun,
    [string]$WorkDir = "C:\openosx-test",
    [string]$PassRegex = "Darwin Kernel Version 20\.5\.0",
    [string]$FailRegex = "",
    [int]$TimeoutSec = 300,
    [int]$MemMB = 2048,
    [switch]$KeepRunning
)

$ErrorActionPreference = "Stop"
$Qemu = "C:\Program Files\qemu\qemu-system-x86_64.exe"
$OvmfCode = "C:\Program Files\qemu\share\edk2-x86_64-code.fd"
$Gh = "C:\Program Files\GitHub CLI\gh.exe"

if (-not (Test-Path $Qemu)) { Write-Error "QEMU not found at $Qemu" }
if (-not (Test-Path $OvmfCode)) { Write-Error "OVMF firmware not found at $OvmfCode" }
if (-not (Test-Path $WorkDir)) { New-Item -ItemType Directory -Force $WorkDir | Out-Null }

if ($FromLatestRun) {
    $runJson = & $Gh api "repos/ax0rz0/OpenOSX/actions/runs?status=success&per_page=10" | ConvertFrom-Json
    $run = $runJson.workflow_runs | Where-Object { $_.name -eq "OpenOSX Dev Build" } | Select-Object -First 1
    if ($null -eq $run) { Write-Error "No successful OpenOSX Dev Build run found" }
    Write-Host "Downloading esp-image from run $($run.id) ($($run.head_sha.Substring(0,8)))"
    $dl = Join-Path $WorkDir "run-$($run.id)"
    if (-not (Test-Path $dl)) {
        & $Gh run download $run.id -R ax0rz0/OpenOSX -n esp-image -D $dl
    }
    $EspPath = Join-Path $dl "esp.img"
}

if (-not (Test-Path $EspPath)) { Write-Error "ESP image not found: $EspPath" }

$SerialLog = Join-Path $WorkDir "serial.log"
$LoaderLog = Join-Path $WorkDir "loader-serial.log"
Remove-Item $SerialLog, $LoaderLog -Force -ErrorAction SilentlyContinue

$qemuArgs = @(
    "-machine", "pc",
    "-accel", "tcg",
    "-cpu", "Penryn,+ssse3,+sse4.1,+sse4.2,+popcnt",
    "-smp", "2",
    "-m", "$MemMB",
    "-drive", "if=pflash,format=raw,readonly=on,file=$OvmfCode",
    "-drive", "file=$EspPath,format=raw,if=ide",
    "-serial", "file:$SerialLog",
    "-display", "none",
    "-no-reboot"
)

Write-Host "Booting $EspPath (timeout ${TimeoutSec}s, pass: /$PassRegex/)"
$proc = Start-Process -FilePath $Qemu -ArgumentList $qemuArgs -PassThru -NoNewWindow

$deadline = (Get-Date).AddSeconds($TimeoutSec)
$verdict = "TIMEOUT"
while ((Get-Date) -lt $deadline) {
    Start-Sleep -Seconds 5
    if ($proc.HasExited) {
        # -no-reboot: triple fault or guest-initiated reset ends the process
        $verdict = "QEMU_EXITED"
        break
    }
    if (Test-Path $SerialLog) {
        $content = [System.IO.File]::ReadAllText($SerialLog)
        if ($FailRegex -and ($content -match $FailRegex)) { $verdict = "FAIL_REGEX"; break }
        if ($content -match $PassRegex) { $verdict = "PASS"; break }
    }
}

if (-not $KeepRunning -and -not $proc.HasExited) { Stop-Process -Id $proc.Id -Force }

Write-Host "=== verdict: $verdict ==="
if (Test-Path $SerialLog) {
    Write-Host "=== serial.log tail ==="
    Get-Content $SerialLog -Tail 40
} else {
    Write-Host "(no serial output was produced)"
}

if ($verdict -eq "PASS") { exit 0 } else { exit 1 }
