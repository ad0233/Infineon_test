# Lunawake BY001 — 项目文档

ESP32-P4 + BGT60TR13C 60 GHz 雷达床头硬件（机型 **BY001**，主板 **BY-SA-V001**）。

文档分三块：

| 目录 | 角色 | 入口 |
|---|---|---|
| [hardware/](hardware/) | 硬件资料（现状快照，每模块一份 md） | [hardware/README.md](hardware/README.md) → [hardware/lunawake-hardware.md](hardware/lunawake-hardware.md) |
| [progress/](progress/) | 项目进度 + 开发测试计划（里程碑、实验记录） | [progress/README.md](progress/README.md) |
| [standards/](standards/) | 编码规范、文献依据 | [standards/coding-standard.md](standards/coding-standard.md) |

## 速查

| 我想… | 看哪里 |
|---|---|
| 知道整机能做什么 / 不能做什么 | [hardware/lunawake-hardware.md](hardware/lunawake-hardware.md) |
| 看雷达能力 / 算法参数 | [hardware/radar.md](hardware/radar.md) + [progress/evt-vitals-baseline.md](progress/evt-vitals-baseline.md) |
| 查整机电气规格 | [hardware/product-spec.md](hardware/product-spec.md) |
| 给结构 / ID 出图 | [hardware/industrial-design.md](hardware/industrial-design.md) + [hardware/product-spec.md §5 雷达罩](hardware/product-spec.md) |
| 接 App | [hardware/app-protocol.md](hardware/app-protocol.md) |
| 对 Luna 软件团队 | [hardware/luna-panel-bridge.md](hardware/luna-panel-bridge.md) |
| 查项目历史 / 调参记录 | [progress/](progress/) |
| 看引脚 / 网表 | `components/my_lidar_inf/resource_map.h` + [hardware/assets/netlist-BY-SA-V001-2026-05-06.enet](hardware/assets/netlist-BY-SA-V001-2026-05-06.enet) |

## 命名约定

- **hardware/** 与 **progress/** 内部 md 一律 `kebab-case-english.md`（参照 Luna 仓 `lunawake-device.md` 风格）
- 进度文档按里程碑前缀：`evt-*.md`、`dvt-*.md`、`pvt-*.md`
- 第三方提供的供应商规格、选型对比可保留中文原名，归入 [hardware/assets/](hardware/assets/)
- 不主动新增 markdown，每个新文件先回答"是 hardware 模块还是 progress 节点"
