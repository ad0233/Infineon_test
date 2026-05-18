# Open Questions

> Status: 🟢 current · Last reviewed: 2026-05-18
>
> 写下来 = 释放工作内存。一行一问题；含**阻塞谁、对接谁、当前状态**。决策完毕的删掉或迁到 [decisions.md](decisions.md)。

---

## 🔴 高优先（卡里程碑）

| # | 问题 | 对接 | 卡哪 | 状态 |
|---|---|---|---|---|
| Q1 | Luna ConfigCard 怎么传到 firmware？HTTP / MQTT / BLE GATT / 自定义？ | Luna 软件团队 | DVT firmware 入口设计 | pending 等会议 |
| Q2 | 状态屏选型（OLED / 字符 LCD / 点阵 / 铜色装饰轨形态） | ID + 硬件 | PCB 改版 + 外壳光学 | pending |
| Q3 | 弧形滑条选型（电容触摸条 vs 旋钮编码器 vs 触摸环 vs 霍尔） | 结构 + 硬件 + ID | PCB 改版 | in-discussion → 倾向"金属背贴电容 + LED 灯柱 + 线性马达"（见 [触摸开关选型对比.md](../hardware/assets/触摸开关选型对比.md)） |
| Q4 | 麦克风 4 → 2 是否定？V1.0 DNP 还是 V1.1 改板？ | 声学 + 硬件 | PCB 改版 | in-discussion，倾向 V1.1 改板（决策依据见 [microphone.md §8](../hardware/microphone.md)） |

## 🟡 中优先（影响 DVT 选型）

| # | 问题 | 对接 | 卡哪 | 状态 |
|---|---|---|---|---|
| Q5 | 隐私闸断电范围：只断雷达，还是雷达 + 麦一起？环境光温湿是否一并断？ | Luna 软件团队 | PCB 走线 + 外壳开关位置 | pending 软件文档矛盾 |
| Q6 | 气压计 BMP580 保留还是删？软件文档当前未使用 | Luna 软件团队 | BOM 决策 | pending |
| Q7 | 蓝牙音频路径资源预算：ESP32-P4 + 协处理器能否同时跑 Wi-Fi + BT 音频 + 雷达 SPI + 4 麦 PDM + 双 codec？ | 自验证（固件资源测算） | 蓝牙音频功能能否承诺 | not-started |
| Q8 | 亚洲 SKU 三档铰链（−10° / −25° / −40°）vs 软件校准方案？北美 SKU 已定 −15° | 结构 + 算法 | 整机出图 + 算法适配 | pending |
| Q9 | 雷达 IC 在 PCB 上的最终位置（顶端 5 mm 内 vs 中段）—— 决定俯仰角 | 硬件 | 顶视坐标 | pending |

## 🟢 低优先（不卡里程碑，记一下）

| # | 问题 | 状态 |
|---|---|---|
| Q10 | 心率下限扩展 51 → 42 BPM 覆盖深睡 / 运动员的算法成本 | not-started |
| Q11 | OTA 协议立项（HTTP / MQTT 路径 + 签名校验） | not-started |
| Q12 | WS 鉴权（protocol_version=2 起启用 token） | future |
| Q13 | 调试口 CN2 / U13 量产是否拆除 | DVT 期决 |
| Q14 | 17 个 firmware 兜底常量的最终值（Luna `firmware.md §2`） | 调试后回填 |

---

## 维护规则

- 一行一问题，**含对接方 + 卡哪**；不写解决方案
- 决策完毕：**剪贴**到 [decisions.md](decisions.md) 当条目，本表删行
- 每周扫一遍 status；卡同一状态 > 2 周就去找对接方推
