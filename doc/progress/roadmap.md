# Roadmap

> Status: 🟢 current · Last reviewed: 2026-05-18
>
> 三阶段：**EVT**（已完成）→ **DVT**（进行中）→ **PVT**（未开始）。完成情况按模块矩阵看；卡点对应 [open-questions.md](open-questions.md)。

---

## 模块完成度矩阵

| 模块 | EVT bring-up | EVT 算法 | DVT 改板 | DVT 验收 | 状态 |
|---|---|---|---|---|---|
| 60 GHz 雷达 | ✅ | ✅ | 🔄 雷达罩 / 俯仰角 | ❌ | 算法稳定，待结构 |
| 麦克风阵列 | ✅ 4 麦 | — | 🔄 改 V1.1 双麦 | ❌ | 决策已下，待改板 |
| 音频输出 | ✅ codec + 功放 | — | 🔄 喇叭选型 | ❌ | 待 ID 选型 |
| 环境传感 | ✅ AHT20 + VEML7700 + BH1750 | ✅ | 🔄 BMP580 去留 | ❌ | 待软件团队确认 |
| 状态指示 + 输入 | ✅ RGB 3 色 + 3 按键 | — | ❌ AUTO + 隐私闸 + 滑条 + 状态屏 | ❌ | **重改造**，4 项决策待定 |
| 电源管理 | ✅ | — | 🔄 光柱驱动通道 | ❌ | 待补 PWM + 升压 |
| 通信 | ✅ Wi-Fi + BLE 透传 | 🔄 App 协议 v1 | ❌ OTA / 鉴权 | ❌ | 等 Luna 协议定 |

图例：✅ 完成 / 🔄 进行中 / ❌ 未开始 / — 不适用

---

## EVT — 雷达点亮 + 算法基线（✅ 完成，2026-04 ~ 2026-05-13）

**目标**：单板验证整机所有传感 + 算法基线达到 demo 级。

**达成**：
- 雷达点亮：SPI 20 MHz、FIFO 6×4096、距离 / presence / breath / heart / RBM 全部跑通
- 算法基线：呼吸 12-30 BPM、心率 51-132 BPM、presence 进场 2 s 离场 1 s、RBM 检测延迟 0.5 s
- 4 麦 + AHT20 + VEML7700 + BH1750 + ES8311 + 功放 + WS2812 全部点亮

**输出**：[evt-radar-bringup.md](evt-radar-bringup.md) / [evt-vitals-baseline.md](evt-vitals-baseline.md) / [acceptance/evt-gate.md](acceptance/evt-gate.md)

---

## DVT — 结构定型 + 模块改板 + 验收（🔄 进行中，2026-05 ~ ）

**目标**：结构件首件 + PCB V1.1 改板 + 雷达罩验证通过，达到客户演示状态。

**目标日期**：**TBD**（受 Luna 软件团队 ConfigCard 协议、ID 滑条 / 状态屏选型卡）

**关键任务**：

| # | 任务 | 阻塞 | 出文档 |
|---|---|---|---|
| 1 | 麦克风 V1.1 改板（4 → 双麦） | 决策已下，等 PCB 出图 | 改板说明 |
| 2 | 暖光光柱驱动通道（PWM + 升压）补到主板 | DVT 待立项 | [power.md §6](../hardware/power.md) |
| 3 | 弧形滑条 / AUTO 单键 / 隐私闸机械拨钮 PCB 改造 | Q3 滑条选型 → ID + 结构 | [indicator.md §6](../hardware/indicator.md) |
| 4 | 状态屏选型 + 外壳光学 | Q2 显示器形态 → ID | [indicator.md §5](../hardware/indicator.md) |
| 5 | 雷达罩首件 3D 验证 | 等结构件回来 | [test-plan/radar-radome-validation.md](test-plan/radar-radome-validation.md) |
| 6 | 亚洲 SKU 三档铰链 / 软件校准方案 | Q8 结构 + 算法 | TBD |
| 7 | Luna ConfigCard 摄取实现 | Q1 协议未定 → 等会议 | TBD |
| 8 | 整机散热曲线实测（满载 8 h ≤ 45 ℃） | 等结构件 | TBD |
| 9 | 雷达 IC 在 PCB 顶视坐标定位 | Q9 PCB 改版同步 | TBD |

**输出**：[acceptance/dvt-gate.md](acceptance/dvt-gate.md)（验收清单）

**关键风险**：
- ⚠️ Luna 软件协议未定 → ConfigCard 摄取无法落地 → 拖整个软硬整合
- ⚠️ 滑条选型未定 → PCB V1.1 不敢出，连带状态屏改造也卡

---

## PVT — 量产试制 + 认证（❌ 未开始）

**前置**：DVT 全部 ✅。

**任务**：
- 60 GHz 认证（SRRC / FCC Part 15.255 / RED）
- Wi-Fi / BT 认证（SRRC / FCC / CE）
- 安规（GB 4943 / IEC 60950 / 62368）
- EMC（GB 9254 / FCC Part 15B / CE EN55032）
- 量产工艺验证（DFM / DFA）
- 客服故障树 / FAQ

**输出**：`acceptance/pvt-gate.md`（DVT 通过后建）

---

## 长期 / Phase 2（不在主路线上，记录）

- 3 RX 角度估计 → 启用侧向 / 多目标检测
- HRV 心率变异性（需帧率 ≥ 50 Hz，当前硬件不可行 → 需替换雷达或 ESP 配置）
- 睡眠分期（依赖 HRV + 体动聚合，需大数据训练 / 外采模型 / PSG 同步采集计划）

---

## 维护规则

- 每次重大节点完成更新模块矩阵
- DVT 目标日期落定后填上
- 风险变化时更新"关键风险"段
- 任务卡死 > 2 周 → 升级到 [open-questions.md 🔴](open-questions.md) 高优先
