"""Review the 2026-11-04 tunnel run without changing firmware calibration.

Example: python tools/analyze_tunnel_run.py 8_20261104_171022
The conservative movement cutoff is based on the trace and user report;
the exact first instability time is uncertain. Times start at the first student row.
"""

import argparse
import hashlib
import json
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
import pandas as pd


def metrics(prediction, reference):
    error = prediction - reference
    return dict(n=int(len(error)), rmse_mps=float(np.sqrt(np.mean(error**2))),
                mae_mps=float(np.mean(np.abs(error))), bias_mps=float(np.mean(error)),
                max_abs_error_mps=float(np.max(np.abs(error))))


def fit_gain(sensor, reference, force_space=False):
    if force_space:
        x, y = sensor**2, reference**2
        return float(np.sqrt(np.dot(x, y) / np.dot(x, x)))
    return float(np.dot(sensor, reference) / np.dot(sensor, sensor))


def describe(values):
    return dict(n=int(len(values)), mean=float(np.mean(values)),
                median=float(np.median(values)), stddev=float(np.std(values, ddof=1)),
                minimum=float(np.min(values)), maximum=float(np.max(values)),
                distinct_values=int(len(np.unique(values))))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("base", type=Path)
    parser.add_argument("--output", type=Path, default=Path("calibration/ground-truth-20261104"))
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    paths = {name: Path(str(args.base) + f"_{name}.csv") for name in ("student", "vdas", "aligned")}
    s, r, a = (pd.read_csv(paths[name]) for name in ("student", "vdas", "aligned"))
    t0 = float(s.timestamp_epoch.iloc[0])
    for frame in (s, r, a):
        frame["elapsed_s"] = frame.timestamp_epoch - t0
        assert frame.timestamp_epoch.is_monotonic_increasing
    assert np.isfinite(s.value_raw_mps).all()
    assert np.isfinite(a[["ground_truth_mps", "student_mps"]]).all().all()
    st = s.elapsed_s.to_numpy()
    sv = s.value_raw_mps.to_numpy()
    at = a.elapsed_s.to_numpy()
    x = a.student_mps.to_numpy()
    y = a.ground_truth_mps.to_numpy()

    # Verify the logger's nearest-time alignment against the source student rows.
    # The release sorts (timestamp,value) tuples; serial batches can share the
    # same receipt timestamp, so retain its value tie-break rather than treating
    # duplicate timestamps as acquisition-time measurements.
    order = np.lexsort((sv, st))
    sorted_times = st[order]
    indices = np.searchsorted(sorted_times, at)
    nearest_sorted = np.array([min((max(0, i-1), min(i, len(st)-1)), key=lambda j: abs(sorted_times[j]-t))
                        for i, t in zip(indices, at)])
    nearest = order[nearest_sorted]
    assert np.allclose(x, sv[nearest], atol=1e-12)

    fall_s = 22.6  # Visible reversal and user-reported movement; onset uncertain.
    fit_start, fit_end = 17.0, 22.0  # Exclude onset and add guard before movement.
    keep = (at >= fit_start) & (at < fit_end) & (y > 0)
    gain = fit_gain(x[keep], y[keep])
    force_gain = fit_gain(x[keep], y[keep], force_space=True)
    preliminary = dict(
        status="EXPLORATORY_CANDIDATE_NOT_A_VALIDATED_CALIBRATION", equation="corrected_mps = gain * current_v7_mps",
        gain=gain, rounded_gain_selected_as_rough_guide=0.55,
        force_space_fit_gain=force_gain,
        reference_range_mps=[float(y[keep].min()), float(y[keep].max())],
        sensor_range_mps=[float(x[keep].min()), float(x[keep].max())],
        selected_interval_s=[fit_start, fit_end],
        selected_pairs=int(keep.sum()),
        uncorrected=metrics(x[keep], y[keep]),
        corrected_in_sample=metrics(gain*x[keep], y[keep]),
        rounded_gain_in_sample=metrics(0.55*x[keep], y[keep]),
        force_space_corrected_in_sample=metrics(force_gain*x[keep], y[keep]),
        interpretation="Describes this short pre-fall mount state. In-sample error is not accuracy; no independent run verifies transfer to a repaired/remounted setup.",
    )
    validation = []
    split = 19.5
    for label, train, test in (
        ("earlier half predicts later half", keep & (at < split), keep & (at >= split)),
        ("later half predicts earlier half", keep & (at >= split), keep & (at < split)),
    ):
        k = fit_gain(x[train], y[train])
        validation.append(dict(description=label, train_pairs=int(train.sum()),
                               test_pairs=int(test.sum()), fitted_gain=k,
                               **metrics(k*x[test], y[test])))

    sensitivity = []
    for left, right in [(16.4,22.4),(17,22),(17,21.8),(16.4,20),(20,22.4),(17,19),(19,22.4)]:
        mask = (at>=left) & (at<right) & (y>0)
        k = fit_gain(x[mask], y[mask])
        sensitivity.append(dict(start_s=left,end_s=right,gain=k,
                                **metrics(k*x[mask], y[mask])))
    lag_checks = []
    # Fixed reference times ensure timing comparisons use the same observations.
    # Positive shift queries a later sensor reading (e.g. +0.15 s to account
    # for the known firmware interpolation delay). Do not optimize a true delay
    # from a single monotonic ramp with a changing mount.
    rt, rv = r.elapsed_s.to_numpy(), r.value_raw_mps.to_numpy()
    lag_mask = (rt >= 17) & (rt < 21.8) & (rv > 0)
    for delta in (-1.0,-0.5,-0.15,0.0,0.15,0.5):
        sensor = np.interp(rt[lag_mask]+delta, st, sv)
        k = fit_gain(sensor, rv[lag_mask])
        lag_checks.append(dict(sensor_query_shift_s=delta,gain=k,
                               **metrics(k*sensor, rv[lag_mask])))

    base = describe(sv[(st>=0) & (st<14.5)])
    tail = describe(sv[(st>=32) & (st<=st[-1])])
    post_fall = (at>=fall_s)
    flow = r[r.value_raw_mps > 0]
    pressure_density = np.median(2*abs(flow.pa_corrected)/flow.value_raw_mps**2)
    secs_per_ref = np.diff(r.elapsed_s)
    nominal_pressure_quantization_mps = np.sqrt(2/pressure_density)
    mass_factor = 0.8551871081528
    result = dict(
        decision="Partially usable for diagnostics and a provisional pre-fall gain; insufficient for a validated or transferable wind calibration.",
        conservative_movement_cutoff_s=fall_s,
        exact_first_instability_time_known=False,
        movement_cutoff_timestamp=pd.to_datetime(t0+fall_s,unit="s",utc=True).isoformat(),
        duration_s=float(st[-1]), student_rows=int(len(s)), reference_rows=int(len(r)),
        mean_student_line_rate_hz=float((len(s)-1)/(st[-1]-st[0])),
        mean_reference_rate_hz=float(1/np.mean(secs_per_ref)),
        reference_nonzero_rows=int(len(flow)),
        reference_nonzero_interval_s=[float(flow.elapsed_s.min()),float(flow.elapsed_s.max())],
        excluded_after_fall_fraction=float((st[-1]-fall_s)/st[-1]),
        final_reference_zero_from_s=float(r.loc[(r.elapsed_s>28)&(r.value_raw_mps==0),"elapsed_s"].iloc[0]),
        nearest_pair_max_time_error_ms=float(1000*max(abs(at-st[nearest]))),
        student_rows_sharing_receipt_timestamps=int(s.timestamp_epoch.duplicated(keep=False).sum()),
        reference_pressure_grid_pa=sorted(map(float,r.pa_corrected.unique())),
        reference_density_implied_by_saved_speed_kg_m3=float(pressure_density),
        one_pa_reference_speed_mps=float(nominal_pressure_quantization_mps),
        reference_pressure_sign="Negative nonzero pressure; exact logger release uses abs(pressure) for speed. Negative sign alone does not invalidate this file.",
        initial_baseline=base, final_failed_baseline=tail,
        final_equivalent_load_g=float((tail["mean"]/mass_factor)**2),
        provisional_fit=preliminary, blocked_same_run_checks=validation,
        interval_sensitivity=sensitivity, timing_sensitivity=lag_checks,
        selected_sensor_samples_beyond_35g_equivalent=int(np.sum(x[keep] > mass_factor*np.sqrt(35))),
        limitations=[
            "No independent validation run or repeated up/down sweeps in the same intact mount state.",
            "Usable interval is about five seconds and largely transient; no several-second steady plateaus across the range.",
            "200 serial lines/s contain interpolated HX711 conversions at about 10 Hz; they are not 200 independent sensor measurements/s.",
            "Reference updates at about 4 Hz, with integer-Pa saved pressure. Low-speed pairs are coarse; sensor and pressure dynamics can differ.",
            "The logger pairs receipt times but does not compensate the known 150 ms sensor interpolation delay; timing sensitivity is reported separately.",
            "USB batches share receipt timestamps; the logger sorts (timestamp,value) tuples. Matching was checked with that exact tie-break.",
            "The true moment of initial mount instability may precede the visible fall; the pre-fall gain is exploratory.",
            "A large gain correction must not automatically be interpreted as a physical flat-plate drag coefficient.",
            "Final mechanical offset must not be subtracted from the initial geometry or from m/s values across the whole run.",
        ],
        sources={name:dict(path=path.as_posix(),sha256=hashlib.sha256(path.read_bytes()).hexdigest()) for name,path in paths.items()},
        analysis_tool_changes_firmware=False,
        statistical_reference="https://www.itl.nist.gov/div898/handbook/mpc/section3/mpc365.htm",
    )
    (args.output/"assessment.json").write_text(json.dumps(result,indent=2)+"\n",encoding="utf-8")
    annotated = a.copy()
    annotated["selection"] = np.select([keep,post_fall],
        ["PROVISIONAL_PREFALL_FIT","EXCLUDE_POSSIBLE_MOUNT_MOVEMENT"],default="EXCLUDE_BASELINE_OR_TRANSIENT")
    annotated["provisional_corrected_mps"] = np.where(keep,0.55*x,np.nan)
    annotated.to_csv(args.output/"annotated_pairs.csv",index=False)
    annotated.loc[keep].to_csv(args.output/"provisional_prefall_pairs.csv",index=False)

    plt.rcParams.update({"font.size":10,"axes.spines.top":False,"axes.spines.right":False})
    fig,axs=plt.subplots(3,1,figsize=(12,11),layout="constrained")
    axs[0].plot(st,sv,label="Plate sensor (v7 estimate)",color="#b12a90",lw=1.2)
    axs[0].plot(rt,rv,"o-",label="Tunnel reference",color="#007d9a",ms=3,lw=1.4)
    axs[0].axvspan(fall_s,st[-1],color="#dd5555",alpha=.15,label="Exclude: possible mount movement/fall")
    axs[0].axvspan(fit_start,fit_end,color="#28a46c",alpha=.15,label="Provisional pre-fall fit only")
    axs[0].axvline(fall_s,color="#b33333",ls="--")
    axs[0].annotate("Persistent ~3.88 m/s offset\nwhile reference is zero",xy=(40,tail["mean"]),xytext=(34,7),
                    arrowprops=dict(arrowstyle="->",color="#555555"))
    axs[0].set(xlabel="Seconds since recording started",ylabel="Speed (m/s)",title="Full run: conservatively exclude the reversal and later data; exact failure onset is uncertain")
    axs[0].legend(loc="upper left",fontsize=8,ncol=2);axs[0].grid(alpha=.2)
    axs[1].plot(st,sv,color="#b12a90",lw=1.2,label="Original sensor")
    display=(st>=fit_start)&(st<fit_end)
    axs[1].plot(st[display],0.55*sv[display],color="#23904d",lw=2,label="Rough guide: 0.55 x original sensor")
    axs[1].plot(rt,rv,"o-",color="#007d9a",ms=4,lw=1.4,label="Reference")
    axs[1].axvspan(fall_s,30,color="#dd5555",alpha=.15)
    axs[1].set(xlim=(14.5,30),ylim=(0,10.7),xlabel="Seconds since recording started",ylabel="Speed (m/s)",title="Short pre-fall interval supports a rough gain; no independent validation")
    axs[1].legend(fontsize=8);axs[1].grid(alpha=.2)
    active=(at>=15)&(at<30)
    sc=axs[2].scatter(y[active],x[active],c=at[active],cmap="viridis",s=30)
    axs[2].plot(y[active],x[active],color="gray",alpha=.4,lw=.8)
    axs[2].scatter(y[keep],0.55*x[keep],color="#23904d",marker="x",s=35,label="Rough corrected pre-fall points")
    axs[2].plot([0,10],[0,10],"--",color="black",lw=1,label="Perfect agreement")
    axs[2].scatter(y[at>=32],x[at>=32],color="#dd5555",s=12,label="Failed final zero")
    axs[2].set(xlabel="Reference speed (m/s)",ylabel="Sensor estimate (m/s)",xlim=(-.2,9),ylim=(-.2,10.5),title="Time-ordered comparison: changing mount state invalidates a whole-run fit")
    axs[2].legend(fontsize=8);axs[2].grid(alpha=.2)
    fig.colorbar(sc,ax=axs[2],label="Elapsed time (s)")
    fig.savefig(args.output/"overview.png",dpi=160)
    fig.savefig(args.output/"overview.pdf")
    plt.close(fig)
    print(json.dumps({key:result[key] for key in ("decision","duration_s","reference_nonzero_rows","initial_baseline","final_failed_baseline","provisional_fit","blocked_same_run_checks","timing_sensitivity")},indent=2))


if __name__=="__main__":
    main()
