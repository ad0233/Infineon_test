# Indicator & User Input 模块

用户与设备的直接交互通道：状态显示（LED + 状态屏）、按键、弧形滑条、隐私闸。

主板：BY-SA-V001 / 项目：Lunawake (BY001)。

---

## 1. RGB 状态指示灯（已上板）

| 颜色 | GPIO | 含义 |
|------|------|------|
| 红 | GPIO15 | 红色通道 |
| 绿 | GPIO16 | 绿色通道 |
| 蓝 | GPIO17 | 蓝色通道 |

**注意**：radar bring-up 用的 dev board 是 WS2812@GPIO20，**产品板 BY-SA-V001 是 3 颗分立单色 LED**，以 `components/my_lidar_inf/resource_map.h` 为准。

### 状态语义（[industrial-design.md §3.1](industrial-design.md)）

| 颜色 | 含义 |
|------|------|
| 绿 呼吸 | 监测态，雷达在线 |
| 蓝 呼吸 | 助眠态-静默光 / 蓝牙配对 |
| 紫 慢闪 | 助眠态-语音引导中 |
| 黄 慢闪 | 唤醒态 |
| 红 稳态 | 隐私闸已关 / 故障 |
| 红 快闪 | 严重故障（雷达初始化失败、过温、电源异常） |

### 结构 / ID 约束

- 单一发光面，不要 3 个分开圆点（用户会以为是 3 个独立指示灯）
- 透光面雾度足够，LED die 不可见
- 床头最暗环境下不刺眼，**最大轴向照度 ≤ 5 lx @ 50 cm**

## 2. 按键

| 位号 | 类型 | 当前用途（EVT） |
|------|------|----------------|
| SW1 | SKRPACE010 立贴轻触开关 | 待重定义 |
| SW2 | 同上 | 待重定义 |
| SW3 | 同上 | 待重定义 |

软件契约（[luna-panel-bridge.md](luna-panel-bridge.md) / Luna 仓 `lunawake-device.md`）要求：

- **AUTO 圆形键 ×1** — 单键状态机切换
- **隐私闸 ×1** — 必须是 **机械硬开关**，物理切断雷达 VCC，不可改成软件 mute
- 剩 1 颗按键 DVT 决策（保留 / 删 / 改其他功能）

## 3. 弧形滑条 / 旋钮（DVT 待选型）

详细模块规格见 **[touch-slider.md](touch-slider.md)**（候选 IC、电极几何、SNR、固件接口、验收红线）。

**速览**：

- 软件契约 [luna-panel-bridge.md §7](luna-panel-bridge.md) Auto / Manual 双语义
- 推荐方向：**金属背贴电容触摸 + LED 灯柱 + LRA 振动反馈**（IC 倾向 Azoteq IQS572）
- 通用要求：连续位置输入、单手可完成、手指离开 1.5 s 不漂移
- 电气接口：I²C + 中断 GPIO（PCB V1.1 改版补）

## 4. 隐私闸（CRITICAL）

| 项 | 要求 |
|---|---|
| 物理形式 | 机械拨钮 / 机械硬开关 |
| 切断对象 | 雷达 VCC（不仅是软件 mute） |
| APP 远程开启 | **禁止**；可远程关闭但需机身二次确认 |
| 状态外显 | RGB 红色稳态 = 关；熄灭 = 开 |
| 单手操作 | 1 秒内触达 |
| 是否切断麦克风 | 软硬待对齐（见 [lunawake-hardware.md §5](lunawake-hardware.md) 待澄清问题 6） |

## 5. 状态屏（DVT 待选型）

软件契约要求：**底座铜色装饰轨**显示时钟 HH:MM + 系统图标 + 事件提示。

当前 PCB 无任何字符 / 点阵显示器。DVT 需立项：

- [ ] 显示器选型（OLED / 字符 LCD / 点阵）
- [ ] 颜色 / 亮度档位
- [ ] 外壳光学开窗与铜色装饰轨设计

## 6. 待办（DVT 遗留）

| # | 项 | 责任方 |
|---|----|------|
| 1 | AUTO 单键 + 隐私闸 + 第 3 颗按键定义 | ID + 硬件 |
| 2 | 弧形滑条选型（参考 §3 对比文档） | 结构 + 硬件 + ID |
| 3 | 状态屏选型 + 外壳光学 | ID + 硬件 |
| 4 | 隐私闸切断范围（仅雷达 vs 雷达 + 麦） | 软硬对齐 |

## 7. 配套资料

- [touch-slider.md](touch-slider.md) — 弧形滑条模块完整规格（IC / 电极 / 算法 / 验收）
- [industrial-design.md §2 + §3](industrial-design.md) — 物理交互与视觉反馈需求
- [luna-panel-bridge.md §7](luna-panel-bridge.md) — Auto / Manual 双语义
- `components/my_lidar_inf/resource_map.h` — 雷达 / LED 引脚（权威）
