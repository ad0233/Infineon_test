# EVT 验收 Gate

> Status: 🟢 完成 · Last reviewed: 2026-05-18
>
> EVT = Engineering Verification Test。**单板验证整机所有传感 + 算法基线达到 demo 级**。
> 已通过 → 进入 DVT 阶段。本文档归档保留，量产复盘可查。

---

## 通过判定

**全部 ✅ 才算 EVT 通过**。逐项打钩 + 注日期 + 留对应原始数据。

## 1. 硬件 bring-up

- [x] **2026-04-06** ESP32-P4 烧录 + 串口正常
- [x] **2026-04-06** 雷达 BGT60TR13C SPI 通信，chipid 读到 `0x0208AC`
- [x] **2026-04-09** 雷达 FIFO 分片读取（6 × 4096）稳定运行 ≥ 30 min 无崩溃
- [x] **2026-03** AHT20 温湿度 I²C 读到合理值（22-28 ℃ / 30-70 %RH）
- [x] **2026-03** VEML7700 + BH1750 双光传感都能读到 lux
- [x] **2026-03** ES8311 × 2 codec + 功放 U17 出声
- [x] **2026-03** 4 麦 MIC1-4 PDM 数据流可读
- [x] **2026-04** WS2812 / RGB 三色 LED 受控

## 2. 算法基线

- [x] **2026-04-06** 距离测量：bin 10-53（37.5-198.75 cm）覆盖
- [x] **2026-04** Presence 检测：进场 ~2 s / 离场 ~1 s（实测，[evt-radar-bringup.md](../evt-radar-bringup.md)）
- [x] **2026-04-07** 呼吸率：12-30 BPM 范围稳定输出
- [x] **2026-04-07** 心率：51-132 BPM 范围稳定输出（修 FFT twiddle 表竞争后）
- [x] **2026-04-12** 体动 RBM：0-100% 输出，校准阈值 0.38（[decisions.md](../decisions.md) 2026-04-13）
- [x] **2026-05-10** vitals 算法翻新：谐波感知 HR + Doppler RBM

## 3. 工具链

- [x] **2026-04-09** unified panel GUI（`tools/sleep_analyze.py` / radar_panel.py）可视化 + CSV 录制
- [x] **2026-04-13** CLI 调参入口（presence 阈值 / 距离 / vitals 参数 runtime 可调）
- [x] **2026-04** App 通信协议 v1 定稿（[app-protocol.md](../../hardware/app-protocol.md)）

## 4. 文档

- [x] **2026-05-11** EVT → DVT 交接资料：操作体验 / 整机规格 / 硬件能力简报
- [x] **2026-05-18** doc/ 重构 + PM 工作流文档落地

## 5. 已知遗留（带到 DVT 解决）

- ⚠️ SPI 20 MHz 而非目标 25 MHz（dev board XTAL 限制；产品板必须 ≥ 25 MHz）→ 见 [decisions.md](../decisions.md) 2026-04-09
- ⚠️ 偶发 FIFO 卡死需冷启动 3 s（硬件偶发，非代码）→ DVT 改板时验证是否复现
- ⚠️ 呼吸下限 12 BPM > Luna 软件契约 8 BPM 起点 → DVT 改 FFT ROI
- ⚠️ 心率下限 51 BPM 不覆盖深睡 / 运动员 → DVT 扩展到 42 BPM
- ⚠️ 体动 > 15% 时 BPM 跳过估算（已门控，UI 须配合提示"保持静止"）

---

## 归档

- 原始数据：`tools/records/2026-04/*` / `tools/records/2026-05/*`
- 关键截图：`doc/hardware/assets/bgt60-beam-*.png`
- 调参实验：[../evt-radar-bringup.md](../evt-radar-bringup.md)
- 参数基线：[../evt-vitals-baseline.md](../evt-vitals-baseline.md)
