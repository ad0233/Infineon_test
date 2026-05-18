# Luna Home Panel 硬件实施逻辑图

> Status: 🟡 needs-review · Last reviewed: 2026-05-05 · Luna 源文档命名已对齐 2026-05-18

面向硬件 / 固件工程师。把 Luna 软件团队的 Panel 需求重排为接口契约、信号链与状态机，看完此文应能直接动手。

**Luna 仓上游源文档**（`LunawakeGo/luna-product`）：

| 文档 | 用途 |
|---|---|
| `lunawake-device.md` | 整机交互 master：产品定义、状态机、操控/感知/输出、Pairing、能力矩阵、降级、服务方案 |
| `hardware-blueprint.md` | App Home Panel 模块功能需求：按卡组织（Hero / Wind Down / Wake / Home / Sensors / Network / Controls） |
| `firmware.md` | firmware 工程师契约册：兜底常量、ConfigCard 摄取契约、stage 事件协议、时钟同步、自检失效、持久化、OTA、物理控件标定 |

> 本文内的章节引用（`§3.x`、`§6` 等）以 Luna 仓 `lunawake-device.md` 为准。本仓不维护 Luna 软件代码路径与 `lunawake-sleep-onset.md` / `sleep-env-spec.md`，需要时去 Luna 仓查。

---

## 1. 顶层分工（谁做什么）

```
┌──────────────────────────────────────────────────────────────────┐
│                       手机 APP（云端 LLM）                        │
│  Persona + 会话 + 体征趋势 + 设备能力 + 时地天气                  │
│                  ↓ LLM 推理                                      │
│            ConfigCard（机器可消费 schema）                        │
└──────────────────────────────────────────────────────────────────┘
            │ A 域：阶段切换点一次性下发                │ B 域：素材 + prompt + cap
            ▼                                           ▼
┌────────────────────────────────┐    ┌────────────────────────────────┐
│   A 域：Matter 第三方设备       │    │   B 域：Lunawake 床头硬件       │
│   light / climate / curtain    │    │   声景 + 光柱 + 雷达 + 麦       │
│   设备本地执行曲线              │    │   本机闭环常驻，不依赖云        │
│   不做 sensing 闭环             │    │   60GHz 雷达 → BPM → 时间拉伸  │
└────────────────────────────────┘    └────────────────────────────────┘
```

CRITICAL：B 域不依赖每晚 ConfigCard 重置；断网仍跑本地闭环。ConfigCard 对 B 域只是「素材偏好 + 引导词语义层 prompt + 参数上限 cap」。

---

## 2. APP 端形态：六模块卡（已实现）

入口位于 home tag（Lunawake 圆球）。已配对设备进入 `LunawakePairedView`，未配对走 `LunawakeUnpairedView`。

六卡布局 + Hero 状态条：

```
┌─────────────────────────────────────────┐
│  Hero（设备照片）                        │
│  badge: 在线灯 + phase + degradeMode    │   ← phase ∈ {A,B,C}，对应 windDown/asleep/wake
└─────────────────────────────────────────┘
┌──────────────┬──────────────┐
│  Wind Down   │  Wake        │  ← B 域 Lunawake 自身契约
├──────────────┼──────────────┤
│  Home        │  Sensors     │  ← Home = A 域 Matter；Sensors = 感知轴
├──────────────┼──────────────┤
│  Network     │  Controls    │  ← 连接 / Lunawake 自身灯&音
└──────────────┴──────────────┘
```

asleep 态（phase=B）规则：仅显示 Wind Down（read-only）/ Sensors / Network；Wake / Home / Controls 隐藏。Wind Down 卡此时不可点开 sheet。

每张卡 ↔ 硬件归属：

| 卡 | 域 | 接管 | 硬件实现位置 |
|---|---|---|---|
| Wind Down | B | 助眠声景 + 光柱 + 入睡渐灭 + skip 今晚 + auto-off + 起床推 | Lunawake firmware |
| Wake | B | 智能唤醒（wakeTime / intensity / 浅睡窗口） | Lunawake firmware |
| Home | A | Matter 设备清单 + 三段场景编排（windDown/asleep/wake） | Matter Controller |
| Sensors | 感知轴 | 隐私闸镜像 + Live readings + 云同步 + 数据导出 | Lunawake telemetry |
| Network | — | Wi-Fi 配置 + 蓝牙重连 + 固件更新策略 | Lunawake 通信子系统 |
| Controls | B | Lunawake 自身灯/音 Auto/Manual 双语义 + 重新校准 | Lunawake firmware |

CRITICAL：Home 卡只管 Matter 第三方；Lunawake 自身光声归 Controls。两者互斥，不能交叉操控（`lunawake-device.md §3.9`）。

---

## 3. 信号 → 决策 → 执行 主链路

```
[触发信号]                     [决策]                    [执行]
  时间锚点 ──┐
  HRV/呼吸 ──┤
  会话语义 ──┼──► LLM 推理 ──► ConfigCard ──┬──► A 域 Matter Controller ──► 设备本地曲线
  Persona  ──┤   （云端）                    │
  天气日落 ──┤                               └──► B 域 Lunawake（material + prompt + cap）
  环境异常 ──┘                                       │
                                                    └─► 本地 sensing 闭环（雷达/麦克风）
                                                          独立运行，不回云
```

无白名单规则表，无 persona→config 映射表。差异化由 LLM 推理产生。

---

## 4. ConfigCard 接口契约

机器可消费 schema 详见 Luna 仓 `sleep-env-spec.md` Part B。硬件侧只需关心两个出口：

### 4.1 A 域出口（每个 stage 一次性下发）

```
stages: [windDown, asleep, wake]    // 与前端 SceneSegment 命名一致
每个 stage 携带：
  light    : { brightness 0-100, color_temp 1800-6500K, curve }
  climate  : { target_temp °C, airflow 0-100, curve }
  curtain  : { openness 0-100, curve }
```

阶段语义：
| stage     | light                    | climate         | curtain       |
|-----------|--------------------------|-----------------|---------------|
| windDown  | 暖光低亮渐降             | 缓降温          | 闭合遮光      |
| asleep    | 关闭                     | 维持            | 保持闭合      |
| wake      | 模拟日出渐亮渐冷         | 回温            | 渐开          |

### 4.2 B 域出口（素材 + 语义层 + 参数上限）

```
sound:
  material              : enum from curated 库（核心声景 3-5 / 引导音轨 2-3 / 唤醒音乐 5-8）
  guidance_text         : AIGC 文案，端侧 TTS 合成
  recommendation_reason : 文本，仅 UI 外显，不进闭环
caps:
  masterVolumeCap       : 0-100   // Auto 契约下声景音量天花板
  brightnessCap         : 0-100   // Auto 契约下光柱亮度天花板
contract_flags:
  autoOffAfterSleep     : bool    // 入睡确认后自动收束
  getUpNudge            : bool    // CBT-i 起床推
  skippedTonight        : bool    // 今晚跳过助眠序列
wake:
  wakeTime              : "HH:mm"
  wakeDuringLightSleep  : bool    // 智能窗口 vs 定点
  intensity             : enum {gentle, bright}
```

B 域 firmware 收到后：缓存素材偏好 + prompt + cap；实时控制由本地 sensing 决策，不读 stages 时序。cap 仅作为序列内参数天花板，不打断序列（`lunawake-device.md §3.2`）。

---

## 5. A 域 Matter 链路（Home 卡）

```
ConfigCard.stages
    │
    ▼
APP 阶段调度器（按时钟 + Persona 阶段切换点触发）
    │
    ▼
A 域抽象层（DeviceCapability 路由）
    │
    ▼
Matter Controller（Cluster 映射适配器）
    │
    ▼
绑定的 endpoint（一房间一域一 endpoint，见 §10）
    │
    ▼
设备本地执行曲线（不做实时反馈控制）
```

适配器要做的事：把 light/climate/curtain 抽象命令翻译成对应 Matter Cluster；处理设备状态回读用于 Luna 后续推理 + Home 卡 lastTrigger 显示。

---

## 6. B 域 Lunawake 本地闭环（Wind Down + Wake + Controls）

完整规范见 Luna 仓 `lunawake-sleep-onset.md`。硬件工程师只看这张图：

```
┌─ 60GHz 雷达 ──► 在床检测 ─┐
│                            ├──► 呼吸 BPM 估计 ──► 时间拉伸引擎 ──► 声景 playback
│                ► 入睡判定 ─┘                       (BPM 跟随，only ↓)        │
│                                                                            │
├─ 麦克风 ─────► 噪音 dB / 鼾声 / 婴儿啼哭 ─► 事件上屏 + 干扰策略           │
│                                                                            │
├─ 环境 ───────► 温/湿/光照（始终采集，不受隐私闸影响）                     │
│                                                                            │
└─ ConfigCard 缓存 ──► material 选择 + guidance_text TTS + cap              │
                                                                              ▼
                                                            光柱（低 melanopic 暖光）
                                                            与声景共相位，蜡烛熄灭曲线
```

CRITICAL 约束：
- 声景节奏跟随呼吸只减不加（避免反向激活）
- 入睡判定后声景与光柱时间轴同步极缓熄灭（主声景渐弱 → pink noise 底噪 → 完全静默）
- 链路全本地，不经云端 LLM

---

## 7. Auto / Manual 双语义（Controls 卡）

弧形滑条与 APP Controls 卡承载同一控件、两种语义（`lunawake-device.md §3.2 / §3.9`）。前端用单一字段切换：

```
mode = auto:
  音量字段 = contract.masterVolumeCap        // 序列内天花板，不打断序列
  亮度字段 = contract.brightnessCap
  额外动作 = 重新校准（recalibrate）

mode = manual:
  音量字段 = contract.masterVolume           // 直接驱动当前输出
  亮度字段 = contract.currentBrightness      // 默认待机下的床头灯亮度
```

firmware 必须区分这两组字段，不能合并。手动介入不切换设备状态机，仅在当前态内收敛参数上限。

---

## 8. 设备状态机 ↔ Phase 命名对齐

`lunawake-device.md §3.4` 设备状态机与前端 phase / SceneSegment 对应：

| firmware 状态 | 前端 phase | SceneSegment | 触发 |
|---|---|---|---|
| 待机态 | `idle` | — | 通电默认 |
| 监测态 | `idle` | — | 单击 AUTO |
| 助眠态-静默光 | `A` | `windDown` | 监测 + 预设时间 + 雷达检测到人 |
| 助眠态-语音 | `A` | `windDown` | 助眠态单击 AUTO |
| （入睡） | `B` | `asleep` | 雷达确认入睡，光声极缓渐灭回监测 |
| 唤醒态 | `C` | `wake` | 预设唤醒时间到 |

phase=B 时 APP 收起可调卡片；phase=A/C 全卡可见。Hero badge 同时显示 phase + degradeMode（见 §9）。

---

## 9. 降级树 + Hero 显示口径

```
A 域任意域离线           ─► 跳过该域，承诺回落 B 域
A 域全部离线             ─► B 域独立跑 wind_down 闭环（声 + 光柱）
B 域雷达失效             ─► 助眠按预设时长线性渐灭；唤醒按定点闹钟
B 域 Tier 1 律动无雷达    ─► 退化为固定助眠节律，声音不变
离网                     ─► 关 Matter 联动 + 日报同步；本地光声契约保留
Tier 2 无网络            ─► 云端改造档不可用；第一档本地执行
Tier 2 无蓝牙输入        ─► 回落 Tier 1 默认 curated 声景
```

降级口径必须 APP 同步显示。Hero badge 使用三档枚举 `degradeMode`：

| degradeMode | 含义 | 触发 |
|---|---|---|
| `biological` | 生物自适应（满档） | 雷达在线 + 网络在线 |
| `scheduled` | 时间定点 | 雷达失效或 Tier 2 无网 |
| `ambience` | 仅氛围 | 多重失效，仅本地 curated 声景 |

方案生成始终基于「当前实际可用能力」，不假设设备齐全。

---

## 10. 设备能力声明 + 单房间绑定

```
DeviceCapability {
  device_id, device_type
  matter_cluster      : string
  domains             : ['light' | 'climate' | 'curtain']   // B 域不进此表
  ranges              : { brightness:[0,100], color_temp:[2000,6500], ... }
  room_binding        : 'bedroom' | <space_id>
  online_status       : bool
}
```

**单房间绑定原则（CRITICAL）**：
- 调度对象 = 卧室一个房间。不做整屋拓扑发现，不做房间推断
- 一域一房间一 endpoint。多设备共存由用户指定主控
- Luna 只对绑定的 endpoint 下发命令，不广播
- 房间形态映射：
  - 欧洲水暖：Matter TRV 天然房间级
  - 中国/日韩/东南亚：卧室分体空调（美的 Matter / 米家 / OneConnect / 红外桥接）
  - 北美中央空调 + zoning：卧室 zone 子恒温器
  - 北美中央空调无 zoning：**climate 域显式降级 → 压回 B 域局部微环境兜底**

---

## 11. Sensors 卡 telemetry 接口

前端 Sensors sheet 已落型，firmware 提供以下 live readings：

```
LiveReadings {
  radar?      : { breathRateBrPerMin, presence, breathRateHistory[] }   // 隐私闸开
  microphone? : { noiseDbSpl }                                           // 隐私闸开
  ambient     : { temperatureCelsius, humidityPercent, lux }             // 始终在线
  lastUpdatedAt : ISO8601
}
```

闸控规则（`lunawake-device.md §3.9`）：
- 隐私闸关：radar / microphone 字段为 null（前端显示 paused）
- ambient 始终有读数
- breathRateHistory 用于 sparkline，长度由 firmware 决定，paused 期间用 null 占位
- 仅作即时透明度，不做趋势/告警/持久化
- 云同步开关：只控制事件标签上云，呼吸数据始终留端

APP 不能远程开启隐私闸；关闭可远端发起但需机身二次确认（`lunawake-device.md §3.9`）。

---

## 12. 操控轴 × 感知轴（正交）

ConfigCard 不是设备状态，是叠加于设备状态机之上的契约。

```
                  感知轴（隐私闸）
                   开 ──────── 关
操     AI契约    ┌─────────┬─────────┐
控               │ 全功能   │ 时间定点│   ◄── ConfigCard 在此两格生效
轴               │（雷达驱动）│（降级）│
默认待机          ├─────────┼─────────┤
                  │ 蓝牙音箱 │ 蓝牙音箱│   ◄── ConfigCard 不参与
                  │ + 床头灯 │ + 床头灯│
                  └─────────┴─────────┘
```

---

## 13. 三阶段时间轴 + 多模态唤醒

```
T-X min                就寝点                     起床点-30min        起床点
  │                      │                           │                  │
  │── windDown ──────────┤── asleep ────────────────┤── wake ──────────┤
  │                      │                           │                  │
A域│ 灯：暖光低亮渐降      │ 灯：关                    │ 灯：日出渐亮渐冷  │
   │ 温：缓降             │ 温：维持                  │ 温：回温         │
   │ 帘：闭合             │ 帘：保持                  │ 帘：渐开         │
   │                      │                           │                  │
B域│ 光柱+声景 entrainment │ 入睡后渐灭，整夜仅采集    │ 与 A 域三件套     │
   │ 雷达驱动             │ 鼾/噪音事件上屏           │ 相位锁定          │
```

醒前 30 min A 域三件套与 B 域协同唤醒，B 域本地与 A 域阶段切换信号对齐。

---

## 14. 硬指标与 hard clamp

| 项 | 约束 | 来源 |
|---|---|---|
| 婴幼儿场景声压 | dB SPL ≤ 50 | 需求 §Persona |
| 婴幼儿整夜白噪 | 禁用 | 需求 §Persona |
| 老人 climate 下限 | target_temp ≥ 18°C | 需求 §Persona |
| 光柱光谱 | 低 melanopic，峰值压在褪黑素抑制阈下 | `lunawake-device.md §3.5a` |
| 声景节奏跟随 | only decrease | Luna 仓 `lunawake-sleep-onset.md` |
| 隐私闸 | 物理切断雷达供电；APP 无法远程开启 | `lunawake-device.md §3.1` |
| Auto 契约音量/亮度 | 不超过 masterVolumeCap / brightnessCap | `lunawake-device.md §3.2` |
| B 域断网行为 | wind-down 闭环独立可跑 | 需求 §验收 |
| 呼吸数据 | 始终留端，不上云 | `lunawake-device.md §3.9` |

---

## 15. 实施边界一览

| 工作 | 谁做 | 在哪 |
|---|---|---|
| LLM 推理生成 ConfigCard | APP / 云 | 不在硬件侧 |
| ConfigCard schema 校验与持久化 | APP | web/ |
| 六模块卡 UI + sheet morph | APP | Luna 仓 web/ |
| Matter Controller / Cluster 适配 | A 域硬件阶段 | 端侧或网关 |
| Matter 设备状态回读 + lastTrigger | A 域硬件阶段 | Matter |
| 雷达 / 麦克风 / 环境 sensing | B 域 firmware | Lunawake 本机 |
| 呼吸 BPM 估计 + 声景时间拉伸 | B 域 firmware | Lunawake 本机 |
| TTS 引导词合成 | B 域 firmware | Lunawake 本机 |
| 素材库下发与切换 | B 域 firmware + APP | Lunawake 本机 |
| Auto/Manual 双字段路由 | B 域 firmware | Lunawake 本机 |
| 重新校准（recalibrate） | B 域 firmware | Lunawake 本机 |
| Live readings telemetry | B 域 firmware | Lunawake 本机 |
| 隐私闸物理切断 | B 域工业设计 + 电气 | Lunawake 本机 |
| 单房间绑定 UI | APP | web/ |
| 降级 mode 外显 (badge) | APP + firmware 上报 | web/ + Lunawake |
| 固件 OTA 策略 | B 域通信子系统 | Lunawake 本机 |

---

## 16. 验收红线

- 给定 Persona + 近期会话，LLM 输出结构合法的 ConfigCard：A 域三阶段曲线 + B 域素材 + prompt + cap
- 模拟 A 域全离线，B 域独立跑 windDown 闭环；Hero badge 切到 `ambience` 或 `scheduled`
- B 域 Lunawake 不依赖每晚 ConfigCard 重置；断网仍本地闭环
- 不同 Persona（4 岁 / 老人 / 精力管理）在 A 域 + B 域呈现差异化趋势
- 单房间绑定：调度只命中绑定 endpoint，不广播
- 降级链全路径覆盖：任意域缺失都有承诺路径，APP 同步显示 degradeMode
- phase=B 时 APP 自动收起 Wake/Home/Controls 卡，Wind Down 卡不可点开
- Auto/Manual 切换：firmware 路由到正确的 cap / 直驱字段，不串扰
- 隐私闸关：radar/mic telemetry 立即返回 null；ambient 不受影响
- 远端关闭隐私闸需机身二次确认；远端不可开启
