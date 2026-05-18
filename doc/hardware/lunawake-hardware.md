# BY001 硬件能力说明书

> Status: 🟢 current · Last reviewed: 2026-05-18

受众：Luna 软件团队（first），硬件 / 固件团队（second）。
目的：让软件侧准确知道硬件「能做什么 / 不能做什么 / 哪些还在调试」，避免基于错误假设设计上层。
对齐对象：`luna-product/docs/hardware/lunawake-device.md`、`firmware.md`、`hardware-blueprint.md`、`references/by001-radar-system.md`。
状态：EVT 实测基线 + DVT 规划。所有「已实现」= 真机跑过；「规划中」= 尚未实现；「物理不可行」= 当前硬件做不到，别设计。

---

## 1. 硬件是什么（最底层事实）

### 1.1 主控与计算（P4 + C6 双芯架构）

- **主控模组**：**JC-ESP32P4-M3**（U5，嘉立创自封）—— ESP32-P4 双核 RISC-V + **16 MB Flash + 8 MB PSRAM**，FreeRTOS / ESP-IDF v5.5
- **协处理器模组**：**Cross Air OA-W01**（U6）—— ESP32-C6 + 板载天线，提供 **Wi-Fi 6（2.4 GHz） + BLE 5 + 802.15.4（Thread/Zigbee）**；P4 自身无 Wi-Fi/BT 射频，通过 `esp_hosted` 透传协议（SDIO/SPI）挂载
- **板外存储**：MKDV4GCL-ABB 4 Gb（512 MB）SLC NAND（U60）—— 模组内 16 MB Flash 之外的额外存储，用于音频素材 / 模型 / 日志缓冲
- 无独立 NPU；TF-Lite Micro 级别的端侧推理可行，大模型不可行
- 8 MB PSRAM 已可放大数组，但仍建议大数组 static 或堆分配；函数内 >512 B 局部数组禁止（FreeRTOS 任务栈有限）

> 网表 description 把 OA-W01 标为 "2.4G/5G Wi-Fi" 是误把 **Wi-Fi 6（802.11ax 代际名）** 写成了 "5G 频段"。C6 实际只支持 2.4 GHz Wi-Fi 6，**不支持 5 GHz 频段**。详见 [networking.md §1](networking.md)。

### 1.2 雷达
- 器件：Infineon **BGT60TR13C**，60 GHz FMCW，1 Tx / 3 Rx（**当前固件只用 RX1**）
- 接口：SPI（SCLK/MOSI/MISO/CSN = GPIO0/1/3/5），IRQ = GPIO2，RSTN = GPIO13
- 工作 profile（已验证）：59–63 GHz，BW 4 GHz，ADC 1 MHz，128 样本/chirp，64 chirp/帧，帧率 ~10 Hz
- 距离分辨率：3.75 cm/bin；有效窗口 bin 10 ~ bin 53 = **37.5 cm ~ 198.75 cm**（物理上限 240 cm）
- SPI 时钟：20 MHz（受 dev board XTAL 限制；产品板目标 25 MHz，详见 §4）

### 1.3 其它感知（板上已有）
| 器件 | 功能 | 接口 | 采样率 |
|---|---|---|---|
| AHT20 | 温湿度 | I²C | 1 Hz；精度 ±2 %RH（温度精度待器件手册确认） |
| VEML7700 | 环境光 | I²C | 0.67 Hz；0–120000 lux |
| BH1750FVI | 环境光（第二颗，交叉校验/冗余） | I²C | 可配 |
| BMP580 | 气压 | I²C / SPI | 软件文档没用到，可选保留或 DVT 删 |
| MIC1–4 | 数字 MEMS 麦（PDM），上进音，灵敏度 −26 dB，SNR 58–64 dB | PDM | 软件文档说「实测可能从 4 缩到 2」 |

### 1.4 输出与人机（板上已有）
| 项 | 现状 |
|---|---|
| 音频输出 | 2× ES8311 codec + 2×10 W AB/D 功放（U17）；喇叭接口 CN4/CN18（PicoBlade 6P×2） |
| 状态指示灯 | 3× 单色 LED（GPIO15/16/17）—— 注意：radar bring-up 用的 dev board 是 WS2812@GPIO20，**产品板 BY-SA-V001 是 3 颗分立单色 LED**，以 `resource_map.h` 为准 |
| 按键 | 3× 轻触开关（SW1/SW2/SW3，SKRPACE010） |
| 电源 | 5 V DC 输入（USB-C / PH2.0），DC-DC SY8368 + LDO XC6219 + 保险 F1 + TVS D1 |
| 通信 | Wi-Fi 2.4/5 GHz（Cross Air OA-W01 模组天线）+ 蓝牙（ESP32-P4 内置 + 协处理器，需固件确认 BT 音频路径） |
| 备份 | CR1220 仅 RTC（AiP8563），不参与系统供电 |
| USB-A | 下游供电 ≤ 1.5 A（5 V passthrough） |

### 1.5 软件契约里有、我们 PCB 上没有的
- **状态屏（底座铜色装饰轨，时钟 HH:MM + 系统图标 + 事件提示）** —— 当前 PCB 无任何字符/点阵显示器，DVT 需选型 + 走线 + 外壳光学
- **暖光光柱 + 其驱动通道（PWM + 升压）** —— 当前 PCB 未预留
- **弧形触控滑条 ×2（左亮度 / 右音量）** —— 当前 PCB 无电容触摸通道预留
- **AUTO 圆形键 / 隐私闸物理拨钮** —— 现有 3 颗轻触开关需重新定义为 AUTO + 隐私闸（隐私闸必须是机械硬开关，物理切断雷达 VCC）

---

## 2. 雷达能给什么（实测能力 + 边界）

### 2.1 已实现（真机跑过，可直接用）

| 输出 | 范围 / 精度 | 更新率 | 延迟 | 备注 |
|---|---|---|---|---|
| `detected` 在床/有人 | 0/1 | 10 Hz | 进场 ~2 s / 离场 ~1 s | 4 指标融合（相位偏移 + 振幅 CV + bin 跨度 + 呼吸峰） |
| `confidence` 置信度 | 0.0–1.0 | 10 Hz | 实时 | < 0.5 视为信号弱 |
| `distance_cm` 目标距离 | 37.5–198.75 cm | 10 Hz | 实时 | 无人时 = 0；跟踪式峰值，±3 bin 局部搜索 |
| `breath_bpm` 呼吸率 | **12–30 BPM** | 每 20 s | 需稳定静止 | filtfilt BPF 0.1–0.6 Hz + FFT 1024；体动 >15% 时跳过估算 |
| `heart_bpm` 心率 | **51–132 BPM** | 每 20 s | 需稳定静止 | 独立路径，去呼吸谐波（notch ×2~×8） |
| `rbm` 当前是否体动 | 0/1 | 10 Hz | ~0.5 s | 独立 1 s 窗口（振幅 CV >0.38 或 bin 跨度 ≥4） |
| `body_move` 体动强度 | 0–100% | 10 Hz | ~0.5 s | (ampCV1s − 0.38) / (0.92 − 0.38) × 100 |
| `frame` 帧计数 | uint32 | 10 Hz | — | — |

实际导出结构（C，`my_lidar_inf.h`）：

```c
typedef struct {
    uint8_t  detected;       // 1=有人 0=无人
    float    confidence;     // 0.0-1.0
    float    distance_cm;    // cm, 无人=0
    float    breath_bpm;     // BPM, 0=未检测
    float    heart_bpm;      // BPM, 0=未检测
    uint8_t  rbm;            // 1=正在体动
    float    body_move;      // 0-100%
    uint32_t frame;          // 帧计数
} radar_result_t;
```

还有一个 `radar_debug_t`（signal_db / range_bin / phase_excursion_mm / amplitude_cv / breath_wave / heart_wave 等）—— 调试用，软件侧一般不需要，但 **breath_wave / heart_wave 是 10 Hz 实时波形**，软件如果要做"呼吸 sparkline / 律动跟随"可以取这两个。

### 2.2 规划中（需要立项做，目前没有）

| 能力 | 软件契约里要求 | 现状 | 工作量 |
|---|---|---|---|
| **入睡判定**（windDown → 夜间监测） | firmware.md §4.5：呼吸 BPM 稳定 + 体动衰减 + sleepOnsetWindowMs(180 s) | ❌ 未实现 | 中——信号都有，需写判定逻辑 + 滞回 |
| **睡 / 醒状态**（对齐 Apple Health） | sensor-modules-spec.md §3：必须 | ❌ 未实现 | 大——需要数据 + 模型，参考 arXiv 2604.16442（1022 夜 PSG） |
| **离床 / 入床事件流** | firmware.md §4.5 | 部分（有 presence，没事件化 + 去抖） | 小——presence 上加 bedExitDebounceMs(120 s) 去抖 |
| **体动连续时间序列** | lunawake-device.md §2.2 / sensor-modules-spec.md §3 | ❌ 只导出瞬时 `body_move` | 小——把 body_move 缓冲成时间序列对外 |
| **浅睡 / 深睡分期** | "非必需，有更好" | ❌ | 大——同睡/醒分期，依赖 HRV 级精度（见 §2.3） |
| **鼾声端侧分类** | sensor-modules-spec.md §1 | ❌（麦克风固件未启用） | 中——他们建议复用 YAMNet TF-Lite |
| **呼吸暂停检测** | 复用呼吸健康双模态 | ❌ | 中——基于现有呼吸波形连续性 |
| 呼吸 BPM 范围拉宽到 8–24（与 firmware 兜底常量对齐） | firmware.md §2 breathRateMin/Max | ⚠️ 当前 12–30，需把 BPF 下限 0.1→保持、ROI 改 | 小——改 FFT ROI |

### 2.3 物理不可行（当前硬件做不到，别在上层假设）

| 想要的 | 为什么不行 |
|---|---|
| **HRV（心率变异）** | 需要逐拍 RR 间期，要帧率 ≥ 50 Hz；当前 10 Hz 帧率物理上拿不到。FFT 心率只能给"平均心率"，不能给 HRV |
| 高精度睡眠分期（接近 PSG 的 4 类） | 学术上 60 GHz 雷达单模态 4 分类约 77–78%（arXiv 2604.16442），且需要大数据集训练；不要承诺"准确分期" |
| 多人区分 | 当前固件单目标跟踪；多目标需要启用 3RX 波束成形 + 角度估计（属于"低优先 / 长期"，没排期） |
| 穿墙 / 穿厚被检测 | 60 GHz 穿透力弱，厚棉被会显著衰减；这是物理特性 |
| 运动中精确心率 | 体动 >15% 时 vitals 不可信，固件会跳过估算并保留上次值 |
| 左右 / 角度方向检测 | 需启用 3RX（Phase 2，未实现） |
| 血压 / 血氧 | 雷达原理上做不到 |

### 2.4 已知工况边界
- 最佳直线距离：50–100 cm（37.5 cm 起，远端 ~2 m 信号弱）
- 单人、室内
- 避免正对空调出风口 / 风扇等机械振动源（会被当成"呼吸"）
- 体动 / 翻身期间 BPM 不可信（固件已门控，但软件 UI 要相应提示"保持静止"）
- 入场 ~2 s、离场 ~1 s（presence）；vitals 估算每 20 s 出一次新值

---

## 3. 关键约束（违反会崩溃或检测失效，软件侧无须管但要知道存在）

- 雷达每帧 24576 样本 > 硬件 FIFO 上限，**必须** 6 片 × 4096 分片读取（固件已处理）
- SPI 时钟最低 25 MHz；低于此 FIFO 溢出 → 连续失败 → 重启崩溃（当前 dev board 受 XTAL 限制跑 20 MHz，产品板需保证 25 MHz）
- 雷达初始化顺序固定：释放 GPIO hold → SPI init → sensor init → 寄存器配置 → IRQ/FIFO limit → 启动 frame gen（不能调换）
- FFT twiddle 表全局唯一：sensor-dsp 以 128 初始化，vitals 用 1024 → 必须 deinit + reinit(1024)，否则心率锁死（固件已处理）
- 隐私闸 = **硬件断电路径**：闸关时雷达 / 麦电源物理断开，固件软逻辑无法绕过（需要 PCB 上把雷达 VCC 串到拨钮）

---

## 4. 与软件契约的差异点（hardware ↔ luna-product/docs/hardware/）

| 项 | 软件契约（lunawake-device.md / firmware.md） | 我们现状 | 处理方向 |
|---|---|---|---|
| 物理控件 | 隐私闸拨钮 + AUTO 圆键 + 弧形滑条 ×2 | 3× 轻触开关，无滑条 | DVT 改板：滑条选型、AUTO 单键、隐私闸机械拨钮 |
| 状态屏 | 铜色装饰轨，时钟 + 系统图标 + 事件提示 | 无显示器 | DVT 改板：选型（OLED / 字符 LCD / 点阵）+ 外壳光学 |
| 光柱 | 暖光主视觉，PWM ≤ 0.1% 精度，melanopic 低 | 无驱动通道 | DVT 改板：PWM + 升压 + 1800–2200 K LED |
| 麦克风 | 2 麦面向床体 | 4 麦 | 评估 4→2 影响后定 |
| 雷达能力 | 入睡判定 / 睡醒分期 / 离入床事件 / 体动序列 | 部分缺（见 §2.2） | 固件立项；分期需数据 |
| 呼吸律动范围 | 8–24 BPM | 12–30 BPM | 改 FFT ROI |
| 状态机 | 5 场景 + 6 stage 事件 + 17 兜底常量 | 无（只到 vitals 输出） | 固件立项，工作量最大 |
| ConfigCard 摄取 | firmware 摄取 soundtrackHint / guidanceTemplate / cap / startAnchor | 无（通信协议未定） | 双方定协议（HTTP / MQTT / BLE？） |
| OTA 门控 | 仅 Manual 默认 / Auto 空闲接受 OTA | 无 OTA | 固件立项 |
| 时钟对齐 | 雷达 / 麦 / 环境 / 光 / 声 ≤ 50 ms | 单雷达任务，未做多通道对齐 | 固件立项 |
| Matter Controller | 在 APP 端，不在硬件 | — | 无须我们做 |

---

## 5. 待双方对齐的问题（软件文档没钉死，需要会议定）

1. **通信协议**：ConfigCard 怎么传到 firmware？stage 事件 / live readings 怎么上报？（HTTP REST / MQTT / BLE GATT / 自定义？）—— firmware.md 没指定
2. **状态屏形态**：分辨率 / 是字符还是点阵 / 颜色 / 亮度档位 —— 决定 PCB 改版和外壳
3. **滑条选型**：电容触摸条 / 旋钮编码器 / 触摸环 —— 决定 PCB 改版
4. **麦克风 4→2**：保留 4 还是缩 2，几何排布 —— 决定 PCB 改版和声学算法
5. **睡眠分期数据采集计划**：要不要立项采 PSG 同步数据？还是外采模型？
6. **隐私闸断电范围**：只断雷达，还是雷达 + 麦一起断？（lunawake-device.md 说"全部感知元件硬件级断电"，含环境光温湿度吗？文档 §2.2 说环境不受隐私闸影响 —— 需澄清）
7. **气压计 BMP580**：保留还是删
8. **蓝牙音频路径**：ESP32-P4 + 协处理器能否同时跑 Wi-Fi + BT 音频 + 雷达 SPI + 4 麦 PDM + 双 codec？固件需做资源预算
9. **17 个 firmware 兜底常量的最终值**：firmware.md §2 给的是"DVT 推荐起点"，固件团队调试后回填

---

## 6. 配套资料索引（硬件侧权威源）

| 文件 | 内容 |
|---|---|
| [industrial-design.md](industrial-design.md) | 操作与体验需求（结构组用，含摆放几何） |
| [product-spec.md](product-spec.md) | 整机规格书（含雷达透波硬约束） |
| [radar.md](radar.md) | 60 GHz 雷达模块（输出契约 + 算法参数） |
| [microphone.md](microphone.md) | 麦克风阵列模块 |
| [audio.md](audio.md) | 音频输出模块 |
| [environment.md](environment.md) | 环境传感模块（温湿/光/气压） |
| [indicator.md](indicator.md) | LED 状态指示 + 按键 + 滑条 |
| [power.md](power.md) | 电源管理模块 |
| [networking.md](networking.md) | WiFi / BT / 天线 |
| [app-protocol.md](app-protocol.md) | ESP ↔ App 通信契约（BluFi + WS） |
| [luna-panel-bridge.md](luna-panel-bridge.md) | 对 Luna 软件团队的硬件实施契约 |
| [../progress/evt-radar-bringup.md](../progress/evt-radar-bringup.md) | 从硬件点亮到产品化的实验记录 + 问题↔解决方案对照表 |
| [../progress/evt-vitals-baseline.md](../progress/evt-vitals-baseline.md) | 雷达 vitals 算法基线快照 + 校准记录 |
| [assets/netlist-BY-SA-V001-2026-05-06.enet](assets/netlist-BY-SA-V001-2026-05-06.enet) | 主板网表（完整 BOM + 网络连接） |
| `components/my_lidar_inf/my_lidar_inf.h` | 对外数据结构与 API（权威） |
| `components/my_lidar_inf/resource_map.h` | 雷达 / LED 引脚定义（权威） |
| `CLAUDE.md` | 项目硬约束与禁忌（FIFO 分片 / SPI 时钟 / 初始化顺序 / 嵌入式约束） |
| `.claude/skills/radar-vitals/SKILL.md` | vitals 调参方法论与参数索引 |
| `.codex/skills/infineon-bgt60tr13c-radar/SKILL.md` | BGT60TR13C 雷达驱动与配置 |
