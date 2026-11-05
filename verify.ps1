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
$speedStatistics = $rows | ForEach-Object { [double]::Parse($_.wind_speed_mps, [Globalization.CultureInfo]::InvariantCulture) } | Measure-Object -Minimum -Maximum -Average
$windModel = @{}
Get-Content -LiteralPath $transcriptPath | ForEach-Object {
    if ($_ -match '^# WIND_MODEL (area_m2|cd|rho_kg_m3|gravity_m_s2|pressure_pa|temperature_c|speed_gain)=(-?\d+\.\d+)$') {
        $windModel[$Matches[1]] = [double]::Parse($Matches[2], [Globalization.CultureInfo]::InvariantCulture)
    } elseif ($_ -match '^# WIND_MODEL force_polarity=(-?1);') {
        $windModel.force_polarity = [int]$Matches[1]
    }
}
if ($windModel.Count -ne 8) { throw 'Incomplete reported wind-model assumptions.' }
$scalePattern = '^# MASS_CALIBRATION tare_counts=(-?\d+) grams_per_count=(-?\d+\.\d+) tare_samples=(\d+) ready=1$'
$scaleLines = @(Get-Content -LiteralPath $transcriptPath | Where-Object { $_ -match $scalePattern })
if ($scaleLines.Count -eq 0 -or $scaleLines[-1] -notmatch $scalePattern) { throw 'Ready mass calibration metadata missing.' }
$zeroOffset = [long]$Matches[1]
$gramsPerCount = [double]::Parse($Matches[2], [Globalization.CultureInfo]::InvariantCulture)
$tareSamples = [int]$Matches[3]
$identity = @(Get-Content -LiteralPath $transcriptPath | Where-Object { $_.StartsWith('# FIRMWARE=') } | ForEach-Object { $_.ToString() } | Select-Object -Unique)
$wiring = @(Get-Content -LiteralPath $transcriptPath | Where-Object { $_.StartsWith('# HX711 channel=') } | ForEach-Object { $_.ToString() } | Select-Object -Unique)
$measured = @(Get-Content -LiteralPath $transcriptPath | ForEach-Object {
    if ($_ -match '^# ADC timestamp_ms=(\d+) raw_counts=(-?\d+)$') {
        [PSCustomObject]@{ timestamp = [long]$Matches[1]; count = [long]$Matches[2] }
    }
})
if ($measured.Count -lt 5) { throw 'Insufficient measured HX711 conversions in the serial transcript.' }
$statistics = $measured | ForEach-Object { $_.count } | Measure-Object -Minimum -Maximum
$massStatistics = $measured | ForEach-Object { ($_.count - $zeroOffset) * $gramsPerCount } | Measure-Object -Minimum -Maximum -Average
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
    units = 'm/s (plate-drag estimate; airspeed accuracy UNVALIDATED)'
    aerodynamic_model = 'v=speed_gain*sqrt(2*max(directed_force_N,0)/(rho*Cd*A)); provisional rough empirical gain'
    aerodynamic_assumptions_reported_by_board = $windModel
    airspeed_accuracy_validated = $false
    startup_tare_counts = $zeroOffset
    startup_tare_adc_samples = $tareSamples
    grams_per_count = $gramsPerCount
    mass_calibrated_range_g = @(0,35)
    measured_equivalent_mass_min_g = $massStatistics.Minimum
    measured_equivalent_mass_max_g = $massStatistics.Maximum
    measured_equivalent_mass_mean_g = $massStatistics.Average
    wind_speed_min_mps = $speedStatistics.Minimum
    wind_speed_max_mps = $speedStatistics.Maximum
    wind_speed_mean_mps = $speedStatistics.Average
    zero_clipped_output_samples = @($rows | Where-Object { $_.status -eq 'ESTIMATED_AIRSPEED_ZERO_CLIPPED' }).Count
    outputs_extrapolated_beyond_mass_calibration = @($rows | Where-Object { $_.status -eq 'ESTIMATED_AIRSPEED_EXTRAPOLATED' }).Count
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
    saturated_samples = @($measured | Where-Object { $_.count -le -8388608 -or $_.count -ge 8388607 }).Count
    csv_file = [IO.Path]::GetFileName($csvPath)
    serial_transcript = [IO.Path]::GetFileName($transcriptPath)
    scope = 'Estimated-m/s output, declared aerodynamic assumptions, startup tare, communication and publication rate. No reference airspeed comparison; this PASS does not establish airspeed accuracy.'
}
$report | ConvertTo-Json | Set-Content -LiteralPath $reportPath -Encoding ascii
Write-Host "Saved verification report: $reportPath"
Write-Output ([PSCustomObject]$report)
