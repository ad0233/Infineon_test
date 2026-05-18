# EVT — 雷达 vitals 算法基线

> Status: 🟡 needs-review · Last reviewed: 2026-04-11 · 2026-05-10 算法翻新（谐波 HR + Doppler RBM）后参数表需重核
>
> 项目：BY001-ESP32-P4 + BGT60TR13C 雷达
> 分支：feat_lidar_inf

---

## 一、硬件配置

| 参数 | 值 | 宏/位置 |
|------|-----|---------|
| 频率范围 | 59-63 GHz, BW=4GHz | `RADAR_PROFILE_START_FREQ_HZ` / `BANDWIDTH_HZ` |
| ADC 采样率 | 1 MHz | `RADAR_PROFILE_ADC_DIV=80` |
| 每 chirp 采样 | 128 | `RADAR_PROFILE_NUM_SAMPLES_PER_CHIRP` |
| 每帧 chirp | 64 | `RADAR_PROFILE_NUM_CHIRPS_PER_FRAME` |
| RX 天线 | 3（仅用 RX1） | `RADAR_MAIN_RX_IDX=1` |
| IF 增益 | 43 dB (VGA=5) | `RADAR_PROFILE_VGA_GAIN_RX1` |
| 帧率 | 10 Hz | `RADAR_PRESENCE_FRAME_RATE_HZ` |
| SPI 频率 | 20 MHz | `XENSIV_BGT60TRXX_SPI_FREQUENCY` |
| FIFO | 6片×4096 = 24576样本/帧 | `FIFO_SLICE_SAMPLES` |
| 距离分辨率 | 3.75 cm/bin | c / (2×BW) |

---

## 二、距离检测 & 目标跟踪

| 参数 | 值 | 说明 |
|------|-----|------|
| 有效起点 | bin 10 = 37.5cm | `RADAR_DISTANCE_FIRST_VALID_BIN=10` |
| 信号阈值 | -5.0 dB | `RADAR_DISTANCE_THRESHOLD_DB` |
| 局部搜索半径 | ±3 bin | `LOCAL_RADIUS_BINS=3` |
| 切换比 | 1.25× | `SWITCH_RATIO=1.25` |
| 步进限制 | 2 bin/帧 | `MAX_STEP_BINS=2` |
| 直接跳转 | 能量>2×且距离≤10bin | 防止跳到远处杂波 |
| 跟踪历史 | 5帧中值 | `TRACK_HISTORY_LEN=5` |
| 无人冻结 | presence=no 时停止更新 | 防止 bin 飘到远处 |
| 回来重置 | no→yes 清空 history | 重新锁定目标 |

---

## 三、人存检测（Presence）

### 4 指标融合

| 指标 | 窗口 | 阈值 | 权重 | 物理意义 |
|------|------|------|------|----------|
| phExc 相位偏移 | 6s | ≥0.08mm | +0.25 | 胸壁位移 |
| ampCV 振幅CV | 5s | ≤0.35 | +0.10 | 信号稳定 |
| binSpan bin跨度 | 5s | ≤2.0 | +0.10 | 位置稳定 |
| breathPk 呼吸峰 | 6s | ≥2峰,高>0.03 | +0.55 | 呼吸周期 |

### 判定逻辑

```
raw_present = (confidence ≥ 0.80)
           OR (breathPk AND phExc)
           OR (phExc AND binSpan)

锁存: raw=true → latched=true
      raw=false → misses++, 连续3帧 → latched=false
```

### 行为

| 事件 | 响应 |
|------|------|
| 人到达 | ~2s 检出 |
| 人离开 | ~1s 判 no |
| 无人时 | distance=0, breath=0, heart=0（立即清零）|
| 无人时 bin | 冻结在最后有人位置 |
| 人回来 | 清空 bin history, 重新锁定 |

---

## 四、呼吸率检测

### 信号路径

```
phase_history[200] → 去均值 → 差分 → clip(±0.30) → 7点平滑
→ filtfilt BPF (HPF@0.1Hz + LPF@0.6Hz)
→ FFT 1024点 → ROI 0.20-0.50Hz → 抛物线插值 → 3点中值
```

### 参数

| 参数 | 值 |
|------|-----|
| 有效范围 | **12.0 ~ 30.0 BPM** |
| BPF | HPF@0.1Hz + LPF@0.6Hz, 2级biquad |
| FFT | 1024点, 分辨率0.00977Hz |
| 估算间隔 | 20s (200帧) |
| 平滑 | 3点中值 |
| 实时波形 | 每帧因果IIR 2级, 10Hz |

---

## 五、心率检测

### 信号路径（独立，无7点平滑）

```
phase_history[200] → 去均值 → 差分 → clip(±0.30)（不平滑）
→ filtfilt BPF (HPF@0.85Hz + LPF@2.2Hz + BPF@1.525Hz)
→ 去呼吸谐波 (notch Q=30 -40dB, 2~8次)
→ FFT 1024点 → ROI 0.85-2.20Hz → 抛物线插值
→ 离群拒绝(前3次不拒绝, 之后±25BPM) → 5点中值
```

### 参数

| 参数 | 值 |
|------|-----|
| 有效范围 | **51.0 ~ 132.0 BPM** |
| BPF | HPF@0.85Hz + LPF@2.2Hz + BPF@1.525Hz, 3级biquad |
| 谐波消除 | 呼吸×2~×8, notch Q=30, -40dB |
| 离群拒绝 | 前3次不拒绝, 之后±25BPM |
| 平滑 | 5点中值 |
| 实时波形 | 每帧因果IIR 3级, 10Hz |
| FFT twiddle | deinit+init(1024), 防sensor-dsp抢占 |

---

## 六、体动检测（RBM）

### 独立1s窗口（不共用presence的5s窗口）

| 参数 | 值 | 说明 |
|------|-----|------|
| ampCV1s 窗口 | 10帧=1s | 独立环形缓冲 |
| binSpan1s 窗口 | 10帧=1s | 独立环形缓冲 |
| ampCV1s 阈值 | **>0.38** | 校准: 静坐max=0.353, 运动min=0.413 |
| binSpan1s 阈值 | **≥4 bins** | 15cm位移 |
| bodyMov 输出 | 0-100% | (ampCV1s-0.38)/(0.92-0.38)×100 |
| 检测延迟 | ~0.5s | |
| 恢复延迟 | ~1s | |

### Vitals 门控

| 条件 | 行为 |
|------|------|
| rbm_ratio ≤ 15% | 正常估算BPM |
| rbm_ratio > 15% | 跳过估算, 保留上次结果 |
| presence=no | 立即清零BPM, 不等20s周期 |

### 频谱基线（保留供Phase 2）

```
baseline[64] IIR α=0.05 始终更新
move_raw = Σ|cur-baseline|/N (目标bin±5)
```

---

## 七、日志格式

### 1Hz 综合日志

```
Radar: detected=yes bin=12 level=3.9dB movement=0.371 confidence=1.00
       distance=45.0cm breath=11.7bpm heart=87.2bpm frame=400
       phExc=3.6mm(Y) ampCV=0.31(Y) binSpan=2.0(Y) breathPk=Y
       rbm=N bodyMov=0% ampCV1s=0.054
```

| 字段 | 含义 | 更新频率 |
|------|------|----------|
| detected | 人存在 yes/no | 每帧 |
| bin / distance | 目标bin和距离 | 每帧 |
| level | FFT信号强度dB | 每帧 |
| confidence | 人存置信度 0-1 | 每帧 |
| breath / heart | BPM (0=未检测) | 每20s / 无人立即归零 |
| phExc(Y/N) | 相位偏移mm + 判定 | 每帧 |
| ampCV(Y/N) | 5s振幅CV + 判定 | 每帧 |
| binSpan(Y/N) | 5s bin跨度 + 判定 | 每帧 |
| breathPk | 呼吸峰检出 | 每帧 |
| rbm | 体动标记 Y/N | 每帧 |
| bodyMov | 体动强度 0-100% | 每帧 |
| ampCV1s | 1s振幅CV(体动用) | 每帧 |

---

## 八、待优化

### 高优先

| # | 功能 | 说明 |
|---|------|------|
| 1 | 心率下限扩展 | 51→42 BPM, 覆盖深睡/运动员 |
| 2 | 信号质量指数(SQI) | 判断vitals窗口数据可信度 |
| 3 | 自适应窗口 | 剔除体动段后缩窗估算 |

### 中优先

| # | 功能 | 说明 |
|---|------|------|
| 4 | 3RX侧向检测(Phase 2) | 启用多天线相位差, 检测左右运动 |
| 5 | 多Bin融合(MRC) | 相位对齐加权, 提升SNR |
| 6 | LMS自适应谐波消除 | 替代固定Notch Q=30 |
| 7 | 呼吸暂停检测 | 基于现有波形连续性分析 |
| 8 | SPI提速到25MHz | 切换PLL时钟源, 减少偶发FIFO溢出 |

### 低优先 / 长期

| # | 功能 | 说明 |
|---|------|------|
| 9 | 多天线波束成形 | 角度估计, 多目标 |
| 10 | 抗混叠 | 帧率提高到20Hz |
| 11 | HRV | 需帧率≥50Hz, 当前硬件不可行 |
| 12 | 睡眠分期 | 依赖HRV |

---

## 九、校准记录

### 部署参数（2026-04-11, 距离45-68cm）

| 指标 | 静坐值域 | 运动值域 | 阈值 |
|------|----------|----------|------|
| ampCV1s | 0.05-0.353 | 0.413-0.920 | **0.38** |
| binSpan1s | 0-2 | 4-9 | **4** |
| ampCV(5s) | 0.15-0.35 | 0.45-1.63 | 0.35(presence) |
| phExc | 0.001mm(空场) | 1.0-7.0mm(有人) | 0.08mm(presence) |
