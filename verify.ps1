param(
    [string]$Port = '',
    [ValidateRange(5, 3600)]
    [int]$Seconds = 15
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if (-not $Port) {
    $ports = @([System.IO.Ports.SerialPort]::GetPortNames())
    if ($ports.Count -ne 1) { throw "Found $($ports.Count) serial ports. Pass -Port COM5 explicitly." }
    $Port = $ports[0]
}
$verificationDirectory = Join-Path $PSScriptRoot 'verification'
$null = New-Item -ItemType Directory -Path $verificationDirectory -Force
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss-fff'
$prefix = Join-Path $verificationDirectory "hx711-$stamp"
$csvPath = "$prefix.csv"
$transcriptPath = "$prefix-serial.txt"
$reportPath = "$prefix-report.json"

& (Join-Path $PSScriptRoot 'stream.ps1') -Port $Port -Seconds $Seconds -OutputFile $csvPath -TranscriptFile $transcriptPath | Out-Null

$rows = @(Import-Csv -LiteralPath $csvPath)
$span = 0.0
for ($index = 1; $index -lt $rows.Count; ++$index) {
    $span += [double]::Parse($rows[$index].elapsed_ms, [Globalization.CultureInfo]::InvariantCulture) - [double]::Parse($rows[$index - 1].elapsed_ms, [Globalization.CultureInfo]::InvariantCulture)
}
$statistics = $rows | ForEach-Object { [long]$_.raw_counts } | Measure-Object -Minimum -Maximum
$normalizedStatistics = $rows | ForEach-Object { [double]::Parse($_.normalized_signal, [Globalization.CultureInfo]::InvariantCulture) } | Measure-Object -Minimum -Maximum -Average
$scaleLines = @(Get-Content -LiteralPath $transcriptPath | Where-Object { $_ -match '^# SCALE zero_offset_counts=(-?\d+) counts_per_unit=(\d+) polarity=(-?1)$' })
if ($scaleLines.Count -eq 0 -or $scaleLines[-1] -notmatch '^# SCALE zero_offset_counts=(-?\d+) counts_per_unit=(\d+) polarity=(-?1)$') { throw 'Display scale metadata missing.' }
$zeroOffset = [long]$Matches[1]
$countsPerUnit = [long]$Matches[2]
$displayPolarity = [long]$Matches[3]
$identity = @(Get-Content -LiteralPath $transcriptPath | Where-Object { $_.StartsWith('# FIRMWARE=') } | ForEach-Object { $_.ToString() } | Select-Object -Unique)
$wiring = @(Get-Content -LiteralPath $transcriptPath | Where-Object { $_.StartsWith('# HX711 channel=') } | ForEach-Object { $_.ToString() } | Select-Object -Unique)
$measured = @(Get-Content -LiteralPath $transcriptPath | ForEach-Object {
    if ($_ -match '^# ADC timestamp_ms=(\d+) raw_counts=(-?\d+)$') {
        [PSCustomObject]@{ timestamp = [long]$Matches[1]; count = [long]$Matches[2] }
    }
})
if ($measured.Count -lt 5) { throw 'Insufficient measured HX711 conversions in the serial transcript.' }
$measuredSpan = 0L
for ($index = 1; $index -lt $measured.Count; ++$index) {
    $interval = ($measured[$index].timestamp - $measured[$index - 1].timestamp + 4294967296L) % 4294967296L
    if ($interval -eq 0 -or $interval -gt 1000) { throw 'Invalid measured HX711 conversion interval.' }
    $measuredSpan += $interval
}
$report = [ordered]@{
    result = 'PASS'
    checked_at_utc = [DateTime]::UtcNow.ToString('o')
    serial_port = $Port
    serial_format = 'one signed decimal per line; # diagnostics are ignored by Wind Tunnel Logger'
    units = 'zeroed normalized signal (provisional; NOT calibrated m/s)'
    zero_offset_counts = $zeroOffset
    counts_per_display_unit = $countsPerUnit
    display_polarity = $displayPolarity
    gain_relative_to_v4 = 10
    normalized_min = $normalizedStatistics.Minimum
    normalized_max = $normalizedStatistics.Maximum
    normalized_mean = $normalizedStatistics.Average
    interpolation = 'linear between completed ADC conversions'
    interpolation_delay_ms = 150
    target_output_hz = 200
    rate_clock = 'PC monotonic receipt time'
    firmware_reported_by_board = $identity
    configuration_reported_by_board = $wiring
    samples = $rows.Count
    samples_per_second = [Math]::Round(($rows.Count - 1) * 1000.0 / $span, 3)
    measured_adc_samples = $measured.Count
    measured_adc_hz = [Math]::Round(($measured.Count - 1) * 1000.0 / $measuredSpan, 3)
    raw_min = $statistics.Minimum
    raw_max = $statistics.Maximum
    saturated_samples = @($rows | Where-Object { $_.status -eq 'SATURATED' }).Count
    csv_file = [IO.Path]::GetFileName($csvPath)
    serial_transcript = [IO.Path]::GetFileName($transcriptPath)
    scope = 'Digital communication, resting zero and display normalization, and interpolated publication rate. Actual ADC rate is separate; nonzero airspeed calibration is not available.'
}
$report | ConvertTo-Json | Set-Content -LiteralPath $reportPath -Encoding ascii
Write-Host "Saved verification report: $reportPath"
Write-Output ([PSCustomObject]$report)
