param(
    [string]$Port = '',
    [string]$TranscriptFile = ''
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if (-not [Console]::IsInputRedirected) {
    # Classic Windows console selection can otherwise pause serial acquisition.
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class SerialMonitorConsole {
    [DllImport("kernel32.dll")] public static extern IntPtr GetStdHandle(int id);
    [DllImport("kernel32.dll")] public static extern bool GetConsoleMode(IntPtr handle, out uint mode);
    [DllImport("kernel32.dll")] public static extern bool SetConsoleMode(IntPtr handle, uint mode);
}
'@
    $consoleInput = [SerialMonitorConsole]::GetStdHandle(-10)
    $consoleMode = [uint32]0
    if ([SerialMonitorConsole]::GetConsoleMode($consoleInput, [ref]$consoleMode)) {
        $null = [SerialMonitorConsole]::SetConsoleMode($consoleInput, (($consoleMode -band 4294967231L) -bor 0x80))
    }
}
if (-not $Port) {
    $ports = @([System.IO.Ports.SerialPort]::GetPortNames())
    if ($ports.Count -ne 1) { throw "Found $($ports.Count) serial ports. Pass -Port COM5 explicitly." }
    $Port = $ports[0]
}
$serial = New-Object System.IO.Ports.SerialPort $Port,115200,None,8,one
$serial.ReadTimeout = 500
$writer = $null

try {
    $serial.Open()
    $serial.DiscardInBuffer()
    if ($TranscriptFile) {
        $fullPath = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($TranscriptFile)
        $file = [IO.File]::Open($fullPath, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::Read)
        $writer = [IO.StreamWriter]::new($file, [Text.UTF8Encoding]::new($false))
        $writer.AutoFlush = $true
    }
    Write-Host "Serial monitor: $Port, 115200 baud, 8N1. Ctrl+C stops."
    Write-Host 'The board reports its running firmware, units and calibration in # diagnostic lines. Secure the mount; fan OFF and masses removed during startup tare. Wind-speed accuracy remains unvalidated.'
    $timer = [Diagnostics.Stopwatch]::StartNew()
    $lastActivity = 0.0
    while ($true) {
        try {
            $line = $serial.ReadLine().Trim()
            if ($writer) { $writer.WriteLine($line) }
            Write-Output $line
            $lastActivity = $timer.Elapsed.TotalSeconds
        } catch [TimeoutException] {
            if ($timer.Elapsed.TotalSeconds - $lastActivity -ge 5) {
                Write-Host '# WAITING: no serial output. Check board power and press the black RESET button.' -ForegroundColor Yellow
                $lastActivity = $timer.Elapsed.TotalSeconds
            }
        }
    }
} finally {
    if ($writer) { $writer.Dispose() }
    $serial.Dispose()
}
