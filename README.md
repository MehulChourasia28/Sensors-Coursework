# NUCLEO-F411RE + HX711

Bare-metal HX711 acquisition for the STM32 Nucleo F411RE. **Firmware v9 outputs
estimated wind speed in m/s by default**, with a provisional **0.55 speed multiplier**
guided by the short intact-looking section of the 2026-11-04 tunnel run. The
serial publication target is 200 lines/s. Startup
lets the sensor settle, then automatically tares it using 32 actual ADC conversions.

## Wiring

| HX711 | Nucleo | STM32 |
| --- | --- | --- |
| VCC | 3V3 | 3.3 V supply |
| GND | GND | Common ground |
| DT / DOUT | D7 | PA8 |
| SCK / PD_SCK | D8 | PA9 |

Use the Nucleo's ST-LINK USB socket. COM5 is its verified serial port and D: is
its ST-LINK mass-storage programming drive. Unplug USB before changing wiring.

## Start and monitor

Mount in the final measurement orientation with the fan OFF. Remove all masses
before powering up or pressing RESET. Keep the sensor unloaded
for approximately 8 seconds: firmware v9 discards 50 real ADC conversions
(about 5 seconds) to let the sensor settle, then averages 32 conversions to
establish the startup zero in its current position. Zero wind is assumed at
every power-on or RESET. The fitted mass coefficient, aerodynamic conversion
and 0.55 speed multiplier are preserved by zeroing. Numeric output starts after tare and
interpolation history are ready.
The zero remains fixed until the next board restart; placing a mass does not
cause it to re-zero. After a communication interruption, an established tare is
preserved so a held mass cannot accidentally become zero. Saturation during tare
restarts settling and the 32-sample average; saturation after tare pauses output.

```powershell
.\monitor.ps1 -Port COM5
```

COM5 uses **115200 baud, 8 data bits, no parity, 1 stop bit**. Only one app can
open it at a time. Ctrl+C releases it. The terminal disables classic-console
selection pause and clears stale buffered input when it attaches.

The firmware publishes one nonnegative number per line, in **estimated m/s**, with six decimal
places. Decimal places describe the serial format, not physical precision:

```text
0.000000
0.270434
3.824513
```

Comment lines begin with `#` and identify units, firmware, measured raw ADC counts,
interpolation, and the live tare. For example:

```text
# UNITS=m/s; METHOD=plate_drag_estimate; UNVALIDATED_AIRSPEED
# MASS_CALIBRATION tare_counts=444619 grams_per_count=-0.00089903368772 tare_samples=32 ready=1
# ADC timestamp_ms=10000 raw_counts=422274
```

## Mass calibration

Calibration uses the user's sequence **0 g, 20 g, 30 g total, 35 g total** on
2026-11-04. The fit includes actual ADC conversions from
`calibration/mass-sequence-20261104-152148.serial.txt`. Earlier restarted attempts,
transitions, and everything after the 35 g plateau are excluded. Times are
seconds relative to that capture's first ADC record.

| Applied mass | Steady interval | Actual ADC samples | Fitted mass |
| --- | --- | --- | --- |
| 0 g | 5-12 s | 71 | 0.00 g |
| 20 g | 15.5-18.7 s | 32 | 20.09 g |
| 30 g | 20-24 s | 41 | 30.01 g |
| 35 g | 26-34 s | 82 | 34.94 g |

```text
mass_g = (raw_counts - startup_tare_counts) * -0.0008990336877246245
```

The slope is **-1112.305371 counts/g**. The fit fixes the zero from the initial
unloaded mean and gives equal weight to each plateau mean. The maximum residual
is 0.089 g on the fitted data; this is not an independent accuracy measurement.
Data cover 0-35 g in the tested mounting/loading geometry. Keep that geometry
unchanged. Signed noise is retained rather than clamped to zero.

The report, including noise and residuals, is
`calibration/mass-fit-20261104.json`. The coefficient is generated in
`firmware/mass_calibration_coefficients.h`. `firmware/main.c` calls the conversion
in `firmware/mass_calibration.c`, then converts force to estimated airspeed for
every published value. The mass API also supports
an explicit coefficient for reuse:

```c
mass_calibration scale;
mass_calibration_start(&scale); /* Once at startup, with no mass applied. */
/* Feed successful actual ADC conversions, not repeated/interpolated samples. */
mass_calibration_tare(&scale, raw_counts);
float grams;
if (mass_calibration_grams(&scale, raw_counts, &grams)) {
    /* Available after 50 settling + 32 unloaded conversions. */
}
```

Reproduce the fit using a new report path (existing reports are protected):

```powershell
python tools/calibrate_mass.py calibration/mass-sequence-20261104-152148.serial.txt --plateau 0:5:12 --plateau 20:15.5:18.7 --plateau 30:20:24 --plateau 35:26:34 --output calibration/mass-fit-repeat
```

## Current scaling: 2026-11-05

At the user's request, the speed gain is restored to **0.55** after briefly
trying 0.60. The current conversion is approximately
`speed_mps = 0.47035291 * sqrt(max(equivalent_mass_g, 0))`.
Startup zeroing and the mass coefficient are unchanged. The earlier 0.60
verification is preserved in `verification/hx711-20261105-095343-312*` and
does not describe the current gain.
The restored 0.55 firmware was uploaded and verified on COM5 at **199.494 Hz**.
The board reported `speed_gain=0.550000` and a fresh 32-sample startup zero;
this capture ranged 0-0.3554 m/s (mean 0.2311 m/s).
Evidence: `verification/hx711-20261105-095809-833*`.

## Startup-zero v9 verification before gain adjustment: 2026-11-05

Firmware v9 was uploaded and restarted with the fan off. COM5 verification
captured 3932 outputs at **200.719 Hz**; mean estimated speed was **0.0046 m/s**,
with a range of **0-0.4531 m/s** including a brief excursion. The fresh startup
zero was 495816 counts, and mean signed force-equivalent mass was -0.0756 g.
The board reported all 50 settling conversions complete, then a 32-sample tare.
The mass coefficient, physical model and 0.55 correction match v8. Host tests
verify that settling samples are excluded, a restart replaces the zero while
preserving the scale, and sustained loads do not become zero.

Evidence: `verification/hx711-20261105-093021-212*`. This checks startup zero,
conversion and serial rate; it does not validate airspeed accuracy or eliminate
later mechanical/thermal drift. An earlier check in the same session displayed
zero while its signed mass baseline had shifted by -2.72 g; that clipped result
was not accepted as evidence of a correctly centred zero.

## Previous v8 board verification: 2026-11-05

Firmware v8 with the provisional 0.55 speed multiplier was uploaded and verified
on COM5 after correcting the wiring and restarting unloaded. The clean 15-second
check saved 2714 outputs at **200.911 Hz**, with 153 actual ADC conversions at
**10.163 Hz**, and no communication errors or saturation. The resting estimate
ranged **0.147-0.307 m/s**, mean **0.243 m/s**: residual zero drift remains, and
these near-zero values must not be treated as validated airflow. The live
terminal uses the running board's unit/calibration metadata.

Evidence is `verification/hx711-20261105-091658-873*`. The deployed nonlinear
output also passed the inverse-ADC-grid consistency check in
`verification/wind-model-v8-implementation-check.json`. This verifies software,
configuration and communication; independent airspeed accuracy remains unvalidated.

## Rough guidance from the first tunnel run

The run `8_20261104_171022` is partially usable as a rough guide. It is not a
validated wind calibration. The mount moved/fell and the exact first moment of
instability is uncertain. We conservatively exclude data from **22.6 s onward**,
including the later apparent agreement near 8 m/s and the failed final zero.
At reference zero, the failed setup continued reporting about **3.877 m/s**.
These values vary slightly, so the trace is not an exactly frozen serial value.

Only the **17-22 s** intact-looking interval is used for a one-parameter guide:
20 paired reference samples over about five seconds, reference speeds
1.82-5.31 m/s, largely during a ramp. A through-zero speed fit gives 0.548;
a force-space fit gives 0.558. The original selected gain was **0.55**, with no
fitted offset, polynomial or optimized time shift:

```text
v8_estimated_mps = 0.55 * v7_plate_drag_estimate_mps
                ~= 0.47035291 * sqrt(max(equivalent_mass_g, 0))
```

This factor is separate from the assumed physical Cd; do not claim it measures
the plate's drag coefficient. The corrected in-sample RMSE is 0.187 m/s instead
of 2.664 m/s on this selected interval. That improvement is a fit diagnostic,
not an accuracy guarantee. Splitting the same short run into two chronological
halves gives gains about 0.532 and 0.552, but no independent run validates them.
Timing shifts of +/-150 ms also change the inferred gain. A repaired or remounted
assembly may need a different gain; secure it and re-zero before further testing.

The figure, annotated/excluded pairs, input hashes, interval and timing sensitivity
are in `calibration/ground-truth-20261104/`. Reproduce the analysis with:

```powershell
python tools/analyze_tunnel_run.py 8_20261104_171022
```

The three input CSVs are preserved. This analysis uses the already-installed
NumPy, pandas and Matplotlib. The firmware and normal serial scripts do not
require those packages. The firmware's `# WIND_MODEL speed_gain` and rough-guide
metadata expose the actual multiplier. A board upload is a separate hardware
operation; inspect its reported firmware version to establish what is running.

## Physics-based wind estimate

With the broad 15 x 11.6 cm face normal to airflow, the frontal area is
**0.0174 m2** (174 cm2). Under an approximately uniform pressure distribution,
the resultant acts through the plate centre. Central calibration weights are
therefore a useful force reference when wind and weights load the **same axis**
of the load cell, as the user confirmed. Rotating the assembly changes gravity
loading, so tare after mounting in its final orientation, with the fan off.

The underlying aerodynamic model (before the rough 0.55 speed multiplier) is:

```text
F_N = equivalent_mass_g * 0.001 * 9.80665
rho = p_Pa / (287.05 * (temperature_C + 273.15))
v_mps = sqrt(2 * max(F_N, 0) / (rho * Cd * A))
```

This is the [NASA drag equation](https://www1.grc.nasa.gov/beginners-guide-to-aeronautics/drag-equation/)
rearranged for speed. For an initial estimate we assume **Cd = 1.28**, NASA's
[generic flat-plate value](https://www1.grc.nasa.gov/beginners-guide-to-aeronautics/shape-effects-on-drag/),
and dry air at **20 C and 101325 Pa**, giving rho = **1.204118316 kg/m3** using
[the ideal-gas relation](https://www1.grc.nasa.gov/beginners-guide-to-aeronautics/equation-of-state/).
This yields:

```text
uncorrected_model_mps = 0.855187108 * sqrt(max(equivalent_mass_g, 0))
v8_estimated_mps = 0.55 * uncorrected_model_mps
```

| Equivalent load | Force | Uncorrected model | v8 rough guide |
| --- | --- | --- | --- |
| 1 g | 0.009807 N | 0.855 m/s | 0.470 m/s |
| 20 g | 0.196133 N | 3.825 m/s | 2.103 m/s |
| 30 g | 0.294200 N | 4.684 m/s | 2.576 m/s |
| 35 g | 0.343233 N | 5.059 m/s | 2.783 m/s |

Negative force is clipped to zero for the configured wind direction, **not
turned positive with abs()**. No arbitrary zero deadband is used. Sensor noise
can therefore produce positive apparent speeds even with the fan off; clipping
and the square root introduce a positive bias near zero. Six output decimals
are not an airspeed-accuracy claim. The historical unloaded scatter of 0.143 g
has a force-equivalent speed scale of about 0.324 m/s before the rough multiplier
(0.178 m/s after it); this is
not a symmetric speed uncertainty or a validated detection limit.

Model parameters are in `firmware/wind_model_config.h`; the model and assumptions
are recorded in `calibration/wind-model-20261104.json`. They are transmitted as
`# WIND_MODEL` diagnostics. Updating pressure/temperature in the logger does not
update MCU constants: rebuild and upload when changing firmware parameters.

### Accuracy limits and reference calibration

**The aerodynamic speed estimate is not validated against real wind.** Cd = 1.28
is not a measurement of this rectangular wooden plate, its support, or this
tunnel. Plate thickness/aspect ratio, mount drag, pressure distribution, wall
blockage, alignment, humidity, and approximately central placement of the
calibration weights can cause systematic errors. Full containment within the
tunnel does not remove blockage; its fraction is `plate_area / tunnel_area`,
and the tunnel cross-section is still unknown. A single generic blockage
correction would not be justified without the tunnel geometry and reference.

The 0-35 g mass calibration corresponds to forces up to 0.343 N and an estimated
5.059 m/s before the rough speed multiplier, or 2.783 m/s with it. Higher force outputs extrapolate the mass
scale. Extend force checks within the cell's rated load range if the intended
wind speeds produce larger loads. No percentage speed accuracy can currently
be established. Away from zero, first-order independent uncertainty would be:

```text
sigma_v / v ~= 0.5 * sqrt((sigma_F/F)^2 + (sigma_rho/rho)^2
                         + (sigma_Cd/Cd)^2 + (sigma_A/A)^2)
```

For example, a 10% Cd uncertainty alone means roughly 5% speed uncertainty;
this example does not assert that our unknown Cd error is bounded by 10%.
Force-zero drift dominates near zero, where this linear uncertainty expression
is poorly conditioned.

For empirical refinement, use the existing logger to record several steady
reference speeds across the intended range, with repeat upward/downward runs.
At each interval, infer `Cd_effective = 2*F/(rho*A*v_reference^2)` or fit force
against reference speed squared. Check residuals on separate held-out runs;
retain the final mounting, fan-off zero and air conditions. A speed-dependent
effective coefficient may be needed if one coefficient does not fit the data.
The complete mounted sensor should be calibrated against the same reference
quantity the logger reports; a nonuniform flow gives an area-weighted response.

The deployed v7 verification captured **2774 values at 201.064 Hz**, with actual
ADC rate **10.149 Hz**, no errors/saturation, and all declared model parameters.
Evidence is `verification/hx711-20261104-154326-585*`. Fan-off apparent speed ranged
from 0 to 1.374 m/s in this capture, with mean 0.267 m/s; the raw trace includes
large excursions, so this is not a still-air precision test. It demonstrates
why an unvalidated low-speed readout must not be treated as measured wind.

Host tests compare 150001 loads against an independent double-precision formula
and check area/density/Cd scaling, direction and invalid inputs. Exact v1.0.1
logger parsing accepted **3006 numeric hardware lines**, rejecting zero lines.
Inverse-model checks on 1445 positive saved outputs agree with the firmware's
integer-interpolated ADC grid to within 0.00233 count. These checks establish
implementation/protocol consistency, not aerodynamic accuracy. Details are in
`verification/wind-model-v7-implementation-check.json`.

## Output rate and verification

The HX711 provides approximately **10 actual conversions/s**. Firmware linearly
interpolates raw counts between completed conversions, with a fixed **150 ms
delay**, then converts each interpolated sample to grams, newtons and estimated m/s at nominal 200 Hz.
Interpolation increases the publication rate, not the measurement bandwidth.
The output pauses when measured endpoints no longer bracket the requested time.
ADC acquisition and buffered USART2 output are polled without blocking a
conversion wait. LD2 blinks while streaming.

```powershell
.\verify.ps1 -Port COM5 -Seconds 15
.\stream.ps1 -Port COM5 -Seconds 60 -OutputFile measurements.csv -TranscriptFile serial.txt
```

`verify.ps1` records under `verification/` and requires firmware v9, estimated-m/s
units, ready tare and wind-model metadata, 196-204 published values/s, advancing ADC timestamps and no
communication errors. Initial startup allows extra time for tare.

Diagnostic CSV columns are
`elapsed_ms,wind_speed_mps,equivalent_mass_g_estimate,raw_counts_estimate,status`.
The last two numeric fields reverse the model for positive speeds. At zero speed
they are blank because clipping discards the sign/magnitude of a negative force.
`# ADC` transcript lines preserve exact actual counts at every measured conversion.
Status flags distinguish interpolated estimates, zero clipping and extrapolation
beyond the measured 0-35 g mass range. `elapsed_ms` is PC monotonic receipt time.
Existing captures are never overwritten.

The historical v6 gram-output check passed at **200.866 published values/s** and
**10.160 actual ADC conversions/s**, with 2,799 saved mass values and no
communication errors or saturation. The startup tare was 445571 counts from
32 samples. Evidence is under `verification/hx711-20261104-152933-829*`. The
capture includes movement/noise and is a communication/conversion check, not an
independent physical accuracy or still-zero test.

## Wind Tunnel Logger

The numeric-line serial protocol remains compatible with Wind Tunnel Logger
v1.0.0 and v1.0.1: signed decimal numbers at 115200 baud, with `#` comments ignored.
Close the terminal before connecting COM5 in the app.

**The values are now model-estimated m/s.** In `_student.csv`, `value_raw_mps`
contains this unclipped speed estimate; in `_aligned.csv`, `student_mps` is the
paired estimate and `ground_truth_mps` is the actual tunnel reference. The app's
plot and `value_clipped_mps` apply its display limits. For positive speeds the
model can recover equivalent force from those saved values. At a zero-clipped
speed, the original negative/zero force cannot be recovered from speed alone.
The logger ignores `# ADC` diagnostics; our separate verification transcript
retains them, but ordinary logger CSV files do not.

A wind-tunnel reference is still needed to validate and refine this estimate. For steady data,
record the reference and calibrated sensor signal together in the logger. For
transient comparisons, account for the 150 ms interpolation delay plus ADC and
USB latency. The v1.0.1 release changes tunnel pressure-cell selection; its
student parser and saved column meanings were verified from the release source.
Historical compatibility checks and raw captures remain in `verification/`.

## Build, upload and tests

```powershell
.\run.ps1
.\run.ps1 -Flash
.\tests\run-mass-tests.cmd
.\tests\run-wind-tests.cmd
python -m unittest discover -s tests -p test_mass_fit.py -v
```

ARM GCC is discovered on PATH or under the existing Arduino tools installation.
The firmware uses registers directly, with no Arduino libraries, STM32 HAL,
PlatformIO or Python packages. The build links compiler arithmetic support and the standard math library for
force conversion and `sqrtf`. The rough speed multiplier is applied once, after
the square root. CSV inverse-force estimates divide it out before reversing the
drag model. Outputs are `build/hx711.bin`, `.elf` and `.map`.

`-Flash` copies the image to the single detected MBED drive and checks for
`FAIL.TXT`. With multiple boards use `-BoardDrive 'D:\'`. Copy completion alone
is not verification: verify the reported firmware identity and live readings.
If uploading leaves the board silent, press black RESET with no masses applied.
A USB unplug/reconnect was needed for an earlier ST-LINK serial-path stall.

Mass tests cover startup settling, fresh zero after restart with the scale
preserved, both coefficient polarities, signed noise,
saturation, held loads, restarted tare, the fitted coefficient and fit interval
selection. Interpolation tests remain in `tests/test_interpolation.c`.

## Historical captures

Firmware v6 published calibrated equivalent grams; v7 supersedes that default
with a declared drag-model speed estimate. Firmware v5 and earlier published
provisional normalized counts rather than
mass. For v5 the reversible transform was
`signal = (491064 - raw_counts) / 100000`. Its fixed zero, display gain and sign
are historical and are superseded by the startup tare and gram conversion
retained in v7.
The old transformation record is `calibration/display-transform-v5.json`;
initial zero measurements and all earlier verification logs remain available.
