---
name: radar-vitals
description: 分析或调优 ESP32 BGT60TR13C 雷达的生命体征检测（呼吸率、心率）。当用户讨论 vitals 参数调优、波形异常、BPM 不准、滤波器配置时触发。
---

# Radar Vitals — ESP32 生命体征检测

## 架构概览

生命体征检测在 `components/my_lidar_inf/my_lidar_inf.c` 中实现，基于相位信号提取胸壁位移。

### 数据流
```
SPI FIFO (6片×4096) → ADC 原始数据 → Range FFT (128点)
→ 目标 bin 选择（跟踪式峰值, ≥bin8=30cm）
→ 相位提取 atan2 → 解缠 → 循环缓冲区 (200帧, 20s @ 10Hz)
    ├─ 实时路径 (每帧 10Hz): 因果 IIR BPF → breath_wave / heart_wave
    └─ 估算路径 (每 200帧 20s): linearize → filtfilt → FFT 1024 → BPM
```

### 双路径设计

| 路径 | 频率 | 方法 | 用途 |
|------|------|------|------|
| 实时波形 | 10Hz/帧 | 因果 IIR biquad 级联 | 可视化、波形调试 |
| BPM 估算 | 0.05Hz/20s | filtfilt 零相位 + FFT | 呼吸率/心率数值 |

### 信号处理分支

- **呼吸路径**: 相位 → 去均值 → 差分 → clip(±0.30) → 7点平滑 → filtfilt BPF → FFT
- **心率路径**: 相位 → 去均值 → 差分 → clip(±0.30) → **不平滑** → filtfilt BPF → 去呼吸谐波 → FFT

心率不做平滑是因为 7 点均值在 10Hz 采样率下截止频率 ~1.4Hz，会严重衰减心率频段 (0.85-2.2Hz)。

### 核心函数
| 函数 | 作用 |
|------|------|
| `radar_vitals_init_filters()` | 初始化 BPF 系数 + FFT twiddle 表 (deinit+init 1024) |
| `radar_process_phase_signal()` | 去均值 → 差分裁剪 → 7点平滑（呼吸路径用） |
| `radar_filtfilt_biquad()` | 零相位 IIR 滤波（正向+反向 biquad 级联） |
| `radar_estimate_rate_hz()` | Blackman 窗 → 零填充 1024 → FFT → ROI 峰值 → 抛物线插值 |
| `radar_remove_breath_harmonics()` | 呼吸 2~8 次谐波 notch 去除（Q=30, -40dB） |
| `radar_estimate_vitals()` | 主编排：呼吸估算 → 心率信号(diff-clip无平滑) → 去谐波 → 心率估算 → 中值+离群拒绝 |
| `radar_linearize_float()` | 循环缓冲区线性化输出 |
| `radar_small_median()` | 小数组中值（n≤5，插入排序） |
| `radar_select_tracked_range_bin()` | 全局+局部峰值跟踪，步进限制 |
| `radar_update_presence()` 末尾 | 实时 IIR 波形 + vitals 触发调度 |

### 滤波器参数
| 滤波器 | 类型 | 频段 | BPM 范围 | 级数 |
|--------|------|------|----------|------|
| 呼吸 BPF | HPF@0.1Hz + LPF@0.6Hz | 0.1–0.6 Hz | 6–36 BPM | 2 级 biquad |
| 心率 BPF | HPF@0.85Hz + LPF@2.2Hz + BPF@1.525Hz | 0.85–2.2 Hz | 51–132 BPM | 3 级 biquad |
| 谐波 notch | notch Q=30, -40dB | 呼吸频率 ×2~×8 | 动态 | 每谐波 1 级 |

### FFT 参数
| 参数 | 值 | 说明 |
|------|-----|------|
| 输入长度 | 200 样本 | `RADAR_PRESENCE_HISTORY_LEN` |
| FFT 大小 | 1024 点 | 零填充, `RADAR_VITALS_FFT_SIZE` |
| 频率分辨率 | 0.00977 Hz/bin | fs(10Hz) / 1024 |
| 窗函数 | Blackman | 旁瓣抑制 |
| 呼吸 ROI | 0.20–0.50 Hz | bin 20–51, **12.0–30.0 BPM** |
| 心率 ROI | 0.85–2.20 Hz | bin 87–225, **51.0–132.0 BPM** |
| 峰值精化 | 3 点抛物线插值 | 亚 bin 精度 |

### 调参入口
| 参数 | 位置 | 当前值 | 影响 |
|------|------|--------|------|
| `RADAR_VITALS_INTERVAL_FRAMES` | 宏定义 | 200 (20s) | 估算间隔 |
| `RADAR_PRESENCE_HISTORY_LEN` | 宏定义 | 200 (20s) | 历史/FFT 输入长度 |
| `RADAR_PHASE_DIFF_CLIP` | 宏定义 | 0.30f | 相位差分裁剪阈值 |
| `RADAR_PHASE_SMOOTH_LEN` | 宏定义 | 7 | 呼吸路径平滑窗口 |
| `RADAR_VITALS_FFT_SIZE` | 宏定义 | 1024 | FFT 零填充目标大小 |
| BPF 截止频率 | `radar_vitals_init_filters()` | 见滤波器参数表 | 滤波频段 |
| 呼吸 FFT ROI | `radar_estimate_vitals()` | 0.20–0.50 Hz | 呼吸搜索范围 |
| 心率 FFT ROI | `radar_estimate_vitals()` | 0.85–2.20 Hz | 心率搜索范围 |
| 心率离群值拒绝 | `radar_estimate_vitals()` | 前 3 次不拒绝, 之后 ±25 BPM | 跳变抑制 |
| 呼吸中值窗口 | `breath_hz_history[3]` | 3 点 | 平滑度 |
| 心率中值窗口 | `heart_hz_history[5]` | 5 点 | 平滑度 |

### 输出与日志
```
Wave: breath=X.XXXXXX heart=X.XXXXXX frame=XXX           (每帧 10Hz, 实时波形)
Radar: detected=yes bin=XX level=X.XdB movement=X.XXX confidence=X.XX distance=XX.Xcm breath=XX.Xbpm heart=XX.Xbpm frame=XXX  (1Hz 综合)
Vitals raw: breath_hz=X.XXXX heart_hz=X.XXXX (XX.X/XX.X BPM)   (调试, 每 20s)
Vitals result: breath=XX.Xbpm heart=XX.Xbpm                     (调试, 每 20s)
Heart outlier rejected: XX.X vs median XX.X                      (警告, 触发时)
```

### 不显示 (输出 0) 的条件
- 无人 (`presence_detected = false`) → 清零 BPM 和历史
- 历史不足 200 帧 → 不执行估算
- FFT 峰值幅度 < 1e-12 → 返回 0 Hz
- 呼吸超出 12–30 BPM → breath = 0
- 心率超出 51–132 BPM → heart = 0
- 心率离群值 (≥3 次后偏离中值 >25 BPM) → 该次丢弃, 保持上次值

### FFT twiddle 表注意事项
`dsps_fft2r_init_fc32()` 是全局一次性初始化。`sensor-dsp` 库可能先以 64/128 初始化，导致 1024 点 FFT 越界。`radar_vitals_init_filters()` 中通过 `dsps_fft2r_deinit_fc32()` + `dsps_fft2r_init_fc32(NULL, 1024)` 强制重新分配。

### Presence 检测参数（已校准）
| 参数 | 值 | 说明 |
|------|-----|------|
| `FIRST_VALID_BIN` | 10 (37.5cm) | 排除近场杂波 |
| `SWITCH_RATIO` | 1.25 | bin 跟踪切换比 |
| `MAX_STEP_BINS` | 2 | 每帧最大 bin 步进 |
| `CONFIDENCE_TH` | 0.80 | 置信度门限（需 phExc + 其他指标） |
| `BREATH_WINDOW` | 6s | 呼吸峰检测窗口 |
| `MISS_LIMIT` | 3 帧 | 锁存容忍帧数 |
| 判定逻辑 | `conf≥0.80 OR (breath AND phase) OR (phase AND bin)` | breathPk 不再单独触发 |

### 体动检测 (RBM) — Phase 1 已实现
基于自适应基线的前向能量差分:
```
每帧: cur_spectrum[64] = |FFT(chirp平均)|
      diff = Σ|cur[b] - baseline[b]| / N  (目标 bin ±5 范围)
      baseline: 静止时 α=0.50 快速跟随, 运动时 α=0.01 近冻结
      noise_floor: 跟踪 raw 的滑动最小值
      bodyMov: 10帧平滑后, < noise×2.5 → 输出 0 (死区)
      rbm_ratio: 200帧窗口内体动帧占比, >15% → 跳过 vitals 估算
```

校准期: 前 30 帧 (3s) 强制学习基线和噪声底, bodyMov=0

日志: `rbm=Y/N(ratio%) bodyMov=X.XXXX raw=X.XXXX noise=X.XXXX`

Phase 2 (待实现): 启用 3RX 天线相位差, 检测侧向运动

### 已知限制与待优化
1. **侧向运动检测弱**: 仅用 RX1, 需启用 3RX 相位差 (Phase 2)
2. **心率下限 51 BPM**: 深睡/运动员可能 40-50 BPM → 计划降到 42 BPM
3. **固定 Notch 谐波消除**: Q=30 对呼吸频率波动敏感 → 未来考虑 LMS 自适应
4. **单 Bin 提取**: 未做多 Bin 相干融合 (MRC) → SNR 有提升空间
5. **无 HRV**: 10Hz 帧率时间精度不足 (±50ms) → 当前硬件不可行
6. **SPI 20MHz**: CLAUDE.md 要求 25MHz, 当前用 XTAL 限制 20MHz, 可能是偶发 FIFO 崩溃原因

### 常见问题排查
- **BPM 不变**: 检查 `Vitals raw:` 日志是否出现; FFT twiddle 表是否正确初始化为 1024
- **BPM 偏低卡死**: FFT ROI 下限过低会锁在噪声峰; 检查 ROI 范围
- **波形全零**: `vitals_filters_inited` 为 false, 或 `presence_detected` 一直为 false
- **栈溢出**: vitals 函数有大量局部数组, 确保 `RADAR_TASK_STACK_SIZE` ≥ `configMINIMAL_STACK_SIZE * 16`
- **心率被离群拒绝**: 查看 `Heart outlier rejected` 日志, 初始锁定值不准导致后续全拒绝 → 前 3 次不拒绝已修复
- **启动时 FIFO 崩溃**: 断电 3 秒冷启动恢复, 非代码问题 (SPI 电平转换器 TXS0108 + 20MHz 临界)
- **bodyMov 全零**: 检查 noise 是否过高 (校准期有运动), 重启保持静止 3 秒
- **bin 跟踪卡在远处**: 全局能量 >2× 局部时直接跳转, 无步进限制
