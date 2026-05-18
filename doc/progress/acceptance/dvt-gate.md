# DVT 验收 Gate

> Status: 🟡 进行中 · Last reviewed: 2026-05-18
>
> DVT = Design Verification Test。**结构件首件 + PCB V1.1 改板 + 雷达罩验证通过，达到客户演示状态**。
>
> 与 EVT 不同，DVT 全部对照**带壳整机**测，不再用裸板。

---

## 通过判定

**全部 ✅ 才能进 PVT**。允许"有条件通过"（注 caveat 但不打 ✅）。

## 1. PCB V1.1 改板验证

- [ ] **MIC1 / MIC4 部件号统一为 LCD3526B261-OFA03**（决策见 [decisions.md](../decisions.md) 2026-05-13）
- [ ] **MIC2 / MIC3 + 对应去耦电容删除**（GPIO54 释放）
- [ ] **MIC1 ↔ MIC4 实测间距 80 mm ±2 mm**
- [ ] **暖光光柱驱动通道（PWM + 升压）** 补到主板，PWM 精度可调到 0.1% 以下
- [ ] **弧形滑条 / AUTO 单键 / 隐私闸机械拨钮** PCB 改造完成（具体器件依赖 Q3 滑条选型）
- [ ] **状态屏接口** 预留（具体形态依赖 Q2 显示器选型）
- [ ] **雷达 IC PCB 顶视坐标** 出图（Q9 → 决定俯仰角设计基线）
- [ ] **隐私闸 = 物理切断雷达 VCC**，PCB 走线串入拨钮
- [ ] **SPI 25 MHz 在产品板上稳定运行**（dev board XTAL 限制不在此适用）

## 2. 雷达罩首件验证

按 [test-plan/radar-radome-validation.md](../test-plan/radar-radome-validation.md) 跑：

- [ ] 裸板 80 cm 仰卧测试 60 s，CSV 归档为 baseline
- [ ] 装 DVT 外壳同位置同姿势复测 60 s
- [ ] `distance_cm` 曲线最大偏差 ≤ 5 cm
- [ ] `breath_bpm` 稳态偏差 ≤ 2 BPM
- [ ] `heart_bpm` 稳态偏差 ≤ 5 BPM
- [ ] `confidence` 均值下降 ≤ 0.1
- [ ] 任何不达标 → 按 §5.1–5.3 排查（材料 / 厚度 / 装饰条 / 筋位 / 喇叭磁铁）

## 3. 结构 / ID 验收触点（[industrial-design.md §8](../../hardware/industrial-design.md)）

- [ ] 床头黑暗环境，单手 1 s 内能摸到隐私闸键并关闭（红灯亮）
- [ ] 80 cm 仰卧时雷达 BPM 稳定 ≥ 60 s
- [ ] asleep 模式下相机长曝光 5 s，整机面板无可见光
- [ ] 满音量播放粉噪 1 min，外壳无可闻共振
- [ ] 1.5 m 跌落到地毯，外壳不开裂 / 按键不卡死 / 雷达功能不损坏
- [ ] 连续 8 h 满载（雷达 + 音频 + Wi-Fi）后，外壳用户接触面温度 ≤ 45 ℃

## 4. 算法升级（DVT 期完成）

- [ ] 呼吸下限扩到 8 BPM（FFT ROI 改）—— 对齐 Luna `firmware.md §2` 兜底常量
- [ ] 心率下限扩到 42 BPM —— 覆盖深睡 / 运动员
- [ ] 信号质量指数 SQI 输出（判断 vitals 可信度）
- [ ] 入睡判定（呼吸 BPM 稳定 + 体动衰减 + sleepOnsetWindowMs 180 s）
- [ ] 离床 / 入床事件流（presence + bedExitDebounceMs 120 s 去抖）
- [ ] 体动连续时间序列（缓冲对外）

## 5. 软件契约（与 Luna 软件团队对齐）

依赖 Q1 ConfigCard 通信协议落定。

- [ ] ConfigCard 摄取链路打通（HTTP / MQTT / BLE GATT 三选一）
- [ ] stage 事件上报机制
- [ ] 17 个 firmware 兜底常量回填（Luna `firmware.md §2`）
- [ ] 时钟同步 ≤ 50 ms（雷达 / 麦 / 环境 / 光 / 声）
- [ ] OTA 门控（仅 Manual 默认 / Auto 空闲）

## 6. 喇叭 / 音频

- [ ] 喇叭单元选型定（频响 60 Hz - 18 kHz ±6 dB）
- [ ] 音腔密封验证（dB SPL ≤ 50 时无嗡鸣）
- [ ] MIC ↔ 喇叭距离 ≥ 30 mm 且中间有结构隔挡，AEC 性能可接受
- [ ] 婴幼儿场景 dB SPL ≤ 50 @ 1 m hard clamp 验证

## 7. 隐私闸断电范围（Q5 待澄清）

- [ ] 决议：仅雷达 / 雷达+麦 / 雷达+麦+环境
- [ ] PCB 走线匹配决议
- [ ] 状态外显：RGB 红色稳态 = 关；熄灭 = 开

## 8. 已知遗留（带到 PVT 解决）

待 DVT 推进时回填。

---

## 通过后的产物

- [ ] PCB V1.1 改板首件 + ICT / FCT 测试报告
- [ ] DVT 外壳首件 + 雷达罩验证报告（CSV + 对比图）
- [ ] 整机 8 h 满载温升报告
- [ ] 软硬整合 demo 录像（≥ 2 分钟，含入睡 → asleep → wake 全流程）
- [ ] `progress/dvt-handover.md` 写一份打包简报（哪些做了 / 哪些 caveat / PVT 待办）
