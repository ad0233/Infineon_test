# Changelog — Lunawake BY001

> Status: 🟢 current · Last reviewed: 2026-05-18

按时间倒序记关键节点。版本号留给量产硬件件本号；这里只追**软件 + 硬件契约层**的变更。

格式：`日期 | 类别 | 摘要`

类别：`hw` 硬件 / `fw` 固件 / `algo` 算法 / `doc` 文档 / `tool` 工具 / `dec` 决策。

---

## 2026-05

| 日期 | 类别 | 摘要 |
|---|---|---|
| 2026-05-18 | doc | 文档大重构：`doc/` 分 `hardware/` + `progress/` + `standards/`；每硬件模块独立 md；命名 kebab-case 参照 Luna 风格 |
| 2026-05-13 | hw / dec | 麦克风方案：4 麦 → 双麦（BOM DNP V1.0 或改板 V1.1），鉴于鼾声场景 4 麦过设计 |
| 2026-05-11 | doc | EVT → DVT 交接打包：操作体验 / 整机规格 / 硬件能力简报 |
| 2026-05-10 | algo | vitals：谐波感知 HR、Doppler RBM；废弃 LDO_EN |
| 2026-05-06 | hw | 网表归档：BY-SA-V001 2026-05-06 版 |
| 2026-05  | fw | presence 检测 + vitals 稳定性整体翻新 |

## 2026-04

| 日期 | 类别 | 摘要 |
|---|---|---|
| 2026-04-27 | doc | BGT60 波束方向图归档（极坐标 + XY） |
| 2026-04-24 | algo | radar tracker：MTI 帧差 + 多特征胸腔评分 |
| 2026-04-20 | doc | App 通信协议 v1 定稿（BluFi + WebSocket） |
| 2026-04-13 | doc | 雷达调参实验总结、APP 集成说明文档 |
| 2026-04-12 | algo | RBM ampCV1s 阈值校准（实测：静坐 0.353 / 运动 0.413 → 阈值 0.38） |
| 2026-04-11 | algo | RBM 与 presence 解耦：独立 1 s ampCV / binSpan 窗口 |
| 2026-04-11 | doc | vitals 全参数表首版（= `progress/evt-vitals-baseline.md` 前身） |
| 2026-04-10 | algo | presence 校准、RBM 体动检测、bin tracker 改进 |
| 2026-04-09 | tool | 统一 radar panel GUI + CSV 录制；废弃旧脚本 |
| 2026-04-08 | fw | RGB 串口控制、统一 GUI、presence 门控 BPM 清零 |
| 2026-04-07 | algo | 加入呼吸 + 心率（FFT + filtfilt + 谐波消除）；修 FFT twiddle 表竞争 |
| 2026-04-06 | fw | port Infineon 雷达 distance + presence pipeline；修 FIFO 溢出 |

## 2026-03 及更早

| 日期 | 类别 | 摘要 |
|---|---|---|
| 2026-03-30 | fw | Infineon 雷达 bring-up 稳定；加 skill |
| — | hw | 音频功能（4 麦初始化、功放、ES8311 codec）、传感器（AHT20 + VEML7700 + BH1750）调通 |

---

## 维护规则

- 新增一行：日期取动作完成日，不取 PR 合并日
- 类别 ≤ 1 个；跨类别拆两行
- 摘要 ≤ 70 字
- **不**记代码 review、命名 rename、注释调整这类琐碎变更——那些去看 `git log`
