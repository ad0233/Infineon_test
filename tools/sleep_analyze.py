"""
sleep_analyze.py — 把 radar_panel.py 录制的宽表 CSV 喂给 sleep_staging

输入: radar_panel.py 录制的 1Hz 宽表 CSV（列：时间, 帧号, 有人, 置信度, 距离_cm,
      呼吸_BPM, 心率_BPM, 体动, 体动强度_%, 温度_℃, 湿度_%, 环境光_lux）
输出: <csv_stem>/report.csv + summary.json + hypnogram.png

用法:
    python sleep_analyze.py <path_to_csv> [--out-dir DIR] [--no-png]
"""
from __future__ import annotations

import argparse
import os
import sys
from pathlib import Path
from typing import Optional

import numpy as np
import pandas as pd

# ── 导入算法库（sleep_staging.py 与本文件同目录）──────────────────
_HERE = os.path.dirname(os.path.abspath(__file__))
if _HERE not in sys.path:
    sys.path.insert(0, _HERE)
import sleep_staging as gem  # noqa: E402

# ══════════════════════════════════════════════════════════════════════
# 体动量纲校准 —— 当前固件「体动强度_%」是 0-100
# sleep_staging 的默认阈值针对旧固件量纲，此处覆盖为 % 尺度下的保守初值。
# 实测一整夜后应根据体动分布重新定标（见 README / 评估文档）。
# ══════════════════════════════════════════════════════════════════════
gem.AWAKE_MOVE_MEAN    = 15.0   # 清醒判定：10 分钟窗内 Move 均值阈值
gem.AWAKE_MOVE_MAX     = 60.0   # 清醒判定：峰值阈值
gem.DEEP_MOVE_CEIL     = 5.0    # 深睡判定：Move 上限
gem.WAKEUP_MOVE_THRESH = 12.0   # 醒来检测：Move 阈值

# HR 量程适配固件检测范围（51-132 BPM）
gem.HR_MIN = 45
gem.HR_MAX = 135

# ── 宽表 CSV 列名（与 radar_panel.py _start_recording 完全一致）────
COL_TIME     = "时间"
COL_PRESENT  = "有人"
COL_HR       = "心率_BPM"
COL_RESP     = "呼吸_BPM"
COL_MOVE_PCT = "体动强度_%"


def wide_csv_to_ts(csv_path: str) -> pd.DataFrame:
    """把宽表 1Hz CSV 转成 sleep_staging 期望的 HR/Resp/Move 时间序列。

    - 有人=0 的行：HR/Resp 视为无效 (NaN) — 这些行会被 Stage 1 的
      dropna(subset=[HR_mean,Resp_mean], how='all') 当作离床剔除。
    - BPM=0（grace 清零 / 未收敛）视为无效。
    - 越界值（HR_MIN..HR_MAX / RESP_MIN..RESP_MAX）视为无效。
    - Move (体动强度_%) 全程保留，sleep_staging 对缺失自动填 0。
    """
    raw = None
    for enc in ("utf-8-sig", "utf-8", "gbk", "gb2312"):
        try:
            raw = pd.read_csv(csv_path, encoding=enc)
            if COL_TIME in raw.columns:
                break
        except Exception:
            raw = None
    if raw is None or COL_TIME not in raw.columns:
        raise ValueError(f"无法读取 CSV 或缺少「{COL_TIME}」列: {csv_path}")

    needed = [COL_TIME, COL_PRESENT, COL_HR, COL_RESP, COL_MOVE_PCT]
    missing = [c for c in needed if c not in raw.columns]
    if missing:
        raise ValueError(f"CSV 缺少列: {missing}；实际列: {list(raw.columns)}")

    ts_idx = pd.to_datetime(raw[COL_TIME], errors="coerce")
    keep = ts_idx.notna()
    raw = raw.loc[keep].copy()
    ts_idx = ts_idx[keep]

    present = pd.to_numeric(raw[COL_PRESENT], errors="coerce").fillna(0).astype(int)
    hr   = pd.to_numeric(raw[COL_HR],       errors="coerce")
    resp = pd.to_numeric(raw[COL_RESP],     errors="coerce")
    move = pd.to_numeric(raw[COL_MOVE_PCT], errors="coerce").fillna(0.0)

    # 无人 → HR/Resp NaN（当作离床）
    hr   = hr.where(present.values == 1)
    resp = resp.where(present.values == 1)

    # BPM=0 视为无效（grace 清零 / 冷启动未收敛）
    hr   = hr.where(hr > 0)
    resp = resp.where(resp > 0)

    # 生理合理范围外 → NaN
    hr   = hr.where((hr   >= gem.HR_MIN)   & (hr   <= gem.HR_MAX))
    resp = resp.where((resp >= gem.RESP_MIN) & (resp <= gem.RESP_MAX))

    out = pd.DataFrame(
        {"HR": hr.values, "Resp": resp.values, "Move": move.values},
        index=pd.DatetimeIndex(ts_idx.values),
    )
    out = out[~out.index.duplicated(keep="last")].sort_index()

    if out.dropna(subset=["HR", "Resp"], how="all").empty:
        raise ValueError(
            "HR/Resp 全部无效：录制期间可能始终无人，或 BPM 从未收敛。"
        )

    return out


def run(csv_path: str,
        out_root: Optional[str] = None,
        generate_png: bool = True) -> gem.RunOutputs:
    """跑完整管线：宽表 → Stage 1(resample) → Stage 2..5 + 产出。"""
    if not os.path.isfile(csv_path):
        raise FileNotFoundError(f"CSV not found: '{csv_path}'")

    paths = gem.build_output_paths(csv_path, out_root)
    print(f"\n[sleep_analyze] Processing: {csv_path}")

    result = gem.StagingResult()

    print("[1/5] 宽表解析 + 重采样...")
    ts = wide_csv_to_ts(csv_path)
    gaps = gem.detect_gaps(ts)
    clean_df = gem.resample_and_clean(ts, gaps)
    result.resampled_df = clean_df
    result.gaps = gaps
    for g in gaps:
        blocked = "BLOCKED" if g.duration_minutes > gem.GAP_BLOCK_MIN else "interpolated"
        result.warnings.append(
            f"数据空白 {g.duration_minutes:.0f} min "
            f"({g.start.strftime('%H:%M')} -> {g.end.strftime('%H:%M')}) - {blocked}"
        )

    print("[2/5] 特征 + 5D Wake Index...")
    feat_df = gem.compute_features(clean_df)
    result.features_df = feat_df

    print("[3/5] 入睡 / 醒来边界...")
    onset = gem.find_sleep_onset(feat_df, result)
    wakeup = gem.find_wakeup(feat_df, onset, result)
    if onset >= wakeup:
        result.warnings.append("入睡晚于醒来，整段视为清醒")
        onset = feat_df.index[0]
        wakeup = feat_df.index[0]
    result.onset_time = onset
    result.wakeup_time = wakeup

    print("[4/5] 每分钟分期...")
    raw_stages = gem.classify_stages(feat_df, onset, wakeup)

    print("[5/5] 后处理 + 输出...")
    smoothed = gem.apply_median_filter(raw_stages)
    final = gem.enforce_boundary_awake(smoothed, onset, wakeup)
    feat_df["stage"] = final
    result.staged_df = feat_df
    gem.compute_statistics(result)
    result.insights = gem.generate_insights(result)
    gem.print_console_report(result)

    Path(paths.out_dir).mkdir(parents=True, exist_ok=True)
    gem.save_csv_report(result, paths.csv_path)
    gem.save_json_report(result, paths.json_path)

    outputs = gem.RunOutputs(result=result, paths=paths)
    if generate_png:
        import matplotlib.pyplot as plt
        if str(plt.get_backend()).lower() != "agg":
            plt.switch_backend("Agg")
        gem.plot_hypnogram(result, paths.png_path)
        outputs.png_generated = True

    return outputs


def _parse_args(argv=None):
    p = argparse.ArgumentParser(
        description="把 radar_panel.py 录制的宽表 CSV 分析为睡眠分期报告")
    p.add_argument("csv_path", help="宽表 CSV 路径")
    p.add_argument("--out-dir", dest="out_dir",
                   help="输出根目录（默认与 CSV 同目录），结果放到 <out-dir>/<csv_stem>/")
    p.add_argument("--no-png", action="store_true", help="不生成 hypnogram.png")
    return p.parse_args(argv)


def main(argv=None):
    args = _parse_args(argv)
    out = run(args.csv_path, out_root=args.out_dir, generate_png=not args.no_png)
    gem.print_run_summary(out)
    return out


if __name__ == "__main__":
    try:
        main()
    except (FileNotFoundError, ValueError) as exc:
        print(f"\n[FATAL] {exc}", file=sys.stderr)
        sys.exit(2)
    except Exception as exc:
        print(f"\n[UNEXPECTED] {exc}", file=sys.stderr)
        import traceback
        traceback.print_exc()
        sys.exit(3)
