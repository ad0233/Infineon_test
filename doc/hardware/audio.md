# Audio Output 模块

立体声音频输出 — 用于助眠声景、TTS 引导词、唤醒音乐。

主板：BY-SA-V001 / 项目：Lunawake (BY001)。

---

## 1. 板上器件

| 位号 | 器件 | 角色 |
|------|------|------|
| U14 | ES8311 codec | 左声道 I²S → 模拟输出 |
| U16 | ES8311 codec | 右声道 I²S → 模拟输出 |
| U17 | AB/D 类功放（2×10 W） | 双声道功放，驱动外接喇叭 |
| CN4 | PicoBlade 1.25 mm 6P | 喇叭 L 输出 |
| CN18 | PicoBlade 1.25 mm 6P | 喇叭 R 输出 |

## 2. 接口

- ESP32-P4 通过 I²S（具体引脚分配见 [assets/netlist-BY-SA-V001-2026-05-06.enet](assets/netlist-BY-SA-V001-2026-05-06.enet)）连两片 ES8311
- 喇叭接口 6P 含正负 + 检测/反馈线，喇叭单元由结构 + 声学组选型
- 蓝牙音频路径走 ESP32-P4 内置 + 协处理器（路径待固件确认）

## 3. 能力 / 边界

| 项 | 规格 |
|---|---|
| 输出形式 | 立体声 |
| 功率 | 2 × 10 W 峰值 |
| 频响目标 | 60 Hz – 18 kHz ±6 dB（助眠声景需 60 Hz 以下底噪） |
| 婴幼儿场景 | dB SPL ≤ 50 @ 1 m（hard clamp，软件层强约束） |
| 整夜白噪 | 婴幼儿场景禁用（软件层强约束） |
| 满载耗散 | 约 4–5 W 热，散热主源之一 |

## 4. 结构 / 声学约束

详见 [industrial-design.md §4](industrial-design.md) 与 [product-spec.md](product-spec.md) §音频。结构组需保证：

- 喇叭单元朝向用户耳侧（侧出声 / 顶出声），不正对墙背或床面
- 音腔密封，避免被动辐射器与外壳共振产生嗡鸣
- MIC 阵列与喇叭物理距离 ≥ 30 mm，且中间有结构隔挡（降低 AEC 难度）

## 5. 待办

- [ ] 喇叭单元选型（配合 60 Hz – 18 kHz ±6 dB 频响目标）—— `product-spec.md §10` DVT 遗留项 8
- [ ] 固件层验证蓝牙音频路径可与 Wi-Fi + 雷达 SPI + 4 麦 PDM + 双 codec 同时运行（资源预算）

## 6. 配套资料

- [industrial-design.md §4](industrial-design.md) — 音频体验需求与声学约束
- [product-spec.md](product-spec.md) — 整机电气规格
- [microphone.md](microphone.md) — 麦克风阵列（AEC 配套）
- [assets/netlist-BY-SA-V001-2026-05-06.enet](assets/netlist-BY-SA-V001-2026-05-06.enet) — 网表（含 U14/U16/U17 完整连接）
