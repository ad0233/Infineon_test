# Matter 协议（前瞻）

> Status: 🔴 planning · Last reviewed: 2026-05-18 · 尚未启动开发
>
> 本模块尚未在固件落地。本文档作为**为后续开发 Matter 做准备的知识库**，记录硬件能力、SDK 路径、候选角色与待决策项。

---

## 1. 一句话定位

借助 **U6 OA-W01 模组的 ESP32-C6 + 802.15.4 + Wi-Fi 6** 能力，BY001 硬件**已具备** Matter 协议三种角色中至少两种：
- **Matter over Wi-Fi** 设备（自身作为可被控的 Matter Device）
- **Matter Controller**（控制 A 域第三方 Matter 设备）
- **Thread Border Router**（桥接 Thread 设备与 Wi-Fi/IP 网络）

具体走哪条由产品策略决定，详见 §5。

## 2. 硬件能力来源

| 能力 | 来源 | 状态 |
|---|---|---|
| Wi-Fi 6 (2.4 G) | C6（OA-W01 模组） | ✅ 已实现 |
| BLE 5（配网） | C6（OA-W01 模组） | ✅ 已实现 |
| **802.15.4（Thread）** | **C6 原生支持** | ✅ 硬件就绪，固件未启用 |
| Wi-Fi/BLE 透传到 P4 | `esp_hosted` SDIO/SPI | ✅ 已跑通 |

→ 对接细节见 [networking.md](networking.md)。

## 3. ESP-Matter SDK 概览

| 项 | 信息 |
|---|---|
| 官方仓 | https://github.com/espressif/esp-matter |
| 上游 | 基于 connectedhomeip (CHIP) 开源项目 |
| ESP-IDF | 推荐 **v5.5.4**（当前项目 v5.5，需小升级） |
| 安装入口 | `install.sh` + `export.sh` |
| 文档 | https://docs.espressif.com/projects/esp-matter/en/latest/ |
| 设备类型清单 | https://github.com/espressif/esp-matter/blob/main/SUPPORTED_DEVICE_TYPES.md |

### 关键示例工程（espressif/esp-matter/examples/）

| 示例 | 我们可能用得到 |
|---|---|
| `light` / `light_switch` / `generic_switch` | A 域被控（智能灯模拟） |
| `controller` | **作为 Matter Controller，控制第三方设备**（Luna A 域核心场景） |
| `thread_border_router` | **C6 提供 TBR，桥接 Thread 设备到 IP 网络** |
| `bridge_apps` | 桥接非 Matter 设备到 Matter 网络 |
| `sensors` | 把雷达 / 温湿光数据暴露为 Matter Sensor 设备 |
| `icd_app` | 低功耗间歇连接（如电池 sensors） |
| `ota_provider` | 内部 OTA 服务 |
| `rainmaker` | 阿里 / 乐鑫云接入参考（不一定用） |

## 4. P4 + C6 拓扑下的 Matter 集成路径

```
┌──────────────────────────────┐         ┌────────────────────────────┐
│ U5 主控（ESP32-P4）          │         │ U6 协处理器（ESP32-C6）    │
│                              │         │                            │
│ ┌──────────────────────────┐ │         │ ┌────────────────────────┐ │
│ │ 应用层                   │ │         │ │ Wi-Fi 6 / BLE          │ │
│ │  - vitals 任务           │ │         │ │ 802.15.4 / Thread 协议 │ │
│ │  - 状态机                │ │ esp_    │ │   栈                   │ │
│ │  - **Matter 应用逻辑**   │ │ hosted  │ │                        │ │
│ │  - **CHIP stack**        │◄┼─SDIO/──►│ │ Matter 报文经由 Wi-Fi  │ │
│ │  - **Cluster 实现**      │ │ SPI     │ │   或 Thread 透传给 P4  │ │
│ └──────────────────────────┘ │         │ └────────────────────────┘ │
└──────────────────────────────┘         └────────────────────────────┘
```

- **CHIP stack + 设备类型 cluster 跑在 P4 侧**（充分利用 8 MB PSRAM + 双核）
- **C6 当透传 RF**，不跑 Matter 应用层，只走协议栈底层
- 这是乐鑫 `esp-matter` SDK 对 host MCU + co-processor 的标准玩法

> **待核实**：esp-matter SDK 当前对 P4 + C6（via esp_hosted）的官方支持成熟度——README / docs 没明确列出，需要 `examples/` 跑一遍 light demo 验证。

## 5. 三个候选角色（待决策）

| # | 角色 | 触发场景 | 优劣 | 我们要做？ |
|---|---|---|---|---|
| A | **Matter Device** 自己被控 | 用户在 Apple Home / Google Home / SmartThings 里加入 BY001 | 容易做，曝光度高（任何 Matter app 都能"看见"）。缺：仅暴露 vitals / 灯 / 声等已知 cluster | **建议做**（Phase 2 引流） |
| B | **Matter Controller** 控其他 | Luna App A 域：床头硬件直接控房间灯 / 空调 / 窗帘 | 价值高，把 A 域 Matter Controller 从手机搬到硬件，**断网也能跑** | **建议做**（Luna 原架构是 App 端 Controller，需软件团队对齐） |
| C | **Thread Border Router** | 床头硬件作为家里 Thread 网络入口 | 极高战略价值（不依赖 Apple/Google），但需要持续供电 + 完整证书 | 长期可选，先观望 |

> Luna 软件契约（[luna-panel-bridge.md §5](luna-panel-bridge.md)）当前假设 Matter Controller **在 App 端**。如果我们硬件做了角色 B，等于把这部分搬到本地，需要与 Luna 软件团队重新对齐 A 域分工。

## 6. 与 Luna 软件团队的对接

参考 Luna 三大主文档：
- `lunawake-device.md` §A 域 Matter 部分
- `hardware-blueprint.md` Home 卡（六模块卡的 A 域出口）
- `firmware.md` 兜底常量

详细桥接见 [luna-panel-bridge.md](luna-panel-bridge.md) §5。

**对齐点**：
1. A 域 Matter Controller 是放 App 还是本地硬件？
2. 如果本地，ConfigCard `stages.light/climate/curtain` 命令是 App 经 WS 下发给本地 Controller，还是 App 直接通过云调 Controller？
3. 配对 / 凭证存储路径？

## 7. 工作量预估（粗）

| 角色 | 工作量 | 前置条件 |
|---|---|---|
| A. Matter Device（仅暴露雷达 sensor） | 1-2 周 | esp-matter SDK 跑通 + 选定 cluster |
| B. Matter Controller | 4-6 周 | A 完成 + 软硬协议对齐 + Luna 软件 A 域改造 |
| C. Thread Border Router | 4-8 周 | C6 firmware 升级 + 证书 + 长期供电方案 |

## 8. 待办

- [ ] ESP-IDF v5.5 → v5.5.4 升级评估（esp-matter 推荐版本）
- [ ] `esp-matter/examples/light` 跑一遍 demo，验证 P4 + C6 via esp_hosted 拓扑可用
- [ ] 与 Luna 软件团队对齐 A 域 Matter Controller 归属（[open-questions.md Q1](../progress/open-questions.md)）
- [ ] 选定 Phase 1 试水角色（建议从 A 起步，最小可演示）
- [ ] 评估 Thread Border Router 必要性（视市场策略）

## 9. 参考

- [networking.md](networking.md) — C6 硬件能力（Wi-Fi 6 / BLE 5 / 802.15.4）
- [../../reference/hardware/espressif-esp32c6-datasheet-cn.pdf](../../reference/hardware/espressif-esp32c6-datasheet-cn.pdf) — C6 数据手册（本地，PDF 已被 gitignore）
- [luna-panel-bridge.md §5](luna-panel-bridge.md) — Luna A 域 Matter 链路
- [../progress/decisions.md](../progress/decisions.md) `2026-05-18 主控方案确认` — Thread 能力价值
- 官方 esp-matter 仓：https://github.com/espressif/esp-matter
- 官方文档：https://docs.espressif.com/projects/esp-matter/en/latest/
- 设备类型清单：https://github.com/espressif/esp-matter/blob/main/SUPPORTED_DEVICE_TYPES.md
- 上游协议 SDK：https://github.com/project-chip/connectedhomeip
