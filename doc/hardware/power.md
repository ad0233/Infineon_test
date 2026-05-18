# Power Management 模块

5 V DC 输入 → 3V3 / 1V8 多电源域分配，含 RTC 备份与下游 USB-A 供电。

主板：BY-SA-V001 / 项目：Lunawake (BY001)。

---

## 1. 板上器件

| 位号 | 器件 | 角色 |
|------|------|------|
| USB1 | USB-C 16P 卧贴 | 5 V 主输入 + 数据 + OTA |
| H1 | PH2.0 2P 卧贴 | DC 5 V 主输入（备选） |
| H2 | PH2.0 2P 卧贴 | 备用电源输入 |
| U12 | SY8368 | DC-DC 主降压 |
| U15 | XC6219 | LDO（codec / 3V3 专用） |
| F1 | 保险 | 过流保护 |
| D1 | TVS | 浪涌保护 |
| U20 + U39 | AiP8563 + CR1220 | RTC 时钟 + 纽扣电池 RTC 备份（**仅 RTC，不参与系统供电**） |
| USB2 | USB-A 直插 180° | 下游 5 V 供电 ≤ 1.5 A |

## 2. 电源域

| 域 | 来源 | 用电对象 |
|---|---|---|
| 5 V | USB1 / H1 | DC-DC、USB-A 透传 |
| 3V3（数字） | DC-DC | ESP32-P4、雷达 SPI 数字侧、麦克风、传感器、状态 LED |
| 3V3（codec/模拟） | XC6219 LDO | ES8311 codec、麦克风 VDD（避免与数字噪声耦合） |
| 1V8 | 雷达内部 LDO 后 | BGT60TR13C 内部 |
| RTC | CR1220 | AiP8563 RTC |

雷达侧从 5 V → 3V3 → 1V8 逐级供电，电平转换 TXS0108EPWR；详见 [radar.md](radar.md)。

## 3. 功耗规格

| 工况 | 功耗 |
|---|---|
| 待机 | ≤ 0.5 W |
| 监测态（雷达 + 传感器 + 蓝牙待机） | ≈ 1.5 W |
| 满载（雷达 + 音频满音量 + Wi-Fi + 麦） | ≈ 8 W 峰值 |
| USB-A 下游 | ≤ 1.5 A（5 V passthrough） |

## 4. 热与结构约束

主散热源：

1. **U17 功放** — 满载 4–5 W 热
2. **ESP32-P4** — 雷达 SPI 25 MHz + Wi-Fi + 多任务时偏高
3. **U12 DC-DC** — 全负载时温升明显

要求：

- 自然对流（无风扇），结构在底部 + 背面预留进出风开槽
- 8 h 满载后用户接触面温升 ≤ 45 ℃（GB 4943 室温环境定义）
- AHT20 距上述主热源 ≥ 30 mm（保证温度读数不被污染，详见 [environment.md](environment.md)）

## 5. 关键约束

- **CR1220 不参与系统供电**，仅 RTC 时钟保持
- **隐私闸 = 雷达 VCC 物理串入路径**（[indicator.md §4](indicator.md)），切断必须断雷达 VCC 而非软件 mute
- USB-C 主输入支持 PD，标称 5 V / 3 A

## 6. 待办（DVT 遗留）

| # | 项 | 参考 |
|---|---|---|
| 1 | 暖光光柱驱动通道（PWM + 升压）补到主板 | [product-spec.md §10](product-spec.md) 遗留项 1 |
| 2 | 整机散热曲线实测 | 同上遗留项 6 |
| 3 | 隐私闸串入雷达 VCC 路径的 PCB 改版确认 | [indicator.md §4](indicator.md) |

## 7. 配套资料

- [product-spec.md §7](product-spec.md) — 电源与热规格
- [assets/netlist-BY-SA-V001-2026-05-06.enet](assets/netlist-BY-SA-V001-2026-05-06.enet) — 网表（U12/U15/F1/D1 完整连接）
- [radar.md](radar.md) — 雷达侧的 5 V → 3V3 → 1V8 电平链
