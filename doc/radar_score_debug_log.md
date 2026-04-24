# Radar Score 调试日志采集

躺卧丢人问题的结构化数据采集档案。每次测试把串口原文粘到对应场景段，**不要删旧数据**，累积起来做对比。

## 0. 采集方法

- 固件开关：[my_lidar_inf.c](../components/my_lidar_inf/my_lidar_inf.c) 头部的 `#define RADAR_DEBUG_SCORE_LOG (1)` 已开启
- 采完一轮测试后把该宏改回 `0` 关掉
- 串口工具任意（GUI 的日志黑框、`idf.py monitor`、MobaXterm、TeraTerm 都行）
- 采集时**同时**勾选串口工具的保存到文件 + 打开 GUI 的 CSV 录制，两份数据对照

### 日志格式速查

```
ScoreDbg anchor=<bin> age=<frames> P=<power> staticEMA=<val>
         narrow=<0..1> posW=<0..1.3> statG=<0..1> burstG=<0..1> score=<final>
```
每 1 秒一行，**仅锚点 bin**。能看出 score 各分量此刻在哪个量级。

```
LockDbg top_score=<val> threshold=<val> latched=Y/N low_frames=<n>
        bin=<tracked_bin> anchor=<anchor_bin> age=<frames>
```
每 1 秒一行。看 top_score 有没有过阈、当前锁不锁、连续低分了多少帧（到 30 就 release）。

```
Radar: detected=yes/no confidence=... distance=...cm breath=...bpm heart=...bpm
       rbm=Y/N bodyMov=...% T=... RH=... lux=...
```
1Hz 业务数据（原有）。

### 阈值参考

- acquire threshold = **1e-4**（未锁定时要过这个线才能锁）
- 
- maintain threshold = **1e-6**（已锁定时低于这个且持续 3 秒才解锁）
- anchor 需连续稳定 30 帧（3 秒）才激活 `posW` 奖励

---

## 1. 测试矩阵

按顺序跑，中间**不重启固件**。每场 3 分钟。雷达位置 / 角度 / 距离全程不变，测试前拍一张摆位照（存 `doc/photos/` 或本地均可）。

| # | 场景 | 预期 | 采集重点 |
|---|------|------|---------|
| 1 | 空房 | detected=no, top_score<1e-6 | 噪声底基线 |
| 2 | 坐姿 1m 正对 | detected=yes, top_score>1e-3 | 正例基线 |
| 3 | 走过去坐/躺的过渡 | 波动大 | 锚点会不会切换错 |
| 4 | **仰卧静止，正对雷达** | ← 核心失败场景 | score 各分量谁跳水 |
| 5 | 仰卧静止，侧对雷达 | RCS 最差 | 极端情况 |
| 6 | 仰卧 + 被子覆盖 | 信号衰减 | 实际睡眠场景 |
| 7 | 翻身 / 小动作 | 能否重新捕获 | 从 release 恢复到 latch 的时间 |
| 8 | 离开房间 | 30s 内 detected=no | release 时机 |

### 采集辅助

每场开始前对串口日志**大声报一句"时间戳 + 场景号"**，比如 "22:05:00 场景4 仰卧开始"。串口原文里会被抓到（ESP 打不出来但会有时间戳 gap + 便于人工对齐 CSV）。或者在 GUI 日志黑框里用"清空"按钮分段。

---

## 2. 采集记录

### 场景 1 — 空房

**摆位**：（补）  
**时间**：~70s 采集窗口  
**主观观察**：空房无人，全程无预期信号

**分析要点**：
- 稳态 `top_score` 中位数 ~10⁻¹²，最低 4.6×10⁻¹⁴
- 全段最大尖峰 6.9×10⁻⁶（单次偶发，仍低于 acquire 1e-4）
- **噪声底比 acquire 阈值低 6 个量级** → acquire 阈值余量充足，后续若需要可下调
- **异常 A（冷启动误检）**：t=6.7s–13.4s 期间 `detected=yes` @ 56.2cm，原因是 warmup 前 4 秒 score 门控未生效，其他特征路径把 latched 拉起来后需要 3s release grace
- **异常 B（锚点陈旧）**：`anchor=16` 从头保留到尾；`age=29<30` 未激活，但数值仍被保留，下次有人从不同距离进入可能把 posW 偏到废锚点

**原始日志**：

```
I (4463) radar_task: Radar: detected=no confidence=0.20 distance=0.0cm breath=0.0bpm heart=0.0bpm rbm=N bodyMov=0% T=27.1 RH=56.5 lux=1.4
...（首次误检段）
I (6733) radar_task: Radar: detected=yes confidence=1.00 distance=56.2cm ...
I (8525) radar_task: LockDbg top_score=3.313e-08 threshold=1.000e-06 latched=Y low_frames=0 bin=16 anchor=-1 age=0
I (10153) radar_task: LockDbg top_score=1.002e-08 threshold=1.000e-06 latched=Y low_frames=10 bin=16 anchor=16 age=10
I (11795) radar_task: LockDbg top_score=3.602e-09 threshold=1.000e-06 latched=Y low_frames=20 bin=16 anchor=16 age=20
I (13424) radar_task: LockDbg top_score=1.512e-09 threshold=1.000e-04 latched=N low_frames=0 bin=16 anchor=16 age=29
I (13589) radar_task: Radar: detected=no ...
...（后续稳态）
I (34534) radar_task: LockDbg top_score=9.203e-13 threshold=1.000e-04 latched=N low_frames=0 bin=11 anchor=16 age=29
I (44266) radar_task: LockDbg top_score=4.566e-14 threshold=1.000e-04 latched=N low_frames=0 bin=14 anchor=16 age=29
I (75121) radar_task: LockDbg top_score=6.927e-06 threshold=1.000e-04 latched=N low_frames=0 bin=15 anchor=16 age=29   ← 偶发尖峰
```

（完整原始日志见本次测试会话记录）

---

### 场景 2 — 坐姿 1m 正对

**摆位**：人正对雷达盘坐，距离约 80cm（bin 21-22）  
**时间**：~30s 采集段（age 90–340）  
**主观观察**：detected=yes 稳定，distance 在 74.9–86.2cm 之间漂移

**关键数据（代表性 ScoreDbg 采样）**：

| age | P | staticEMA | narrow | statG | score | top_score |
|---|---|---|---|---|---|---|
| 90  | 6.8e-02 | 27.1 | 0.36 | 0.250 | 7.9e-03 | 6.0e-03 |
| 130 | 5.4e-03 | 27.8 | 0.05 | 0.019 | 6.8e-06 | 1.6e-02 |
| 170 | 2.9e-03 | 32.7 | 0.02 | 0.009 | 6.0e-07 | - |
| 180 | 1.9e-02 | 32.9 | 0.06 | 0.057 | 8.7e-05 | 6.8e-02 |
| 220 | 2.2e-03 | 33.9 | 0.07 | 0.007 | 1.4e-06 | 7.0e-04 |

**分析**：
1. **statG 在坐姿已压制 10–200×**：static_gate 对坐姿人本身就不友好，不只是躺卧问题
2. **narrow 基本失效**（0.01–0.2）：胸腔实际占 3–5 个 bin，单 bin 孤峰假设不成立，反而压分
3. **top_score ≠ ScoreDbg.score**：tracker 常选到锚点邻近 bin（22/23），锚点的 posW 奖励白加
4. **score 低点已触底 6.8e-06**：呼吸周期中距 maintain_th=1e-6 只差一个量级；躺卧外推预计 10⁻⁷，会跌破 maintain
5. **acquire_th 1e-4 对躺卧几乎不可能过**：一旦掉锁就回不来

**附带发现**（后续另立问题）：
- 呼吸 BPM 读数 29.9（真实 15–20，FFT 疑似谐波当基频）
- RBM 阈值 2.0 rad 导致静坐深呼吸频繁 `Vitals skipped`
- Heart outlier rejection 工作，但 median=75.9 固定，streak 累积

**原始日志节选**：

```
I (23302) ScoreDbg anchor=21 age=90  P=6.779e-02 staticEMA=27.10 narrow=0.36 posW=1.30 statG=0.250 burstG=1.00 score=7.914e-03
I (24944) ScoreDbg anchor=21 age=100 P=4.103e-02 staticEMA=26.65 narrow=0.38 posW=1.30 statG=0.154 burstG=1.00 score=3.084e-03
I (26598) ScoreDbg anchor=21 age=110 P=2.101e-02 staticEMA=26.83 narrow=0.36 posW=1.30 statG=0.078 burstG=1.00 score=7.614e-04
I (28240) ScoreDbg anchor=21 age=120 P=9.966e-03 staticEMA=27.10 narrow=0.19 posW=1.30 statG=0.037 burstG=1.00 score=9.043e-05
I (29894) ScoreDbg anchor=21 age=130 P=5.409e-03 staticEMA=27.78 narrow=0.05 posW=1.30 statG=0.019 burstG=1.00 score=6.788e-06
I (31536) ScoreDbg anchor=21 age=140 P=2.490e-03 staticEMA=29.35 narrow=0.02 posW=1.30 statG=0.008 burstG=1.00 score=5.472e-07
I (33190) ScoreDbg anchor=21 age=150 P=1.538e-03 staticEMA=31.24 narrow=0.01 posW=1.30 statG=0.005 burstG=1.00 score=1.278e-07
I (38129) ScoreDbg anchor=21 age=180 P=1.870e-02 staticEMA=32.86 narrow=0.06 posW=1.30 statG=0.057 burstG=1.00 score=8.737e-05
I (52974) ScoreDbg anchor=21 age=270 P=1.756e-02 staticEMA=39.44 narrow=0.12 posW=1.30 statG=0.045 burstG=0.89 score=1.069e-04
I (56272) ScoreDbg anchor=21 age=290 P=2.108e-02 staticEMA=41.04 narrow=0.11 posW=1.30 statG=0.051 burstG=1.00 score=1.496e-04
I (62868) ScoreDbg anchor=21 age=330 P=4.527e-03 staticEMA=40.74 narrow=0.01 posW=1.30 statG=0.011 burstG=1.00 score=7.842e-07
```

---

### 场景 3 — 走过去 sit→lie 过渡

**摆位**：  
**时间**：  
**主观观察**：

```

```

---

### 场景 4 — 仰卧静止，正对雷达 ★核心失败场景

**摆位**：仰卧距雷达 70–80cm，anchor bin 22（约 82cm）  
**时间**：~70s 采集段（age 30–362）  
**主观观察**：**全程 detected=yes 未丢失**（结论与预期相反）

**关键数据（ScoreDbg 代表性采样）**：

| age | P | staticEMA | narrow | statG | score | top_score | 备注 |
|---|---|---|---|---|---|---|---|
| 30  | 1.15e-02 | 15.4 | 0.44 | 0.075 | 4.9e-04 | 4.9e-04 | 入场初期 |
| 86  | 2.53e-03 | 40.2 | 0.44 | 0.006 | 9.2e-06 | 7.8e-05 | |
| 136 | 1.06e-02 | 33.4 | 0.61 | 0.032 | 2.7e-04 | 2.7e-04 | narrow 峰值 |
| 156 | 2.32e-03 | 40.1 | 0.57 | 0.006 | 9.9e-06 | 1.4e-05 | |
| 256 | 9.55e-02 | 33.3 | 0.66 | 0.287 | 1.0e-02 | 1.0e-02 | 深呼吸峰值 |
| 306 | 1.36e-03 | 57.4 | 0.44 | 0.002 | **1.8e-06** | 4.1e-05 | ★最低点 |
| 362 | 1.64e-03 | 42.5 | 0.15 | 0.004 | 1.3e-06 | 7.5e-04 | |

**核心发现**：
1. **躺卧 narrow 比坐姿高**（0.6 vs 0.05）：胸腔垂直起伏在 range 方向扩散更小，narrow 不是元凶
2. **score 最低点 1.8e-6，仅比 maintain_th 高 1.8×**：稳态能挂住，但偶发波谷（深吸、屏息、翻身）必击穿
3. **acquire 1e-4 完全过不了**：ScoreDbg 最高 1e-2，只在深呼吸瞬间过线；丢锁后再也回不来
4. **statG 常态 0.002–0.03**：坐姿/躺卧都被压制 30–500×，公式偏严

**呼吸/心率读数**：
- breath_bpm: 18.9→25.2（真实 12–18，偏高 ~30%，谐波问题）
- heart_bpm: 79.4→97.0 漂移，outlier rejected median 锁死

**原始日志节选**：

```
I (13424) ScoreDbg anchor=22 age=30  P=1.151e-02 staticEMA=15.44 narrow=0.44 posW=1.30 statG=0.075 burstG=1.00 score=4.936e-04
I (26570) ScoreDbg anchor=22 age=76  P=4.046e-03 staticEMA=35.37 narrow=0.53 posW=1.30 statG=0.011 burstG=1.00 score=3.181e-05
I (28212) ScoreDbg anchor=22 age=86  P=2.532e-03 staticEMA=40.22 narrow=0.44 posW=1.30 statG=0.006 burstG=1.00 score=9.169e-06
I (36459) ScoreDbg anchor=22 age=136 P=1.064e-02 staticEMA=33.42 narrow=0.61 posW=1.30 statG=0.032 burstG=1.00 score=2.686e-04
I (39756) ScoreDbg anchor=22 age=156 P=2.318e-03 staticEMA=40.14 narrow=0.57 posW=1.30 statG=0.006 burstG=1.00 score=9.901e-06
I (56243) ScoreDbg anchor=22 age=256 P=9.546e-02 staticEMA=33.25 narrow=0.66 posW=1.30 statG=0.287 burstG=0.44 score=1.036e-02
I (62837) ScoreDbg anchor=22 age=296 P=2.814e-03 staticEMA=54.19 narrow=0.54 posW=1.30 statG=0.005 burstG=1.00 score=1.028e-05
I (64479) ScoreDbg anchor=22 age=306 P=1.357e-03 staticEMA=57.42 narrow=0.44 posW=1.30 statG=0.002 burstG=1.00 score=1.818e-06   ← 最低
I (74371) ScoreDbg anchor=22 age=362 P=1.638e-03 staticEMA=42.48 narrow=0.15 posW=1.30 statG=0.004 burstG=1.00 score=1.266e-06
```

**结论**：本轮采集已够，场景 5–8 可以先不做。修改方案见 §3.2 根因定位。

---

### 场景 5 — 仰卧静止，侧对雷达

**摆位**：  
**时间**：  
**主观观察**：

```

```

---

### 场景 6 — 仰卧 + 被子覆盖

**摆位**：  
**时间**：  
**被子厚度**：  
**主观观察**：

```

```

---

### 场景 7 — 翻身 / 小动作

**摆位**：  
**时间**：  
**主观观察**：

```

```

---

### 场景 8 — 离开房间

**摆位**：  
**时间**：  
**主观观察**：（离开后多少秒 detected 翻 no）

```

```

---

## 3. 分析清单（采完填）

### 3.1 score 量级对照表

| 场景 | top_score 均值 | top_score 最低 | P 均值 | statG 均值 | narrow 均值 | 备注 |
|------|------|------|------|------|------|------|
| 1 空房 | | | | | | |
| 2 坐姿 | | | | | | |
| 4 仰卧 | | | | | | |
| 5 侧躺 | | | | | | |
| 6 被子 | | | | | | |

### 3.2 根因定位

躺卧场景（4/5/6）相对坐姿（2），哪个分量掉得最狠？

- [ ] `P`（带通功率）掉 >10× → 信号确实弱，需要加大增益或换距离 bin
- [ ] `statG`（静态门控）掉 <0.1 → **主要元凶**，锚点邻域豁免方案（A）可解
- [ ] `narrow`（窄瘦度）掉 <0.5 → 目标散开到多个 bin，考虑合并能量
- [ ] `posW`（锚点奖励）= 1.0 说明锚点没激活 → 检查 `age` 是否够 30
- [ ] `burstG` 异常低 → 突发门控误伤

### 3.3 锁定状态追踪

| 场景 | latched 翻 N 的时刻 | 离开起多少秒翻 no | 再进入多久重新 latched |
|------|------|------|------|
| 4→7 | | | |
| 8 | - | | - |

---

## 4. 改动后回测

采完一轮得到基线后，实施改动（比如锚点豁免 static_gate）再跑一次场景 4/5/6/7，两次数据对比：

| 指标 | 改前 | 改后 |
|---|---|---|
| 场景 4 top_score 均值 | | |
| 场景 4 失锁次数 | | |
| 场景 7 重捕获时间 | | |
| 场景 2 坐姿是否受影响 | | |

---

## 5. 关闭调试

全部场景采完 + 分析完 + 决策完改动方向后：

1. 把 [my_lidar_inf.c](../components/my_lidar_inf/my_lidar_inf.c) 顶部 `RADAR_DEBUG_SCORE_LOG` 改回 `0`
2. 重新编译烧录，确认 `Radar:` 行正常但没有 ScoreDbg/LockDbg
3. 本文档**归档保留**，作为之后回归测试基线
