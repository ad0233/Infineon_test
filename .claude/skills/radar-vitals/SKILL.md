---
name: radar-vitals
description: 分析或调优 ESP32 BGT60TR13C 雷达的生命体征检测（呼吸率、心率）。当用户讨论 vitals 参数调优、波形异常、BPM 不准、滤波器配置时触发。
---

# Radar Vitals — ESP32 生命体征检测

## 架构概览

生命体征检测在 `components/my_lidar_inf/my_lidar_inf.c` 中实现，基于相位信号提取胸壁位移。

### 数据流
```
SPI FIFO → ADC 原始数据 → Range FFT → 目标 bin 选择 → 相位提取/解缠
→ 相位历史 (200帧循环缓冲区, 20s @ 10Hz)
→ 实时 IIR 滤波 → 呼吸波形 / 心率波形 (每帧输出)
→ 每 200 帧 filtfilt 零相位滤波 → FFT 频率估算 → BPM
```

### 核心函数
| 函数 | 作用 |
|------|------|
| `radar_vitals_init_filters()` | 初始化 BPF 系数（呼吸 HPF+LPF 2级, 心率 HPF+LPF+BPF 3级） |
| `radar_filtfilt_biquad()` | 零相位 IIR 滤波（正向+反向 biquad） |
| `radar_estimate_rate_hz()` | FFT 频率估算：Blackman 窗 + 1024 零填充 + 抛物线插值 |
| `radar_remove_breath_harmonics()` | 呼吸 2~8 次谐波 notch 去除（Q=30, -40dB） |
| `radar_estimate_vitals()` | 主编排：BPF → 呼吸率 → 去谐波 → 心率 → 中值平滑 |
| `radar_update_presence()` 末尾 | 实时 IIR 波形更新 + vitals 触发 |

### 滤波器参数
| 滤波器 | 类型 | 频段 | 级数 |
|--------|------|------|------|
| 呼吸 BPF | HPF@0.1Hz + LPF@0.6Hz | 6–36 BPM | 2 级 biquad |
| 心率 BPF | HPF@0.85Hz + LPF@2.2Hz + BPF@1.525Hz | 51–132 BPM | 3 级 biquad |
| 谐波 notch | notch Q=30 | 呼吸频率 ×2~×8 | 动态 |

### 调参入口
| 参数 | 位置 | 当前值 | 影响 |
|------|------|--------|------|
| `RADAR_VITALS_INTERVAL_FRAMES` | 宏定义 | 200 | 估算间隔（帧数） |
| `RADAR_PRESENCE_HISTORY_LEN` | 宏定义 | 200 | 历史缓冲区长度 |
| `RADAR_PHASE_DIFF_CLIP` | 宏定义 | 0.30f | 相位差分裁剪阈值 |
| `RADAR_PHASE_SMOOTH_LEN` | 宏定义 | 7 | 平滑窗口长度 |
| BPF 截止频率 | `radar_vitals_init_filters()` | 见上表 | 滤波频段 |
| FFT ROI | `radar_estimate_vitals()` 调用参数 | 呼吸 0.15–0.45Hz, 心率 0.85–2.2Hz | 频率搜索范围 |
| 心率离群值拒绝 | `radar_estimate_vitals()` | ±12 BPM | 跳变抑制 |
| 呼吸中值窗口 | `breath_hz_history[3]` | 3 点 | 平滑度 |
| 心率中值窗口 | `heart_hz_history[5]` | 5 点 | 平滑度 |

### 日志格式
```
Wave: breath=X.XXXXXX heart=X.XXXXXX frame=XXX     (每帧 10Hz, 实时波形)
Radar: detected=yes bin=XX level=X.XdB ... breath=XX.Xbpm heart=XX.Xbpm frame=XXX  (1Hz 综合)
```

### 常见问题排查
- **BPM 不变**：检查 `vitals_frame_counter` 是否达到阈值，`presence_detected` 是否为 true
- **BPM 偏低**：FFT ROI 下限可能过低，或呼吸谐波未被正确去除
- **波形全零**：`vitals_filters_inited` 为 false，或相位解缠异常
- **栈溢出**：vitals 函数有大量局部数组，确保 `RADAR_TASK_STACK_SIZE` 足够大
