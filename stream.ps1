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
$header = 'elapsed_ms,wind_speed_mps,equivalent_mass_g_estimate,raw_counts_estimate,status'
$samplePattern = '^-?\d+\.\d{6}$'
$firmwareSeen = $false
$interpolationSeen = $false
$scaleSeen = $false
$zeroOffset = 0L
$gramsPerCount = 0.0
$unitsSeen = $false
$model = @{}
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
    Write-Host "Streaming $Port at 115200 baud; 200 Hz estimated m/s. Cd/density assumed; no tunnel accuracy validation. Fan OFF during tare. Ctrl+C stops."
    $timer = [System.Diagnostics.Stopwatch]::StartNew()
    $lastSampleTime = 0.0
    while ($Seconds -eq 0 -or $timer.Elapsed.TotalSeconds -lt $Seconds) {
        $silenceLimit = if ($sampleCount -eq 0) { 12 } else { 4 }
        if ($timer.Elapsed.TotalSeconds - $lastSampleTime -gt $silenceLimit) {
            throw "No valid HX711 samples for $silenceLimit seconds. Check 3V3/GND, DT=D7 and SCK=D8 and the diagnostics above."
        }
        try {
            $line = $serial.ReadLine().Trim()
        } catch [System.TimeoutException] {
            continue
        }
        if ($transcript) { $transcript.WriteLine($line) }
        if ($line.StartsWith('# ERROR:')) {
            throw $line
        } elseif ($line.StartsWith('# FIRMWARE=nucleo-f411re-hx711-v9;')) {
            $firmwareSeen = $true
            Write-Host $line
        } elseif ($line.StartsWith('# FIRMWARE=')) {
            throw "This verification script expects v9 with settled startup zero and the existing speed correction; the board reports: $line. Upload v9 when ready."
        } elseif ($line.StartsWith('# INTERPOLATION=linear; output_hz=200; delay_ms=150;')) {
            $interpolationSeen = $true
            Write-Host $line
        } elseif ($line.StartsWith('# UNITS=m/s; METHOD=plate_drag_estimate; UNVALIDATED_AIRSPEED')) {
            $unitsSeen = $true
            Write-Host $line
        } elseif ($line -match '^# MASS_CALIBRATION tare_counts=(-?\d+) grams_per_count=(-?\d+\.\d+) tare_samples=(\d+) ready=([01])$') {
            $newZero = [long]$Matches[1]
            $newScale = [double]::Parse($Matches[2], [Globalization.CultureInfo]::InvariantCulture)
            $tareSamples = [int]$Matches[3]
            $ready = $Matches[4] -eq '1'
            if ($scaleSeen -and (-not $ready -or $newZero -ne $zeroOffset -or $newScale -ne $gramsPerCount)) { throw 'Calibration changed during capture; restart verification.' }
            if ($ready) {
                if ($tareSamples -ne 32 -or [Math]::Abs($newScale - (-0.00089903368772)) -gt 1e-12) { throw 'Unexpected mass calibration coefficient or tare size.' }
                $zeroOffset = $newZero
                $gramsPerCount = $newScale
                $scaleSeen = $true
            }
            Write-Host $line
        } elseif ($line -match '^# WIND_MODEL (area_m2|cd|rho_kg_m3|gravity_m_s2|speed_gain)=([0-9]+\.[0-9]+)$') {
            $key = $Matches[1]
            $value = [double]::Parse($Matches[2], [Globalization.CultureInfo]::InvariantCulture)
            if ($value -le 0) { throw 'Invalid wind-model parameter.' }
            if ($model.ContainsKey($key) -and $model[$key] -ne $value) { throw 'Wind-model parameters changed during capture.' }
            $model[$key] = $value
            Write-Host $line
        } elseif ($line -match '^# WIND_MODEL force_polarity=(-?1);') {
            $value = [int]$Matches[1]
            if ($model.ContainsKey('force_polarity') -and $model.force_polarity -ne $value) { throw 'Wind force polarity changed during capture.' }
            $model.force_polarity = $value
            Write-Host $line
        } elseif ($line.StartsWith('#')) {
            Write-Host $line
        } elseif ($line -match $samplePattern) {
            if (-not $scaleSeen -or $model.Count -ne 6) { continue }
            $timestamp = $timer.Elapsed.TotalMilliseconds
            $speed = [double]::Parse($line, [Globalization.CultureInfo]::InvariantCulture)
            if ($speed -lt 0) { throw "Expected nonnegative wind speed: $line" }
            $massText = ''
            $rawText = ''
            $status = 'ESTIMATED_AIRSPEED_ZERO_CLIPPED'
            if ($speed -gt 0) {
                $modelSpeed = $speed / $model.speed_gain
                $force = 0.5 * $model.rho_kg_m3 * $model.cd * $model.area_m2 * $modelSpeed * $modelSpeed
                $mass = $force * 1000.0 / $model.gravity_m_s2 * $model.force_polarity
                $raw = [long][Math]::Round($mass / $gramsPerCount + $zeroOffset)
                $massText = $mass.ToString('F6', [Globalization.CultureInfo]::InvariantCulture)
                $rawText = $raw.ToString([Globalization.CultureInfo]::InvariantCulture)
                $status = if ($mass -lt 0 -or $mass -gt 35) { 'ESTIMATED_AIRSPEED_EXTRAPOLATED' } else { 'ESTIMATED_AIRSPEED_INTERPOLATED' }
            }
            if ($null -ne $lastTimestamp) {
                $delta = $timestamp - $lastTimestamp
                if ($delta -gt 1500) { throw 'Serial receipt gap exceeded 1500 ms.' }
                $sampleSpanMs += $delta
            }
            $lastTimestamp = $timestamp
            $lastSampleTime = $timer.Elapsed.TotalSeconds
            ++$sampleCount
            if ($writer) {
                $timestampText = $timestamp.ToString('F3', [Globalization.CultureInfo]::InvariantCulture)
                $writer.WriteLine("$timestampText,$line,$massText,$rawText,$status")
            }
            Write-Output $line
        } elseif ($line -ne $header -and $line.Length -gt 0) {
            Write-Warning "Ignoring unrecognized serial line: $line"
        }
    }
    if (-not $firmwareSeen) { throw 'Expected HX711 firmware identity was not received. Build and flash with .\run.ps1 -Flash.' }
    if (-not $interpolationSeen) { throw 'The expected 200 Hz linear interpolation metadata was not received.' }
    if (-not $scaleSeen -or -not $unitsSeen -or $model.Count -ne 6) { throw 'Ready mass calibration, wind model and estimated-m/s metadata were not received.' }
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
