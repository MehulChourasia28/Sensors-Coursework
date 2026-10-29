# NUCLEO-F411RE + HX711

HX711 acquisition with a 200 Hz linearly interpolated serial output for the
STM32 Nucleo F411RE. Firmware is built with ARM GCC and uploaded through
the onboard ST-LINK USB mass-storage interface. No Arduino libraries, Python
packages, PlatformIO, or STM32 HAL installation are needed.

## Wiring

| HX711 connection | Nucleo connection | STM32 pin |
| --- | --- | --- |
| VCC | 3V3 | 3.3 V supply |
| GND | GND | Common ground |
| DT / DOUT | D7 | PA8, input |
| SCK / PD_SCK | D8 | PA9, output |

These data and clock assignments were verified on the connected hardware on
2026-10-29. Keep the module on 3.3 V for this setup. For modules with separate
analog and digital supplies, the digital supply must match the MCU's 3.3 V logic.
Unplug USB before changing wires. Connect USB to the Nucleo's ST-LINK socket.

The load cell was connected for a second test on 2026-10-29. Communication passed,
and the capture contains a substantial count increase followed by a return near
the initial value. The system still needs tare and calibration before reporting
weight. Firmware cannot determine whether a bridge is physically attached.
With no bridge connected, floating inputs can drift, pick up noise, or saturate;
do not tare or calibrate those readings.

## Live readings

Open PowerShell in this directory and run:

```powershell
.\stream.ps1
```

The script automatically chooses the serial port when exactly one is present.
The connected board was verified on COM5. With multiple ports, use
`.\stream.ps1 -Port COM5`. Close any other serial monitor first. Ctrl+C stops
monitoring and releases the port.

For a serial monitor that keeps waiting when the board is silent (for example,
while pressing RESET), use `.\monitor.ps1 -Port COM5`. It displays the received
text as-is and does not declare a verification pass. Close an existing serial
monitor before running a different script against COM5.

Serial settings: **115200 baud, 8 data bits, no parity, 1 stop bit**.

Firmware v5 sends one signed decimal per line at a nominal 200 Hz. It zeroes
and normalizes counts linearly interpolated between completed ADC conversions.
For example (plain numeric format):

```text
-0.000120
0.000080
-0.000030
```

Diagnostics start with `#`; Wind Tunnel Logger v1.0.0 and v1.0.1 ignore these. `# ADC`
lines report actual measured samples, and `# INTERPOLATION` identifies the
200 Hz output and its 150 ms delay. There is
no CSV header, timestamp, status text or unit suffix on a data line. LD2 blinks
while conversions are received and is turned off on a communication fault.

When `stream.ps1 -OutputFile` records a separate diagnostic CSV, its columns
are `elapsed_ms,normalized_signal,raw_counts,status`. `elapsed_ms` is PC elapsed receipt time
since monitoring began, not MCU uptime. The script derives `SATURATED` at
either signed 24-bit limit and otherwise uses `ZEROED_NORMALIZED_INTERPOLATED`.
`raw_counts` is recovered exactly using the firmware's offset and scale.

## Initial zero and provisional display

The sensor was resting in still air for the zero measurement on 2026-10-29.
Its 153 actual ADC measurements averaged 491063.725 counts with a standard
deviation of 36.877 counts. Firmware uses the rounded offset **491064 counts**:

```text
displayed_signal = (491064 - interpolated_raw_counts) / 100000
interpolated_raw_counts = 491064 - displayed_signal * 100000
```

This reversible display normalization makes resting output close to 0.000
instead of clipping the logger's display at 100. **It is not a calibrated
airspeed in m/s.** A nonzero tunnel reference is still needed to establish the
airspeed scale. Small signed readings around zero represent noise and drift.
Firmware v5 reverses the sign and applies **10 times the v4 display gain**;
for the same raw sample, the new output is exactly `-10 * old_output`.
The v5 hardware check passed at **200.295 published values/s**, with zero
communication faults or saturation. All 2,902 saved values passed the exact
inverse transform and sign/gain check. Evidence is under
`verification/hx711-20261029-120422-387*`.
The zero offset remains 491064 counts. Polarity is selected by
`DISPLAY_POLARITY` and gain by `DISPLAY_COUNTS_PER_UNIT` in `firmware/config.h`.
Six decimal places retain one-count precision. The offset is fixed across
resets; startup does not automatically zero a sensor that might be in airflow.
The measurement record is `calibration/hx711-20261029-114836-613-zero.json`;
the offset and normalization divisor are in `firmware/config.h` and are also
transmitted as `# SCALE` metadata.

The historical v4 check after programming measured a resting display mean of **0.004000**
(range **0.003877-0.004118**) at **200.404 published values/s**. The v1.0.0
logger parser accepted the decimal output, and all 2,869 recorded CSV values
reconstructed their interpolated raw counts exactly. Evidence is under
`verification/hx711-20261029-115401-541*`.

## Collect calibration data in Wind Tunnel Logger

The installed application is `Downloads/WindTunnelLogger.exe`. Use it to
record both sources together; no separate acquisition program is needed.

Compatibility with **Wind Tunnel Logger v1.0.1** was checked against the exact
release source. Its student serial parser, serial reader, CSV columns and
student logging methods are unchanged from v1.0.0. All **3,007 captured v4
decimal values** were accepted with zero rejected lines; signed near-zero
values and diagnostic comments also passed. The downloaded EXE matches the
official v1.0.1 SHA-256 checksum. No STM32 firmware changes or reflash are
needed for this logger update. The release improves white-tunnel pressure
cell selection. Evidence is in
`verification/wind-tunnel-logger-v1.0.1-compatibility.json`.

1. Close PowerShell serial monitors so they release the Nucleo COM port.
2. Connect the tunnel reference, select **Wind tunnel** (white tunnel) or
   **GIGA pitot tube** (wood tunnel), keep the fan off and wait for zeroing.
3. Enter the team, air pressure and temperature. Under **Your board**, select
   the Nucleo port (COM5 during verification), then **Connect**.
4. Click **Start Logging**, collect reference speeds alongside the raw sensor
   counts, and use **Stop & Save** to save the run.

During this calibration stage, the app's sensor label still says m/s, but the
sensor data are the **zeroed normalized signal**. In the saved `_student.csv`,
`value_raw_mps` contains this signal, and in `_aligned.csv`, `student_mps`
contains the paired normalized signal. The formula above recovers the
interpolated raw counts exactly. The real tunnel speed is `ground_truth_mps` in the
aligned file. The app's live plot/readout and `value_clipped_mps` clip large
counts to the display range; do not use those clipped values for calibration.
The raw and aligned columns preserve the normalized signal. Use actual tunnel data for
the fit; simulated ground truth is only for checking software connections.

The HX711 currently produces approximately 10 new measurements/s. Firmware
resamples them to a nominal 200 published values/s using linear interpolation
with a fixed **150 ms delay**. This delay ensures that both endpoints have
already been measured. Acquisition is polled without waiting for the next
conversion, and buffered UART transmission keeps diagnostics from blocking
the 5 ms output schedule. Interpolation increases the publication rate;
the sensor's measurement bandwidth and actual conversion rate are unchanged.
Output pauses if measured endpoints no longer bracket the requested time.

For calibration, use steady speed intervals. When analysing steps or ramps,
account for the 150 ms signal delay in addition to ADC and USB latency. After
collecting the data, fit the calibration and then change the transmitted
quantity to actual airspeed in m/s. The rate display should now be close to
200 Hz; `verify.ps1` checks publication and measured ADC rates separately.

The v3 hardware check captured **3,014 interpolated values in 15 seconds** at
**201.372 published values/s**, with **152 measured ADC conversions** at
**10.152 conversions/s**, no communication faults and no saturation. All 3,014
values were preserved by the v1.0.0 logger parser. One incomplete line at the
instant the PC attached mid-transmission was excluded from that parser replay.
Evidence is under `verification/hx711-20261029-113507-659*`.

The v2 serial check on 2026-10-29 captured 154 samples in 15 seconds, with no
communication errors or saturation. All 154 values were then accepted
unchanged by `StudentSerial._handle` from the exact v1.0.0 release source;
no lines were rejected. Evidence is in `verification/` under the prefix
`hx711-20261029-111259-227`.

To record CSV for 60 seconds:

```powershell
.\stream.ps1 -Seconds 60 -OutputFile measurements.csv
```

Omit `-Seconds` to record until Ctrl+C. Optional `-TranscriptFile serial.txt`
saves firmware diagnostics alongside samples. Existing log files are never
overwritten. Diagnostics go to the console and transcript; CSV contains only
the header and validated samples.

## Verify the amplifier connection

The initial 2026-10-29 hardware check, with no load cell, passed: **307 samples in 30 seconds**, approximately
**10.179 samples/s**, with no communication errors and no saturated samples.
The actual CSV, serial transcript and report are in `verification/` with the
prefix `hx711-20261029-103642-703`.

With the load cell connected, the later 30-second check passed with **306
samples at 10.152 samples/s**, no communication errors and no saturation. Raw
counts ranged from **489444 to 1071349** and returned near **491000**. Its capture
prefix is `hx711-20261029-105322-512`. These are uncalibrated counts, not grams
or kilograms. Serial output resumed after a board restart during this test.

```powershell
.\verify.ps1 -Seconds 15
```

This records CSV, a serial transcript, and a JSON report under `verification/`.
A pass requires the running HX711 firmware and interpolation metadata,
196-204 published values/s, measured ADC diagnostic samples, advancing PC
receipt times, and no reported communication faults. A timeout or
stuck-low diagnostic fails the check, even if earlier samples were received.
Logs remain available if verification fails; a PASS report is only written
when the entire capture succeeds.

The check establishes HX711-compatible digital communication. The HX711 has
no readable chip identification register. Without a load cell, this cannot
verify bridge wiring, accuracy, mechanical response, or calibration. Constant
or saturated readings can still accompany a functioning digital link.

## Build and upload

```powershell
.\run.ps1
.\run.ps1 -Flash -Monitor -MonitorSeconds 10
```

The first command builds only; the second replaces the current MCU program and
verifies the running firmware. Outputs are `build/hx711.bin`, `build/hx711.elf`,
and `build/hx711.map`. The script detects a single MBED board drive. The board
was D: during setup; with multiple boards use `-BoardDrive 'D:\'` explicitly.

ARM GCC is discovered on PATH or under the existing Arduino tools installation.
Override with `-Compiler 'C:\path\to\arm-none-eabi-gcc.exe'` if needed.
If PowerShell execution policy prevents running scripts, use:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\run.ps1 -Flash -Monitor
```

Uploading uses MBED mass storage and checks for `FAIL.TXT`. Copy success alone
is not verification; the serial monitor must identify the new firmware and
receive valid HX711 conversions. The ST-LINK debug driver is not needed for
this upload route. OpenOCD could not access the debug interface during setup.

## Implementation

- `firmware/config.h`: DT/SCK assignment and conversion timeout.
- `firmware/hx711.c`: HX711 reset, timing, signed samples, and link checks.
- `firmware/board.c`: 16 MHz internal clock timing, UART, SysTick, and LD2.
- `firmware/main.c`: startup, acquisition, periodic firmware identity, diagnostics.
- `firmware/interpolation.c`: bounded interpolation between measured samples,
  including timer wraparound handling; `tests/test_interpolation.c` exercises
  ramps, signed 24-bit extremes, bounds, gaps, and history rotation.
- `firmware/stm32f411re.ld`: 512 KiB flash and 128 KiB RAM layout.
- `run.ps1`, `stream.ps1`, `verify.ps1`: build/upload, live logging, verification.
- `monitor.ps1`: continuous serial display that waits through periods of silence.
- `verification/`: actual captures and reports from the connected hardware.

Acquisition uses channel A at gain 128. Each read waits for DT low, sends 24
clock pulses for the sample plus one to select the next conversion, and checks
that DT returns high. The DT input has a pull-up so a disconnected wire times
out. Ordinary clock highs are approximately 1 microsecond; interrupts are masked
during the short read to prevent accidental HX711 power-down. Startup and fault
recovery deliberately reset the amplifier with a 100 microsecond clock high.
A one-second ready timeout allows startup settling at either hardware RATE
setting. RATE is selected on the module, not by this firmware.

## Troubleshooting

- `HX711_READY_TIMEOUT`: check VCC=3V3, common GND, DT=D7, SCK=D8, and module power.
- `HX711_DOUT_STUCK_LOW`: check for a shorted DT line, missing clock wire, or
  swapped DT/SCK. The firmware does not emit a sample for this failed read.
- Old `L,...` records or a different identity: upload `build/hx711.bin` using
  `run.ps1 -Flash`, then verify again.
- COM port access denied: close Arduino Serial Monitor or another application
  holding the port. If disconnected, reconnect and check its new port number.
- Drift/noise/saturation with no bridge: expected possibilities for open analog
  inputs; evaluate weight only after connecting and calibrating a load cell.

## References

- [HX711 manufacturer datasheet, hosted by SparkFun](https://cdn.sparkfun.com/assets/0/2/7/d/1/HX711_datasheet.pdf)
- [ST Nucleo-64 board manual (UM1724)](https://www.st.com/resource/en/user_manual/um1724-stlinkv21-in-circuit-debuggerprogrammer-for-stm8-and-stm32-stmicroelectronics.pdf)
- [STM32F411RE datasheet](https://www.st.com/resource/en/datasheet/stm32f411re.pdf)
- [Wind Tunnel Logger v1.0.0 source](https://github.com/Dkaka/COMP0219_Tutorials/blob/wind-tunnel-logger-v1.0.0/src/wind_tunnel_logger/app.py)
- [Wind Tunnel Logger v1.0.1 release](https://github.com/Dkaka/COMP0219_Tutorials/releases/tag/wind-tunnel-logger-v1.0.1)
