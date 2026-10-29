param(
    [switch]$Flash,
    [switch]$Monitor,
    [string]$BoardDrive = '',
    [string]$Port = '',
    [ValidateRange(5, 3600)]
    [int]$MonitorSeconds = 10,
    [string]$Compiler = ''
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

if (-not $Compiler) {
    $command = Get-Command arm-none-eabi-gcc -ErrorAction SilentlyContinue
    if ($command) {
        $Compiler = $command.Source
    } else {
        $toolRoot = Join-Path $env:LOCALAPPDATA 'Arduino15\packages\arduino\tools\arm-none-eabi-gcc'
        $candidates = @(Get-ChildItem "$toolRoot\*\bin\arm-none-eabi-gcc.exe" -ErrorAction SilentlyContinue | Sort-Object FullName -Descending)
        if ($candidates.Count -eq 0) {
            throw 'ARM GCC not found. Pass -Compiler with the path to arm-none-eabi-gcc.exe.'
        }
        $Compiler = $candidates[0].FullName
    }
}

$toolDirectory = Split-Path $Compiler
$objcopy = Join-Path $toolDirectory 'arm-none-eabi-objcopy.exe'
$size = Join-Path $toolDirectory 'arm-none-eabi-size.exe'
$buildDirectory = Join-Path $PSScriptRoot 'build'
$null = New-Item -ItemType Directory -Path $buildDirectory -Force
$elf = Join-Path $buildDirectory 'hx711.elf'
$binary = Join-Path $buildDirectory 'hx711.bin'
$linkerScript = Join-Path $PSScriptRoot 'firmware\stm32f411re.ld'
$sources = @('main.c', 'board.c', 'hx711.c', 'interpolation.c') | ForEach-Object { Join-Path $PSScriptRoot "firmware\$_" }

& $Compiler -mcpu=cortex-m4 -mthumb -mfloat-abi=soft -std=c11 -Os -g3 -Wall -Wextra -Werror -ffreestanding -fno-builtin -ffunction-sections -fdata-sections -nostdlib '-Wl,--gc-sections' "-Wl,-Map=$buildDirectory\hx711.map" -T $linkerScript @sources -o $elf
if ($LASTEXITCODE -ne 0) { throw 'Firmware compilation failed.' }
& $objcopy -O binary $elf $binary
if ($LASTEXITCODE -ne 0) { throw 'Binary conversion failed.' }
& $size $elf
if ($LASTEXITCODE -ne 0) { throw 'Firmware size check failed.' }
Write-Host "Built $binary"

if ($Flash) {
    if (-not $BoardDrive) {
        $boards = @(Get-PSDrive -PSProvider FileSystem | Where-Object {
            (Test-Path -LiteralPath (Join-Path $_.Root 'DETAILS.TXT')) -and
            (Test-Path -LiteralPath (Join-Path $_.Root 'MBED.HTM'))
        })
        if ($boards.Count -ne 1) {
            throw "Found $($boards.Count) MBED drives. Pass -BoardDrive 'D:\' explicitly."
        }
        $BoardDrive = $boards[0].Root
    }
    if (-not (Test-Path -LiteralPath (Join-Path $BoardDrive 'DETAILS.TXT')) -or
        -not (Test-Path -LiteralPath (Join-Path $BoardDrive 'MBED.HTM'))) {
        throw "No MBED board found at $BoardDrive."
    }
    Write-Host "Uploading to $BoardDrive (replaces the board's current program)..."
    Copy-Item -LiteralPath $binary -Destination (Join-Path $BoardDrive 'hx711.bin') -Force
    Start-Sleep -Seconds 3
    $failureFile = Join-Path $BoardDrive 'FAIL.TXT'
    if (Test-Path -LiteralPath $failureFile) {
        throw "Board reported an upload failure: $(Get-Content -LiteralPath $failureFile -Raw)"
    }
    Write-Host 'Copy completed. Use -Monitor to verify the running firmware and HX711 response.'
}

if ($Monitor) {
    & (Join-Path $PSScriptRoot 'stream.ps1') -Port $Port -Seconds $MonitorSeconds
}
