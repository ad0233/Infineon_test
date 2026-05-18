# Decisions Log

> Status: 🟢 current · Last reviewed: 2026-05-18
>
> 按时间倒序。每条 3-5 行，回答**做了什么 / 备选 / 为什么**。防止 6 个月后又把同一件事讨论一遍。
>
> 未决项见 [open-questions.md](open-questions.md)。决策完毕的从 open-questions 剪贴过来。

格式：

```
## YYYY-MM-DD — 标题
**做了**：选 X。
**备选**：Y、Z。
**为什么**：……
**影响**：……（哪些文档 / 代码因此改了）
```

---

## 2026-05-18 — doc/ 按 Luna 风格 kebab-case 重构

**做了**：`doc/` 拆 `hardware/` + `progress/` + `standards/`，每硬件模块一份独立 md（7 模块），命名 kebab-case 参照 Luna `lunawake-device.md` 风格。

**备选**：① 维持 handover 编号制（03/04/05/06）；② 4 模块合薄 vs 7 模块完全切；③ 三方目录 vs 双方（不分 standards）。

**为什么**：① Luna 那边已经用 kebab-case，命名同源便于交叉引用；② 7 模块切分留扩展空间；③ standards 独立便于 Luna 也能复用 coding-standard。

**影响**：所有现存 md 都换位置；CLAUDE.md / AGENTS.md / 两个 SKILL.md / 两个 mempalace.yaml 全部更新路径；新建 5 个模块 md（audio/environment/indicator/power/networking）。

---

## 2026-05-13 — 麦克风 4 麦 → 双麦

**做了**：决定从 4 麦改双麦（V1.0 BOM DNP 验证 / V1.1 改板落地）。仅保留 MIC1 + MIC4（左右间距 80 mm）。

**备选**：① 维持 4 麦做波束成形；② 单 MIC 做基础鼾声分类。

**为什么**：鼾声场景核心特征是**频谱与包络**，不依赖空间定位；4 麦的波束成形 / DOA 在床头鼾声场景下是过设计。双麦兼顾鼾声分类 + 双人床鼾声来源区分 + 相干性降噪（SNR 提升 3-6 dB），是性价比最优点。

**影响**：[microphone.md](../hardware/microphone.md)；GPIO54 释放；PCB V1.1 需删 MIC2/MIC3 网络；统一型号为 LCD3526B261-OFA03。

---

## 2026-05-10 — vitals 算法翻新：谐波感知 HR + Doppler RBM

**做了**：心率估算改用谐波感知（避免呼吸 2-8 倍谐波被当成心率）；RBM 从 ampCV 升级为 Doppler-based。废弃 `LDO_EN` 信号。

**备选**：保留固定 Notch Q=30；保留 ampCV1s。

**为什么**：原 Notch 在体动时漂移；Doppler RBM 信噪比更高，门控 vitals 估算更准。

**影响**：`components/my_lidar_inf/my_lidar_inf.c`；[evt-vitals-baseline.md](evt-vitals-baseline.md) 参数表需重核。

---

## 2026-04-13 — RBM 体动检测算法（4 方案迭代后定 1s 滑窗 ampCV + binSpan）

**做了**：1 s 滑窗 ampCV（阈值 0.38）+ binSpan（阈值 ≥4 bins）作为体动判定。

**备选**：① 帧间相位跳变；② 自适应频谱差分 + 噪声底；③ 全频段能量差分。

**为什么**：① 受 bin 跳动污染、② 噪声底死锁、③ SNR 太差。1s 滑窗 ampCV/binSpan 信噪比好、校准点明确（实测静坐 max=0.353 / 运动 min=0.413，阈值取 0.38）。

**影响**：[evt-radar-bringup.md §四 D](evt-radar-bringup.md)；CLI 调参入口固化。

---

## 2026-04-09 — SPI 时钟提速到 25 MHz（实际跑 20 MHz）

**做了**：雷达 SPI 配置目标 25 MHz；当前 dev board 受 XTAL 限制实测 20 MHz。

**备选**：维持 10 MHz；切 PLL 时钟源。

**为什么**：10 MHz 经 GPIO matrix 实际不足支撑 ADC 9.2 Mbps 填充速率，FIFO 溢出 → FOF_ERR → 连续失败 → 重启崩溃。25 MHz 是稳定下限。

**影响**：[radar.md](../hardware/radar.md)、CLAUDE.md "关键约束" 段。产品板需保证 ≥ 25 MHz。

---

## 2026-04 — FIFO 分片读取 6×4096

**做了**：24576 样本 / 帧 分 6 片 × 4096 读取。

**备选**：单次读完。

**为什么**：硬件 FIFO 上限 16384，无法单次容纳。`set_fifo_limit()` 必须传 `FIFO_SLICE_SAMPLES=4096`，传 24576 会断言崩溃。

**影响**：所有 `init_sensor()` / `radar_rearm_next_measurement()` / `radar_restart_frame_generator()` 调用；CLAUDE.md "关键约束" 段。

---

## 2026-04 — FFT twiddle 表强制 deinit + reinit(1024)

**做了**：vitals 估算路径在调 FFT 前 `dsps_fft2r_deinit_fc32()` + 重新 `init(1024)`。

**备选**：维持 sensor-dsp 默认 128 初始化。

**为什么**：sensor-dsp 先以 128 初始化 twiddle 表 → 1024-FFT 越界 → 心率锁死在 52.1 BPM。

**影响**：`my_lidar_inf.c` vitals 估算函数；后续若加更大 FFT（如 HRV 路径）需同样处理。
