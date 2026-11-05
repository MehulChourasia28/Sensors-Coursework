"""Fit an HX711 mass scale from steady intervals of actual # ADC samples.

Times are seconds relative to the first ADC sample in the transcript.
Example: python tools/calibrate_mass.py capture.txt --plateau 0:2:12
    --plateau 20:22:32 --plateau 30:42:52 --plateau 35:62:72
    --output calibration/mass-fit
"""

import argparse
import json
import math
import re
import statistics
from pathlib import Path


def read_adc(path, end_s=None):
    samples = []
    previous = None
    elapsed = 0
    for line in Path(path).read_text(encoding="utf-8-sig").splitlines():
        match = re.fullmatch(r"# ADC timestamp_ms=(\d+) raw_counts=(-?\d+)", line)
        if not match:
            continue
        timestamp, raw = map(int, match.groups())
        if previous is not None:
            interval = (timestamp - previous) % (2**32)
            if not 0 < interval <= 1000:
                raise ValueError("ADC timestamps reset, repeat, or have a >1 s gap")
            elapsed += interval
        previous = timestamp
        if end_s is not None and elapsed / 1000 >= end_s:
            break
        if not -8388608 < raw < 8388607:
            raise ValueError("Saturated/out-of-range ADC reading in capture")
        samples.append((elapsed / 1000, raw))
    if not samples:
        raise ValueError("No actual ADC diagnostic samples found")
    return samples


def fit_mass(samples, windows):
    plateaus = []
    last_end = -1
    for mass, start, end in windows:
        if not all(math.isfinite(x) for x in (mass, start, end)):
            raise ValueError("Window values must be finite")
        if mass < 0 or start < 0 or end <= start or start < last_end:
            raise ValueError("Use nonoverlapping, chronological windows and nonnegative masses")
        values = [raw for t, raw in samples if start <= t < end]
        if len(values) < 30 or end - start < 3:
            raise ValueError("Each plateau needs >=3 seconds and >=30 actual ADC samples")
        midpoint = len(values) // 2
        plateaus.append(dict(
            mass_g=mass, start_s=start, end_s=end, adc_samples=len(values),
            mean_counts=statistics.mean(values), stddev_counts=statistics.stdev(values),
            min_counts=min(values), max_counts=max(values),
            drift_counts=statistics.mean(values[midpoint:]) - statistics.mean(values[:midpoint]),
        ))
        last_end = end
    if len(plateaus) < 3 or plateaus[0]["mass_g"] != 0:
        raise ValueError("Need an initial zero and at least two distinct nonzero masses")
    masses = [p["mass_g"] for p in plateaus]
    if len(set(masses)) != len(masses):
        raise ValueError("Calibration masses must be distinct")
    zero = plateaus[0]["mean_counts"]
    # Equal weight per steady plateau, rather than weighting long recordings more.
    counts_per_gram = sum(p["mass_g"] * (p["mean_counts"] - zero) for p in plateaus) / sum(m*m for m in masses)
    if counts_per_gram == 0:
        raise ValueError("No measurable response to mass")
    for p in plateaus:
        p["fitted_mass_g"] = (p["mean_counts"] - zero) / counts_per_gram
        p["residual_g"] = p["fitted_mass_g"] - p["mass_g"]
        p["noise_stddev_g"] = p["stddev_counts"] / abs(counts_per_gram)
    return dict(
        units="grams", model="mass_g = (raw_counts - tare_counts) * grams_per_count",
        tare_counts_from_capture=zero, counts_per_gram=counts_per_gram,
        grams_per_count=1 / counts_per_gram,
        calibrated_range_g=[0, max(masses)], plateaus=plateaus,
        max_abs_residual_g=max(abs(p["residual_g"]) for p in plateaus),
        limitations="Fit residuals are not an independent accuracy test. Same loading geometry and startup-unloaded tare required. Not an airspeed calibration.",
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("transcript", type=Path)
    parser.add_argument("--plateau", action="append", required=True, help="mass_g:start_s:end_s")
    parser.add_argument("--output", type=Path, required=True, help="New report filename prefix")
    parser.add_argument("--header", type=Path, help="Optional new C coefficient header")
    args = parser.parse_args()
    windows = [tuple(map(float, value.split(":"))) for value in args.plateau]
    if any(len(w) != 3 for w in windows):
        parser.error("--plateau requires mass_g:start_s:end_s")
    result = fit_mass(read_adc(args.transcript, end_s=max(w[2] for w in windows)), windows)
    result["source_transcript"] = args.transcript.as_posix()
    report = Path(str(args.output) + ".json")
    with report.open("x", encoding="utf-8") as file:
        json.dump(result, file, indent=2)
        file.write("\n")
    if args.header:
        with args.header.open("x", encoding="ascii") as file:
            file.write("/* Generated from actual HX711 calibration plateaus. See " + report.as_posix() + ". */\n")
            file.write("#ifndef MASS_CALIBRATION_COEFFICIENTS_H\n#define MASS_CALIBRATION_COEFFICIENTS_H\n")
            file.write(f"#define MASS_GRAMS_PER_COUNT ({result['grams_per_count']:.10e}f)\n")
            file.write(f'#define MASS_GRAMS_PER_COUNT_TEXT "{result["grams_per_count"]:.14f}"\n')
            file.write("#endif\n")
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
