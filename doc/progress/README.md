# Progress — 项目进度 + 开发测试计划

按里程碑（EVT / DVT / PVT）组织。每个里程碑下的实验记录、调参基线、决策依据各成一份 md。

## 里程碑总览

| 阶段 | 时间窗 | 状态 |
|---|---|---|
| **EVT** — 雷达点亮、算法基线 | 2026-04 ~ 2026-05 | ✅ 完成 |
| **DVT** — 结构定型、模块改板、声学/光学验证 | 2026-05 ~ 进行中 | 🔄 进行中 |
| **PVT** — 量产试制、认证 | 待定 | 🔲 未开始 |

完整版本变更见 [changelog.md](changelog.md)。

## EVT 阶段记录

| 文档 | 内容 |
|---|---|
| [evt-radar-bringup.md](evt-radar-bringup.md) | 雷达硬件点亮 → 算法移植 → 调参实验全过程；问题↔解决方案对照表 |
| [evt-vitals-baseline.md](evt-vitals-baseline.md) | 雷达 vitals 算法基线快照（呼吸 / 心率 / presence / RBM 全参数表 + 校准记录） |

## DVT 阶段记录

| 文档 | 内容 |
|---|---|
| TODO: `dvt-handover.md` | 5/10 EVT → DVT 交接打包（汇总 03/04/05/06 当时的状态） |
| TODO: `dvt-dual-mic-decision.md` | 4 麦缩减为双麦的决策依据 |
| TODO: `dvt-radome-firstpiece.md` | 雷达罩首件 3D 验证报告（按下方 test-plan 跑） |

> 这些 DVT 决策的"现状面"已分散归入 [../hardware/microphone.md](../hardware/microphone.md) 等模块文档；
> `progress/` 里记的是**为什么这么决策、当时还有哪些备选**。

## 测试计划

| 文档 | 内容 |
|---|---|
| [test-plan/flash-device-certs.md](test-plan/flash-device-certs.md) | 设备证书 + IoT 配置烧录流程 |
| TODO: `test-plan/radar-radome-validation.md` | 雷达罩首件验证（distance / breath / heart 偏差红线） |

## 命名约定

- `<milestone>-<topic>.md` — 里程碑前缀，不带日期（日期写在文档头）
- 里程碑取值：`evt` / `dvt` / `pvt`
- 测试计划放 `test-plan/`，命名 `<scope>-<verb>.md`
