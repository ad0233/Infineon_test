---
name: lunawake-hardware
description: Lunawake 床头硬件（BY001 / 主板 BY-SA-V001）的硬件能力知识库——板上器件、雷达能做/不能做、与 Luna 软件团队的契约、EVT→DVT 交接资料、结构/CMF 决策（滑条选型等）。当用户讨论整机规格、软硬件对齐、雷达产品级能力边界、交接文档、状态屏/光柱/滑条/隐私闸方案、或要给软件团队蒸馏硬件资料时触发。
---

# Lunawake 硬件知识库（BY001）

本 skill 是 Lunawake 床头硬件的**单一事实源索引**。回答"我们硬件能做什么 / 不能做什么 / 软件团队期望什么 / 资料在哪"。
不重复 vitals 算法细节（看 `radar-vitals` skill）和雷达驱动细节（看 `.codex/skills/infineon-bgt60tr13c-radar`）。

---

## 0. 产品一句话

床头硬件中枢 = 卧床近场感知（60 GHz 雷达 + 麦阵 + 环境）+ 光声调节（暖光光柱 + 立体声声景）+ Matter 全屋联动入口。
**必须可断网独立工作整夜**。代号 BY001，主板 BY-SA-V001，主控 ESP32-P4，根工程名 `Lunawake`。

---

## 1. 板上有什么（BY-SA-V001 网表事实）

| 类别 | 器件 | 备注 |
|---|---|---|
| 主控 | ESP32-P4（U5，80-pin，双核 RISC-V，IDF v5.5） | 无 NPU；TF-Lite Micro 可行，大模型不行 |
| 雷达 | Infineon **BGT60TR13C**（60 GHz FMCW，1Tx/3Rx，当前固件只用 RX1） | SPI=GPIO0/1/3/5，IRQ=GPIO2，RSTN=GPIO13 |
| 存储 | 4 Gb SLC NAND（U60，MKDV4GCL-ABB） | — |
| 音频 | 2× ES8311 codec（U14/U16）+ 2×10 W AB/D 功放（U17） | 喇叭口 CN4/CN18（PicoBlade 6P×2） |
| 麦克风 | MIC1–4 数字 MEMS（PDM，−26 dB，SNR 58–64 dB） | 软件契约说"实测可能 4→2" |
| 环境光 | BH1750FVI（U1）+ VEML7700（U2） | I²C，VEML 0.67 Hz，0–120000 lux |
| 温湿度 | AHT20（U3） | I²C，1 Hz |
| 气压 | BMP580（U4） | 软件文档没用到，可选保留/删 |
| 状态灯 | 3× 单色 LED（GPIO15/16/17） | 注意：radar bring-up dev board 用 WS2812@GPIO20；**产品板是 3 颗分立单色 LED，以 resource_map.h 为准** |
| 按键 | SW1/SW2/SW3 轻触开关（SKRPACE010） | DVT 需重定义为 AUTO + 隐私闸（隐私闸要机械硬开关，物理切断雷达 VCC） |
| 电源 | 5 V DC（USB-C USB1 / PH2.0 H1/H2），DC-DC SY8368（U12）+ LDO XC6219（U15）+ 保险 F1 + TVS D1 | USB-A（USB2）下游 ≤ 1.5 A 5V passthrough |
| RTC | AiP8563（U20）+ CR1220（U39，仅 RTC 备份，不参与系统供电） | — |
| 通信 | Wi-Fi 2.4/5 GHz（Cross Air OA-W01 模组天线 U6）+ BT（P4 内置/协处理器，BT 音频路径需固件确认） | — |

### 板上没有、软件契约里要的（DVT 必补）
- **状态屏**（底座铜色装饰轨：时钟 HH:MM + 系统图标 + 事件提示）—— 当前无任何显示器，需选型 + 走线 + 外壳光学
- **暖光光柱 + 驱动通道**（PWM ≤ 0.1% 精度 + 升压，1800–2200 K 低 melanopic LED）—— PCB 未预留
- **弧形触控滑条 ×2**（左亮度 / 右音量）—— PCB 无触摸通道预留，方案见 §5
- **AUTO 圆键 / 隐私闸物理拨钮** —— 用现有 3 颗轻触开关重定义；隐私闸必须串到雷达 VCC 做硬断电

---

## 2. 雷达产品级能力（实测基线）

### 已实现（真机跑过，对外结构体 `radar_result_t` in `my_lidar_inf.h`）

| 输出 | 范围/精度 | 更新率 | 延迟 |
|---|---|---|---|
| detected 在床 | 0/1 | 10 Hz | 进场 ~2 s / 离场 ~1 s |
| confidence | 0.0–1.0 | 10 Hz | 实时（<0.5 信号弱） |
| distance_cm | 37.5–198.75 cm（物理上限 240）| 10 Hz | 实时（无人=0） |
| breath_bpm | **12–30 BPM** | 每 20 s | 需静止；体动>15% 跳过估算 |
| heart_bpm | **51–132 BPM** | 每 20 s | 需静止 |
| rbm 当前体动 | 0/1 | 10 Hz | ~0.5 s |
| body_move 强度 | 0–100% | 10 Hz | ~0.5 s |
| frame | uint32 | 10 Hz | — |

另有 `radar_debug_t`，其中 **breath_wave / heart_wave 是 10 Hz 实时波形**（软件做 sparkline / 律动跟随用这俩）。

### 规划中（没有，需立项）
入睡判定 · 睡/醒状态（对齐 Apple Health）· 离/入床事件流 · 体动连续时间序列 · 浅/深睡分期 · 鼾声端侧分类（YAMNet TF-Lite）· 呼吸暂停检测 · 呼吸 BPM 拉宽到 8–24（对齐 firmware 兜底常量）

### 物理不可行（别在上层假设）
- **HRV** —— 需帧率 ≥ 50 Hz，当前 10 Hz 拿不到逐拍 RR
- 高精度 4 类睡眠分期 —— 60 GHz 单模态学术上限 ~77–78%（arXiv 2604.16442，1022 夜 PSG），别承诺"准确分期"
- 多人区分 / 角度方向 —— 需启用 3RX 波束成形（Phase 2，无排期）
- 穿墙 / 穿厚被 —— 60 GHz 穿透力弱
- 运动中精确心率 / 血压 / 血氧 —— 原理不行

### 工况边界
最佳直线距离 50–100 cm（37.5 cm 起，~2 m 远端信号弱）· 单人室内 · 避开空调出风口/风扇等机械振动源 · 体动/翻身期间 BPM 不可信（固件已门控，UI 要提示"保持静止"）

---

## 3. 不可违反的硬约束（崩溃/失效）

- 雷达每帧 24576 样本 > 硬件 FIFO 上限 → **必须** 6 片×4096 分片读取
- SPI 时钟 **最低 25 MHz**（低于此 FIFO 溢出 → 连续失败 → 重启崩溃）；dev board 受 XTAL 限 20 MHz，产品板须保 25 MHz
- 雷达初始化顺序固定：释放 GPIO hold → SPI init → sensor init → 寄存器配置 → IRQ/FIFO limit → 启动 frame gen（不能调换）
- FFT twiddle 表全局唯一：sensor-dsp 以 128 初始化，vitals 用 1024 → 必须 deinit + reinit(1024)，否则心率锁死
- ESP32-P4 任务栈有限：函数内禁止 >512 B 局部数组，用 static 或堆
- SPI CS 由 sensor 库软件控制：`spics_io_num=-1`，CS 引脚仍需 gpio_config 为输出
- LP GPIO hold 必须在 SPI 总线 init **之前**释放
- 隐私闸 = 硬件断电路径，固件软逻辑无法绕过（PCB 上雷达 VCC 串到拨钮）

---

## 4. 与 Luna 软件团队的契约

软件 repo：GitHub `LunawakeGo/luna-product`（clone 需 `git -c http.proxy= -c https.proxy=` 绕本地代理）。markdown-driven 需求库 + Next.js web 预览。

对硬件侧有用的文档：
- `docs/hardware/-device.md` —— 整机交互（隐私闸/AUTO键/弧形滑条×2/光柱/声/状态屏；5 场景状态机；事件优先级 手动>自动；降级树）
- `docs/hardware/firmlunawakeware.md` —— firmware 契约（**5 场景状态机 + 6 类 stage 事件 + 17 个兜底常量 + ConfigCard 摄取 + 跨域时钟对齐 ≤ 50 ms + OTA 仅 Manual默认/Auto空闲**）
- `docs/hardware/hardware-blueprint.md` —— App Home 六卡需求（Hero/WindDown/Wake/Home/Sensors/Network/Controls）
- `docs/references/by001-radar-system.md` —— 他们复制的我们 `doc/hardware/radar.md`
- `docs/appendix/sensor-modules-spec.md` —— 感知模组 checklist（他们等我们回答的项）
- `docs/glossary.md` —— 术语表（Manual/Auto · A域/B域 · Phase/Stage/SceneSegment · degradeMode · ConfigCard · 隐私闸）

关键差异点：物理控件（3 轻触 vs 隐私闸+AUTO+滑条×2）· 状态屏（无 vs 铜色装饰轨）· 光柱（无驱动 vs 主视觉）· 麦克风（4 vs 2）· 雷达能力（缺入睡判定/睡醒分期/事件流）· 状态机（无 vs 5场景）· ConfigCard 通信协议（未定）。

待双方会议定的：通信协议（HTTP/MQTT/BLE？）· 状态屏形态 · 滑条选型（§5）· 麦克风 4→2 · 睡眠分期数据计划 · 隐私闸断电范围（含不含环境传感器？文档自相矛盾）· BMP580 去留 · BT+Wi-Fi+雷达+4麦+双codec 资源预算 · 17 个兜底常量最终值。

---

## 5. 滑条选型决策（CMF：底座金属面板上做触摸条很难）

**核心事实**：真实心金属面板上做不出"像屏幕一样丝滑"的电容滑条——金属屏蔽电容场，物理限制。

软件实际需求（看 firmware.md §8.2 / hardware-blueprint.md §8）：**"设一个 0–100 电平"**，不是连续手势。256 级 @ 50 Hz @ 10 ms 延迟绰绰有余。契约明确允许"旋钮编码器/触摸环"替代滑条。**先逼软件团队精确化需求**："交互是设电平 0–100，确认 256 级/50 Hz/10 ms 可接受？"

三方案：

| 方案 | 原理 | 评价 |
|---|---|---|
| **A. 金属拉丝旋钮 + 磁编码器**（AS5600 一类） | 旋钮实心金属，背后非接触磁旋转编码器 | **工程上最干净**——绕开"金属做触摸"伪命题；CMF 零妥协；可做机械段落感（盲操作最好）；成本中；契约允许。代价：改了"视觉语言" |
| **B. NCVM 塑料条 + 电容滑条 IC**（Azoteq IQS7211 / Infineon CapSense / Microchip MTCH） | 滑条区是金属感塑料（NCVM 非导电镀膜），背后 FPC 电容电极 | **推荐主线**——256–1024 级、50–200 Hz，最逼近屏幕级；成熟廉价多供应商；DVT 友好；**dead-front 背光把材料断点变 UX 卖点**（黑暗中透表面显示电平）。代价：该条是金属感塑料不是实心金属（做好了肉眼难分辨，可设计成"控制岛"）。风险：电磁干扰（旁边有雷达/Wi-Fi/D类/DC-DC，但有标准对策）、NCVM 耐磨（需 PU/UV 硬化顶涂）、浮地灵敏度（5V 双绝缘适配器需验证） |
| **C. 实心金属 + 力感滑条**（UltraSense/TDK TouchPoint、Qorvo/NextInput） | 弧形区是实心金属，背后力传感器阵列测微米级形变，算压力质心 | **仅当 CMF 把"零材料断点"定为不可谈判红线时用**——CMF 零妥协是唯一干净赢的；但实际 ~20–50 个准离散位置（到不了屏幕级）；交互变成"按压拖动有级感"（需和软件重谈）；几乎必配 LRA 触感；BOM 成本高；供应链小众；面板刚度与触摸强耦合，co-design 迭代多、排期风险高；电平反馈不能放滑条上 |

**结论**：主线 B；ID 对形态有弹性则 A 更优；C 仅 CMF 红线时。会议上把 A/B/C 一起摆，让 ID 在"形态弹性 / 材料弹性 / 体验妥协"里挑一个让步。

---

## 6. 摆放几何（北美 SKU 基准）

整机本体高 30 cm，雷达 IC 在设备顶部，放床头柜上对准床上仰卧的人。
- 北美：床面距地 61–76 cm（典型 63.5），床头柜面 ≈ 床面 ±5 cm → 雷达 ≈ 床面 +30 cm → 高于仰卧胸口 ≈ 15 cm → **俯仰 −15°（向下，固定）**
- 亚洲（榻榻米/矮床 + 高床头柜）：高差跨度大（−5 ~ +55 cm）→ 需 **三档铰链 −10°/−25°/−40°** 或软件校准（DVT 待决）
- 雷达罩硬约束：±45° 主波束锥内禁金属/禁导电涂层；罩 1.0±0.1 mm PC（禁纤维填充/禁金属漆/禁 IMD 金属膜）；罩到 IC 0 mm 或 ≥5 mm（禁 1–4 mm）；IC 到顶壳 ≤ 8 mm

---

## 7. 交接文档索引（硬件侧权威源）

| 文件 | 内容 |
|---|---|
| `doc/hardware/lunawake-hardware.md` | 给软件团队的硬件能力蒸馏（本 skill 的长版，整机硬件 master） |
| `doc/hardware/product-spec.md` | 整机规格书（功能清单/物理规格/引脚/雷达罩硬约束/电源热/认证/DVT 遗留/已冻结） |
| `doc/hardware/industrial-design.md` | 操作与体验需求（结构组用，摆放几何/按键/RGB/光柱/音频/端口/装配/验收触点） |
| `doc/hardware/radar.md` | 60 GHz 雷达模块（输出契约 + 算法参数） |
| `doc/hardware/microphone.md` | 麦克风阵列模块 |
| `doc/hardware/audio.md` | 音频输出模块 |
| `doc/hardware/environment.md` | 环境传感模块 |
| `doc/hardware/indicator.md` | 状态指示 + 用户输入模块 |
| `doc/hardware/power.md` | 电源管理模块 |
| `doc/hardware/networking.md` | 通信模块 |
| `doc/hardware/app-protocol.md` | ESP ↔ App 通信契约 |
| `doc/hardware/luna-panel-bridge.md` | 对 Luna 软件团队的实施契约 |
| `doc/progress/evt-vitals-baseline.md` | 雷达 vitals 算法基线 + 校准记录 |
| `doc/progress/evt-radar-bringup.md` | 硬件点亮 → 产品化实验记录 + 问题↔解决方案对照表 |
| `doc/hardware/assets/netlist-BY-SA-V001-2026-05-06.enet` | 主板网表（完整 BOM + 网络连接） |
| `components/my_lidar_inf/my_lidar_inf.h` | 对外数据结构与 API（权威） |
| `components/my_lidar_inf/resource_map.h` | 雷达/LED 引脚（权威） |
| `CLAUDE.md` | 项目硬约束与禁忌 |
| `.claude/skills/radar-vitals/SKILL.md` | vitals 调参方法论 |
| `.claude/skills/embedded-stack-workflow/SKILL.md` | 四层工作流方法论 |
| `.codex/skills/infineon-bgt60tr13c-radar/SKILL.md` | BGT60TR13C 驱动与配置 |

---

## 8. 参考文献

- arXiv 2604.16442 — "The Breakthrough of Sleep: Sleepal AI Lamp"（60 GHz FMCW 床头灯睡眠分期，1022 夜 PSG 验证）。结论：同类形态可行；sleep/wake 二分类 92.8%、4 分类 ~77–78%；呼吸 BPF 0.1–0.6 Hz；主要混淆 quiet wake ↔ N1/N2。**背书我们的硬件路线，但也划定睡眠分期的精度天花板**。
