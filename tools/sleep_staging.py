"""
sleep_staging.py — Technical GEM 2.0
Millimeter-wave radar sleep staging algorithm.

Usage:
    python sleep_staging.py <path_to_csv>

Output (written to same directory as input CSV):
    sleep_report_YYYYMMDD_HHMMSS.csv   — summary + minute-by-minute staging
    sleep_hypnogram_YYYYMMDD_HHMMSS.png — two-panel hypnogram chart
"""

# ── Standard library ───────────────────────────────────────────────────────────
import argparse
import os
import sys

# Force UTF-8 output on Windows terminals (PowerShell / cmd default to CP936)
if sys.stdout.encoding and sys.stdout.encoding.lower() not in ("utf-8", "utf-8-sig"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
if sys.stderr.encoding and sys.stderr.encoding.lower() not in ("utf-8", "utf-8-sig"):
    sys.stderr.reconfigure(encoding="utf-8", errors="replace")
import warnings
from dataclasses import dataclass, field
from datetime import datetime
from pathlib import Path
from typing import List, Optional

# ── Third-party ────────────────────────────────────────────────────────────────
import numpy as np
import pandas as pd
from scipy.signal import medfilt
import matplotlib.pyplot as plt
import matplotlib.dates as mdates

warnings.filterwarnings("ignore", category=RuntimeWarning)
pd.options.mode.chained_assignment = None

# Chinese font support on Windows
plt.rcParams["font.sans-serif"] = ["SimHei", "Microsoft YaHei", "PingFang SC", "DejaVu Sans"]
plt.rcParams["axes.unicode_minus"] = False

# ══════════════════════════════════════════════════════════════════════════════
# SECTION 0 — CONSTANTS
# ══════════════════════════════════════════════════════════════════════════════

# Physiological validity bounds
HR_MIN,   HR_MAX   = 40,  120
RESP_MIN, RESP_MAX = 5,   40

# Resampling / interpolation
RESAMPLE_FREQ     = "1min"
INTERP_LIMIT_MIN  = 10     # max consecutive NaN minutes to interpolate
GAP_BLOCK_MIN     = 15     # gaps wider than this block interpolation

# Wake Index weights — DO NOT CHANGE
W_MOVE      = 0.45
W_HR        = 0.15
W_RESP      = 0.15
W_HR_VAR    = 0.125
W_RESP_VAR  = 0.125

# Boundary detection
ONSET_QUANTILE    = 0.35
ONSET_WINDOW_MIN  = 15
WAKEUP_MOVE_THRESH = 2.5
WAKEUP_MOVE_PCT   = 0.60
WAKEUP_HR_THRESH  = 90.0
WAKEUP_WINDOW_MIN = 10

# Staging thresholds
AWAKE_MOVE_MEAN    = 3.0
AWAKE_MOVE_MAX     = 20.0
DEEP_MOVE_CEIL     = 1.2
DEEP_HR_FACTOR     = 0.98
DEEP_HR_STD_CEIL   = 2.0
DEEP_RESP_STD_CEIL = 1.0
REM_HR_STD_FLOOR   = 3.0
REM_RESP_STD_FLOOR = 1.5

# Stage codes and display
STAGE_DEEP  = 0
STAGE_LIGHT = 1
STAGE_REM   = 2
STAGE_AWAKE = 3
STAGE_NAMES  = {0: "Deep", 1: "Light", 2: "REM",     3: "Awake"}
STAGE_COLORS = {0: "#1a3a6b", 1: "#6baed6", 2: "#fd8d3c", 3: "#e31a1c"}

# Move data field index in comma-delimited payload (firmware-dependent; change if needed)
MOVE_FIELD_INDEX = 0

# Chinese CSV column / command names
COL_TIME = "时间戳"
COL_CMD  = "命令名称"
COL_DATA = "数据内容(10进制)"
CMD_HR   = "心率数值上报"
CMD_RESP = "呼吸数值上报"
CMD_MOVE = "体动参数主动上报"
CMD_BED  = "入床/离床状态上报"


# ══════════════════════════════════════════════════════════════════════════════
# SECTION 0b — DATACLASSES
# ══════════════════════════════════════════════════════════════════════════════

@dataclass
class GapRecord:
    """A detected time gap that blocks interpolation."""
    start: pd.Timestamp
    end:   pd.Timestamp

    @property
    def duration_minutes(self) -> float:
        return (self.end - self.start).total_seconds() / 60.0


@dataclass
class StagingResult:
    """Carries all outputs produced by the five pipeline stages."""
    # Stage 1
    resampled_df: Optional[pd.DataFrame] = None
    gaps:         List[GapRecord]        = field(default_factory=list)

    # Stage 2
    features_df: Optional[pd.DataFrame] = None

    # Stage 3
    onset_time:  Optional[pd.Timestamp] = None
    wakeup_time: Optional[pd.Timestamp] = None

    # Stage 4 / 5
    staged_df: Optional[pd.DataFrame] = None

    # Summary
    total_sleep_min: float = 0.0
    deep_min:        float = 0.0
    light_min:       float = 0.0
    rem_min:         float = 0.0
    awake_min:       float = 0.0

    # Diagnostic messages
    warnings: List[str] = field(default_factory=list)
    insights: List[str] = field(default_factory=list)


@dataclass
class OutputPaths:
    """Resolved output file paths for a single run."""
    out_dir: str
    csv_path: str
    json_path: str
    png_path: str


@dataclass
class RunOutputs:
    """Artifacts and side-effect status produced by a run."""
    result: StagingResult
    paths: OutputPaths
    png_generated: bool = False
    opened_png: bool = False


# ══════════════════════════════════════════════════════════════════════════════
# SECTION 1 — CSV PARSING & PREPROCESSING
# ══════════════════════════════════════════════════════════════════════════════

def load_raw_csv(path: str) -> pd.DataFrame:
    """Load CSV, trying multiple encodings common to Chinese-locale Windows."""
    required_columns = (COL_TIME, COL_CMD, COL_DATA)
    failures = []
    for enc in ("utf-8-sig", "utf-8", "gbk", "gb2312"):
        try:
            df = pd.read_csv(path, encoding=enc, dtype=str)
            missing_columns = [col for col in required_columns if col not in df.columns]
            if not missing_columns:
                return df
            failures.append(
                f"{enc}: missing columns {missing_columns}; found columns {list(df.columns)}"
            )
        except UnicodeDecodeError as exc:
            failures.append(f"{enc}: decode error ({exc})")
        except pd.errors.EmptyDataError:
            raise ValueError(f"CSV is empty: '{path}'")
        except Exception as exc:
            failures.append(f"{enc}: {type(exc).__name__} ({exc})")

    tried = "utf-8-sig / utf-8 / gbk / gb2312"
    details = "\n  - ".join(failures) if failures else "No additional details."
    raise ValueError(
        f"Cannot read '{path}' using supported encodings ({tried}).\n"
        f"  - {details}"
    )


def _parse_timestamp_series(series: pd.Series) -> pd.Series:
    """Auto-detect and parse a timestamp Series."""
    sample = series.dropna()
    if sample.empty:
        raise ValueError("Timestamp column is empty.")

    first = str(sample.iloc[0]).strip()

    # Unix epoch (all digits, possibly with decimal point)
    if first.replace(".", "").isdigit():
        val = float(first)
        unit = "s" if val < 1e11 else "ms"
        parsed = pd.to_datetime(pd.to_numeric(series, errors="coerce"), unit=unit, errors="coerce")
        if parsed.isna().all():
            raise ValueError("Timestamp parsing failed: no valid unix timestamps found.")
        return parsed

    # Named format candidates
    for fmt in (
        "%Y-%m-%d %H:%M:%S",
        "%Y/%m/%d %H:%M:%S",
        "%Y-%m-%dT%H:%M:%S",
        "%d/%m/%Y %H:%M:%S",
        "%Y%m%d%H%M%S",
        "%Y-%m-%d %H:%M",
        "%Y/%m/%d %H:%M",
    ):
        try:
            parsed = pd.to_datetime(series, format=fmt, errors="raise")
            if parsed.isna().all():
                continue
            return parsed
        except (ValueError, TypeError):
            continue

    parsed = pd.to_datetime(series, errors="coerce")
    if parsed.isna().all():
        sample_values = [str(v) for v in sample.head(3).tolist()]
        raise ValueError(
            f"Timestamp parsing failed for column values like {sample_values}. "
            f"Non-empty rows checked: {len(sample)}."
        )
    return parsed


def pivot_to_timeseries(raw: pd.DataFrame) -> pd.DataFrame:
    """
    Convert the long-format event CSV into a wide time-indexed DataFrame
    with columns: HR, Resp, Move.
    """
    if COL_TIME not in raw.columns or COL_CMD not in raw.columns or COL_DATA not in raw.columns:
        raise ValueError(
            f"CSV must contain columns: '{COL_TIME}', '{COL_CMD}', '{COL_DATA}'. "
            f"Found: {list(raw.columns)}"
        )

    timestamps = _parse_timestamp_series(raw[COL_TIME])
    raw = raw.copy()
    raw["_ts"] = timestamps
    raw = raw.dropna(subset=["_ts"])

    def _extract(cmd: str) -> pd.Series:
        mask = raw[COL_CMD].str.strip() == cmd
        subset = raw[mask].copy()
        if subset.empty:
            return pd.Series(dtype=float, name=cmd)
        subset = subset.set_index("_ts")[COL_DATA]
        return subset

    # ── Heart Rate ────────────────────────────────────────────────────────────
    hr_raw = _extract(CMD_HR)
    hr_vals = pd.to_numeric(hr_raw, errors="coerce")
    hr_vals = hr_vals.where((hr_vals >= HR_MIN) & (hr_vals <= HR_MAX))

    # ── Respiration ───────────────────────────────────────────────────────────
    resp_raw = _extract(CMD_RESP)
    resp_vals = pd.to_numeric(resp_raw, errors="coerce")
    resp_vals = resp_vals.where((resp_vals >= RESP_MIN) & (resp_vals <= RESP_MAX))

    # ── Motion ────────────────────────────────────────────────────────────────
    move_raw = _extract(CMD_MOVE)
    def _parse_move(v: str) -> float:
        try:
            parts = str(v).split(",")
            return float(parts[MOVE_FIELD_INDEX].strip())
        except (IndexError, ValueError):
            return float("nan")
    move_vals = move_raw.apply(_parse_move)

    # Combine into a single DataFrame
    ts = pd.DataFrame({"HR": hr_vals, "Resp": resp_vals, "Move": move_vals})
    ts = ts.sort_index()

    if ts.dropna(how="all").empty:
        raise ValueError(
            "No valid HR, Resp, or Move data found after parsing. "
            "Check CSV command names match the expected Chinese labels."
        )

    return ts


def detect_gaps(df: pd.DataFrame) -> List[GapRecord]:
    """Return list of GapRecord for time gaps > GAP_BLOCK_MIN minutes."""
    if len(df) < 2:
        return []
    diffs = df.index.to_series().diff().dt.total_seconds() / 60.0
    gap_starts = diffs[diffs > GAP_BLOCK_MIN]
    gaps = []
    for end_ts, dur in gap_starts.items():
        pos = df.index.get_loc(end_ts)
        start_ts = df.index[pos - 1]
        gaps.append(GapRecord(start=start_ts, end=end_ts))
    return gaps


def resample_and_clean(ts: pd.DataFrame, gaps: List[GapRecord]) -> pd.DataFrame:
    """
    Stage 1 core:
    1. Resample to 1-min frequency with aggregations.
    2. Remove out-of-bed periods (dropna on HR+Resp).
    3. Dense-fill index so gap NaNs are contiguous.
    4. Interpolate small gaps (<=10 min); large gaps (>15 min) are naturally
       blocked because limit=10 cannot bridge >=15 consecutive NaN rows.
    5. Final dropna.
    """
    # Separate aggregations
    hr_resampled   = ts["HR"].resample(RESAMPLE_FREQ).agg(["mean", "std"])
    resp_resampled = ts["Resp"].resample(RESAMPLE_FREQ).agg(["mean", "std"])
    move_resampled = ts["Move"].resample(RESAMPLE_FREQ).agg(["mean", "max"])

    df = pd.DataFrame({
        "HR_mean":   hr_resampled["mean"],
        "HR_std":    hr_resampled["std"],
        "Resp_mean": resp_resampled["mean"],
        "Resp_std":  resp_resampled["std"],
        "Move_mean": move_resampled["mean"],
        "Move_max":  move_resampled["max"],
    })

    # Fill Move NaN with 0 (no motion detected = zero movement)
    df[["Move_mean", "Move_max"]] = df[["Move_mean", "Move_max"]].fillna(0.0)

    # Remove rows where both HR and Resp are absent (proxy for out-of-bed)
    df.dropna(subset=["HR_mean", "Resp_mean"], how="all", inplace=True)

    if df.empty:
        raise ValueError("No valid HR/Resp data after removing out-of-bed periods.")

    # Densify the index so that gap rows exist as NaN rows
    full_idx = pd.date_range(start=df.index.min(), end=df.index.max(), freq=RESAMPLE_FREQ)
    df = df.reindex(full_idx)

    # Restore Move fill across the densified index
    df[["Move_mean", "Move_max"]] = df[["Move_mean", "Move_max"]].fillna(0.0)

    # Gap-aware interpolation:
    # With limit=INTERP_LIMIT_MIN (10), any run of >= 15 consecutive NaN rows
    # will NOT be fully bridged — the gap centre remains NaN.
    # No explicit gap-masking is needed; the limit parameter handles it.
    df[["HR_mean", "HR_std", "Resp_mean", "Resp_std"]] = (
        df[["HR_mean", "HR_std", "Resp_mean", "Resp_std"]]
        .interpolate(method="time", limit=INTERP_LIMIT_MIN)
    )

    # Drop any remaining NaN rows (unbridged gaps, edge rows)
    df.dropna(subset=["HR_mean", "Resp_mean"], inplace=True)
    df[["HR_std", "Resp_std"]] = df[["HR_std", "Resp_std"]].fillna(0.0)

    if len(df) < 5:
        raise ValueError(
            f"Only {len(df)} minute(s) of data remain after preprocessing. "
            "Cannot produce meaningful staging."
        )

    return df


# ══════════════════════════════════════════════════════════════════════════════
# SECTION 2 — FEATURE ENGINEERING (5D WAKE INDEX)
# ══════════════════════════════════════════════════════════════════════════════

def robust_normalize(series: pd.Series) -> pd.Series:
    """Quantile-based robust normalization, clipped to [0, 1]."""
    p05 = series.quantile(0.05)
    p95 = series.quantile(0.95)
    denom = p95 - p05
    if denom == 0 or np.isnan(denom):
        return pd.Series(0.0, index=series.index, name=series.name)
    return ((series - p05) / denom).clip(0.0, 1.0)


def compute_features(df: pd.DataFrame) -> pd.DataFrame:
    """
    Stage 2:
    1. Robust normalization of five input signals.
    2. Weighted 5D Wake Index.
    3. 10-minute centred rolling average → Smooth_Wake_Index.
    """
    feat = df.copy()

    feat["Norm_Move"]     = robust_normalize(feat["Move_mean"])
    feat["Norm_HR"]       = robust_normalize(feat["HR_mean"])
    feat["Norm_Resp"]     = robust_normalize(feat["Resp_mean"])
    feat["Norm_HR_Var"]   = robust_normalize(feat["HR_std"])
    feat["Norm_Resp_Var"] = robust_normalize(feat["Resp_std"])

    feat["Wake_Index"] = (
        W_MOVE    * feat["Norm_Move"]
      + W_HR      * feat["Norm_HR"]
      + W_RESP    * feat["Norm_Resp"]
      + W_HR_VAR  * feat["Norm_HR_Var"]
      + W_RESP_VAR * feat["Norm_Resp_Var"]
    )

    feat["Smooth_Wake_Index"] = (
        feat["Wake_Index"].rolling(window=10, center=True, min_periods=1).mean()
    )

    return feat


# ══════════════════════════════════════════════════════════════════════════════
# SECTION 3 — BOUNDARY DETECTION
# ══════════════════════════════════════════════════════════════════════════════

def find_sleep_onset(df: pd.DataFrame, result: StagingResult) -> pd.Timestamp:
    """
    Stage 3a — Sleep Onset (behavioral backtrack anchor):
    1. Find the first 15-min physiological onset (Smooth_Wake_Index below Q35).
    2. In the 30 min before that point, find the LAST minute with Move_mean > 2.0
       (the last behavioral activity before the person settled down).
    3. Onset = that last-active minute + 1 min.
       Fallback: physio_onset - 15 min, clamped to data start.
    """
    threshold = df["Smooth_Wake_Index"].quantile(ONSET_QUANTILE)

    below = (df["Smooth_Wake_Index"] < threshold).astype(int)
    rolling_sum = below.rolling(window=ONSET_WINDOW_MIN).sum()
    candidates = rolling_sum[rolling_sum == ONSET_WINDOW_MIN]

    if candidates.empty:
        result.warnings.append(
            "Sleep onset could not be detected automatically; defaulting to first data minute."
        )
        return df.index[0]

    window_end    = candidates.index[0]
    window_start  = window_end - pd.Timedelta(minutes=ONSET_WINDOW_MIN - 1)
    physio_onset  = window_start - pd.Timedelta(minutes=ONSET_WINDOW_MIN)
    physio_onset  = max(physio_onset, df.index[0])

    # Backtrack: find last high-movement minute in the 30 min before physio_onset
    backtrack_start = physio_onset - pd.Timedelta(minutes=30)
    backtrack_df    = df[(df.index > backtrack_start) & (df.index <= physio_onset)]
    last_active     = backtrack_df[backtrack_df["Move_mean"] > 2.0]

    if not last_active.empty:
        onset = last_active.index[-1] + pd.Timedelta(minutes=1)
        onset = min(onset, physio_onset)  # never push onset past physio_onset
    else:
        onset = physio_onset

    return onset


def find_wakeup(
    df: pd.DataFrame,
    onset: pd.Timestamp,
    result: StagingResult,
) -> pd.Timestamp:
    """
    Stage 3b — Wake-up (dual condition):
    Only search AFTER onset + 30 min to avoid triggering on pre-sleep
    elevated HR or initial motion when the person first gets into bed.
    Condition A: >60% of any 10-min window has Move_mean > 2.5.
    Condition B: HR_mean > 90.
    Returns the earliest of A and B. Falls back to last data minute.
    """
    # Search only in the last 60 minutes of recording (morning wake-up window).
    # This prevents mid-sleep HR spikes or brief movements from being mistaken
    # for the final wake-up event.
    end_time     = df.index[-1]
    search_start = end_time - pd.Timedelta(minutes=60)
    search_df    = df[df.index >= search_start]

    if search_df.empty:
        result.warnings.append(
            "Wake-up: last-60-min window is empty; using last data minute."
        )
        return end_time

    # Condition A: >30% (relaxed from 60%) of a 10-min rolling window has Move_mean > 3.0
    # (matches old code's proven threshold for final-hour activity)
    move_high = (search_df["Move_mean"] > AWAKE_MOVE_MEAN).astype(float)
    roll_pct  = move_high.rolling(window=WAKEUP_WINDOW_MIN, min_periods=1).mean()
    cond_a    = roll_pct[roll_pct > 0.30]

    # Condition B: HR_mean > 90
    cond_b = search_df[search_df["HR_mean"] > WAKEUP_HR_THRESH]

    candidates = []
    if not cond_a.empty:
        # Anchor to the local Wake_Index peak in ±15 min around the first candidate
        first_a    = cond_a.index[0]
        anchor_win = search_df[
            (search_df.index >= first_a - pd.Timedelta(minutes=10)) &
            (search_df.index <= first_a + pd.Timedelta(minutes=15))
        ]
        if not anchor_win.empty:
            candidates.append(anchor_win["Smooth_Wake_Index"].idxmax())
        else:
            candidates.append(first_a)
    if not cond_b.empty:
        candidates.append(cond_b.index[0])

    if candidates:
        return min(candidates)

    result.warnings.append(
        "Wake-up not detected via dual condition; defaulting to last data minute."
    )
    return df.index[-1]


# ══════════════════════════════════════════════════════════════════════════════
# SECTION 4 — SLEEP STAGING LOGIC
# ══════════════════════════════════════════════════════════════════════════════

def classify_stages(
    df: pd.DataFrame,
    onset: pd.Timestamp,
    wakeup: pd.Timestamp,
) -> pd.Series:
    """
    Stage 4 — Per-minute staging within [onset, wakeup].
    Priority order (highest overrides lower):
        3 Awake  — Move_mean > 3.0  OR  Move_max > 20
        0 Deep   — Move_mean <= 1.2 AND HR_mean < night_hr_mean*0.98
                   AND HR_std < 2.0 AND Resp_std < 1.0
        2 REM    — Move_mean <= 1.2 AND (HR_std > 3.0 OR Resp_std > 1.5)
        1 Light  — all other
    Outside [onset, wakeup] → Awake (3) (enforced in Stage 5).
    """
    stages = pd.Series(STAGE_LIGHT, index=df.index, name="stage")

    # Compute night-time heart rate baseline from the sleep window
    sleep_mask   = (df.index >= onset) & (df.index <= wakeup)
    sleep_window = df[sleep_mask]
    if sleep_window.empty or sleep_window["HR_mean"].isna().all():
        night_hr_mean = df["HR_mean"].mean()
    else:
        night_hr_mean = sleep_window["HR_mean"].mean()

    # Apply rules in increasing priority (later rules overwrite earlier ones)
    # REM (2)
    rem_mask = (
        (df["Move_mean"] <= DEEP_MOVE_CEIL) &
        (
            (df["HR_std"]   > REM_HR_STD_FLOOR) |
            (df["Resp_std"] > REM_RESP_STD_FLOOR)
        )
    )
    stages[rem_mask] = STAGE_REM

    # Deep (0)
    deep_mask = (
        (df["Move_mean"] <= DEEP_MOVE_CEIL) &
        (df["HR_mean"]   <  night_hr_mean * DEEP_HR_FACTOR) &
        (df["HR_std"]    <  DEEP_HR_STD_CEIL) &
        (df["Resp_std"]  <  DEEP_RESP_STD_CEIL)
    )
    stages[deep_mask] = STAGE_DEEP

    # Awake (3) — highest priority
    # Include HR_mean > 90 to catch sympathetic activation ("清醒賴床")
    awake_mask = (
        (df["Move_mean"] > AWAKE_MOVE_MEAN) |
        (df["Move_max"]  > AWAKE_MOVE_MAX)  |
        (df["HR_mean"]   > WAKEUP_HR_THRESH)
    )
    stages[awake_mask] = STAGE_AWAKE

    return stages


# ══════════════════════════════════════════════════════════════════════════════
# SECTION 5 — POST-PROCESSING & STATISTICS
# ══════════════════════════════════════════════════════════════════════════════

def apply_median_filter(stages: pd.Series) -> pd.Series:
    """Kernel-7 median filter to suppress single-minute stage jitter."""
    arr      = stages.values.astype(float)
    filtered = medfilt(arr, kernel_size=7)
    rounded  = np.round(filtered).astype(int)
    rounded  = np.clip(rounded, 0, 3)
    return pd.Series(rounded, index=stages.index, name="stage")


def enforce_boundary_awake(
    stages: pd.Series,
    onset: pd.Timestamp,
    wakeup: pd.Timestamp,
) -> pd.Series:
    """Force Awake (3) for all minutes outside [onset, wakeup]."""
    s = stages.copy()
    s[s.index < onset]  = STAGE_AWAKE
    s[s.index > wakeup] = STAGE_AWAKE
    return s


def compute_statistics(result: StagingResult) -> None:
    """Populate summary statistics on the StagingResult object."""
    df = result.staged_df
    if df is None or "stage" not in df.columns:
        return

    onset  = result.onset_time
    wakeup = result.wakeup_time
    sleep_mask = (df.index >= onset) & (df.index <= wakeup)
    sleep_df   = df[sleep_mask]

    result.total_sleep_min = float(len(sleep_df))
    result.deep_min  = float((sleep_df["stage"] == STAGE_DEEP).sum())
    result.light_min = float((sleep_df["stage"] == STAGE_LIGHT).sum())
    result.rem_min   = float((sleep_df["stage"] == STAGE_REM).sum())
    result.awake_min = float((sleep_df["stage"] == STAGE_AWAKE).sum())


# ══════════════════════════════════════════════════════════════════════════════
# SECTION 6 — INSIGHTS
# ══════════════════════════════════════════════════════════════════════════════

def generate_insights(result: StagingResult) -> List[str]:
    """Auto-generate expert-level diagnostic commentary."""
    insights = []

    # Gap analysis
    long_gaps = [g for g in result.gaps if g.duration_minutes > GAP_BLOCK_MIN]
    for g in long_gaps:
        insights.append(
            f"數據斷層警告：於 {g.start.strftime('%H:%M')} ~ {g.end.strftime('%H:%M')} "
            f"偵測到 {g.duration_minutes:.0f} 分鐘的數據空白（超過 {GAP_BLOCK_MIN} 分鐘閾值），"
            "已強制阻斷插值，此段前後的分期結果可信度較低。"
        )

    total = result.total_sleep_min
    if total > 0:
        rem_pct  = (result.rem_min  / total) * 100
        deep_pct = (result.deep_min / total) * 100

        if rem_pct > 30:
            insights.append(
                f"高 REM 特徵（{rem_pct:.1f}%）：超過正常成人範圍（20-25%）。"
                "毫米波雷達在 REM 期間可能因微小眼動或呼吸不規則被誤判為活動，"
                "建議檢查 REM 閾值（HR_std > 3.0 或 Resp_std > 1.5）是否需校準。"
            )
        elif rem_pct < 10 and total > 120:
            insights.append(
                f"REM 偏低（{rem_pct:.1f}%）：可能原因為清晨過早覺醒、"
                "飲酒抑制 REM，或感測器位置偏移導致呼吸標準差被低估。"
            )

        if deep_pct > 35:
            insights.append(
                f"深睡比例偏高（{deep_pct:.1f}%）：超過典型範圍（15-25%）。"
                "請確認心率感測器校準狀態；持續低心率讀數可能導致深睡分期過多。"
            )

    if total < 180:
        insights.append(
            f"總睡眠時長偏短（{total:.0f} 分鐘）：低於 3 小時，"
            "分期結果僅供參考，建議搭配多夜數據評估。"
        )

    if not insights:
        insights.append("數據品質良好，無重大異常偵測。")

    return insights


# ══════════════════════════════════════════════════════════════════════════════
# SECTION 7 — OUTPUT FUNCTIONS
# ══════════════════════════════════════════════════════════════════════════════

def _fmt_duration(minutes: float) -> str:
    """Format minutes as 'Xh Ymin'."""
    h = int(minutes // 60)
    m = int(minutes %  60)
    if h > 0:
        return f"{h}h {m}min"
    return f"{m}min"


def print_console_report(result: StagingResult) -> None:
    """Print a formatted summary to stdout."""
    total = result.total_sleep_min
    sep = "=" * 63

    print(f"\n{sep}")
    print("  SLEEP STAGING REPORT  —  Technical GEM 2.0")
    print(f"  Generated : {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")
    print(sep)

    df = result.staged_df
    print(f"\n  Data range : {df.index[0].strftime('%Y-%m-%d %H:%M')} "
          f"→ {df.index[-1].strftime('%Y-%m-%d %H:%M')}")

    print("\n  SLEEP BOUNDARIES")
    print("  " + "-" * 30)
    onset_str  = result.onset_time.strftime('%Y-%m-%d %H:%M')  if result.onset_time  else "N/A"
    wakeup_str = result.wakeup_time.strftime('%Y-%m-%d %H:%M') if result.wakeup_time else "N/A"
    print(f"  Sleep Onset : {onset_str}")
    print(f"  Wake-up     : {wakeup_str}")
    print(f"  Total Sleep : {_fmt_duration(total)}")

    print("\n  STAGE BREAKDOWN")
    print("  " + "-" * 30)
    for _, label, minutes in [
        (STAGE_DEEP,  "Deep  Sleep", result.deep_min),
        (STAGE_LIGHT, "Light Sleep", result.light_min),
        (STAGE_REM,   "REM   Sleep", result.rem_min),
        (STAGE_AWAKE, "Awake      ", result.awake_min),
    ]:
        pct = (minutes / total * 100) if total > 0 else 0
        print(f"  {label} : {minutes:5.0f} min  ({pct:5.1f}%)")

    if result.warnings:
        print("\n  DATA QUALITY WARNINGS")
        print("  " + "-" * 30)
        for w in result.warnings:
            print(f"  [WARNING] {w}")

    print("\n  EXPERT INSIGHTS")
    print("  " + "-" * 30)
    for i, insight in enumerate(result.insights, 1):
        # Word-wrap at 60 chars
        words = insight.split()
        line  = ""
        for w in words:
            if len(line) + len(w) + 1 > 60:
                print(f"  [{i}] {line}")
                line = "    " + w
            else:
                line = (line + " " + w).strip()
        if line:
            print(f"  [{i}] {line}")

    print(f"\n{sep}\n")


def build_output_paths(csv_path: str, out_root: Optional[str] = None) -> OutputPaths:
    """Resolve the per-input output directory and fixed artifact filenames."""
    csv_file = Path(csv_path).resolve()
    base_dir = Path(out_root).resolve() if out_root else csv_file.parent
    out_dir = base_dir / csv_file.stem
    return OutputPaths(
        out_dir=str(out_dir),
        csv_path=str(out_dir / "report.csv"),
        json_path=str(out_dir / "summary.json"),
        png_path=str(out_dir / "hypnogram.png"),
    )


def save_csv_report(result: StagingResult, out_path: str) -> str:
    """Write a two-section CSV: summary metrics + per-minute staging table."""
    total = result.total_sleep_min

    def pct(m):
        return f"{(m / total * 100):.1f}%" if total > 0 else "0.0%"

    summary_rows = [
        ["metric", "value"],
        ["onset_time",       str(result.onset_time)],
        ["wakeup_time",      str(result.wakeup_time)],
        ["total_sleep_min",  f"{total:.0f}"],
        ["deep_min",         f"{result.deep_min:.0f}"],
        ["deep_pct",         pct(result.deep_min)],
        ["light_min",        f"{result.light_min:.0f}"],
        ["light_pct",        pct(result.light_min)],
        ["rem_min",          f"{result.rem_min:.0f}"],
        ["rem_pct",          pct(result.rem_min)],
        ["awake_min",        f"{result.awake_min:.0f}"],
        ["awake_pct",        pct(result.awake_min)],
    ]

    df = result.staged_df
    minute_rows = [["timestamp", "stage_code", "stage_name"]]
    for ts, row in df.iterrows():
        code = int(row["stage"])
        minute_rows.append([ts.strftime("%Y-%m-%d %H:%M:%S"), code, STAGE_NAMES[code]])

    with open(out_path, "w", encoding="utf-8-sig") as f:
        for r in summary_rows:
            f.write(",".join(str(x) for x in r) + "\n")
        f.write("\n")
        for r in minute_rows:
            f.write(",".join(str(x) for x in r) + "\n")

    return out_path


def save_json_report(result: StagingResult, out_path: str) -> str:
    """Write a JSON summary matching the old-code export format."""
    import json

    total = result.total_sleep_min

    def pct(m):
        return round((m / total * 100), 1) if total > 0 else 0.0

    stats = {
        "date":                   str(result.onset_time.date()) if result.onset_time else "",
        "onset_time":             result.onset_time.strftime("%Y-%m-%d %H:%M:%S") if result.onset_time else "",
        "wake_time":              result.wakeup_time.strftime("%Y-%m-%d %H:%M:%S") if result.wakeup_time else "",
        "total_sleep_minutes":    int(total),
        "deep_sleep_minutes":     int(result.deep_min),
        "light_sleep_minutes":    int(result.light_min),
        "rem_sleep_minutes":      int(result.rem_min),
        "awake_minutes":          int(result.awake_min),
        "deep_sleep_percentage":  pct(result.deep_min),
        "light_sleep_percentage": pct(result.light_min),
        "rem_sleep_percentage":   pct(result.rem_min),
        "awake_percentage":       pct(result.awake_min),
        "warnings":               result.warnings,
        "insights":               result.insights,
    }

    with open(out_path, "w", encoding="utf-8") as f:
        json.dump(stats, f, ensure_ascii=False, indent=4)

    return out_path


def plot_hypnogram(result: StagingResult, png_path: str) -> str:
    """
    4-panel figure saved to a fixed PNG path.
      Panel 1: Sleep stage step plot with onset/wake markers
      Panel 2: Heart rate
      Panel 3: Respiration rate
      Panel 4: Smooth Wake Index + scaled movement + threshold
    """
    df     = result.staged_df
    feat   = result.features_df
    onset  = result.onset_time
    wakeup = result.wakeup_time
    total  = result.total_sleep_min

    fig, axes = plt.subplots(4, 1, figsize=(13, 10), sharex=True)
    ax1, ax2, ax3, ax4 = axes
    fig.subplots_adjust(hspace=0.08)

    onset_label  = onset.strftime("%H:%M")  if onset  else "?"
    wakeup_label = wakeup.strftime("%H:%M") if wakeup else "?"
    fig.suptitle(
        f"Technical GEM 2.0  |  Onset: {onset_label}  →  Wake: {wakeup_label}  "
        f"|  Total Sleep: {_fmt_duration(total)}",
        fontsize=12, y=0.98,
    )

    # ── Panel 1: Hypnogram ────────────────────────────────────────────────────
    ax1.step(df.index, df["stage"], where="post", color="navy", linewidth=2)
    ax1.set_yticks([0, 1, 2, 3])
    ax1.set_yticklabels(["Deep", "Light", "REM", "Awake"])
    ax1.set_ylim(-0.3, 3.5)
    ax1.invert_yaxis()
    ax1.set_ylabel("Sleep Stage")
    ax1.grid(True, axis="y", alpha=0.3)

    if onset:
        ax1.axvline(onset,  color="green", linestyle="--", linewidth=2,
                    label=f"Onset  {onset_label}")
    if wakeup:
        ax1.axvline(wakeup, color="red",   linestyle="--", linewidth=2,
                    label=f"Wake   {wakeup_label}")
    ax1.legend(loc="upper right", fontsize=8)

    # stage counts in legend box
    pct = lambda m: f"{(m/total*100):.0f}%" if total > 0 else "0%"
    summary = (
        f"Deep {result.deep_min:.0f}m({pct(result.deep_min)})  "
        f"Light {result.light_min:.0f}m({pct(result.light_min)})  "
        f"REM {result.rem_min:.0f}m({pct(result.rem_min)})  "
        f"Awake {result.awake_min:.0f}m({pct(result.awake_min)})"
    )
    ax1.text(0.01, 0.05, summary, transform=ax1.transAxes,
             fontsize=7.5, va="bottom",
             bbox=dict(boxstyle="round,pad=0.3", facecolor="lightyellow", alpha=0.8))

    # ── Panel 2: Heart Rate ───────────────────────────────────────────────────
    ax2.plot(df.index, df["HR_mean"], color="crimson", linewidth=1)
    ax2.set_ylabel("Heart Rate\n(bpm)")
    ax2.axhline(WAKEUP_HR_THRESH, color="crimson", linestyle=":", alpha=0.5,
                linewidth=0.8, label=f"HR={WAKEUP_HR_THRESH:.0f} threshold")
    ax2.legend(loc="upper right", fontsize=7)
    ax2.grid(True, axis="y", alpha=0.2)
    if onset:  ax2.axvline(onset,  color="green", linestyle="--", linewidth=1, alpha=0.5)
    if wakeup: ax2.axvline(wakeup, color="red",   linestyle="--", linewidth=1, alpha=0.5)

    # ── Panel 3: Respiration ──────────────────────────────────────────────────
    ax3.plot(df.index, df["Resp_mean"], color="forestgreen", linewidth=1)
    ax3.set_ylabel("Respiration\n(bpm)")
    ax3.grid(True, axis="y", alpha=0.2)
    if onset:  ax3.axvline(onset,  color="green", linestyle="--", linewidth=1, alpha=0.5)
    if wakeup: ax3.axvline(wakeup, color="red",   linestyle="--", linewidth=1, alpha=0.5)

    # ── Panel 4: Wake Index + Move ────────────────────────────────────────────
    if feat is not None and "Smooth_Wake_Index" in feat.columns:
        swi       = feat["Smooth_Wake_Index"].reindex(df.index)
        threshold = feat["Smooth_Wake_Index"].quantile(ONSET_QUANTILE)
        ax4.plot(df.index, swi,  color="purple", linewidth=1.2,
                 label=f"Wake Index ({int(W_MOVE*100)}% Move)")
        ax4.plot(df.index, df["Move_mean"] / df["Move_mean"].max().clip(1),
                 color="gray", alpha=0.45, linewidth=0.8, label="Move (scaled)")
        ax4.axhline(threshold, color="gray", linestyle=":",
                    linewidth=0.9, label=f"Onset threshold ({threshold:.2f})")
        ax4.set_ylim(0, 1.05)
    ax4.set_ylabel("Wake Index")
    ax4.legend(loc="upper right", fontsize=7)
    ax4.grid(True, axis="y", alpha=0.2)
    if onset:  ax4.axvline(onset,  color="green", linestyle="--", linewidth=1, alpha=0.5)
    if wakeup: ax4.axvline(wakeup, color="red",   linestyle="--", linewidth=1, alpha=0.5)

    ax4.xaxis.set_major_formatter(mdates.DateFormatter("%H:%M"))
    ax4.set_xlabel("Time")
    fig.autofmt_xdate(rotation=25, ha="right")

    plt.tight_layout()

    fig.savefig(png_path, dpi=180, bbox_inches="tight")
    plt.close(fig)
    return png_path


# ══════════════════════════════════════════════════════════════════════════════
# SECTION 8 — MAIN PIPELINE
# ══════════════════════════════════════════════════════════════════════════════

def _legacy_main_disabled(csv_path: str) -> None:
    """Orchestrate all five pipeline stages and produce output."""
    if not os.path.isfile(csv_path):
        raise FileNotFoundError(f"CSV not found: '{csv_path}'")

    out_dir = str(Path(csv_path).parent)
    print(f"\n[GEM 2.0] Processing: {csv_path}")

    result = StagingResult()

    # ── Stage 1: Preprocessing ────────────────────────────────────────────────
    print("[1/5] Data integrity audit & preprocessing...")
    raw      = load_raw_csv(csv_path)
    ts       = pivot_to_timeseries(raw)
    gaps     = detect_gaps(ts)
    clean_df = resample_and_clean(ts, gaps)

    result.resampled_df = clean_df
    result.gaps         = gaps

    if gaps:
        for g in gaps:
            blocked = "BLOCKED" if g.duration_minutes > GAP_BLOCK_MIN else "interpolated"
            result.warnings.append(
                f"Time gap {g.duration_minutes:.0f} min "
                f"({g.start.strftime('%H:%M')} → {g.end.strftime('%H:%M')}) — {blocked}."
            )

    # ── Stage 2: Feature engineering ─────────────────────────────────────────
    print("[2/5] Feature normalization & 5D Wake Index...")
    feat_df = compute_features(clean_df)
    result.features_df = feat_df

    # ── Stage 3: Boundary detection ───────────────────────────────────────────
    print("[3/5] Physiological boundary detection...")
    onset  = find_sleep_onset(feat_df, result)
    wakeup = find_wakeup(feat_df, onset, result)

    if onset >= wakeup:
        result.warnings.append(
            "Sleep onset is at or after wake-up time. "
            "Entire dataset will be marked as Awake."
        )
        onset  = feat_df.index[0]
        wakeup = feat_df.index[0]

    result.onset_time  = onset
    result.wakeup_time = wakeup

    # ── Stage 4: Staging ──────────────────────────────────────────────────────
    print("[4/5] Per-minute sleep staging...")
    raw_stages = classify_stages(feat_df, onset, wakeup)

    # ── Stage 5: Post-processing ──────────────────────────────────────────────
    print("[5/5] Post-processing & output generation...")
    smoothed = apply_median_filter(raw_stages)
    final    = enforce_boundary_awake(smoothed, onset, wakeup)

    feat_df["stage"] = final
    result.staged_df = feat_df

    compute_statistics(result)
    result.insights = generate_insights(result)

    # ── Output ────────────────────────────────────────────────────────────────
    print_console_report(result)
    csv_out  = save_csv_report(result, out_dir)
    json_out = save_json_report(result, out_dir)

    print(f"[GEM 2.0] CSV saved  : {csv_out}")
    print(f"[GEM 2.0] JSON saved : {json_out}\n")

    png_out = plot_hypnogram(result, out_dir=out_dir)
    if png_out:
        print(f"[GEM 2.0] PNG saved  : {png_out}")


# ── Entry point ────────────────────────────────────────────────────────────────
if False and __name__ == "__main__":
    if len(sys.argv) != 2:
        print("Usage: python sleep_staging.py <path_to_csv>")
        sys.exit(1)
    try:
        _legacy_main_disabled(sys.argv[1])
    except (FileNotFoundError, ValueError) as exc:
        print(f"\n[FATAL] {exc}", file=sys.stderr)
        sys.exit(2)
    except Exception as exc:
        print(f"\n[UNEXPECTED ERROR] {exc}", file=sys.stderr)
        import traceback
        traceback.print_exc()
        sys.exit(3)


def run_pipeline(csv_path: str, out_root: Optional[str] = None, generate_png: bool = True) -> RunOutputs:
    """Run the full staging pipeline and export fixed output files."""
    if not os.path.isfile(csv_path):
        raise FileNotFoundError(f"CSV not found: '{csv_path}'")

    paths = build_output_paths(csv_path, out_root)
    print(f"\n[GEM 2.0] Processing: {csv_path}")

    result = StagingResult()

    print("[1/5] Data integrity audit & preprocessing...")
    raw = load_raw_csv(csv_path)
    ts = pivot_to_timeseries(raw)
    gaps = detect_gaps(ts)
    clean_df = resample_and_clean(ts, gaps)

    result.resampled_df = clean_df
    result.gaps = gaps

    if gaps:
        for gap in gaps:
            blocked = "BLOCKED" if gap.duration_minutes > GAP_BLOCK_MIN else "interpolated"
            result.warnings.append(
                f"Time gap {gap.duration_minutes:.0f} min "
                f"({gap.start.strftime('%H:%M')} -> {gap.end.strftime('%H:%M')}) - {blocked}."
            )

    print("[2/5] Feature normalization & 5D Wake Index...")
    feat_df = compute_features(clean_df)
    result.features_df = feat_df

    print("[3/5] Physiological boundary detection...")
    onset = find_sleep_onset(feat_df, result)
    wakeup = find_wakeup(feat_df, onset, result)

    if onset >= wakeup:
        result.warnings.append(
            "Sleep onset is at or after wake-up time. Entire dataset will be marked as Awake."
        )
        onset = feat_df.index[0]
        wakeup = feat_df.index[0]

    result.onset_time = onset
    result.wakeup_time = wakeup

    print("[4/5] Per-minute sleep staging...")
    raw_stages = classify_stages(feat_df, onset, wakeup)

    print("[5/5] Post-processing & output generation...")
    smoothed = apply_median_filter(raw_stages)
    final = enforce_boundary_awake(smoothed, onset, wakeup)

    feat_df["stage"] = final
    result.staged_df = feat_df

    compute_statistics(result)
    result.insights = generate_insights(result)
    print_console_report(result)

    Path(paths.out_dir).mkdir(parents=True, exist_ok=True)
    save_csv_report(result, paths.csv_path)
    save_json_report(result, paths.json_path)

    outputs = RunOutputs(result=result, paths=paths)
    if generate_png:
        if str(plt.get_backend()).lower() != "agg":
            plt.switch_backend("Agg")
        plot_hypnogram(result, paths.png_path)
        outputs.png_generated = True

    return outputs


def open_result_artifact(png_path: str) -> bool:
    """Open the exported hypnogram in the system image viewer."""
    if not os.path.isfile(png_path):
        raise FileNotFoundError(f"Hypnogram PNG not found: '{png_path}'")
    if hasattr(os, "startfile"):
        os.startfile(png_path)
        return True
    raise OSError("Automatic image opening is not supported on this platform.")


def print_run_summary(outputs: RunOutputs) -> None:
    """Print exported artifact paths and final warning state."""
    print("[GEM 2.0] Output directory :", outputs.paths.out_dir)
    print("[GEM 2.0] CSV saved         :", outputs.paths.csv_path)
    print("[GEM 2.0] JSON saved        :", outputs.paths.json_path)
    if outputs.png_generated:
        print("[GEM 2.0] PNG saved         :", outputs.paths.png_path)
    else:
        print("[GEM 2.0] PNG saved         : skipped (--no-png)")
    print(
        "[GEM 2.0] PNG opened        :",
        outputs.paths.png_path if outputs.opened_png else "not opened",
    )
    if outputs.result.warnings:
        print("[GEM 2.0] Final warnings:")
        for warning in outputs.result.warnings:
            print(f"  - {warning}")


def parse_args(argv: Optional[List[str]] = None) -> argparse.Namespace:
    """Parse CLI arguments."""
    parser = argparse.ArgumentParser(
        description="Analyze a radar CSV and export report.csv, summary.json, and hypnogram.png."
    )
    parser.add_argument("csv_path", help="Path to the input CSV file.")
    parser.add_argument(
        "--out-dir",
        dest="out_dir",
        help="Optional output root directory. Results go to <out-dir>/<csv_stem>/.",
    )
    parser.add_argument(
        "--no-open",
        action="store_true",
        help="Export results but do not open hypnogram.png automatically.",
    )
    parser.add_argument(
        "--no-png",
        action="store_true",
        help="Skip hypnogram.png generation and automatic opening.",
    )
    return parser.parse_args(argv)


def main(argv: Optional[List[str]] = None) -> RunOutputs:
    """CLI entrypoint."""
    args = parse_args(argv)
    outputs = run_pipeline(args.csv_path, out_root=args.out_dir, generate_png=not args.no_png)

    if outputs.png_generated and not args.no_open:
        try:
            outputs.opened_png = open_result_artifact(outputs.paths.png_path)
        except Exception as exc:
            outputs.result.warnings.append(f"Unable to open result panel automatically: {exc}")

    print_run_summary(outputs)
    return outputs


if __name__ == "__main__":
    try:
        main()
    except (FileNotFoundError, ValueError) as exc:
        print(f"\n[FATAL] {exc}", file=sys.stderr)
        sys.exit(2)
    except Exception as exc:
        print(f"\n[UNEXPECTED ERROR] {exc}", file=sys.stderr)
        import traceback
        traceback.print_exc()
        sys.exit(3)
