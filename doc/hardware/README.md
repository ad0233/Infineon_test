# Hardware 资料

BY001 整机硬件说明集。每个模块一份 md；总览 + 整机规格在前；模块说明在后；软硬契约最后。

## 总览

| 文档 | 角色 |
|---|---|
| [lunawake-hardware.md](lunawake-hardware.md) | **整机硬件 master**：硬件能做什么 / 不能做什么 / 已实现 / 规划中 / 物理不可行 |
| [product-spec.md](product-spec.md) | 整机电气规格书：功能清单 / 物理尺寸 / 引脚 / 雷达罩硬约束 / 电源热 / 认证 / DVT 遗留 / 已冻结 |
| [industrial-design.md](industrial-design.md) | 操作与体验需求（结构 / ID 组用）：摆放几何、视觉反馈、音频、端口、装配、验收触点 |

## 模块

每模块一份 md，结构统一：板上器件 → 接口 → 能力边界 → 结构约束 → 待办 → 配套资料。

| 模块 | 文档 |
|---|---|
| 60 GHz 雷达（BGT60TR13C） | [radar.md](radar.md) |
| 麦克风阵列（4 麦 / 双麦） | [microphone.md](microphone.md) |
| 音频输出（codec + 功放 + 喇叭） | [audio.md](audio.md) |
| 环境传感（温湿 / 光 / 气压） | [environment.md](environment.md) |
| 状态指示 + 用户输入（LED / 按键 / 滑条 / 隐私闸） | [indicator.md](indicator.md) |
| 电源管理 | [power.md](power.md) |
| 通信（Wi-Fi 6 / BLE 5 / 802.15.4） | [networking.md](networking.md) |
| Matter 协议（前瞻） | [matter.md](matter.md) 🔴 planning |

## 软硬契约（对外接口）

| 文档 | 用途 |
|---|---|
| [app-protocol.md](app-protocol.md) | ESP ↔ 手机 App 通信契约（BluFi 配网 + WebSocket 数据） |
| [luna-panel-bridge.md](luna-panel-bridge.md) | 对 Luna 软件团队的硬件实施契约（A/B 域分离、ConfigCard 摄取、状态机对齐） |

## 素材

[assets/](assets/) 存网表、波束方向图、供应商规格、选型对比等支撑材料：

- `netlist-BY-SA-V001-2026-05-06.enet` — 主板网表（含完整 BOM）
- `bgt60-beam-pattern.{png,py}` / `bgt60-beam-xy.{png,py}` — BGT60TR13C 波束方向图
- `OPT-LENS-001_VEML7700镜片供应商规格要求书.md` — 光传感器镜片规格
- `触摸开关选型对比.md` — 弧形滑条选型
- `霍尔开关_磁编码器_选型对比.md` — 雷达俯仰铰链位置反馈选型
