# 科学依据与参考文献

> 无脑电 (EEG-free) 毫米波雷达睡眠分期算法的学术基础
> 用于：商业计划书、技术白皮书、专利申请、医疗认证背景材料

---

## 一、核心理论与项目功能对应

| 理论 | 本项目对应模块 | 文献 |
|------|---------------|------|
| 心肺耦合 (CPC) | `radar_estimate_vitals()` 呼吸+心率联合分析 | 1-3 |
| 自主神经系统 (ANS) 与 HRV | 心率变异性统计（待实现） | 4 |
| 体动学 (Actigraphy) | `rbm` 标记 + `body_move` (0-100%) 1s 滑窗检测 | 5 |
| FMCW 雷达临床应用 | BGT60TR13C 59-63GHz 全链路 | 6-7 |

---

## 二、核心理论一：心肺耦合 (CPC) 与深度学习分期

**结论**：放弃 EEG，仅靠心率、呼吸及其变异性，完全具备判断深睡、浅睡与 REM 的医学合法性与高精准度。

### [1] CPC 理论开山之作

**An electrocardiogram-based technique to assess cardiopulmonary coupling during sleep**
- 作者：Thomas, R. J., et al. (哈佛大学医学院)
- 期刊：*Sleep*, 28(9), 1151-1161 (2005)
- 链接：https://academic.oup.com/sleep/article-abstract/28/9/1151/2708244

### [2] 非接触式与穿戴设备应用验证

**Cardiopulmonary Sleep Spectrograms Open a Novel Window Into Sleep Biology — Implications for Health and Disease**
- 作者：Thomas, R. J., et al.
- 期刊：*Frontiers in Neuroscience* (2021)
- 链接：https://www.frontiersin.org/journals/neuroscience/articles/10.3389/fnins.2021.755464/full

### [3] 大样本验证：仅靠心跳和呼吸精准 REM/NREM 分期

**Sleep staging from electrocardiography and respiration with deep learning**
- 作者：Fonseca, P., et al. (飞利浦研究院)
- 期刊：*Sleep*, 43(7) (2020)
- 链接：https://www.ncbi.nlm.nih.gov/pmc/articles/PMC7355395/

---

## 三、核心理论二：自主神经系统 (ANS) 与心率变异性

**结论**：深睡状态下心率变异性极低（心跳极度平稳），这是判定深睡的核心特征依据。

### [4] 睡眠各阶段的自主神经活动规律

**Autonomic activity during human sleep as a function of time and sleep stage**
- 作者：Trinder, J., et al.
- 期刊：*Journal of Sleep Research*, 10(4), 253-264 (2001)
- 链接：https://pubmed.ncbi.nlm.nih.gov/11903855/

---

## 四、核心理论三：体动学 (Actigraphy) 边界锚定

**结论**：赋予体动数据高权重（45%~60%），利用骨骼肌运动锚定"入睡"与"起床"边界，是睡眠医学标准做法。

### [5] 体动在睡眠医学中的作用与有效性（权威综述）

**The role and validity of actigraphy in sleep medicine: an update**
- 作者：Sadeh, A.
- 期刊：*Sleep Medicine Reviews* (2011)
- 链接：https://pubmed.ncbi.nlm.nih.gov/21447441/

---

## 五、核心理论四：FMCW 毫米波雷达的前沿临床应用

**结论**：现代 FMCW 毫米波雷达捕捉胸腔微动（心肺信号）的精度已达到医疗级门槛。

### [6] FMCW 雷达事件级睡眠呼吸暂停识别

**Event-Level Identification of Sleep Apnea Using FMCW Radar**
- 作者：Zhao, et al.
- 期刊：*Bioengineering (MDPI)* (2025)
- 链接：https://www.mdpi.com/2306-5354/12/4/399

### [7] PSG → FMCW 雷达的跨模态迁移学习

**Cross-Modality Transfer Learning from PSG to FMCW Radar for Event-Level Apnea–Hypopnea Segmentation**
- 期刊：*Bioengineering (MDPI)* (2026)
- 链接：https://www.mdpi.com/2306-5354/13/3/283

---

## 六、本项目对应的能力证据链

```
FMCW 雷达 (文献 6-7)
    ↓ 捕捉胸腔微动
呼吸 + 心率 (文献 1-3, CPC 理论)
    ↓ 心肺耦合频谱
+ 心率变异性 (文献 4, ANS 理论)
    ↓ 判定深睡/浅睡
+ 体动锚定 (文献 5, Actigraphy)
    ↓ 判定入睡/起床边界
→ 完整睡眠分期（无需 EEG）
```

### 当前实现状态

| 证据链环节 | 实现状态 | 代码位置 |
|---|---|---|
| FMCW 雷达数据采集 | ✅ 已实现 | `my_lidar_inf.c` — BGT60TR13C SPI + Range FFT |
| 呼吸率估算（0.1–0.6 Hz） | ✅ 已实现 | `radar_estimate_vitals()` 呼吸路径 |
| 心率估算（0.85–2.2 Hz） | ✅ 已实现 | `radar_estimate_vitals()` 心率路径 + 谐波消除 |
| 体动检测 | ✅ 已实现 | RBM 1s 滑窗 ampCV + binSpan |
| 心率变异性 (HRV) | ⏳ 规划中 | 需帧率 ≥50Hz（当前 10Hz 精度不足 ±50ms） |
| 睡眠分期（深/浅/REM） | ⏳ 规划中 | 需 CPC 频谱 + HRV + 体动聚合 |
