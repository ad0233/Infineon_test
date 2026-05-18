# Current Focus

> Status: 🟢 current · Last updated: 2026-05-18
>
> **重启项目时第一个打开这份**。每次会话结束花 1 分钟更新。

---

## 当前焦点（一句话）

**doc/ 重构进入收尾**：硬件资料 + 项目进度按 Luna 风格 kebab-case 分块，全模块独立 md，PM 工作流文档（current-focus / roadmap / decisions / acceptance）首次落地。

## 上次停在哪

- ✅ doc/ 从 14 个文件铺开成 hardware/ + progress/ + standards/ 三段
- ✅ 7 模块 md（radar/microphone/audio/environment/indicator/power/networking）齐了
- ✅ Phase 1 Phase 2 已完成，未 commit
- 🔄 正在加 PM 工作流文档（本文件 + roadmap + decisions + acceptance + bom-cost + block diagram）

## 下 3 件事（优先序）

1. **commit 现在已 staged 的 doc 重构**（避免回滚成本）
2. **跑一次 `idf.py build`** 确认重构没污染编译路径（doc 改不该影响构建，但稳）
3. **回到雷达侧**：根据 [evt-vitals-baseline.md](evt-vitals-baseline.md) 列的"高优先 §一"开干（心率下限扩展 51→42 BPM，或 SQI 信号质量指数）

## 阻塞 / 待决策

无硬阻塞。决策类详见 [open-questions.md](open-questions.md)。

## 长期挂在心上的（每周扫一眼）

- DVT 雷达罩首件验证：流程已写（[test-plan/radar-radome-validation.md](test-plan/radar-radome-validation.md)），等结构件回来跑
- Luna 软件团队 ConfigCard 通信协议未定（HTTP / MQTT / BLE GATT？）
- 蓝牙音频路径与 Wi-Fi + 雷达 SPI + 4 麦 PDM + 双 codec 同时跑的资源预算未验证

---

## 维护规则

- 每次会话结束更新 "上次停在哪" + "下 3 件事"
- "当前焦点" 改动写在 [changelog.md](changelog.md)（按里程碑分类）
- 不要把这里写成 todo list；它是"我大脑现在装着什么"的快照
