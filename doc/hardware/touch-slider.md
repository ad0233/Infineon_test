# Touch Slider 模块

> Status: 🔴 planning · Last reviewed: 2026-05-18 · DVT 选型进行中
>
> 弧形触摸滑条 ×2 —— 音量 / 亮度连续位置输入。当前 EVT 板 **未实装**，3 颗轻触开关 SW1/SW2/SW3 占位待重定义。

主板：BY-SA-V001 / 项目：Lunawake (BY001)。

---

## 1. 模块定位

| 项 | 内容 |
|---|---|
| 用途 | 床头硬件**两段线性位置输入**：左侧 = 音量、右侧 = 亮度 |
| 软件契约 | [luna-panel-bridge.md §7](luna-panel-bridge.md) Auto / Manual 双语义 |
| 工作面 | **金属面板背贴**（不是裸 PCB 焊盘） |
| 数量 | 2 条（左右各一） |
| 输入维度 | 一维线性位置 + 触摸/抬起事件 + 长按 |
| 当前状态 | EVT 未实装；3 颗 SW1/SW2/SW3 临时占位 |

## 2. 推荐方案（来自 [选型对比](assets/触摸开关选型对比.md)）

> **金属背贴电容触摸阵列 + LED 灯柱视觉反馈 + 线性谐振马达（LRA）振动反馈** 是综合最优解。
>
> 仅在 ID 坚持金属厚度 ≥ 1 mm 且整面禁止减薄时，才考虑电容压感方案。

### 2.1 核心原理：重心插值（Centroid Interpolation）

```
电极阵列：  ┌──┬──┬──┬──┬──┐ ... ×N
             N₁ N₂ N₃ N₄ N₅
触摸位置 = Σ (xᵢ × ΔCᵢ) / Σ ΔCᵢ
        ↑ 每个电极电容增量
```

- N = 6–10 个电极 → 物理分辨率约 1 cm
- 重心插值后**算法分辨率 256 级**（详见对比文档 §4.3）

### 2.2 候选 IC

| IC | 厂商 | 通道数 | 接口 | 备注 |
|---|---|---|---|---|
| **IQS572** | Azoteq | 8 通道触摸 + LED 控制 | I²C | 金属背贴优化、内置 LRA 驱动、推荐方案 |
| TTP229 | 君正 | 16 通道纯触摸 | I²C | 备选，需外加 LRA + LED 驱动 |
| 集成在 ESP32-P4 触摸 GPIO | Espressif | 多通道 | 原生 | EVT 验证 SNR 够则可省一颗 IC |

> 当前推荐 Azoteq IQS572，确认后回填**位号 / LCSC 料号 / PCB 改版**。

## 3. 电气接口（待 V1.1 改板）

| 信号 | 当前 EVT | DVT 计划 |
|---|---|---|
| 触摸 IC 主接口 | — | I²C（占用 P4 已有 I²C 总线 + 中断 GPIO） |
| 电极 PCB | — | 滑条铜箔阵列（背贴金属面板） |
| LED 灯柱反馈 | — | PWM × N（视 IC 方案，IQS572 内置驱动） |
| LRA 振动反馈 | — | PWM 或 IC 内置驱动 |

EDA 改版要点：

- 触摸 IC 与电极阵列**距离 ≤ 50 mm**，PCB 走线对称（避免引入不平衡寄生）
- 电极阵列下方铺地需开窗（减少寄生电容降低 SNR）
- 金属面板 **背贴位置局部减薄到 0.3–0.5 mm** 或贴附介质垫
- LED 灯柱单元与电极机械对齐，光导避免漏光

## 4. 软件 / 算法清单

详见 [选型对比 §4.4](assets/触摸开关选型对比.md)。关键 5 步：

1. 重心插值（线性位置）
2. 抖动滤波（卡尔曼 / 滑动均值）
3. 触摸 / 抬起检测（hysteresis 阈值）
4. 长按 / 拖动手势
5. **金属背贴 SNR 自动校准**（IDF v5.5 触摸 driver 需定制）

固件层挂载点：建议**独立 task** + 100 Hz 轮询，事件经 queue 上抛给 UI 状态机。

## 5. 验收红线（DVT）

按 [acceptance/dvt-gate.md §3](../progress/acceptance/dvt-gate.md) 收口：

- [ ] 金属面板背贴 SNR ≥ 5（IC 厂商建议值）
- [ ] 重心插值后位置稳定度 ≤ ±1 / 256 持续 1 s
- [ ] 单手盲操作 + 离开 1.5 s 后位置锁定不漂移
- [ ] 灯柱与触摸位置对齐误差 ≤ 1 mm
- [ ] LRA 反馈延迟 ≤ 30 ms（人感无延迟阈）

## 6. 待办 / 决策项

| # | 项 | 卡谁 | 关联 |
|---|----|------|------|
| 1 | ID 是否接受金属面板局部减薄到 0.3–0.5 mm？（决定电容 vs 压感） | ID + 结构 | [open-questions.md Q3](../progress/open-questions.md) |
| 2 | 滑条物理位置是否会被睡姿误压？（决定是否需要力阈值过滤） | ID + UX | — |
| 3 | 触摸 IC 选型最终敲定（IQS572 vs 其他） | 硬件 | [bom-cost.md](bom-cost.md) BOM 回填 |
| 4 | PCB V1.1 改版（删 SW1/2/3 占位，加触摸电极阵列 + LED 灯柱 + LRA） | 硬件 | [acceptance/dvt-gate.md §1](../progress/acceptance/dvt-gate.md) |
| 5 | 固件触摸 task + 事件协议 | 固件 | 待 PCB 定稿后 |

## 7. 备选方案（暂不展开）

| 方案 | 何时考虑 | 详情 |
|---|---|---|
| 电容压感（NextInput / UneoTech） | 金属面板整面禁止减薄 | [选型对比 §5.1](assets/触摸开关选型对比.md) |
| FSR / 应变片 | 不推荐，仅作记录 | [选型对比 §5.2](assets/触摸开关选型对比.md) |
| 旋钮编码器 | 极少数 ID 偏好场景 | EVT 板 SW1/2/3 已支持 GPIO，可临时用旋钮+按键模拟 |

## 8. 配套资料

- [assets/触摸开关选型对比.md](assets/触摸开关选型对比.md) — **完整选型决策文档**：方案横向对比、电极几何、SNR 推导、量产工程化注意、PM 决策建议
- [indicator.md §3](indicator.md) — 用户输入总览（按键 + 滑条 + 隐私闸）
- [industrial-design.md §2.1](industrial-design.md) — 物理交互结构需求
- [luna-panel-bridge.md §7](luna-panel-bridge.md) — Auto / Manual 双语义软件契约
- 参考资料：TI SLAA379《Capacitive Sensing: Slider and Wheel Layout》、Azoteq AZD125《Capacitive Sensing Behind Metal》
