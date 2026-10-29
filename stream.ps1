param(
    [string]$Port = '',
    [ValidateRange(0, 2147483647)]
    [int]$Seconds = 0,
    [string]$OutputFile = '',
    [string]$TranscriptFile = ''
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if (-not $Port) {
    $ports = @([System.IO.Ports.SerialPort]::GetPortNames())
    if ($ports.Count -ne 1) {
        throw "Found $($ports.Count) serial ports. Pass -Port COM5 (the ST-LINK virtual COM port)."
    }
    $Port = $ports[0]
}
$serial = New-Object System.IO.Ports.SerialPort $Port,115200,None,8,one
$serial.ReadTimeout = 250
$serial.NewLine = "`n"
$writer = $null
$transcript = $null
$header = 'elapsed_ms,normalized_signal,raw_counts,status'
$samplePattern = '^-?\d+\.\d{6}$'
$firmwareSeen = $false
$interpolationSeen = $false
$scaleSeen = $false
$zeroOffset = 0L
$countsPerUnit = 0L
$displayPolarity = 0L
$sampleCount = 0
$saturatedCount = 0
$lastTimestamp = $null
$sampleSpanMs = 0.0

function New-LogWriter([string]$Path) {
    $fullPath = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Path)
    $file = [System.IO.File]::Open($fullPath, [System.IO.FileMode]::CreateNew, [System.IO.FileAccess]::Write, [System.IO.FileShare]::Read)
    $log = [System.IO.StreamWriter]::new($file, [System.Text.UTF8Encoding]::new($false))
    $log.AutoFlush = $true
    return $log
}

try {
    $serial.Open()
    $serial.DiscardInBuffer()
    if ($OutputFile) {
        $writer = New-LogWriter $OutputFile
        $writer.WriteLine($header)
    }
    if ($TranscriptFile) { $transcript = New-LogWriter $TranscriptFile }
    Write-Host "Streaming $Port at 115200 baud; 200 Hz zeroed normalized signal, not calibrated airspeed. Ctrl+C stops."
    $timer = [System.Diagnostics.Stopwatch]::StartNew()
    $lastSampleTime = 0.0
    while ($Seconds -eq 0 -or $timer.Elapsed.TotalSeconds -lt $Seconds) {
        if ($timer.Elapsed.TotalSeconds - $lastSampleTime -gt 4) {
            throw 'No valid HX711 samples for 4 seconds. Check 3V3/GND, DT=D7 and SCK=D8 and the diagnostics above.'
        }
        try {
            $line = $serial.ReadLine().Trim()
        } catch [System.TimeoutException] {
            continue
        }
        if ($transcript) { $transcript.WriteLine($line) }
        if ($line.StartsWith('# ERROR:')) {
            throw $line
        } elseif ($line.StartsWith('# FIRMWARE=nucleo-f411re-hx711-v5;')) {
            $firmwareSeen = $true
            Write-Host $line
        } elseif ($line.StartsWith('# INTERPOLATION=linear; output_hz=200; delay_ms=150;')) {
            $interpolationSeen = $true
            Write-Host $line
        } elseif ($line -match '^# SCALE zero_offset_counts=(-?\d+) counts_per_unit=(\d+) polarity=(-?1)$') {
            $zeroOffset = [long]$Matches[1]
            $countsPerUnit = [long]$Matches[2]
            $displayPolarity = [long]$Matches[3]
            if ($countsPerUnit -ne 100000 -or $displayPolarity -ne -1) { throw 'Unexpected display gain or polarity.' }
            $scaleSeen = $true
            Write-Host $line
        } elseif ($line.StartsWith('#')) {
            Write-Host $line
        } elseif ($line -match $samplePattern) {
            if (-not $scaleSeen) { continue }
            $timestamp = $timer.Elapsed.TotalMilliseconds
            $normalized = [decimal]::Parse($line, [Globalization.CultureInfo]::InvariantCulture)
            $raw = [long]($normalized * $countsPerUnit * $displayPolarity + $zeroOffset)
            if ($raw -lt -8388608 -or $raw -gt 8388607) {
                throw "Invalid 24-bit sample: $line"
            }
            $isSaturated = $raw -eq -8388608 -or $raw -eq 8388607
            $status = if ($isSaturated) { 'SATURATED' } else { 'ZEROED_NORMALIZED_INTERPOLATED' }
            if ($null -ne $lastTimestamp) {
                $delta = $timestamp - $lastTimestamp
                if ($delta -gt 1500) { throw 'Serial receipt gap exceeded 1500 ms.' }
                $sampleSpanMs += $delta
            }
            $lastTimestamp = $timestamp
            $lastSampleTime = $timer.Elapsed.TotalSeconds
            ++$sampleCount
            if ($isSaturated) { ++$saturatedCount }
            if ($writer) {
                $timestampText = $timestamp.ToString('F3', [Globalization.CultureInfo]::InvariantCulture)
                $writer.WriteLine("$timestampText,$line,$raw,$status")
            }
            Write-Output $line
        } elseif ($line -ne $header -and $line.Length -gt 0) {
            Write-Warning "Ignoring unrecognized serial line: $line"
        }
    }
    if (-not $firmwareSeen) { throw 'Expected HX711 firmware identity was not received. Build and flash with .\run.ps1 -Flash.' }
    if (-not $interpolationSeen) { throw 'The expected 200 Hz linear interpolation metadata was not received.' }
    if (-not $scaleSeen) { throw 'The zero-offset and normalization metadata were not received.' }
    if ($sampleCount -lt 5 -or $sampleSpanMs -lt 100) { throw 'Insufficient successive conversions to verify the HX711 link. Monitor for at least 5 seconds.' }
    $rate = [Math]::Round(($sampleCount - 1) * 1000.0 / $sampleSpanMs, 2)
    if ($rate -lt 196 -or $rate -gt 204) { throw "Published rate $rate Hz is outside the expected 196-204 Hz range." }
    Write-Host "PASS: $sampleCount interpolated values on $Port, approximately $rate values/s; $saturatedCount saturated."
    Write-Host 'Output is linearly interpolated with 150 ms delay; the HX711 conversion rate is unchanged.'
} finally {
    if ($writer) { $writer.Dispose() }
    if ($transcript) { $transcript.Dispose() }
    $serial.Dispose()
}
